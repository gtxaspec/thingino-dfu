/**
 * DFU backend - public API
 *
 * Host-side USB DFU 1.1 implementation for devices already running in
 * U-Boot DFU mode (the `dfu` command). The medium (SPI NOR / SPI NAND /
 * MMC / MTD / ...) is entirely a device-side concern selected via U-Boot's
 * dfu_alt_info; this host only sees opaque, named alt-settings and moves
 * bytes. Plain DFU 1.1 only - no DfuSe (ST) extension, which U-Boot does
 * not use.
 */

#ifndef TDFU_DFU_H
#define TDFU_DFU_H

#include "tdfu/tdfu.h"

#define TDFU_DFU_MAX_ALTS 32

/* A single DFU alt-setting (one flash region/partition as named by the
 * device's dfu_alt_info, e.g. "spl", "u-boot", "rootfs"). */
typedef struct {
    int alt;       /* bAlternateSetting */
    char name[64]; /* iInterface string, or "" if none */
} tdfu_dfu_alt_t;

/* Capabilities read from the DFU functional descriptor + the alt list. */
typedef struct {
    uint16_t transfer_size; /* wTransferSize - max bytes per DNLOAD/UPLOAD */
    uint16_t bcd_dfu;       /* bcdDFUVersion (0x0110 for DFU 1.1) */
    uint8_t attributes;     /* bmAttributes (bitCanDnload/Upload/Manifest) */
    int interface;          /* DFU interface number (U-Boot: 0) */
    int alt_count;
    tdfu_dfu_alt_t alts[TDFU_DFU_MAX_ALTS];
} tdfu_dfu_info_t;

/**
 * Probe a device that is already in U-Boot DFU mode: read the DFU functional
 * descriptor and enumerate its alt-settings (with names). Fills *info.
 */
tdfu_error_t tdfu_dfu_probe(usb_manager_t *manager, int device_index, tdfu_dfu_info_t *info);

/**
 * Resolve an alt-setting from a name (e.g. "u-boot") or a decimal number
 * string (e.g. "1"). Returns the alt number, or -1 if not found.
 */
int tdfu_dfu_find_alt(const tdfu_dfu_info_t *info, const char *name_or_num);

/**
 * Download (write) a file to the given alt-setting via DFU DNLOAD.
 * Honors the device's wTransferSize and bwPollTimeout.
 */
tdfu_error_t tdfu_dfu_download(usb_manager_t *manager, int device_index, int alt, const char *path);

/**
 * Upload (read) from the given alt-setting to a file via DFU UPLOAD.
 * size = 0 reads until the device returns a short block (whole partition).
 */
tdfu_error_t tdfu_dfu_upload(usb_manager_t *manager, int device_index, int alt, const char *path, uint32_t size);

/* Verify flash contents against a source image: DFU-upload the same alt and
 * compare block-by-block, stopping at the image length. TDFU_ERROR_VERIFY on
 * mismatch, with *mismatch_off (may be NULL) set to the first differing offset
 * (or the device's short read length). Run after tdfu_dfu_download with the
 * same alt and file. */
tdfu_error_t tdfu_dfu_verify(usb_manager_t *manager, int device_index, int alt, const char *path,
                             uint64_t *mismatch_off);

/* Whole-chip erase via the loader's "erase" alt (u-boot-ingenic USB-boot
 * loaders): DFU-download the wipe token to it. DESTRUCTIVE: wipes the entire
 * boot flash (NAND skips bad blocks). Required before writing a NAND UBI
 * image smaller than the chip - UBI needs the space beyond the image erased.
 * Fails with a clear error when the loader predates the "erase" alt. */
#define TDFU_DFU_ERASE_ALT "erase"
#define TDFU_DFU_ERASE_TOKEN "XBURST-FLASH-WIPE"
tdfu_error_t tdfu_dfu_erase(usb_manager_t *manager, int device_index);

/* Reboot the SoC via the "reboot" virt alt: downloading the token makes the
 * loader reset in the manifest phase. Runs last (after any --erase/-w/-r), so
 * the box boots straight into what was just flashed. The reset makes the
 * device vanish, which is treated as success. Fails with a clear error on a
 * loader that predates the "reboot" alt. */
#define TDFU_DFU_REBOOT_ALT "reboot"
#define TDFU_DFU_REBOOT_TOKEN "XBURST-REBOOT"
tdfu_error_t tdfu_dfu_reboot(usb_manager_t *manager, int device_index);

/* Default alt for a transfer when the user gave none: the alt named "flash"
 * (the boot flash on the u-boot-ingenic loaders), else a single alt if that
 * is all there is. Returns the alt number, or -1 (caller must ask for --alt). */
int tdfu_dfu_default_alt(const tdfu_dfu_info_t *info);

/**
 * Bootstrap a device from the Ingenic bootrom (a108:c309) into U-Boot DFU mode:
 * USB-boot a DFU-capable SPL + U-Boot and start it; the device then re-enumerates
 * as a DFU gadget (a108:4d44).
 *
 * If spl_override and uboot_override are both non-empty, those exact files are
 * used and SoC detection is skipped (like t31-usbboot.py). Otherwise the SoC is
 * detected (probe program, or force_cpu) and the images are loaded from
 * <firmware_dir>/dfu/<variant>/{spl,uboot}.bin. spl_override/uboot_override may
 * be NULL.
 */
tdfu_error_t tdfu_dfu_bootstrap(usb_manager_t *manager, int device_index, const char *firmware_dir,
                                const char *force_cpu, const char *spl_override, const char *uboot_override);

/* Non-destructive check for the presence of the U-Boot DFU gadget (a108:4d44).
 * Safe to poll - does not open, probe, or reset any device. */
bool tdfu_dfu_gadget_present(usb_manager_t *manager);

/* Device-level DFU operations on an ALREADY-OPEN usb_device_t (the caller owns
 * it). These back the manager-based functions above and are also used directly
 * by the Android JNI, which wraps an OS-provided fd (no manager enumeration).
 * For read/write, alt < 0 selects the single/first alt setting. */
tdfu_error_t tdfu_dfu_read_device(usb_device_t *dev, int alt, const char *path, uint32_t size);
tdfu_error_t tdfu_dfu_write_device(usb_device_t *dev, int alt, const char *path);
tdfu_error_t tdfu_dfu_verify_device(usb_device_t *dev, int alt, const char *path, uint64_t *mismatch_off);
tdfu_error_t tdfu_dfu_erase_device(usb_device_t *dev);
tdfu_error_t tdfu_dfu_reboot_device(usb_device_t *dev);
tdfu_error_t tdfu_dfu_bootstrap_device(usb_device_t *dev, const uint8_t *spl, size_t spl_len, const uint8_t *uboot,
                                       size_t uboot_len);

/* firmware/dfu/<dir> name for a detected SoC variant (dir == variant string for
 * per-variant loaders; family/grade enums fall back to the closest loader).
 * The bootstrap uses this to locate the loader; the web build calls it too so
 * the JS fetch path matches - single source of truth. */
const char *tdfu_dfu_variant_dir(tdfu_variant_t v);

#endif /* TDFU_DFU_H */
