/**
 * Cloner Core API Implementation
 *
 * Wraps the internal USB manager, bootstrap, and firmware functions
 * behind a clean public interface.
 */

#include "cloner/core.h"
#include "cloner/constants.h"
#include "thingino.h"
#include "ddr_config_database.h"
#include "ddr_binary_builder.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#ifdef _WIN32
#include <direct.h>
#include <io.h>
#define unlink _unlink
#define rmdir  _rmdir
#else
#include <unistd.h>
#endif

/* Internal state */
static usb_manager_t g_manager;
static device_info_t *g_devices = NULL;
static int g_device_count = 0;
static bool g_initialized = false;

/* Helper: invoke progress callback if non-NULL */
static void report_progress(cloner_progress_cb cb, void *ud, int percent, const char *stage, const char *msg) {
    if (cb)
        cb(percent, stage, msg, ud);
}

/* Helper: write buffer to a temp file */
static int write_temp_file(const char *dir, const char *name, const uint8_t *data, size_t len, char *path_out,
                           size_t path_size) {
    snprintf(path_out, path_size, "%s/%s", dir, name);
    FILE *f = fopen(path_out, "wb");
    if (!f)
        return -1;
    size_t written = fwrite(data, 1, len, f);
    fclose(f);
    return written == len ? 0 : -1;
}

cloner_error_t cloner_init(void) {
    if (g_initialized)
        return CLONER_OK;

    thingino_error_t result = usb_manager_init(&g_manager);
    if (result != THINGINO_SUCCESS)
        return CLONER_ERR_USB_INIT;

    g_initialized = true;
    return CLONER_OK;
}

void cloner_cleanup(void) {
    free(g_devices);
    g_devices = NULL;
    g_device_count = 0;
    if (g_initialized) {
        usb_manager_cleanup(&g_manager);
        g_initialized = false;
    }
}

cloner_error_t cloner_discover_devices(cloner_device_list_t *list) {
    if (!list)
        return CLONER_ERR_PARAM;
    if (!g_initialized)
        return CLONER_ERR_USB_INIT;

    free(g_devices);
    g_devices = NULL;
    g_device_count = 0;

    thingino_error_t result = usb_manager_find_devices(&g_manager, &g_devices, &g_device_count);
    if (result != THINGINO_SUCCESS)
        return CLONER_ERR_DEVICE;

    list->count = g_device_count;
    list->devices = calloc(g_device_count, sizeof(cloner_device_info_t));
    if (!list->devices && g_device_count > 0)
        return CLONER_ERR_MEMORY;

    for (int i = 0; i < g_device_count; i++) {
        list->devices[i].bus = g_devices[i].bus;
        list->devices[i].address = g_devices[i].address;
        list->devices[i].vendor_id = g_devices[i].vendor;
        list->devices[i].product_id = g_devices[i].product;
        list->devices[i].stage = (cloner_stage_t)g_devices[i].stage;
        list->devices[i].variant = (cloner_variant_t)g_devices[i].variant;
    }

    return CLONER_OK;
}

void cloner_free_device_list(cloner_device_list_t *list) {
    if (list) {
        free(list->devices);
        list->devices = NULL;
        list->count = 0;
    }
}

cloner_error_t cloner_bootstrap(int device_index, cloner_variant_t variant, const char *firmware_dir,
                                cloner_progress_cb progress, void *user_data) {

    if (!g_initialized)
        return CLONER_ERR_USB_INIT;
    if (device_index < 0 || device_index >= g_device_count)
        return CLONER_ERR_PARAM;

    g_devices[device_index].variant = (processor_variant_t)variant;

    report_progress(progress, user_data, 10, "bootstrap", "Opening device...");

    usb_device_t *device = NULL;
    thingino_error_t result = usb_manager_open_device(&g_manager, &g_devices[device_index], &device);
    if (result != THINGINO_SUCCESS)
        return CLONER_ERR_DEVICE;

    device->info.variant = (processor_variant_t)variant;

    report_progress(progress, user_data, 30, "bootstrap", "Loading firmware...");

    bootstrap_config_t config = {
        .firmware_dir = firmware_dir,
        .timeout = 5000,
    };

    report_progress(progress, user_data, 50, "bootstrap", "Bootstrapping device...");
    result = bootstrap_device(device, &config);

    usb_device_close(device);
    free(device);

    if (result != THINGINO_SUCCESS)
        return CLONER_ERR_TRANSFER;

    report_progress(progress, user_data, 100, "bootstrap", "Complete");
    return CLONER_OK;
}

