/**
 * JNI bridge between Android Java/Kotlin and libtdfu.
 *
 * Key Android USB constraint: the app cannot open USB devices directly.
 * Instead, Android's UsbManager opens the device and gives us a file
 * descriptor. We pass that fd to libusb_wrap_sys_device() to get a
 * libusb_device_handle without needing root or usbfs permissions.
 */

#include <jni.h>
#include <android/log.h>
#include <android/asset_manager.h>
#include <android/asset_manager_jni.h>
#include <libusb-1.0/libusb.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include <pthread.h>

/* libtdfu headers */
#include "tdfu/tdfu.h"
#include "tdfu/core.h"
#include "tdfu/dfu.h"

#define TAG "TdfuJNI"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, TAG, __VA_ARGS__)

/* libtdfu's global debug flag */
bool g_debug_enabled = false;

/* ========================================================================== */
/* Log capture: redirect libtdfu's stderr output to JNI callback            */
/* ========================================================================== */

static JavaVM *g_jvm = NULL;
static jobject g_callback_obj = NULL;
static jmethodID g_log_method = NULL;
static jmethodID g_progress_method = NULL;
static pthread_mutex_t g_callback_mutex = PTHREAD_MUTEX_INITIALIZER;

JNIEXPORT jint JNI_OnLoad(JavaVM *vm, void *reserved) {
    g_jvm = vm;
    return JNI_VERSION_1_6;
}

static JNIEnv *get_env(void) {
    JNIEnv *env = NULL;
    if (g_jvm) {
        (*g_jvm)->GetEnv(g_jvm, (void **)&env, JNI_VERSION_1_6);
        if (!env) {
            (*g_jvm)->AttachCurrentThread(g_jvm, &env, NULL);
        }
    }
    return env;
}

static void jni_log(const char *msg) {
    JNIEnv *env = get_env();
    pthread_mutex_lock(&g_callback_mutex);
    jobject cb = g_callback_obj;
    jmethodID m = g_log_method;
    pthread_mutex_unlock(&g_callback_mutex);
    if (env && cb && m) {
        jstring jmsg = (*env)->NewStringUTF(env, msg);
        if (jmsg) {
            (*env)->CallVoidMethod(env, cb, m, jmsg);
            (*env)->DeleteLocalRef(env, jmsg);
        }
    }
}

static void jni_progress(int percent, const char *stage, const char *message) {
    JNIEnv *env = get_env();
    pthread_mutex_lock(&g_callback_mutex);
    jobject cb = g_callback_obj;
    jmethodID m = g_progress_method;
    pthread_mutex_unlock(&g_callback_mutex);
    if (env && cb && m) {
        jstring jstage = (*env)->NewStringUTF(env, stage);
        jstring jmessage = (*env)->NewStringUTF(env, message);
        if (jstage && jmessage) {
            (*env)->CallVoidMethod(env, cb, m,
                                   (jint)percent, jstage, jmessage);
        }
        if (jstage) (*env)->DeleteLocalRef(env, jstage);
        if (jmessage) (*env)->DeleteLocalRef(env, jmessage);
    }
}

/* Progress callback for tdfu_bootstrap / tdfu_read / tdfu_write */
static void progress_callback(int percent, const char *stage,
                               const char *message, void *user_data) {
    (void)user_data;
    jni_progress(percent, stage, message);
    LOGI("Progress: %d%% [%s] %s", percent, stage, message);
}

/* ========================================================================== */
/* Helper: wrap Android USB fd into libusb device handle                       */
/* ========================================================================== */

static libusb_context *g_ctx = NULL;

static libusb_device_handle *wrap_android_fd(int fd) {
    if (!g_ctx) {
        /* On Android, libusb_init() fails because it tries to scan /dev/bus/usb/
         * which requires root. Use LIBUSB_OPTION_NO_DEVICE_DISCOVERY since we
         * only use libusb_wrap_sys_device() with Android's USB fd. */
        struct libusb_init_option opts[] = {
            { .option = LIBUSB_OPTION_NO_DEVICE_DISCOVERY },
        };
        int rc = libusb_init_context(&g_ctx, opts, 1);
        if (rc != LIBUSB_SUCCESS) {
            LOGE("libusb_init_context failed: %s", libusb_error_name(rc));
            return NULL;
        }
    }

    libusb_device_handle *handle = NULL;
    int rc = libusb_wrap_sys_device(g_ctx, (intptr_t)fd, &handle);
    if (rc != LIBUSB_SUCCESS) {
        LOGE("libusb_wrap_sys_device(fd=%d) failed: %s", fd, libusb_error_name(rc));
        return NULL;
    }

    LOGI("Wrapped Android USB fd=%d into libusb handle %p", fd, handle);
    return handle;
}

