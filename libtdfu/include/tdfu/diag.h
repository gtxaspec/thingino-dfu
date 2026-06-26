/**
 * tdfu Device Diagnostics
 *
 * Read-only readout of a bootrom-stage device's eFuse: SoC identity, serial,
 * secure-boot configuration, RSA key hash, and a raw eFuse-window dump.
 *
 * Reads use the bootrom's own native memory-read (VR_SET_DATA_ADDR +
 * VR_SET_DATA_LEN + bulk-IN) directly on the memory-mapped eFuse shadow window
 * — no uploaded stub. This matters: the eFuse shadow is only live in the
 * pristine bootrom; running any stage1 stub (even SoC auto-detect) clears the
 * window to zero, so the read must happen before any code is executed.
 *
 * Two eFuse layouts are decoded, both ground-truthed against the bootroms:
 *   - XBurst1 secure (T23/T31/T32): security register at 0xB3540210, key hash
 *     at 0xB3540240. Flags byte is bits [23:16] of 0x210.
 *   - XBurst2 (T40/T41/A1): security register at 0xB3540224 (flags fold
 *     ((w>>8)|w)&0xFF), key hash at 0xB3540280 (OR-redundant with 0xB35402C0).
 * Older XBurst1 (T10/T20/T21/T30) have no secure boot — serial only.
 */

#ifndef TDFU_DIAG_H
#define TDFU_DIAG_H

#include "tdfu/types.h"

#define TDFU_DIAG_WINDOW_LEN 256   /* 0x200..0x2FF: covers XB2 key + backup */

/* Normalized secure-boot flag bits (same layout for both eFuse families). */
#define TDFU_SEC_ENABLE     0x01   /* secure boot enable (SC_EN) */
#define TDFU_SEC_RSA_E3     0x04   /* RSA verify exponent: set=3, clear=65537 */
#define TDFU_SEC_USB_OFF    0x08   /* USB boot disabled under secure boot */
#define TDFU_SEC_SD_OFF     0x10   /* XB2: SD/MMC boot disabled / XB1: extra restrict */
#define TDFU_SEC_NORWR_OFF  0x40   /* XB2: NOR-via-USB-write disabled under secure boot */

/* Diagnostics read from a bootrom device. All read-only. */
typedef struct {
    char soc_name[16];       /* SoC label derived from soc_id (e.g. "T31", "A1") */
    char cpu_magic[9];       /* raw bootrom CPU-info string (often generic) */
    uint32_t soc_id;         /* raw SoC ID register (0xB300002C) */
    bool is_xburst2;         /* true for T40/T41/A1 (flag-label differences) */

    bool secure_boot;        /* secure boot enabled (only meaningful if *_known) */
    bool secure_boot_known;  /* false: this SoC family's decode isn't implemented */
    uint32_t security_reg;   /* raw security register (0x210 XB1 / 0x224 XB2) */
    uint8_t sec_flags;       /* normalized flags byte (see TDFU_SEC_* above) */

    uint8_t serial[16];      /* chip serial / UID, little-endian words */
    int serial_len;          /* valid bytes in serial[] */

    uint8_t key_hash[32];    /* RSA public-key hash; 0 if not burned */
    bool key_hash_present;   /* true if key_hash[] is non-zero */

    uint32_t efuse_base;     /* physical address the window was read from */
    uint8_t efuse[TDFU_DIAG_WINDOW_LEN]; /* raw eFuse window dump (0x200..0x2FF) */
    int efuse_len;           /* valid bytes in efuse[] */
} tdfu_diag_info_t;

/**
 * Read device diagnostics from a bootrom-stage device.
 * Requires the device to be in bootrom stage (a108:c309). Read-only — it only
 * reads the eFuse shadow window, never programs a fuse.
 * Returns TDFU_SUCCESS and fills *info on success.
 */
tdfu_error_t tdfu_diag(usb_manager_t *manager, int device_index, tdfu_diag_info_t *info);

/**
 * Format a diagnostics struct into a human-readable multi-line report.
 * Shared by the local CLI, the remote daemon, and (over the wire) the web /
 * Android UIs, so all surfaces show identical output. Always NUL-terminates.
 * Returns the number of bytes written (excluding the NUL), truncated to fit.
 */
int tdfu_diag_format(const tdfu_diag_info_t *info, char *buf, size_t buflen);

#endif /* TDFU_DIAG_H */
