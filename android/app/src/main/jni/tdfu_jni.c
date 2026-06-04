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
#include "tdfu/platform_profile.h"
#include "tdfu/flash_descriptor.h"
#include "spi_nor_db.h"

/* Functions defined in libtdfu but not exposed via headers */
extern tdfu_error_t protocol_read_flash_id(usb_device_t *device, uint32_t *jedec_id);

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

/* Android-specific close: prevent libusb_close since Android owns the fd */
static void device_close_android(usb_device_t *dev) {
    if (dev) {
        device_close_android(dev);
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

    char msg[256];
    snprintf(msg, sizeof(msg), "Bootstrapping device (variant=%s)...\n", variant_cstr);
    jni_log(msg);
    jni_progress(5, "bootstrap", "Opening device...");

    usb_device_t *dev = device_from_fd(fd);
    if (!dev) {
        jni_log("ERROR: Failed to open USB device\n");
        (*env)->ReleaseStringUTFChars(env, variant_str, variant_cstr);
        (*env)->ReleaseStringUTFChars(env, firmware_dir_str, fw_dir_cstr);
        return -1;
    }

    /* Set variant */
    tdfu_variant_t variant = tdfu_variant_from_string(variant_cstr);
    dev->info.variant = variant;

    /* Extract firmware files from APK assets to cache dir */
    const char *variant_name = tdfu_variant_to_string(variant);
    char spl_asset[256], uboot_asset[256];
    char spl_path[512], uboot_path[512];

    /* The firmware directory structure is: firmware/cloner/<variant>/spl.bin, uboot.bin
     * We need to map variant names to directory names (some share dirs) */
    const char *fw_subdir = variant_name;
    /* Handle variants that share firmware directories */
    if (variant == TDFU_VARIANT_T31X || variant == TDFU_VARIANT_T31ZX) {
        fw_subdir = "t31x";
    } else if (variant == TDFU_VARIANT_T31A) {
        fw_subdir = "t31a";
    } else if (variant == TDFU_VARIANT_A1) {
        fw_subdir = "a1_n_ne_x";
    }

    snprintf(spl_asset, sizeof(spl_asset), "firmware/cloner/%s/spl.bin", fw_subdir);
    snprintf(uboot_asset, sizeof(uboot_asset), "firmware/cloner/%s/uboot.bin", fw_subdir);
    snprintf(spl_path, sizeof(spl_path), "%s/%s_spl.bin", fw_dir_cstr, fw_subdir);
    snprintf(uboot_path, sizeof(uboot_path), "%s/%s_uboot.bin", fw_dir_cstr, fw_subdir);

    jni_progress(10, "bootstrap", "Extracting firmware files...");

    if (extract_asset_to_file(env, asset_manager, spl_asset, spl_path) < 0) {
        snprintf(msg, sizeof(msg), "ERROR: Failed to extract %s\n", spl_asset);
        jni_log(msg);
        device_close_android(dev);
        (*env)->ReleaseStringUTFChars(env, variant_str, variant_cstr);
        (*env)->ReleaseStringUTFChars(env, firmware_dir_str, fw_dir_cstr);
        return -1;
    }

    if (extract_asset_to_file(env, asset_manager, uboot_asset, uboot_path) < 0) {
        snprintf(msg, sizeof(msg), "ERROR: Failed to extract %s\n", uboot_asset);
        jni_log(msg);
        device_close_android(dev);
        (*env)->ReleaseStringUTFChars(env, variant_str, variant_cstr);
        (*env)->ReleaseStringUTFChars(env, firmware_dir_str, fw_dir_cstr);
        return -1;
    }

    snprintf(msg, sizeof(msg), "Firmware files extracted to %s\n", fw_dir_cstr);
    jni_log(msg);

    jni_progress(30, "bootstrap", "Loading DDR configuration...");

    /* Build bootstrap config */
    bootstrap_config_t config = {
        .sdram_address = BOOTLOADER_ADDRESS_SDRAM,
        .timeout = BOOTSTRAP_TIMEOUT_SECONDS,
        .verbose = false,
        .skip_ddr = false,
        .config_file = NULL,
        .spl_file = spl_path,
        .uboot_file = uboot_path,
        .firmware_dir = NULL,
    };

    jni_progress(50, "bootstrap", "Running bootstrap sequence...");

    tdfu_error_t result = bootstrap_device(dev, &config);

    /* Clean up temp files */
    unlink(spl_path);
    unlink(uboot_path);

    if (result != TDFU_SUCCESS) {
        snprintf(msg, sizeof(msg), "ERROR: Bootstrap failed: %s\n",
                 tdfu_error_to_string(result));
        jni_log(msg);
        device_close_android(dev);
        (*env)->ReleaseStringUTFChars(env, variant_str, variant_cstr);
        (*env)->ReleaseStringUTFChars(env, firmware_dir_str, fw_dir_cstr);
        return (jint)result;
    }

    jni_progress(100, "bootstrap", "Bootstrap complete!");
    jni_log("Bootstrap completed successfully!\n");

    device_close_android(dev);
    (*env)->ReleaseStringUTFChars(env, variant_str, variant_cstr);
    (*env)->ReleaseStringUTFChars(env, firmware_dir_str, fw_dir_cstr);
    return 0;
}

/* ========================================================================== */
/* JNI: readFirmware                                                          */
/* ========================================================================== */

JNIEXPORT jint JNICALL
Java_com_thingino_dfu_TdfuBridge_nativeReadFirmware(
        JNIEnv *env, jclass clazz, jint fd, jstring variant_str,
        jstring output_file_str, jstring firmware_dir_str, jobject asset_manager) {
    (void)clazz;

    const char *variant_cstr = (*env)->GetStringUTFChars(env, variant_str, NULL);
    const char *output_cstr = (*env)->GetStringUTFChars(env, output_file_str, NULL);
    const char *fw_dir_cstr = (*env)->GetStringUTFChars(env, firmware_dir_str, NULL);
    if (!variant_cstr || !output_cstr || !fw_dir_cstr) {
        if (variant_cstr) (*env)->ReleaseStringUTFChars(env, variant_str, variant_cstr);
        if (output_cstr) (*env)->ReleaseStringUTFChars(env, output_file_str, output_cstr);
        if (fw_dir_cstr) (*env)->ReleaseStringUTFChars(env, firmware_dir_str, fw_dir_cstr);
        return -1;
    }

    LOGI("nativeReadFirmware: fd=%d variant=%s output=%s", fd, variant_cstr, output_cstr);

    char msg[512];
    snprintf(msg, sizeof(msg), "Starting firmware read (variant=%s)...\n", variant_cstr);
    jni_log(msg);
    jni_progress(0, "read", "Opening device...");

    LOGI("Opening device from fd=%d", fd);
    usb_device_t *dev = device_from_fd(fd);
    if (!dev) {
        LOGE("device_from_fd failed");
        jni_log("ERROR: Failed to open USB device\n");
        goto read_err;
    }
    LOGI("Device opened: handle=%p", dev->handle);

    tdfu_variant_t variant = tdfu_variant_from_string(variant_cstr);
    dev->info.variant = variant;
    LOGI("Variant set to: %s (%d)", tdfu_variant_to_string(variant), variant);

    /* Check if device needs bootstrap first */
    cpu_info_t cpu_info;
    LOGI("Getting CPU info...");
    tdfu_error_t result = usb_device_get_cpu_info(dev, &cpu_info);
    LOGI("CPU info result: %d (%s)", result, tdfu_error_to_string(result));
    if (result == TDFU_SUCCESS) {
        dev->info.stage = cpu_info.stage;
        LOGI("Device stage: %s, magic: %s", tdfu_stage_to_string(cpu_info.stage), cpu_info.clean_magic);
    }

    if (dev->info.stage == TDFU_STAGE_BOOTROM) {
        LOGI("Device in bootrom, need bootstrap");

        /* Extract firmware assets for bootstrap */
        const char *fw_subdir = tdfu_variant_to_string(variant);
        if (variant == TDFU_VARIANT_T31X || variant == TDFU_VARIANT_T31ZX) fw_subdir = "t31x";
        else if (variant == TDFU_VARIANT_T31A) fw_subdir = "t31a";
        else if (variant == TDFU_VARIANT_A1) fw_subdir = "a1_n_ne_x";

        char spl_asset[256], uboot_asset[256];
        char spl_path[512], uboot_path[512];
        snprintf(spl_asset, sizeof(spl_asset), "firmware/cloner/%s/spl.bin", fw_subdir);
        snprintf(uboot_asset, sizeof(uboot_asset), "firmware/cloner/%s/uboot.bin", fw_subdir);
        snprintf(spl_path, sizeof(spl_path), "%s/%s_spl.bin", fw_dir_cstr, fw_subdir);
        snprintf(uboot_path, sizeof(uboot_path), "%s/%s_uboot.bin", fw_dir_cstr, fw_subdir);

        LOGI("Extracting SPL asset: %s -> %s", spl_asset, spl_path);
        int rc1 = extract_asset_to_file(env, asset_manager, spl_asset, spl_path);
        LOGI("SPL extract: %d", rc1);
        LOGI("Extracting U-Boot asset: %s -> %s", uboot_asset, uboot_path);
        int rc2 = extract_asset_to_file(env, asset_manager, uboot_asset, uboot_path);
        LOGI("U-Boot extract: %d", rc2);

        if (rc1 != 0 || rc2 != 0) {
            LOGE("Failed to extract firmware assets");
            jni_log("ERROR: Failed to extract firmware from APK\n");
            device_close_android(dev);
            goto read_err;
        }

        bootstrap_config_t config = {
            .sdram_address = BOOTLOADER_ADDRESS_SDRAM,
            .timeout = BOOTSTRAP_TIMEOUT_SECONDS,
            .spl_file = spl_path,
            .uboot_file = uboot_path,
        };

        LOGI("Starting bootstrap_device...");
        result = bootstrap_device(dev, &config);
        LOGI("bootstrap_device returned: %d (%s)", result, tdfu_error_to_string(result));

        unlink(spl_path);
        unlink(uboot_path);

        if (result != TDFU_SUCCESS) {
            snprintf(msg, sizeof(msg), "ERROR: Bootstrap failed: %s\n",
                     tdfu_error_to_string(result));
            jni_log(msg);
            device_close_android(dev);
            goto read_err;
        }

        jni_log("Bootstrap complete, waiting for device to stabilize...\n");
        jni_progress(30, "read", "Waiting for device...");
        usleep(2000000);

        /* After bootstrap, device should be in firmware stage.
         * The USB handle from wrap_sys_device should still be valid
         * since the physical connection hasn't changed. */
        dev->info.stage = TDFU_STAGE_FIRMWARE;
    }

    jni_progress(35, "read", "Initiating flash read...");

    /* Use the tdfu_op_read_firmware path which handles the full
     * flash descriptor / handshake / chunked read protocol.
     *
     * However, tdfu_op_read_firmware uses usb_manager to find devices,
     * which won't work on Android. We need to replicate the read logic
     * using our wrapped device handle directly. */

    /* Step 1: Send partition marker */
    jni_log("Sending partition marker...\n");
    jni_progress(40, "read", "Sending partition marker...");
    result = flash_partition_marker_send(dev);
    if (result != TDFU_SUCCESS) {
        snprintf(msg, sizeof(msg), "ERROR: Partition marker failed: %s\n",
                 tdfu_error_to_string(result));
        jni_log(msg);
        device_close_android(dev);
        goto read_err;
    }

    /* Read ack after marker */
    {
        uint8_t ack_buf[4] = {0};
        int ack_len = 0;
        usb_device_vendor_request(dev, REQUEST_TYPE_VENDOR, VR_FW_READ, 0, 0,
                                   NULL, 4, ack_buf, &ack_len);
    }

    /* Step 2: Auto-detect flash chip via JEDEC ID */
    jni_log("Detecting flash chip...\n");
    jni_progress(45, "read", "Detecting flash chip...");

    uint32_t jedec_id = 0;
    const spi_nor_chip_t *flash_chip = NULL;
    if (protocol_read_flash_id(dev, &jedec_id) == TDFU_SUCCESS) {
        flash_chip = spi_nor_find_by_id(jedec_id);
        if (flash_chip) {
            snprintf(msg, sizeof(msg), "Flash chip: %s (JEDEC 0x%06X, %u MB)\n",
                     flash_chip->name, flash_chip->jedec_id,
                     flash_chip->size / (1024 * 1024));
            jni_log(msg);
        } else {
            snprintf(msg, sizeof(msg), "WARNING: JEDEC 0x%06X not in database\n", jedec_id);
            jni_log(msg);
        }
    }

    if (!flash_chip) {
        jni_log("ERROR: Flash chip not detected\n");
        device_close_android(dev);
        goto read_err;
    }

    /* Step 3: Send read descriptor */
    jni_log("Sending read descriptor...\n");
    jni_progress(50, "read", "Sending read descriptor...");

    const platform_profile_t *profile = platform_get_profile(dev->info.variant);

    if (profile->crc_format == CRC_FMT_A1) {
        uint8_t desc[FLASH_DESCRIPTOR_SIZE_A1];
        flash_descriptor_create_a1_read(flash_chip, desc);
        result = flash_descriptor_send_sized(dev, desc, FLASH_DESCRIPTOR_SIZE_A1);
    } else if (profile->crc_format == CRC_FMT_VENDOR) {
        uint8_t desc[FLASH_DESCRIPTOR_SIZE_XB2];
        flash_descriptor_create_xb2_read(flash_chip, desc);
        result = flash_descriptor_send_sized(dev, desc, FLASH_DESCRIPTOR_SIZE_XB2);
    } else if (dev->info.variant == TDFU_VARIANT_T32) {
        uint8_t desc[FLASH_DESCRIPTOR_SIZE_T32];
        flash_descriptor_create_t32_read(flash_chip, desc);
        result = flash_descriptor_send_sized(dev, desc, FLASH_DESCRIPTOR_SIZE_T32);
    } else {
        uint8_t desc[FLASH_DESCRIPTOR_SIZE];
        flash_descriptor_create_read(flash_chip, desc);
        result = flash_descriptor_send(dev, desc);
    }

    if (result != TDFU_SUCCESS) {
        snprintf(msg, sizeof(msg), "ERROR: Failed to send read descriptor: %s\n",
                 tdfu_error_to_string(result));
        jni_log(msg);
        device_close_android(dev);
        goto read_err;
    }

    usleep(500000);

    /* Step 4: Handshake */
    jni_log("Initializing read handshake...\n");
    jni_progress(55, "read", "Handshaking...");

    result = firmware_handshake_init(dev);
    if (result != TDFU_SUCCESS) {
        snprintf(msg, sizeof(msg), "ERROR: Handshake failed: %s\n",
                 tdfu_error_to_string(result));
        jni_log(msg);
        device_close_android(dev);
        goto read_err;
    }

    /* Step 5: Read firmware data */
    uint32_t read_size = flash_chip->size;
    snprintf(msg, sizeof(msg), "Reading %u bytes (%.2f MB) from flash...\n",
             read_size, (float)read_size / (1024 * 1024));
    jni_log(msg);
    jni_progress(60, "read", "Reading flash...");

    uint8_t *firmware_data = NULL;
    uint32_t firmware_size = 0;
    result = firmware_read_full(dev, read_size, &firmware_data, &firmware_size);

    if (result != TDFU_SUCCESS || !firmware_data) {
        snprintf(msg, sizeof(msg), "ERROR: Read failed: %s\n",
                 tdfu_error_to_string(result));
        jni_log(msg);
        free(firmware_data);
        device_close_android(dev);
        goto read_err;
    }

    jni_progress(90, "read", "Saving to file...");

    /* Step 6: Save to file */
    FILE *outfile = fopen(output_cstr, "wb");
    if (!outfile) {
        snprintf(msg, sizeof(msg), "ERROR: Cannot create %s: %s\n",
                 output_cstr, strerror(errno));
        jni_log(msg);
        free(firmware_data);
        device_close_android(dev);
        goto read_err;
    }

    size_t written = fwrite(firmware_data, 1, firmware_size, outfile);
    fclose(outfile);
    free(firmware_data);

    if (written != firmware_size) {
        snprintf(msg, sizeof(msg), "WARNING: Only %zu of %u bytes written\n",
                 written, firmware_size);
        jni_log(msg);
    }

    snprintf(msg, sizeof(msg), "Firmware read complete: %s (%.2f MB)\n",
             output_cstr, (float)firmware_size / (1024 * 1024));
    jni_log(msg);
    jni_progress(100, "read", "Read complete!");

    device_close_android(dev);
    (*env)->ReleaseStringUTFChars(env, variant_str, variant_cstr);
    (*env)->ReleaseStringUTFChars(env, output_file_str, output_cstr);
    (*env)->ReleaseStringUTFChars(env, firmware_dir_str, fw_dir_cstr);
    return 0;

read_err:
    (*env)->ReleaseStringUTFChars(env, variant_str, variant_cstr);
    (*env)->ReleaseStringUTFChars(env, output_file_str, output_cstr);
    (*env)->ReleaseStringUTFChars(env, firmware_dir_str, fw_dir_cstr);
    return -1;
}

/* ========================================================================== */
/* JNI: writeFirmware                                                         */
/* ========================================================================== */

JNIEXPORT jint JNICALL
Java_com_thingino_dfu_TdfuBridge_nativeWriteFirmware(
        JNIEnv *env, jclass clazz, jint fd, jstring variant_str,
        jstring input_file_str, jstring firmware_dir_str, jobject asset_manager) {
    (void)clazz;

    const char *variant_cstr = (*env)->GetStringUTFChars(env, variant_str, NULL);
    const char *input_cstr = (*env)->GetStringUTFChars(env, input_file_str, NULL);
    const char *fw_dir_cstr = (*env)->GetStringUTFChars(env, firmware_dir_str, NULL);
    if (!variant_cstr || !input_cstr || !fw_dir_cstr) {
        if (variant_cstr) (*env)->ReleaseStringUTFChars(env, variant_str, variant_cstr);
        if (input_cstr) (*env)->ReleaseStringUTFChars(env, input_file_str, input_cstr);
        if (fw_dir_cstr) (*env)->ReleaseStringUTFChars(env, firmware_dir_str, fw_dir_cstr);
        return -1;
    }

    LOGI("nativeWriteFirmware: fd=%d variant=%s input=%s", fd, variant_cstr, input_cstr);

    char msg[512];
    snprintf(msg, sizeof(msg), "Starting firmware write (variant=%s)...\n", variant_cstr);
    jni_log(msg);
    jni_progress(0, "write", "Opening device...");

    usb_device_t *dev = device_from_fd(fd);
    if (!dev) {
        jni_log("ERROR: Failed to open USB device\n");
        goto write_err;
    }

    tdfu_variant_t variant = tdfu_variant_from_string(variant_cstr);
    dev->info.variant = variant;

    /* Check if device needs bootstrap */
    cpu_info_t cpu_info;
    tdfu_error_t result = usb_device_get_cpu_info(dev, &cpu_info);
    if (result == TDFU_SUCCESS) {
        dev->info.stage = cpu_info.stage;
    }

    if (dev->info.stage == TDFU_STAGE_BOOTROM) {
        jni_progress(5, "write", "Device in bootrom, bootstrapping first...");
        jni_log("Device in bootrom stage, bootstrapping...\n");

        const char *fw_subdir = tdfu_variant_to_string(variant);
        if (variant == TDFU_VARIANT_T31X || variant == TDFU_VARIANT_T31ZX) fw_subdir = "t31x";
        else if (variant == TDFU_VARIANT_T31A) fw_subdir = "t31a";
        else if (variant == TDFU_VARIANT_A1) fw_subdir = "a1_n_ne_x";

        char spl_asset[256], uboot_asset[256];
        char spl_path[512], uboot_path[512];
        snprintf(spl_asset, sizeof(spl_asset), "firmware/cloner/%s/spl.bin", fw_subdir);
        snprintf(uboot_asset, sizeof(uboot_asset), "firmware/cloner/%s/uboot.bin", fw_subdir);
        snprintf(spl_path, sizeof(spl_path), "%s/%s_spl.bin", fw_dir_cstr, fw_subdir);
        snprintf(uboot_path, sizeof(uboot_path), "%s/%s_uboot.bin", fw_dir_cstr, fw_subdir);

        if (extract_asset_to_file(env, asset_manager, spl_asset, spl_path) < 0 ||
            extract_asset_to_file(env, asset_manager, uboot_asset, uboot_path) < 0) {
            LOGE("Failed to extract firmware assets for bootstrap");
            jni_log("ERROR: Failed to extract firmware from APK\n");
            device_close_android(dev);
            goto write_err;
        }

        bootstrap_config_t config = {
            .sdram_address = BOOTLOADER_ADDRESS_SDRAM,
            .timeout = BOOTSTRAP_TIMEOUT_SECONDS,
            .spl_file = spl_path,
            .uboot_file = uboot_path,
        };

        jni_progress(15, "write", "Running bootstrap...");
        result = bootstrap_device(dev, &config);

        unlink(spl_path);
        unlink(uboot_path);

        if (result != TDFU_SUCCESS) {
            snprintf(msg, sizeof(msg), "ERROR: Bootstrap failed: %s\n",
                     tdfu_error_to_string(result));
            jni_log(msg);
            device_close_android(dev);
            goto write_err;
        }

        jni_log("Bootstrap complete, waiting for device...\n");
        jni_progress(25, "write", "Waiting for device...");
        usleep(2000000);
        dev->info.stage = TDFU_STAGE_FIRMWARE;
    }

    /* Prepare flash descriptor and handshake */
    jni_progress(30, "write", "Preparing flash...");

    const platform_profile_t *profile = platform_get_profile(dev->info.variant);

    if (profile->descriptor_mode == DESC_MARKER_THEN_SEND) {
        jni_log("Sending partition marker...\n");
        result = flash_partition_marker_send(dev);
        if (result != TDFU_SUCCESS) {
            snprintf(msg, sizeof(msg), "ERROR: Partition marker failed: %s\n",
                     tdfu_error_to_string(result));
            jni_log(msg);
            device_close_android(dev);
            goto write_err;
        }

        /* Read ack */
        {
            uint8_t ack_buf[4] = {0};
            int ack_len = 0;
            usb_device_vendor_request(dev, REQUEST_TYPE_VENDOR, VR_FW_READ, 0, 0,
                                       NULL, 4, ack_buf, &ack_len);
        }

        /* Detect flash chip */
        jni_log("Detecting flash chip...\n");
        jni_progress(35, "write", "Detecting flash...");

        uint32_t jedec_id = 0;
        const spi_nor_chip_t *flash_chip = NULL;
        if (protocol_read_flash_id(dev, &jedec_id) == TDFU_SUCCESS) {
            flash_chip = spi_nor_find_by_id(jedec_id);
            if (flash_chip) {
                snprintf(msg, sizeof(msg), "Flash chip: %s (JEDEC 0x%06X)\n",
                         flash_chip->name, flash_chip->jedec_id);
                jni_log(msg);
            }
        }

        if (!flash_chip) {
            jni_log("ERROR: Flash chip not detected\n");
            device_close_android(dev);
            goto write_err;
        }

        /* Send write descriptor */
        jni_log("Sending write descriptor...\n");
        jni_progress(40, "write", "Sending flash descriptor...");

        if (profile->crc_format == CRC_FMT_A1) {
            uint8_t desc[FLASH_DESCRIPTOR_SIZE_A1];
            flash_descriptor_create_a1(flash_chip, desc);
            result = flash_descriptor_send_sized(dev, desc, FLASH_DESCRIPTOR_SIZE_A1);
        } else if (profile->skip_set_data_addr) {
            uint8_t desc[FLASH_DESCRIPTOR_SIZE_XB2];
            flash_descriptor_create_xb2(flash_chip, desc);
            result = flash_descriptor_send_sized(dev, desc, FLASH_DESCRIPTOR_SIZE_XB2);
        } else if (dev->info.variant == TDFU_VARIANT_T32) {
            uint8_t desc[FLASH_DESCRIPTOR_SIZE_T32];
            flash_descriptor_create_t32(flash_chip, desc);
            result = flash_descriptor_send_sized(dev, desc, FLASH_DESCRIPTOR_SIZE_T32);
        } else {
            uint8_t desc[FLASH_DESCRIPTOR_SIZE];
            flash_descriptor_create(flash_chip, desc);
            result = flash_descriptor_send(dev, desc);
        }

        if (result != TDFU_SUCCESS) {
            snprintf(msg, sizeof(msg), "ERROR: Flash descriptor failed: %s\n",
                     tdfu_error_to_string(result));
            jni_log(msg);
            device_close_android(dev);
            goto write_err;
        }

        usleep(500000);

        /* VR_INIT handshake */
        jni_progress(45, "write", "Initializing flash...");
        result = firmware_handshake_init(dev);
        if (result != TDFU_SUCCESS) {
            snprintf(msg, sizeof(msg), "ERROR: Handshake failed: %s\n",
                     tdfu_error_to_string(result));
            jni_log(msg);
            device_close_android(dev);
            goto write_err;
        }
    }

    /* Write firmware */
    jni_progress(50, "write", "Writing firmware to flash...");
    snprintf(msg, sizeof(msg), "Writing firmware from %s...\n", input_cstr);
    jni_log(msg);

    bool is_a1 = profile->use_a1_handshake;
    result = write_firmware_to_device(dev, input_cstr, NULL, false, is_a1, 0);

    if (result != TDFU_SUCCESS) {
        snprintf(msg, sizeof(msg), "ERROR: Write failed: %s\n",
                 tdfu_error_to_string(result));
        jni_log(msg);
        device_close_android(dev);
        goto write_err;
    }

    jni_log("Firmware write completed successfully!\n");
    jni_progress(100, "write", "Write complete!");

    device_close_android(dev);
    (*env)->ReleaseStringUTFChars(env, variant_str, variant_cstr);
    (*env)->ReleaseStringUTFChars(env, input_file_str, input_cstr);
    (*env)->ReleaseStringUTFChars(env, firmware_dir_str, fw_dir_cstr);
    return 0;

write_err:
    (*env)->ReleaseStringUTFChars(env, variant_str, variant_cstr);
    (*env)->ReleaseStringUTFChars(env, input_file_str, input_cstr);
    (*env)->ReleaseStringUTFChars(env, firmware_dir_str, fw_dir_cstr);
    return -1;
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