/* ========================================================================== */
/* Helper: build a usb_device_t from an Android fd                            */
/* ========================================================================== */

static usb_device_t *device_from_fd(int fd) {
    libusb_device_handle *handle = wrap_android_fd(fd);
    if (!handle) return NULL;

    usb_device_t *dev = (usb_device_t *)calloc(1, sizeof(usb_device_t));
    if (!dev) {
        libusb_close(handle);
        return NULL;
    }

    dev->handle = handle;
    dev->context = g_ctx;
    dev->device = libusb_get_device(handle);
    dev->closed = false;
    dev->stage1_consumed = false;

    /* Read device descriptor to populate VID/PID */
    struct libusb_device_descriptor desc;
    if (libusb_get_device_descriptor(dev->device, &desc) == LIBUSB_SUCCESS) {
        dev->info.vendor = desc.idVendor;
        dev->info.product = desc.idProduct;
        dev->info.bus = libusb_get_bus_number(dev->device);
        dev->info.address = libusb_get_device_address(dev->device);
    }

    dev->info.stage = TDFU_STAGE_BOOTROM;
    dev->info.variant = TDFU_VARIANT_T31X;

    return dev;
}

static void device_close(usb_device_t *dev) {
    if (dev) {
        if (!dev->closed && dev->handle) {
            libusb_close(dev->handle);
            dev->handle = NULL;
        }
        dev->closed = true;
        free(dev);
    }
}

/* Android-specific close: Android's UsbManager owns the underlying fd, so we
 * must NOT libusb_close() it (that would close Android's fd). Just free our
 * wrapper struct. */
static void device_close_android(usb_device_t *dev) {
    free(dev);
}

/* Read a whole file into a malloc'd buffer (caller frees). Returns 0 on success. */
static int read_file_to_mem(const char *path, uint8_t **out, size_t *out_len) {
    FILE *f = fopen(path, "rb");
    if (!f)
        return -1;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0) {
        fclose(f);
        return -1;
    }
    uint8_t *buf = (uint8_t *)malloc((size_t)sz);
    if (!buf) {
        fclose(f);
        return -1;
    }
    if (fread(buf, 1, (size_t)sz, f) != (size_t)sz) {
        free(buf);
        fclose(f);
        return -1;
    }
    fclose(f);
    *out = buf;
    *out_len = (size_t)sz;
    return 0;
}

/* Map a SoC variant to its firmware/dfu/<dir> name (mirrors dfu_variant_dir). */
static const char *dfu_asset_dir(tdfu_variant_t v) {
    switch (v) {
    case TDFU_VARIANT_T31:
    case TDFU_VARIANT_T31X:
    case TDFU_VARIANT_T31ZX:
    case TDFU_VARIANT_T31AL:
        return "t31";
    case TDFU_VARIANT_T31A:
        return "t31_ddr3";
    case TDFU_VARIANT_T23:
    case TDFU_VARIANT_T23DL:
        return "t23";
    case TDFU_VARIANT_T40XP:
        return "t40_ddr3";
    default:
        return tdfu_variant_to_string(v);
    }
}

/* ========================================================================== */
/* Helper: extract assets to cache directory                                  */
/* ========================================================================== */

