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

#endif /* TDFU_DFU_H */
