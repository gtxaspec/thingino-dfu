/**
 * tdfu Core API Implementation
 *
 * Wraps the internal USB manager, bootstrap, and firmware functions
 * behind a clean public interface.
 */

#include "tdfu/core.h"
#include "tdfu/constants.h"
#include "tdfu/tdfu.h"
#include "tdfu/dfu.h"
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
static tdfu_device_info_t *g_devices = NULL;
static int g_device_count = 0;
static bool g_initialized = false;

/* Helper: invoke progress callback if non-NULL */
static void report_progress(tdfu_progress_cb cb, void *ud, int percent, const char *stage, const char *msg) {
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

tdfu_error_t tdfu_init(void) {
    if (g_initialized)
        return TDFU_SUCCESS;

    tdfu_error_t result = usb_manager_init(&g_manager);
    if (result != TDFU_SUCCESS)
        return TDFU_ERROR_INIT_FAILED;

    g_initialized = true;
    return TDFU_SUCCESS;
}

void tdfu_cleanup(void) {
    free(g_devices);
    g_devices = NULL;
    g_device_count = 0;
    if (g_initialized) {
        usb_manager_cleanup(&g_manager);
        g_initialized = false;
    }
}

tdfu_error_t tdfu_discover_devices(tdfu_device_list_t *list) {
    if (!list)
        return TDFU_ERROR_INVALID_PARAMETER;
    if (!g_initialized)
        return TDFU_ERROR_INIT_FAILED;

    free(g_devices);
    g_devices = NULL;
    g_device_count = 0;

    tdfu_error_t result = usb_manager_find_devices(&g_manager, &g_devices, &g_device_count);
    if (result != TDFU_SUCCESS)
        return TDFU_ERROR_DEVICE_NOT_FOUND;

    list->count = g_device_count;
    list->devices = calloc(g_device_count, sizeof(tdfu_device_info_t));
    if (!list->devices && g_device_count > 0)
        return TDFU_ERROR_MEMORY;

    /* Public list now uses the canonical tdfu_device_info_t, so discovery
     * results copy directly (no field translation needed). */
    for (int i = 0; i < g_device_count; i++) {
        list->devices[i] = g_devices[i];
    }

    return TDFU_SUCCESS;
}

void tdfu_free_device_list(tdfu_device_list_t *list) {
    if (list) {
        free(list->devices);
        list->devices = NULL;
        list->count = 0;
    }
}

tdfu_error_t tdfu_bootstrap(int device_index, tdfu_variant_t variant, const char *firmware_dir,
                                tdfu_progress_cb progress, void *user_data) {

    if (!g_initialized)
        return TDFU_ERROR_INIT_FAILED;
    if (device_index < 0 || device_index >= g_device_count)
        return TDFU_ERROR_INVALID_PARAMETER;

    g_devices[device_index].variant = (tdfu_variant_t)variant;

    report_progress(progress, user_data, 10, "bootstrap", "Opening device...");

    usb_device_t *device = NULL;
    tdfu_error_t result = usb_manager_open_device(&g_manager, &g_devices[device_index], &device);
    if (result != TDFU_SUCCESS)
        return TDFU_ERROR_DEVICE_NOT_FOUND;

    device->info.variant = (tdfu_variant_t)variant;

    report_progress(progress, user_data, 30, "bootstrap", "Loading firmware...");

    bootstrap_config_t config = {
        .firmware_dir = firmware_dir,
        .timeout = 5000,
    };

    report_progress(progress, user_data, 50, "bootstrap", "Bootstrapping device...");
    result = bootstrap_device(device, &config);

    usb_device_close(device);
    free(device);

    if (result != TDFU_SUCCESS)
        return TDFU_ERROR_TRANSFER_FAILED;

    report_progress(progress, user_data, 100, "bootstrap", "Complete");
    return TDFU_SUCCESS;
}

tdfu_error_t tdfu_bootstrap_with_data(int device_index, tdfu_variant_t variant, const uint8_t *ddr,
                                          size_t ddr_len, const uint8_t *spl, size_t spl_len, const uint8_t *uboot,
                                          size_t uboot_len, tdfu_progress_cb progress, void *user_data) {

    if (!g_initialized)
        return TDFU_ERROR_INIT_FAILED;
    if (device_index < 0 || device_index >= g_device_count)
        return TDFU_ERROR_INVALID_PARAMETER;
    if (!ddr || !spl || !uboot)
        return TDFU_ERROR_INVALID_PARAMETER;

    g_devices[device_index].variant = (tdfu_variant_t)variant;

    report_progress(progress, user_data, 10, "bootstrap", "Opening device...");

    usb_device_t *device = NULL;
    tdfu_error_t result = usb_manager_open_device(&g_manager, &g_devices[device_index], &device);
    if (result != TDFU_SUCCESS)
        return TDFU_ERROR_DEVICE_NOT_FOUND;

    device->info.variant = (tdfu_variant_t)variant;

    report_progress(progress, user_data, 20, "bootstrap", "Writing firmware to temp...");

    char tmpdir[256];
#ifdef _WIN32
    snprintf(tmpdir, sizeof(tmpdir), "%s\\tdfu-api-XXXXXX", getenv("TEMP") ? getenv("TEMP") : ".");
    if (_mkdir(tmpdir) != 0) {
#else
    snprintf(tmpdir, sizeof(tmpdir), "/tmp/tdfu-api-XXXXXX");
    if (!mkdtemp(tmpdir)) {
#endif
        usb_device_close(device);
        free(device);
        return TDFU_ERROR_FILE_IO;
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
        return TDFU_ERROR_FILE_IO;
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

    if (result != TDFU_SUCCESS)
        return TDFU_ERROR_TRANSFER_FAILED;

    report_progress(progress, user_data, 100, "bootstrap", "Complete");
    return TDFU_SUCCESS;
}

tdfu_error_t tdfu_write_firmware(int device_index, const uint8_t *firmware, size_t len, tdfu_progress_cb progress,
                                     void *user_data) {

    if (!g_initialized)
        return TDFU_ERROR_INIT_FAILED;
    if (device_index < 0 || device_index >= g_device_count)
        return TDFU_ERROR_INVALID_PARAMETER;
    if (!firmware || len == 0)
        return TDFU_ERROR_INVALID_PARAMETER;

    report_progress(progress, user_data, 10, "write", "Preparing...");

    /* Write firmware data to a temp file for tdfu_op_write_firmware */
    char tmpfile[] = "/tmp/tdfu-fw-XXXXXX";
    int tmpfd = mkstemp(tmpfile);
    if (tmpfd < 0)
        return TDFU_ERROR_FILE_IO;
    if (write(tmpfd, firmware, len) != (ssize_t)len) {
        close(tmpfd);
        unlink(tmpfile);
        return TDFU_ERROR_FILE_IO;
    }
    close(tmpfd);

    report_progress(progress, user_data, 20, "write", "Writing firmware...");

    /* Use the full operations-layer write flow (partition marker + descriptor
     * + erase + handshake + chunked write).
     * Don't pass force_cpu — let operations.c auto-detect or use the
     * firmware-stage default profile (T31), which supports JEDEC flash
     * auto-detect. Forcing the bootrom variant (e.g. T20) selects a
     * profile that may require --flash-chip. */
    tdfu_error_t result = tdfu_op_write_firmware(
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

    if (result != TDFU_SUCCESS)
        return TDFU_ERROR_TRANSFER_FAILED;

    report_progress(progress, user_data, 100, "write", "Complete");
    return TDFU_SUCCESS;
}

tdfu_error_t tdfu_read_firmware(int device_index, uint8_t **firmware, size_t *len, tdfu_progress_cb progress,
                                    void *user_data) {

    if (!g_initialized)
        return TDFU_ERROR_INIT_FAILED;
    if (device_index < 0 || device_index >= g_device_count)
        return TDFU_ERROR_INVALID_PARAMETER;
    if (!firmware || !len)
        return TDFU_ERROR_INVALID_PARAMETER;

    report_progress(progress, user_data, 10, "read", "Starting read...");

    /* Use the full operations-layer read flow (partition marker + descriptor
     * + handshake + chunked read).  It writes to a file, so use a temp. */
    char tmpfile[] = "/tmp/tdfu-rd-XXXXXX";
    int tmpfd = mkstemp(tmpfile);
    if (tmpfd < 0)
        return TDFU_ERROR_FILE_IO;
    close(tmpfd);

    report_progress(progress, user_data, 20, "read", "Reading flash...");

    /* Don't pass force_cpu — let operations.c use the firmware-stage
     * default profile, which supports JEDEC flash auto-detect. */
    tdfu_error_t result = tdfu_op_read_firmware(&g_manager, device_index, tmpfile, NULL, NULL);
    if (result != TDFU_SUCCESS) {
        unlink(tmpfile);
        return TDFU_ERROR_TRANSFER_FAILED;
    }

    report_progress(progress, user_data, 90, "read", "Loading data...");

    /* Read temp file into buffer */
    result = load_file(tmpfile, firmware, len);
    unlink(tmpfile);

    if (result != TDFU_SUCCESS)
        return TDFU_ERROR_FILE_IO;

#ifdef __EMSCRIPTEN__
    /* Web flasher: reboot device after read so it boots the firmware */
    {
        usb_device_t *rdev = NULL;
        if (usb_manager_open_device(&g_manager, &g_devices[device_index], &rdev) == TDFU_SUCCESS) {
            usb_device_vendor_request(rdev, REQUEST_TYPE_VENDOR, VR_REBOOT, 0, 0, NULL, 0, NULL, NULL);
            usb_device_close(rdev);
            free(rdev);
        }
    }
#endif

    report_progress(progress, user_data, 100, "read", "Complete");
    return TDFU_SUCCESS;
}

const void *tdfu_find_ddr_chip(const char *name) {
    return ddr_chip_config_get(name);
}

tdfu_error_t tdfu_generate_ddr(const char *processor_name, uint8_t *out, size_t *out_len) {

    if (!processor_name || !out || !out_len)
        return TDFU_ERROR_INVALID_PARAMETER;

    /* Caller must provide at least DDR_BINARY_SIZE_MAX bytes */
    if (*out_len < DDR_BINARY_SIZE_MAX)
        return TDFU_ERROR_INVALID_PARAMETER;

    platform_config_t platform;
    if (ddr_get_platform_config(processor_name, &platform) != 0)
        return TDFU_ERROR_INVALID_PARAMETER;

    const ddr_chip_config_t *chip = ddr_chip_config_get_default(processor_name);
    if (!chip)
        return TDFU_ERROR_INVALID_PARAMETER;

    ddr_phy_params_t params;
    ddr_chip_to_phy_params(chip, platform.ddr_freq, &params);

    *out_len = ddr_build_binary(&platform, &params, out);
    return TDFU_SUCCESS;
}

tdfu_error_t tdfu_set_device_variant(int device_index, tdfu_variant_t variant) {
    if (!g_initialized)
        return TDFU_ERROR_INIT_FAILED;
    if (device_index < 0 || device_index >= g_device_count)
        return TDFU_ERROR_INVALID_PARAMETER;
    g_devices[device_index].variant = (tdfu_variant_t)variant;
    return TDFU_SUCCESS;
}

/* tdfu_variant_to_string / tdfu_variant_from_string live in utils.c
 * (formerly processor_variant_to_string / string_to_processor_variant);
 * the facade no longer wraps them. */

#ifdef __EMSCRIPTEN__
/* Web/JS convenience wrappers for the DFU backend, bound to the core
 * g_manager. They take alt-setting names (or "") to avoid marshalling the
 * tdfu_dfu_info_t struct across the JS boundary. */
tdfu_error_t tdfu_web_dfu_bootstrap(int device_index, const char *firmware_dir, const char *force_cpu) {
    return tdfu_dfu_bootstrap(&g_manager, device_index, firmware_dir, force_cpu, NULL, NULL);
}

/* Bootstrap with caller-supplied SPL + U-Boot (both required). Mirrors the CLI
 * --spl/--uboot override: SoC detection is skipped, the given images are used
 * verbatim. The web app writes the user's files into MEMFS and passes the paths. */
tdfu_error_t tdfu_web_dfu_bootstrap_files(int device_index, const char *spl_path, const char *uboot_path) {
    return tdfu_dfu_bootstrap(&g_manager, device_index, "", NULL, spl_path, uboot_path);
}

/* Refine the SoC variant via the register-stub probe. The bootrom magic alone
 * can't tell T32 from T31 (both report "T31V"), nor the DDR grades apart, so the
 * web runs this after a bootrom-stage discover, before picking the DFU loader.
 * Returns the refined variant name, or "" on failure (caller keeps the magic). */
const char *tdfu_web_detect_soc(int device_index) {
    if (!g_initialized || device_index < 0 || device_index >= g_device_count)
        return "";
    usb_device_t *device = NULL;
    if (usb_manager_open_device(&g_manager, &g_devices[device_index], &device) != TDFU_SUCCESS)
        return "";
    tdfu_variant_t v = g_devices[device_index].variant;
    tdfu_error_t r = protocol_detect_soc(device, &v);
    usb_device_close(device);
    free(device);
    if (r != TDFU_SUCCESS)
        return "";
    g_devices[device_index].variant = v;
    return tdfu_variant_to_string(v);
}

static int web_dfu_resolve_alt(int device_index, const char *alt_name) {
    tdfu_dfu_info_t info;
    tdfu_error_t pr = tdfu_dfu_probe(&g_manager, device_index, &info);
    if (pr != TDFU_SUCCESS) {
        LOG_ERROR("DFU probe failed: %s\n", tdfu_error_to_string(pr));
        return -1;
    }
    LOG_INFO("DFU probe ok: %d alt setting(s), transfer size %u, DFU %x.%02x\n", info.alt_count, info.transfer_size,
             (info.bcd_dfu >> 8) & 0xff, info.bcd_dfu & 0xff);
    for (int i = 0; i < info.alt_count; i++)
        LOG_INFO("  alt %d: \"%s\"\n", info.alts[i].alt, info.alts[i].name);

    if (alt_name && alt_name[0]) {
        int a = tdfu_dfu_find_alt(&info, alt_name);
        if (a < 0)
            LOG_ERROR("DFU alt \"%s\" not found\n", alt_name);
        return a;
    }
    if (info.alt_count == 1)
        return info.alts[0].alt;
    LOG_ERROR("Device exposes %d alt settings - select one in Settings\n", info.alt_count);
    return -1;
}

tdfu_error_t tdfu_web_dfu_download(int device_index, const char *alt_name, const char *path) {
    int alt = web_dfu_resolve_alt(device_index, alt_name);
    if (alt < 0)
        return TDFU_ERROR_INVALID_PARAMETER;
    return tdfu_dfu_download(&g_manager, device_index, alt, path);
}

tdfu_error_t tdfu_web_dfu_upload(int device_index, const char *alt_name, const char *path, uint32_t size) {
    int alt = web_dfu_resolve_alt(device_index, alt_name);
    if (alt < 0)
        return TDFU_ERROR_INVALID_PARAMETER;
    return tdfu_dfu_upload(&g_manager, device_index, alt, path, size);
}
#endif /* __EMSCRIPTEN__ */