static int extract_asset_to_file(JNIEnv *env, jobject asset_manager_obj,
                                  const char *asset_path, const char *dest_path) {
    AAssetManager *mgr = AAssetManager_fromJava(env, asset_manager_obj);
    if (!mgr) {
        LOGE("Failed to get AAssetManager");
        return -1;
    }

    AAsset *asset = AAssetManager_open(mgr, asset_path, AASSET_MODE_STREAMING);
    if (!asset) {
        LOGE("Asset not found: %s", asset_path);
        return -1;
    }

    FILE *out = fopen(dest_path, "wb");
    if (!out) {
        LOGE("Cannot create %s: %s", dest_path, strerror(errno));
        AAsset_close(asset);
        return -1;
    }

    char buf[8192];
    int n;
    while ((n = AAsset_read(asset, buf, sizeof(buf))) > 0) {
        if ((int)fwrite(buf, 1, n, out) != n) {
            LOGE("Short write to %s", dest_path);
            fclose(out);
            AAsset_close(asset);
            unlink(dest_path);
            return -1;
        }
    }

    fclose(out);
    AAsset_close(asset);
    return 0;
}

/* ========================================================================== */
/* JNI: setCallback                                                           */
/* ========================================================================== */

JNIEXPORT void JNICALL
Java_com_thingino_dfu_TdfuBridge_nativeSetCallback(
        JNIEnv *env, jclass clazz, jobject callback) {
    (void)clazz;

    pthread_mutex_lock(&g_callback_mutex);
    if (g_callback_obj) {
        (*env)->DeleteGlobalRef(env, g_callback_obj);
        g_callback_obj = NULL;
    }

    if (callback) {
        g_callback_obj = (*env)->NewGlobalRef(env, callback);
        jclass cls = (*env)->GetObjectClass(env, callback);
        g_log_method = (*env)->GetMethodID(env, cls, "onLog", "(Ljava/lang/String;)V");
        g_progress_method = (*env)->GetMethodID(env, cls, "onProgress",
                                                 "(ILjava/lang/String;Ljava/lang/String;)V");
        (*env)->DeleteLocalRef(env, cls);
    }
    pthread_mutex_unlock(&g_callback_mutex);
}

/* ========================================================================== */
/* JNI: detectSoc                                                             */
/* ========================================================================== */

JNIEXPORT jstring JNICALL
Java_com_thingino_dfu_TdfuBridge_nativeDetectSoc(
        JNIEnv *env, jclass clazz, jint fd) {
    (void)clazz;

    LOGI("nativeDetectSoc: fd=%d", fd);
    jni_log("Detecting SoC...\n");

    usb_device_t *dev = device_from_fd(fd);
    if (!dev) {
        jni_log("ERROR: Failed to open USB device\n");
        return (*env)->NewStringUTF(env, "");
    }

    /* Get CPU info first to determine stage */
    cpu_info_t cpu_info;
    tdfu_error_t result = usb_device_get_cpu_info(dev, &cpu_info);
    if (result != TDFU_SUCCESS) {
        char msg[128];
        snprintf(msg, sizeof(msg), "WARNING: CPU info query failed: %s\n",
                 tdfu_error_to_string(result));
        jni_log(msg);
        device_close(dev);
        return (*env)->NewStringUTF(env, "");
    }

    /* Report CPU magic */
    char msg[256];
    snprintf(msg, sizeof(msg), "CPU magic: '%s' (stage: %s)\n",
             cpu_info.clean_magic,
             tdfu_stage_to_string(cpu_info.stage));
    jni_log(msg);

    const char *variant_str;

    if (cpu_info.stage == TDFU_STAGE_BOOTROM) {
        /* Try auto-detect via MIPS code upload */
        tdfu_variant_t detected = TDFU_VARIANT_T31X;
        result = protocol_detect_soc(dev, &detected);
        if (result == TDFU_SUCCESS) {
            variant_str = tdfu_variant_to_string(detected);
        } else {
            /* Fall back to magic-based detection */
            tdfu_variant_t from_magic = detect_variant_from_magic(cpu_info.clean_magic);
            variant_str = tdfu_variant_to_string(from_magic);
        }
    } else {
        /* Already in firmware stage, use magic */
        tdfu_variant_t from_magic = detect_variant_from_magic(cpu_info.clean_magic);
        variant_str = tdfu_variant_to_string(from_magic);
    }

    snprintf(msg, sizeof(msg), "Detected SoC: %s\n", variant_str);
    jni_log(msg);

    /* Do NOT close the libusb handle -- Android owns the fd.
     * Just free our wrapper struct. */
    device_close_android(dev);

    return (*env)->NewStringUTF(env, variant_str);
}

