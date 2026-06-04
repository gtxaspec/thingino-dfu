/**
 * DFU backend - host-side USB DFU 1.1 for devices in U-Boot DFU mode.
 *
 * Plain DFU 1.1 only (no DfuSe). Medium-agnostic: the host just moves bytes
 * to/from named alt-settings; where they land (SPI NOR / SPI NAND / MMC /
 * MTD / ...) is decided by the device's U-Boot dfu_alt_info.
 *
 * Built on usb_device_control_transfer() for the class requests, so it works
 * over any of libtdfu's USB backends that implement control transfers.
 * Device discovery + descriptor parsing use libusb directly (native/Android);
 * the WebUSB shim needs equivalents for full web support.
 */

#include "tdfu/tdfu.h"
#include "tdfu/dfu.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ----- DFU class requests (USB DFU 1.1, section 3) ----- */
#define DFU_DETACH    0
#define DFU_DNLOAD    1
#define DFU_UPLOAD    2
#define DFU_GETSTATUS 3
#define DFU_CLRSTATUS 4
#define DFU_GETSTATE  5
#define DFU_ABORT     6

/* bmRequestType: class request, recipient = interface */
#define DFU_BMREQ_OUT 0x21 /* host -> device */
#define DFU_BMREQ_IN  0xA1 /* device -> host */

/* DFU states (GETSTATUS bState) */
enum {
    DFU_STATE_appIDLE = 0,
    DFU_STATE_appDETACH,
    DFU_STATE_dfuIDLE,
    DFU_STATE_dfuDNLOAD_SYNC,
    DFU_STATE_dfuDNBUSY,
    DFU_STATE_dfuDNLOAD_IDLE,
    DFU_STATE_dfuMANIFEST_SYNC,
    DFU_STATE_dfuMANIFEST,
    DFU_STATE_dfuMANIFEST_WAIT_RESET,
    DFU_STATE_dfuUPLOAD_IDLE,
    DFU_STATE_dfuERROR,
};

#define DFU_STATUS_OK 0x00

/* DFU functional descriptor + interface identifiers */
#define DFU_FUNC_DESC_TYPE     0x21
#define DFU_INTERFACE_CLASS    0xFE /* application specific */
#define DFU_INTERFACE_SUBCLASS 0x01 /* DFU */

typedef struct {
    uint8_t bStatus;
    uint32_t bwPollTimeout; /* milliseconds (24-bit) */
    uint8_t bState;
    uint8_t iString;
} dfu_status_t;

/* ====================================================================== */
/* Class requests                                                          */
/* ====================================================================== */

static tdfu_error_t dfu_get_status(usb_device_t *dev, uint16_t iface, dfu_status_t *st) {
    uint8_t buf[6];
    int got = 0;
    tdfu_error_t r = usb_device_control_transfer(dev, DFU_BMREQ_IN, DFU_GETSTATUS, 0, iface, buf, sizeof(buf), &got);
    if (r != TDFU_SUCCESS)
        return r;
    if (got != 6)
        return TDFU_ERROR_PROTOCOL;
    st->bStatus = buf[0];
    st->bwPollTimeout = (uint32_t)buf[1] | ((uint32_t)buf[2] << 8) | ((uint32_t)buf[3] << 16);
    st->bState = buf[4];
    st->iString = buf[5];
    return TDFU_SUCCESS;
}

static tdfu_error_t dfu_clr_status(usb_device_t *dev, uint16_t iface) {
    return usb_device_control_transfer(dev, DFU_BMREQ_OUT, DFU_CLRSTATUS, 0, iface, NULL, 0, NULL);
}

static tdfu_error_t dfu_abort(usb_device_t *dev, uint16_t iface) {
    return usb_device_control_transfer(dev, DFU_BMREQ_OUT, DFU_ABORT, 0, iface, NULL, 0, NULL);
}

static tdfu_error_t dfu_dnload(usb_device_t *dev, uint16_t iface, uint16_t block, const uint8_t *data, uint16_t len) {
    return usb_device_control_transfer(dev, DFU_BMREQ_OUT, DFU_DNLOAD, block, iface, (uint8_t *)data, len, NULL);
}

static tdfu_error_t dfu_upload_block(usb_device_t *dev, uint16_t iface, uint16_t block, uint8_t *data, uint16_t len,
                                     int *got) {
    return usb_device_control_transfer(dev, DFU_BMREQ_IN, DFU_UPLOAD, block, iface, data, len, got);
}