cloner_error_t cloner_bootstrap_with_data(int device_index, cloner_variant_t variant, const uint8_t *ddr,
                                          size_t ddr_len, const uint8_t *spl, size_t spl_len, const uint8_t *uboot,
                                          size_t uboot_len, cloner_progress_cb progress, void *user_data) {

    if (!g_initialized)
        return CLONER_ERR_USB_INIT;
    if (device_index < 0 || device_index >= g_device_count)
        return CLONER_ERR_PARAM;
    if (!ddr || !spl || !uboot)
        return CLONER_ERR_PARAM;

    g_devices[device_index].variant = (processor_variant_t)variant;

    report_progress(progress, user_data, 10, "bootstrap", "Opening device...");

    usb_device_t *device = NULL;
    thingino_error_t result = usb_manager_open_device(&g_manager, &g_devices[device_index], &device);
    if (result != THINGINO_SUCCESS)
        return CLONER_ERR_DEVICE;

    device->info.variant = (processor_variant_t)variant;

    report_progress(progress, user_data, 20, "bootstrap", "Writing firmware to temp...");

    char tmpdir[256];
#ifdef _WIN32
    snprintf(tmpdir, sizeof(tmpdir), "%s\\cloner-api-XXXXXX", getenv("TEMP") ? getenv("TEMP") : ".");
    if (_mkdir(tmpdir) != 0) {
#else
    snprintf(tmpdir, sizeof(tmpdir), "/tmp/cloner-api-XXXXXX");
    if (!mkdtemp(tmpdir)) {
#endif
        usb_device_close(device);
        free(device);
        return CLONER_ERR_FILE;
    }

    char ddr_path[512] = "", spl_path[512] = "", uboot_path[512] = "";
    if (write_temp_file(tmpdir, "ddr.bin", ddr, ddr_len, ddr_path, sizeof(ddr_path)) < 0 ||
        write_temp_file(tmpdir, "spl.bin", spl, spl_len, spl_path, sizeof(spl_path)) < 0 ||
        write_temp_file(tmpdir, "uboot.bin", uboot, uboot_len, uboot_path, sizeof(uboot_path)) < 0) {
        unlink(ddr_path);
        unlink(spl_path);
        unlink(uboot_path);
        rmdir(tmpdir);
        usb_device_close(device);
        free(device);
        return CLONER_ERR_FILE;
    }

    bootstrap_config_t config = {
        .config_file = ddr_path,
        .spl_file = spl_path,
        .uboot_file = uboot_path,
        .timeout = 5000,
    };

    report_progress(progress, user_data, 50, "bootstrap", "Bootstrapping device...");
    result = bootstrap_device(device, &config);

    unlink(ddr_path);
    unlink(spl_path);
    unlink(uboot_path);
    rmdir(tmpdir);

    usb_device_close(device);
    free(device);

    if (result != THINGINO_SUCCESS)
        return CLONER_ERR_TRANSFER;

    report_progress(progress, user_data, 100, "bootstrap", "Complete");
    return CLONER_OK;
}

cloner_error_t cloner_write_firmware(int device_index, const uint8_t *firmware, size_t len, cloner_progress_cb progress,
                                     void *user_data) {

    if (!g_initialized)
        return CLONER_ERR_USB_INIT;
    if (device_index < 0 || device_index >= g_device_count)
        return CLONER_ERR_PARAM;
    if (!firmware || len == 0)
        return CLONER_ERR_PARAM;

    report_progress(progress, user_data, 10, "write", "Preparing...");

    /* Write firmware data to a temp file for cloner_op_write_firmware */
    char tmpfile[] = "/tmp/cloner-fw-XXXXXX";
    int tmpfd = mkstemp(tmpfile);
    if (tmpfd < 0)
        return CLONER_ERR_FILE;
    if (write(tmpfd, firmware, len) != (ssize_t)len) {
        close(tmpfd);
        unlink(tmpfile);
        return CLONER_ERR_FILE;
    }
    close(tmpfd);

    report_progress(progress, user_data, 20, "write", "Writing firmware...");

    /* Use the full operations-layer write flow (partition marker + descriptor
     * + erase + handshake + chunked write).
     * Don't pass force_cpu — let operations.c auto-detect or use the
     * firmware-stage default profile (T31), which supports JEDEC flash
     * auto-detect. Forcing the bootrom variant (e.g. T20) selects a
     * profile that may require --flash-chip. */
    thingino_error_t result = cloner_op_write_firmware(
        &g_manager, device_index, tmpfile,
        NULL,           /* force_cpu (auto) */
        NULL,           /* flash_chip_name (auto-detect) */
        false,          /* no_erase */
#ifdef __EMSCRIPTEN__
        true,           /* reboot_after (web flasher: auto-reboot) */
#else
        false,          /* reboot_after */
#endif
        false,          /* do_bootstrap (let ops handle it) */
        false,          /* verbose */
        false,          /* skip_ddr */
        NULL, NULL, NULL, /* config/spl/uboot files */
        "./firmware",  /* firmware_dir */
        0               /* chunk_size (default) */
    );

    unlink(tmpfile);

    if (result != THINGINO_SUCCESS)
        return CLONER_ERR_TRANSFER;

    report_progress(progress, user_data, 100, "write", "Complete");
    return CLONER_OK;
}

cloner_error_t cloner_read_firmware(int device_index, uint8_t **firmware, size_t *len, cloner_progress_cb progress,
                                    void *user_data) {

    if (!g_initialized)
        return CLONER_ERR_USB_INIT;
    if (device_index < 0 || device_index >= g_device_count)
        return CLONER_ERR_PARAM;
    if (!firmware || !len)
        return CLONER_ERR_PARAM;

    report_progress(progress, user_data, 10, "read", "Starting read...");

    /* Use the full operations-layer read flow (partition marker + descriptor
     * + handshake + chunked read).  It writes to a file, so use a temp. */
    char tmpfile[] = "/tmp/cloner-rd-XXXXXX";
    int tmpfd = mkstemp(tmpfile);
    if (tmpfd < 0)
        return CLONER_ERR_FILE;
    close(tmpfd);

    report_progress(progress, user_data, 20, "read", "Reading flash...");

    /* Don't pass force_cpu — let operations.c use the firmware-stage
     * default profile, which supports JEDEC flash auto-detect. */
    thingino_error_t result = cloner_op_read_firmware(&g_manager, device_index, tmpfile, NULL, NULL);
    if (result != THINGINO_SUCCESS) {
        unlink(tmpfile);
        return CLONER_ERR_TRANSFER;
    }

    report_progress(progress, user_data, 90, "read", "Loading data...");

    /* Read temp file into buffer */
    result = load_file(tmpfile, firmware, len);
    unlink(tmpfile);

    if (result != THINGINO_SUCCESS)
        return CLONER_ERR_FILE;

#ifdef __EMSCRIPTEN__
    /* Web flasher: reboot device after read so it boots the firmware */
    {
        usb_device_t *rdev = NULL;
        if (usb_manager_open_device(&g_manager, &g_devices[device_index], &rdev) == THINGINO_SUCCESS) {
            usb_device_vendor_request(rdev, REQUEST_TYPE_VENDOR, VR_REBOOT, 0, 0, NULL, 0, NULL, NULL);
            usb_device_close(rdev);
            free(rdev);
        }
    }
#endif

    report_progress(progress, user_data, 100, "read", "Complete");
    return CLONER_OK;
}

const void *cloner_find_ddr_chip(const char *name) {
    return ddr_chip_config_get(name);
}

cloner_error_t cloner_generate_ddr(const char *processor_name, uint8_t *out, size_t *out_len) {

    if (!processor_name || !out || !out_len)
        return CLONER_ERR_PARAM;

    /* Caller must provide at least DDR_BINARY_SIZE_MAX bytes */
    if (*out_len < DDR_BINARY_SIZE_MAX)
        return CLONER_ERR_PARAM;

    platform_config_t platform;
    if (ddr_get_platform_config(processor_name, &platform) != 0)
        return CLONER_ERR_PARAM;

    const ddr_chip_config_t *chip = ddr_chip_config_get_default(processor_name);
    if (!chip)
        return CLONER_ERR_PARAM;

    ddr_phy_params_t params;
    ddr_chip_to_phy_params(chip, platform.ddr_freq, &params);

    *out_len = ddr_build_binary(&platform, &params, out);
    return CLONER_OK;
}

cloner_error_t cloner_set_device_variant(int device_index, cloner_variant_t variant) {
    if (!g_initialized)
        return CLONER_ERR_USB_INIT;
    if (device_index < 0 || device_index >= g_device_count)
        return CLONER_ERR_PARAM;
    g_devices[device_index].variant = (processor_variant_t)variant;
    return CLONER_OK;
}

const char *cloner_variant_to_string(cloner_variant_t variant) {
    return processor_variant_to_string((processor_variant_t)variant);
}

cloner_variant_t cloner_variant_from_string(const char *name) {
    return (cloner_variant_t)string_to_processor_variant(name);
}