/* ========================================================================== */
/* JNI: bootstrap                                                             */
/* ========================================================================== */

JNIEXPORT jint JNICALL
Java_com_thingino_dfu_TdfuBridge_nativeBootstrap(
        JNIEnv *env, jclass clazz, jint fd, jstring variant_str,
        jstring firmware_dir_str, jobject asset_manager) {
    (void)clazz;

    const char *variant_cstr = (*env)->GetStringUTFChars(env, variant_str, NULL);
    const char *fw_dir_cstr = (*env)->GetStringUTFChars(env, firmware_dir_str, NULL);
    if (!variant_cstr || !fw_dir_cstr) {
        if (variant_cstr) (*env)->ReleaseStringUTFChars(env, variant_str, variant_cstr);
        if (fw_dir_cstr) (*env)->ReleaseStringUTFChars(env, firmware_dir_str, fw_dir_cstr);
        return -1;
    }

    LOGI("nativeBootstrap: fd=%d variant=%s fw_dir=%s", fd, variant_cstr, fw_dir_cstr);

    /* DFU bootstrap: USB-boot the DFU-capable SPL + U-Boot (firmware/dfu/<soc>/)
     * onto the bootrom; the device then re-enumerates as a 4d44 DFU gadget. */
    char dmsg[512];
    tdfu_variant_t v = tdfu_variant_from_string(variant_cstr);
    const char *ddir = dfu_asset_dir(v);
    char spl_asset[256], uboot_asset[256], spl_path[512], uboot_path[512];
    snprintf(uboot_asset, sizeof(uboot_asset), "firmware/dfu/%s/uboot.bin", ddir);
    snprintf(spl_path, sizeof(spl_path), "%s/dfu_%s_stage1.bin", fw_dir_cstr, ddir);
    snprintf(uboot_path, sizeof(uboot_path), "%s/dfu_%s_uboot.bin", fw_dir_cstr, ddir);

    jni_log("DFU bootstrap (bootrom -> U-Boot DFU gadget)...\n");
    jni_progress(10, "bootstrap", "Extracting DFU U-Boot...");

    uint8_t *spl = NULL, *uboot = NULL;
    size_t sl = 0, ul = 0;
    tdfu_error_t dr = TDFU_ERROR_FILE_IO;
    /* stage1 is tpl.bin on the capped XBurst1 SoCs (T10/T20/T21/T30) and
     * spl.bin on the big-SPL SoCs - mirror the tpl-first pick in dfu.c. */
    snprintf(spl_asset, sizeof(spl_asset), "firmware/dfu/%s/tpl.bin", ddir);
    int s1 = extract_asset_to_file(env, asset_manager, spl_asset, spl_path);
    if (s1 != 0) {
        snprintf(spl_asset, sizeof(spl_asset), "firmware/dfu/%s/spl.bin", ddir);
        s1 = extract_asset_to_file(env, asset_manager, spl_asset, spl_path);
    }
    if (s1 == 0 &&
        extract_asset_to_file(env, asset_manager, uboot_asset, uboot_path) == 0 &&
        read_file_to_mem(spl_path, &spl, &sl) == 0 && read_file_to_mem(uboot_path, &uboot, &ul) == 0) {
        usb_device_t *ddev = device_from_fd(fd);
        if (ddev) {
            jni_progress(40, "bootstrap", "USB-booting U-Boot...");
            dr = tdfu_dfu_bootstrap_device(ddev, spl, sl, uboot, ul);
            device_close_android(ddev);
        } else {
            dr = TDFU_ERROR_OPEN_FAILED;
        }
    } else {
        snprintf(dmsg, sizeof(dmsg), "ERROR: missing DFU firmware asset (%s)\n", spl_asset);
        jni_log(dmsg);
    }
    free(spl);
    free(uboot);
    unlink(spl_path);
    unlink(uboot_path);
    if (dr == TDFU_SUCCESS) {
        jni_progress(100, "bootstrap", "DFU U-Boot running");
        jni_log("Device re-enumerating as a U-Boot DFU gadget.\n");
    } else {
        snprintf(dmsg, sizeof(dmsg), "ERROR: DFU bootstrap failed: %s\n", tdfu_error_to_string(dr));
        jni_log(dmsg);
    }
    (*env)->ReleaseStringUTFChars(env, variant_str, variant_cstr);
    (*env)->ReleaseStringUTFChars(env, firmware_dir_str, fw_dir_cstr);
    return (dr == TDFU_SUCCESS) ? 0 : -1;
}