/* Drive the device back to dfuIDLE (clear errors / abort a stuck transfer). */
static tdfu_error_t dfu_make_idle(usb_device_t *dev, uint16_t iface) {
    dfu_status_t st;
    for (int retry = 0; retry < 4; retry++) {
        if (dfu_get_status(dev, iface, &st) != TDFU_SUCCESS) {
            dfu_abort(dev, iface);
            continue;
        }
        if (st.bState == DFU_STATE_dfuIDLE)
            return TDFU_SUCCESS;
        if (st.bState == DFU_STATE_dfuERROR)
            dfu_clr_status(dev, iface);
        else
            dfu_abort(dev, iface);
    }
    return TDFU_ERROR_PROTOCOL;
}

/* Poll GETSTATUS, honoring bwPollTimeout while the device is busy. Returns
 * the settled status in *st. Critical for slow NAND/NOR erase+write. */
static tdfu_error_t dfu_poll_until_ready(usb_device_t *dev, uint16_t iface, dfu_status_t *st) {
    for (int i = 0; i < 1000; i++) {
        tdfu_error_t r = dfu_get_status(dev, iface, st);
        if (r != TDFU_SUCCESS)
            return r;
        if (st->bStatus != DFU_STATUS_OK)
            return TDFU_ERROR_PROTOCOL;
        if (st->bState != DFU_STATE_dfuDNBUSY && st->bState != DFU_STATE_dfuMANIFEST)
            return TDFU_SUCCESS;
        if (st->bwPollTimeout)
            tdfu_sleep_milliseconds(st->bwPollTimeout);
    }
    return TDFU_ERROR_TIMEOUT;
}

/* ====================================================================== */
/* Descriptor parsing + device discovery                                  */
/* ====================================================================== */

/* Extract wTransferSize/bmAttributes/bcdDFUVersion from a DFU functional
 * descriptor (type 0x21) if present in the given extra-descriptor blob. */
static bool dfu_parse_func_desc(const uint8_t *extra, int len, tdfu_dfu_info_t *info) {
    while (len >= 2) {
        uint8_t blen = extra[0];
        uint8_t btype = extra[1];
        if (blen < 2 || blen > len)
            break;
        if (btype == DFU_FUNC_DESC_TYPE && blen >= 9) {
            info->attributes = extra[2];
            info->transfer_size = (uint16_t)extra[5] | ((uint16_t)extra[6] << 8);
            info->bcd_dfu = (uint16_t)extra[7] | ((uint16_t)extra[8] << 8);
            return true;
        }
        extra += blen;
        len -= blen;
    }
    return false;
}

static tdfu_error_t dfu_read_info(usb_device_t *dev, tdfu_dfu_info_t *info) {
    memset(info, 0, sizeof(*info));
    info->transfer_size = 1024; /* conservative default if no functional descriptor */

    struct libusb_config_descriptor *cfg = NULL;
    if (libusb_get_active_config_descriptor(dev->device, &cfg) != 0 || !cfg) {
        /* Fall back to the first config descriptor when the kernel has not
         * set an active configuration on the gadget. */
        if (libusb_get_config_descriptor(dev->device, 0, &cfg) != 0 || !cfg)
            return TDFU_ERROR_PROTOCOL;
    }

    bool found_func = false;
    for (int i = 0; i < cfg->bNumInterfaces && info->alt_count < TDFU_DFU_MAX_ALTS; i++) {
        const struct libusb_interface *itf = &cfg->interface[i];
        for (int j = 0; j < itf->num_altsetting && info->alt_count < TDFU_DFU_MAX_ALTS; j++) {
            const struct libusb_interface_descriptor *id = &itf->altsetting[j];
            if (id->bInterfaceClass != DFU_INTERFACE_CLASS || id->bInterfaceSubClass != DFU_INTERFACE_SUBCLASS)
                continue;

            info->interface = id->bInterfaceNumber;
            tdfu_dfu_alt_t *a = &info->alts[info->alt_count++];
            a->alt = id->bAlternateSetting;
            a->name[0] = '\0';
            if (id->iInterface) {
                unsigned char s[64];
                int n = libusb_get_string_descriptor_ascii(dev->handle, id->iInterface, s, sizeof(s));
                if (n > 0) {
                    if (n >= (int)sizeof(a->name))
                        n = sizeof(a->name) - 1;
                    memcpy(a->name, s, n);
                    a->name[n] = '\0';
                }
            }
            if (!found_func)
                found_func = dfu_parse_func_desc(id->extra, id->extra_length, info);
        }
    }
    if (!found_func)
        dfu_parse_func_desc(cfg->extra, cfg->extra_length, info);
    libusb_free_config_descriptor(cfg);

    if (info->alt_count == 0) {
        LOG_ERROR("No DFU interface found - is the device in U-Boot DFU mode?\n");
        return TDFU_ERROR_DEVICE_NOT_FOUND;
    }
    if (info->transfer_size == 0)
        info->transfer_size = 1024;
    return TDFU_SUCCESS;
}