/* ========================================================================== */
/* JNI: bootstrapFiles - custom SPL + U-Boot from caller-supplied file paths   */
/* ========================================================================== */

/* Like nativeBootstrap, but USB-boots a client-supplied SPL + U-Boot read from
 * two file paths (the Kotlin side stages the user's blobs to cacheDir) instead
 * of the bundled firmware/dfu/<soc>/ assets. The caller owns the temp files and
 * deletes them; we only read them. */
JNIEXPORT jint JNICALL
Java_com_thingino_dfu_TdfuBridge_nativeBootstrapFiles(
        JNIEnv *env, jclass clazz, jint fd, jstring spl_path_str, jstring uboot_path_str) {
    (void)clazz;

    const char *spl_path = (*env)->GetStringUTFChars(env, spl_path_str, NULL);
    const char *uboot_path = (*env)->GetStringUTFChars(env, uboot_path_str, NULL);
    if (!spl_path || !uboot_path) {
        if (spl_path) (*env)->ReleaseStringUTFChars(env, spl_path_str, spl_path);
        if (uboot_path) (*env)->ReleaseStringUTFChars(env, uboot_path_str, uboot_path);
        return -1;
    }

    LOGI("nativeBootstrapFiles: fd=%d spl=%s uboot=%s", fd, spl_path, uboot_path);

    char dmsg[512];
    jni_log("DFU bootstrap with custom SPL/U-Boot (bootrom -> U-Boot DFU gadget)...\n");
    jni_progress(10, "bootstrap", "Loading custom SPL/U-Boot...");

    uint8_t *spl = NULL, *uboot = NULL;
    size_t sl = 0, ul = 0;
    tdfu_error_t dr = TDFU_ERROR_FILE_IO;

    if (read_file_to_mem(spl_path, &spl, &sl) == 0 &&
        read_file_to_mem(uboot_path, &uboot, &ul) == 0) {
        usb_device_t *ddev = device_from_fd(fd);
        if (ddev) {
            jni_progress(40, "bootstrap", "USB-booting U-Boot...");
            dr = tdfu_dfu_bootstrap_device(ddev, spl, sl, uboot, ul);
            device_close_android(ddev);
        } else {
            dr = TDFU_ERROR_OPEN_FAILED;
        }
    } else {
        snprintf(dmsg, sizeof(dmsg), "ERROR: cannot read custom SPL/U-Boot files\n");
        jni_log(dmsg);
    }

    free(spl);
    free(uboot);

    if (dr == TDFU_SUCCESS) {
        jni_progress(100, "bootstrap", "DFU U-Boot running");
        jni_log("Device re-enumerating as a U-Boot DFU gadget.\n");
    } else {
        snprintf(dmsg, sizeof(dmsg), "ERROR: DFU bootstrap failed: %s\n", tdfu_error_to_string(dr));
        jni_log(dmsg);
    }

    (*env)->ReleaseStringUTFChars(env, spl_path_str, spl_path);
    (*env)->ReleaseStringUTFChars(env, uboot_path_str, uboot_path);
    return (dr == TDFU_SUCCESS) ? 0 : -1;
}

/* ========================================================================== */
/* JNI: readFirmware                                                          */
/* ========================================================================== */