/* Open the index-th USB device that exposes a DFU interface (like dfu-util,
 * we match the DFU interface descriptor, not a specific VID/PID). */
static tdfu_error_t dfu_open_device(usb_manager_t *manager, int index, usb_device_t **out) {
    if (!manager || !manager->context)
        return TDFU_ERROR_INIT_FAILED;

    libusb_device **list = NULL;
    ssize_t cnt = libusb_get_device_list(manager->context, &list);
    if (cnt < 0)
        return TDFU_ERROR_INIT_FAILED;

    LOG_DEBUG("dfu_open_device: scanning %zd USB devices for a DFU interface\n", cnt);
    libusb_device *target = NULL;
    int match = 0;
    for (ssize_t i = 0; i < cnt && !target; i++) {
        struct libusb_config_descriptor *cfg = NULL;
        int rc = libusb_get_active_config_descriptor(list[i], &cfg);
        if (rc != 0 || !cfg) {
            /* The "active config" query fails when the kernel has not set a
             * configuration (common for a driverless DFU gadget); fall back
             * to the first config descriptor. */
            rc = libusb_get_config_descriptor(list[i], 0, &cfg);
            if (rc != 0 || !cfg) {
                LOG_DEBUG("  [%zd] no config descriptor (%s)\n", i, libusb_error_name(rc));
                continue;
            }
        }
        bool is_dfu = false;
        for (int a = 0; a < cfg->bNumInterfaces && !is_dfu; a++) {
            for (int b = 0; b < cfg->interface[a].num_altsetting; b++) {
                const struct libusb_interface_descriptor *id = &cfg->interface[a].altsetting[b];
                if (id->bInterfaceClass == DFU_INTERFACE_CLASS && id->bInterfaceSubClass == DFU_INTERFACE_SUBCLASS) {
                    is_dfu = true;
                    break;
                }
            }
        }
        libusb_free_config_descriptor(cfg);
        if (is_dfu) {
            if (match == index)
                target = list[i];
            match++;
        }
    }

    if (!target) {
        libusb_free_device_list(list, 1);
        return TDFU_ERROR_DEVICE_NOT_FOUND;
    }

    libusb_device_handle *handle = NULL;
    int rc = libusb_open(target, &handle);
    libusb_free_device_list(list, 1);
    if (rc != 0 || !handle) {
        LOG_ERROR("Failed to open DFU device: %s\n", libusb_error_name(rc));
        return TDFU_ERROR_OPEN_FAILED;
    }
    libusb_set_auto_detach_kernel_driver(handle, 1);

    usb_device_t *dev = (usb_device_t *)calloc(1, sizeof(*dev));
    if (!dev) {
        libusb_close(handle);
        return TDFU_ERROR_MEMORY;
    }
    dev->handle = handle;
    dev->device = libusb_get_device(handle);
    dev->context = manager->context;
    dev->closed = false;
    *out = dev;
    return TDFU_SUCCESS;
}

static void dfu_close_device(usb_device_t *dev, int iface) {
    if (!dev)
        return;
    if (dev->handle) {
        libusb_release_interface(dev->handle, iface);
        libusb_close(dev->handle);
    }
    free(dev);
}

/* Set the device configuration (the driverless DFU gadget often has none set,
 * which makes claim fail), claim the DFU interface, and select the alt. */
static tdfu_error_t dfu_claim_alt(usb_device_t *dev, int iface, int alt) {
    struct libusb_config_descriptor *cfg = NULL;
    int cfgval = 1;
    if (libusb_get_active_config_descriptor(dev->device, &cfg) == 0 && cfg)
        cfgval = cfg->bConfigurationValue;
    else if (libusb_get_config_descriptor(dev->device, 0, &cfg) == 0 && cfg)
        cfgval = cfg->bConfigurationValue;
    if (cfg)
        libusb_free_config_descriptor(cfg);

    int cur = 0;
    if (libusb_get_configuration(dev->handle, &cur) != 0 || cur != cfgval)
        libusb_set_configuration(dev->handle, cfgval);

    if (libusb_claim_interface(dev->handle, iface) != 0) {
        LOG_ERROR("Failed to claim DFU interface %d\n", iface);
        return TDFU_ERROR_OPEN_FAILED;
    }
    /* Selecting the alt is a no-op for the default alt 0; only fatal otherwise. */
    if (libusb_set_interface_alt_setting(dev->handle, iface, alt) != 0 && alt != 0) {
        LOG_ERROR("Failed to select DFU alt setting %d\n", alt);
        return TDFU_ERROR_PROTOCOL;
    }
    return TDFU_SUCCESS;
}

/* ====================================================================== */
/* Public API                                                              */
/* ====================================================================== */

tdfu_error_t tdfu_dfu_probe(usb_manager_t *manager, int device_index, tdfu_dfu_info_t *info) {
    if (!info)
        return TDFU_ERROR_INVALID_PARAMETER;
    usb_device_t *dev = NULL;
    tdfu_error_t r = dfu_open_device(manager, device_index, &dev);
    if (r != TDFU_SUCCESS)
        return r;
    r = dfu_read_info(dev, info);
    dfu_close_device(dev, info->interface);
    return r;
}

int tdfu_dfu_find_alt(const tdfu_dfu_info_t *info, const char *name_or_num) {
    if (!info || !name_or_num)
        return -1;
    for (int i = 0; i < info->alt_count; i++) {
        if (strcmp(info->alts[i].name, name_or_num) == 0)
            return info->alts[i].alt;
    }
    /* fall back to a decimal alt number */
    char *end = NULL;
    long n = strtol(name_or_num, &end, 10);
    if (end && *end == '\0') {
        for (int i = 0; i < info->alt_count; i++)
            if (info->alts[i].alt == (int)n)
                return (int)n;
    }
    return -1;
}

tdfu_error_t tdfu_dfu_download(usb_manager_t *manager, int device_index, int alt, const char *path) {
    usb_device_t *dev = NULL;
    tdfu_dfu_info_t info;
    uint8_t *data = NULL;
    size_t len = 0;

    tdfu_error_t r = dfu_open_device(manager, device_index, &dev);
    if (r != TDFU_SUCCESS)
        return r;
    r = dfu_read_info(dev, &info);
    if (r != TDFU_SUCCESS) {
        dfu_close_device(dev, 0);
        return r;
    }
    r = dfu_claim_alt(dev, info.interface, alt);
    if (r != TDFU_SUCCESS) {
        dfu_close_device(dev, info.interface);
        return r;
    }

    r = load_file(path, &data, &len);
    if (r != TDFU_SUCCESS) {
        LOG_ERROR("Failed to read %s\n", path);
        dfu_close_device(dev, info.interface);
        return r;
    }

    LOG_INFO("DFU download: %s -> alt %d (%zu bytes, %u-byte blocks)\n", path, alt, len, info.transfer_size);

    if (dfu_make_idle(dev, info.interface) != TDFU_SUCCESS) {
        LOG_ERROR("Device not in a DFU-idle state\n");
        free(data);
        dfu_close_device(dev, info.interface);
        return TDFU_ERROR_PROTOCOL;
    }

    uint16_t block = 0;
    size_t offset = 0;
    dfu_status_t st;
    while (offset < len) {
        uint16_t chunk = (len - offset > info.transfer_size) ? info.transfer_size : (uint16_t)(len - offset);
        r = dfu_dnload(dev, info.interface, block, data + offset, chunk);
        if (r != TDFU_SUCCESS)
            break;
        r = dfu_poll_until_ready(dev, info.interface, &st);
        if (r != TDFU_SUCCESS) {
            LOG_ERROR("DFU download stalled at %zu/%zu (status %u, state %u)\n", offset, len, st.bStatus, st.bState);
            break;
        }
        offset += chunk;
        block++;
        LOG_INFO("\r  %zu/%zu bytes (%d%%)", offset, len, (int)(offset * 100 / len));
    }

    if (r == TDFU_SUCCESS) {
        /* zero-length DNLOAD signals end-of-transfer, then manifest */
        r = dfu_dnload(dev, info.interface, block, NULL, 0);
        if (r == TDFU_SUCCESS)
            r = dfu_poll_until_ready(dev, info.interface, &st);
        LOG_INFO("\n");
        if (r == TDFU_SUCCESS)
            LOG_INFO("DFU download complete\n");
    }

    free(data);
    dfu_close_device(dev, info.interface);
    return r;
}