JNIEXPORT jint JNICALL
Java_com_thingino_dfu_TdfuBridge_nativeReadFirmware(
        JNIEnv *env, jclass clazz, jint fd, jstring variant_str,
        jstring output_file_str, jstring firmware_dir_str, jobject asset_manager) {
    (void)clazz;
    (void)firmware_dir_str;
    (void)asset_manager;

    const char *variant_cstr = (*env)->GetStringUTFChars(env, variant_str, NULL);
    const char *output_cstr = (*env)->GetStringUTFChars(env, output_file_str, NULL);
    if (!variant_cstr || !output_cstr) {
        if (variant_cstr) (*env)->ReleaseStringUTFChars(env, variant_str, variant_cstr);
        if (output_cstr) (*env)->ReleaseStringUTFChars(env, output_file_str, output_cstr);
        return -1;
    }

    LOGI("nativeReadFirmware: fd=%d variant=%s output=%s", fd, variant_cstr, output_cstr);

    /* DFU read: the device is already a running U-Boot DFU gadget - no bootstrap
     * or flash protocol, just a DFU upload of the (single) alt setting. */
    char dmsg[256];
    jni_log("DFU read (U-Boot gadget)...\n");
    jni_progress(0, "read", "Opening DFU gadget...");
    usb_device_t *ddev = device_from_fd(fd);
    tdfu_error_t dr;
    if (ddev) {
        jni_log("DFU read: device wrapped; reading descriptors then uploading...\n");
        dr = tdfu_dfu_read_device(ddev, -1, output_cstr, 0);
        device_close_android(ddev);
    } else {
        jni_log("ERROR: failed to wrap USB device (bad fd?)\n");
        dr = TDFU_ERROR_OPEN_FAILED;
    }
    if (dr == TDFU_SUCCESS) {
        jni_progress(100, "read", "Read complete!");
        jni_log("DFU read complete.\n");
    } else {
        snprintf(dmsg, sizeof(dmsg), "ERROR: DFU read failed: %s\n", tdfu_error_to_string(dr));
        jni_log(dmsg);
    }
    (*env)->ReleaseStringUTFChars(env, variant_str, variant_cstr);
    (*env)->ReleaseStringUTFChars(env, output_file_str, output_cstr);
    return (dr == TDFU_SUCCESS) ? 0 : -1;
}

/* ========================================================================== */
/* JNI: writeFirmware                                                         */
/* ========================================================================== */

JNIEXPORT jint JNICALL
Java_com_thingino_dfu_TdfuBridge_nativeWriteFirmware(
        JNIEnv *env, jclass clazz, jint fd, jstring variant_str,
        jstring input_file_str, jstring firmware_dir_str, jobject asset_manager) {
    (void)clazz;
    (void)firmware_dir_str;
    (void)asset_manager;

    const char *variant_cstr = (*env)->GetStringUTFChars(env, variant_str, NULL);
    const char *input_cstr = (*env)->GetStringUTFChars(env, input_file_str, NULL);
    if (!variant_cstr || !input_cstr) {
        if (variant_cstr) (*env)->ReleaseStringUTFChars(env, variant_str, variant_cstr);
        if (input_cstr) (*env)->ReleaseStringUTFChars(env, input_file_str, input_cstr);
        return -1;
    }

    LOGI("nativeWriteFirmware: fd=%d variant=%s input=%s", fd, variant_cstr, input_cstr);

    /* DFU write: the device is a running U-Boot DFU gadget - just DFU-download
     * the file to the (single) alt setting. */
    char dmsg[256];
    jni_log("DFU write (U-Boot gadget)...\n");
    jni_progress(0, "write", "Opening DFU gadget...");
    usb_device_t *ddev = device_from_fd(fd);
    tdfu_error_t dr = ddev ? tdfu_dfu_write_device(ddev, -1, input_cstr) : TDFU_ERROR_OPEN_FAILED;
    if (ddev) device_close_android(ddev);
    if (dr == TDFU_SUCCESS) {
        jni_progress(100, "write", "Write complete!");
        jni_log("DFU write complete.\n");
    } else {
        snprintf(dmsg, sizeof(dmsg), "ERROR: DFU write failed: %s\n", tdfu_error_to_string(dr));
        jni_log(dmsg);
    }
    (*env)->ReleaseStringUTFChars(env, variant_str, variant_cstr);
    (*env)->ReleaseStringUTFChars(env, input_file_str, input_cstr);
    return (dr == TDFU_SUCCESS) ? 0 : -1;
}

/* ========================================================================== */
/* JNI: setDebug                                                              */
/* ========================================================================== */

JNIEXPORT void JNICALL
Java_com_thingino_dfu_TdfuBridge_nativeSetDebug(
        JNIEnv *env, jclass clazz, jboolean enabled) {
    (void)env;
    (void)clazz;
    g_debug_enabled = (bool)enabled;
    LOGI("Debug logging %s", enabled ? "enabled" : "disabled");
}