tdfu_error_t tdfu_dfu_upload(usb_manager_t *manager, int device_index, int alt, const char *path, uint32_t size) {
    usb_device_t *dev = NULL;
    tdfu_dfu_info_t info;

    tdfu_error_t r = dfu_open_device(manager, device_index, &dev);
    if (r != TDFU_SUCCESS)
        return r;
    r = dfu_read_info(dev, &info);
    if (r != TDFU_SUCCESS) {
        dfu_close_device(dev, 0);
        return r;
    }
    r = dfu_claim_alt(dev, info.interface, alt);
    if (r != TDFU_SUCCESS) {
        dfu_close_device(dev, info.interface);
        return r;
    }

    FILE *f = fopen(path, "wb");
    if (!f) {
        LOG_ERROR("Failed to create %s\n", path);
        dfu_close_device(dev, info.interface);
        return TDFU_ERROR_FILE_IO;
    }

    LOG_INFO("DFU upload: alt %d -> %s\n", alt, path);
    if (dfu_make_idle(dev, info.interface) != TDFU_SUCCESS) {
        fclose(f);
        dfu_close_device(dev, info.interface);
        return TDFU_ERROR_PROTOCOL;
    }

    uint8_t *buf = (uint8_t *)malloc(info.transfer_size);
    if (!buf) {
        fclose(f);
        dfu_close_device(dev, info.interface);
        return TDFU_ERROR_MEMORY;
    }

    uint16_t block = 0;
    uint32_t total = 0;
    for (;;) {
        int got = 0;
        r = dfu_upload_block(dev, info.interface, block, buf, info.transfer_size, &got);
        if (r != TDFU_SUCCESS)
            break;
        if (got > 0) {
            if (fwrite(buf, 1, got, f) != (size_t)got) {
                r = TDFU_ERROR_FILE_IO;
                break;
            }
            total += got;
            LOG_INFO("\r  %u bytes", total);
        }
        block++;
        if (got < (int)info.transfer_size)
            break; /* short block = end of upload */
        if (size && total >= size)
            break;
    }
    LOG_INFO("\n");
    if (r == TDFU_SUCCESS)
        LOG_INFO("DFU upload complete: %u bytes\n", total);

    free(buf);
    fclose(f);
    dfu_close_device(dev, info.interface);
    return r;
}

/* ====================================================================== */
/* Bootrom -> DFU bootstrap (port of tools/t31-usbboot.py)                 */
/* ====================================================================== */

/* USB-boot load addresses - the same for every Ingenic SoC we target. The
 * t31-usbboot.py --ddr-config / --exec-size / address knobs are unused. */
#define DFU_SPL_ADDR   0x80001000u /* SPL load address */
#define DFU_SPL_ENTRY  0x80001800u /* SPL entry (past the 0x800 signature) */
#define DFU_UBOOT_ADDR 0x80100000u /* U-Boot load + entry address */

/* Map a detected SoC variant to its firmware/dfu/<dir> name. The mainline DFU
 * images are keyed by SoC family + DDR type (t31, t31_ddr3, t40, t40_ddr3,
 * ...), which differs from the cloner firmware naming. */
static const char *dfu_variant_dir(tdfu_variant_t v) {
    switch (v) {
    case TDFU_VARIANT_T10:
        return "t10";
    case TDFU_VARIANT_T20:
        return "t20";
    case TDFU_VARIANT_T21:
        return "t21";
    case TDFU_VARIANT_T23:
    case TDFU_VARIANT_T23DL:
        return "t23";
    case TDFU_VARIANT_T30:
        return "t30";
    case TDFU_VARIANT_T31:
    case TDFU_VARIANT_T31X:
    case TDFU_VARIANT_T31ZX:
    case TDFU_VARIANT_T31AL: /* T31AL is DDR2 */
        return "t31";
    case TDFU_VARIANT_T31A: /* T31A is DDR3 */
        return "t31_ddr3";
    case TDFU_VARIANT_T32:
        return "t32";
    case TDFU_VARIANT_T40:
        return "t40";
    case TDFU_VARIANT_T40XP: /* T40XP is DDR3 */
        return "t40_ddr3";
    case TDFU_VARIANT_T41:
        return "t41";
    case TDFU_VARIANT_A1:
        return "a1";
    default:
        return tdfu_variant_to_string(v);
    }
}

tdfu_error_t tdfu_dfu_bootstrap(usb_manager_t *manager, int device_index, const char *firmware_dir,
                                const char *force_cpu) {
    tdfu_device_info_t *devs = NULL;
    int n = 0;
    usb_device_t *dev = NULL;
    uint8_t *spl = NULL, *uboot = NULL;
    size_t spl_len = 0, uboot_len = 0;
    char spl_path[512], uboot_path[512];
    tdfu_variant_t variant;
    const char *name;
    const char *root = firmware_dir ? firmware_dir : "./firmware";

    tdfu_error_t r = usb_manager_find_devices(manager, &devs, &n);
    if (r != TDFU_SUCCESS)
        return r;
    if (device_index < 0 || device_index >= n) {
        free(devs);
        return TDFU_ERROR_DEVICE_NOT_FOUND;
    }
    r = usb_manager_open_device(manager, &devs[device_index], &dev);
    free(devs);
    if (r != TDFU_SUCCESS)
        return r;
    usb_device_claim_interface(dev);

    /* 1) Determine the SoC variant: forced, or the bootrom probe program. */
    if (force_cpu) {
        variant = tdfu_variant_from_string(force_cpu);
    } else {
        r = protocol_detect_soc(dev, &variant);
        if (r != TDFU_SUCCESS) {
            LOG_ERROR("SoC auto-detect failed; pass --cpu <variant>\n");
            usb_device_close(dev);
            free(dev);
            return r;
        }
    }
    name = dfu_variant_dir(variant);
    LOG_INFO("DFU bootstrap: SoC %s\n", name);

    /* 2) Load the DFU-capable SPL + U-Boot for this variant. */
    snprintf(spl_path, sizeof(spl_path), "%s/dfu/%s/spl.bin", root, name);
    snprintf(uboot_path, sizeof(uboot_path), "%s/dfu/%s/uboot.bin", root, name);
    r = load_file(spl_path, &spl, &spl_len);
    if (r != TDFU_SUCCESS) {
        LOG_ERROR("Missing DFU SPL: %s\n", spl_path);
        goto out;
    }
    r = load_file(uboot_path, &uboot, &uboot_len);
    if (r != TDFU_SUCCESS) {
        LOG_ERROR("Missing DFU U-Boot: %s\n", uboot_path);
        goto out;
    }

    /* 3) USB-boot sequence (mirrors t31-usbboot.py). */
    LOG_INFO("stage1 SPL: %zu bytes -> 0x%08x (entry 0x%08x)\n", spl_len, DFU_SPL_ADDR, DFU_SPL_ENTRY);
    r = bootstrap_load_data_to_memory(dev, spl, spl_len, DFU_SPL_ADDR);
    if (r != TDFU_SUCCESS)
        goto out;
    r = protocol_prog_stage1(dev, DFU_SPL_ENTRY);
    if (r != TDFU_SUCCESS)
        goto out;

    /* stage1 brings up clk+DDR and returns to the bootrom; let it settle. */
    tdfu_sleep_milliseconds(1000);

    LOG_INFO("stage2 U-Boot: %zu bytes -> 0x%08x\n", uboot_len, DFU_UBOOT_ADDR);
    r = bootstrap_load_data_to_memory(dev, uboot, uboot_len, DFU_UBOOT_ADDR);
    if (r != TDFU_SUCCESS)
        goto out;
    protocol_flush_cache(dev);
    protocol_prog_stage2(dev, DFU_UBOOT_ADDR); /* tolerates the re-enumeration error */
    LOG_INFO("U-Boot starting; device will re-enumerate in DFU mode\n");
    r = TDFU_SUCCESS;

out:
    free(spl);
    free(uboot);
    usb_device_close(dev);
    free(dev);
    return r;
}
