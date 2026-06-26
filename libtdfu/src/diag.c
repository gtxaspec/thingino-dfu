/**
 * Device diagnostics — read the bootrom eFuse (SoC id, serial, secure-boot,
 * key hash, raw dump).
 *
 * The bootrom's native memory read (VR_SET_DATA_ADDR + VR_SET_DATA_LEN +
 * bulk-IN, i.e. protocol_read_memory) reads the memory-mapped eFuse shadow
 * window at 0xB3540200 directly. No uploaded MIPS stub is used — and a stub
 * must NOT be used: running any stage1 code (even the SoC auto-detect stub)
 * clears the eFuse shadow window to zero. So we read it on the pristine
 * bootrom, and take the SoC family from the soc_id register (the bootrom
 * CPU-info string is generic on XBurst2 — it reports "T31V" even on a T41).
 *
 * Read-only: only memory reads, never the eFuse program path.
 *
 * Layouts (window offsets, base 0xB3540200), ground-truthed against bootroms:
 *   XBurst1 secure (T23/T31/T32): security 0x10 (0x210, flags = byte[23:16]),
 *     key hash 0x40 (0x240, 32B), serial 0x00.
 *   XBurst2 (T40/T41/A1): security 0x24 (0x224, flags = ((w>>8)|w)&0xFF),
 *     key hash 0x80 (0x280, 32B) OR-redundant with 0xC0 (0x2C0), serial 0x00.
 */

#include "tdfu/tdfu.h"
#include "tdfu/diag.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define EFUSE_SOC_ID_ADDR  0xB300002Cu
#define EFUSE_WINDOW_ADDR  0xB3540200u
#define EFUSE_WINDOW_PHYS  0x13540200u

typedef enum {
    EF_FAM_UNKNOWN = 0,
    EF_FAM_XB1_LEGACY,  /* T10/T20/T21/T30: serial only, no secure boot */
    EF_FAM_XB1_SECURE,  /* T23/T31/T32: security 0x210, key 0x240 */
    EF_FAM_XB2,         /* T40/T41/A1: security 0x224, key 0x280 */
} ef_family_t;

static uint32_t le32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* Classify by the SoC ID register (0xB300002C). Family = (soc_id >> 12) & 0xFFFF
 * (T10=0x0005, T20=0x2000, T21=0x0021, T23=0x0023, T30=0x0030, T31=0x0031,
 * T32=0x0032; T40/T41 both 0x0040; A1=0x0001). */
static ef_family_t ef_classify(uint32_t soc_id, char *name, size_t name_len) {
    uint32_t fam = (soc_id >> 12) & 0xFFFF;
    const char *n;
    ef_family_t f;
    switch (fam) {
        case 0x0005: n = "T10";     f = EF_FAM_XB1_LEGACY; break;
        case 0x2000: n = "T20";     f = EF_FAM_XB1_LEGACY; break;
        case 0x0021: n = "T21";     f = EF_FAM_XB1_LEGACY; break;
        case 0x0030: n = "T30";     f = EF_FAM_XB1_LEGACY; break;
        case 0x0023: n = "T23";     f = EF_FAM_XB1_SECURE; break;
        case 0x0031: n = "T31";     f = EF_FAM_XB1_SECURE; break;
        case 0x0032: n = "T32";     f = EF_FAM_XB1_SECURE; break;
        case 0x0040: n = "T40/T41"; f = EF_FAM_XB2;        break;
        case 0x0001: n = "A1";      f = EF_FAM_XB2;        break;
        default:     n = "unknown"; f = EF_FAM_UNKNOWN;    break;
    }
    snprintf(name, name_len, "%s", n);
    return f;
}

static void note_key_hash(tdfu_diag_info_t *info) {
    for (int i = 0; i < 32; i++) {
        if (info->key_hash[i]) {
            info->key_hash_present = true;
            return;
        }
    }
}

tdfu_error_t tdfu_diag(usb_manager_t *manager, int device_index, tdfu_diag_info_t *info) {
    if (!manager || !info)
        return TDFU_ERROR_INVALID_PARAMETER;
    memset(info, 0, sizeof(*info));

    tdfu_device_info_t *devs = NULL;
    int n = 0;
    tdfu_error_t r = usb_manager_find_devices(manager, &devs, &n);
    if (r != TDFU_SUCCESS)
        return r;
    if (device_index < 0 || device_index >= n) {
        free(devs);
        return TDFU_ERROR_DEVICE_NOT_FOUND;
    }
    if (devs[device_index].stage != TDFU_STAGE_BOOTROM) {
        LOG_ERROR("Diag needs a device in bootrom mode (a108:c309)\n");
        free(devs);
        return TDFU_ERROR_INVALID_PARAMETER;
    }

    usb_device_t *dev = NULL;
    r = usb_manager_open_device(manager, &devs[device_index], &dev);
    free(devs);
    if (r != TDFU_SUCCESS)
        return r;

    /* Raw bootrom CPU-info string (informational; generic on XBurst2). */
    cpu_info_t ci;
    if (usb_device_get_cpu_info(dev, &ci) == TDFU_SUCCESS) {
        int w = 0;
        for (int i = 0; ci.clean_magic[i] && w < (int)sizeof(info->cpu_magic) - 1; i++) {
            char c = ci.clean_magic[i];
            if (c > ' ' && c < 0x7F)
                info->cpu_magic[w++] = c;
        }
        info->cpu_magic[w] = '\0';
    }

    /* Claim the interface for the bulk-IN reads. */
    r = usb_device_claim_interface(dev);
    if (r != TDFU_SUCCESS) {
        usb_device_close(dev);
        free(dev);
        return r;
    }

    /* Pure reads on the pristine bootrom — no stub, so the eFuse shadow is
     * live. Capture everything BEFORE any stub runs. */
    uint8_t soc[4] = {0};
    if (protocol_read_memory(dev, EFUSE_SOC_ID_ADDR, 4, soc) == TDFU_SUCCESS)
        info->soc_id = le32(soc);

    r = protocol_read_memory(dev, EFUSE_WINDOW_ADDR, TDFU_DIAG_WINDOW_LEN, info->efuse);
    if (r != TDFU_SUCCESS) {
        usb_device_release_interface(dev);
        usb_device_close(dev);
        free(dev);
        return r;
    }
    info->efuse_len = TDFU_DIAG_WINDOW_LEN;
    info->efuse_base = EFUSE_WINDOW_PHYS;

    /* Family + fallback name from soc_id (authoritative for the eFuse layout). */
    ef_family_t fam = ef_classify(info->soc_id, info->soc_name, sizeof(info->soc_name));

    /* Precise variant via the canonical detector. It runs a stage1 stub — which
     * is exactly why it must come AFTER the eFuse capture (a stub clears the
     * shadow) — but it resolves the sub-variants soc_id alone can't (T40NN vs
     * T40XP vs T41NQ, etc.). Best-effort: keep the soc_id name if it fails. */
    tdfu_variant_t variant;
    if (protocol_detect_soc(dev, &variant) == TDFU_SUCCESS)
        snprintf(info->soc_name, sizeof(info->soc_name), "%s", tdfu_variant_to_string(variant));

    usb_device_release_interface(dev);
    usb_device_close(dev);
    free(dev);

    /* Serial / chip-id: first words of the window (present in the shadow for
     * all families, even where the bootrom itself doesn't read it). */
    memcpy(info->serial, &info->efuse[0x00], 16);
    info->serial_len = 16;

    if (fam == EF_FAM_XB1_SECURE) {
        /* Security register 0xB3540210, flags = byte [23:16]. Bits verified
         * against the T31/T32 bootrom: bit0 SC_EN, bit2 RSA e=3, bit3 USB-boot
         * disable, bit4 extra boot restriction. Key hash 0xB3540240 (32B). */
        info->security_reg = le32(&info->efuse[0x10]);
        info->sec_flags = (uint8_t)((info->security_reg >> 16) & 0xFF);
        info->secure_boot = (info->sec_flags & TDFU_SEC_ENABLE) != 0;
        info->secure_boot_known = true;
        memcpy(info->key_hash, &info->efuse[0x40], 32);
        note_key_hash(info);
    } else if (fam == EF_FAM_XB2) {
        /* Security register 0xB3540224, flags fold = ((w>>8)|w)&0xFF (main|backup
         * bytes). Bits verified against the T40/T41/A1 bootrom: bit0 SC_EN,
         * bit2 RSA e=3, bit3 USB-boot disable, bit4 SD/MMC-boot disable, bit6
         * NOR-via-USB-write disable. Key hash 0xB3540280 (32B), OR-redundant
         * with the backup at 0xB35402C0. */
        info->is_xburst2 = true;
        info->security_reg = le32(&info->efuse[0x24]);
        info->sec_flags = (uint8_t)(((info->security_reg >> 8) | info->security_reg) & 0xFF);
        info->secure_boot = (info->sec_flags & TDFU_SEC_ENABLE) != 0;
        info->secure_boot_known = true;
        for (int i = 0; i < 32; i++)
            info->key_hash[i] = (uint8_t)(info->efuse[0x80 + i] | info->efuse[0xC0 + i]);
        note_key_hash(info);
    } else {
        /* XB1 legacy (T10/T20/T21/T30): chip serial in the window, no secure
         * boot. Unknown family: serial best-effort, no secure-boot decode. */
        info->secure_boot_known = false;
    }

    return TDFU_SUCCESS;
}

/* Append-with-bounds helper for tdfu_diag_format. */
#define DIAG_APP(...)                                                  \
    do {                                                              \
        if (n < buflen) {                                            \
            int _w = snprintf(buf + n, buflen - n, __VA_ARGS__);     \
            if (_w < 0)                                              \
                _w = 0;                                              \
            n = (n + (size_t)_w >= buflen) ? buflen - 1 : n + (size_t)_w; \
        }                                                            \
    } while (0)

int tdfu_diag_format(const tdfu_diag_info_t *d, char *buf, size_t buflen) {
    if (!d || !buf || buflen == 0)
        return 0;
    size_t n = 0;
    buf[0] = '\0';

    DIAG_APP("=== thingino-dfu diagnostics ===\n");
    if (d->cpu_magic[0])
        DIAG_APP("SoC:          %s (bootrom \"%s\", id 0x%08X)\n", d->soc_name, d->cpu_magic, d->soc_id);
    else
        DIAG_APP("SoC:          %s (id 0x%08X)\n", d->soc_name, d->soc_id);

    if (d->serial_len >= 4) {
        DIAG_APP("Serial/UID:   ");
        for (int i = 0; i + 4 <= d->serial_len; i += 4) {
            uint32_t w = (uint32_t)d->serial[i] | ((uint32_t)d->serial[i + 1] << 8) |
                         ((uint32_t)d->serial[i + 2] << 16) | ((uint32_t)d->serial[i + 3] << 24);
            DIAG_APP(" %u", w);
        }
        DIAG_APP("  (");
        for (int i = 0; i < d->serial_len; i++)
            DIAG_APP("%02x", d->serial[i]);
        DIAG_APP(")\n");
    }

    if (d->secure_boot_known) {
        DIAG_APP("Secure boot:  %s  (security reg 0x%08X)\n",
                 d->secure_boot ? "ENABLED (all boot sources)" : "disabled", d->security_reg);
        DIAG_APP("  USB boot:        %s\n", (d->sec_flags & TDFU_SEC_USB_OFF) ? "disabled under secure boot" : "allowed");
        if (d->is_xburst2) {
            DIAG_APP("  SD/MMC boot:     %s\n", (d->sec_flags & TDFU_SEC_SD_OFF) ? "disabled under secure boot" : "allowed");
            DIAG_APP("  NOR USB-write:   %s\n", (d->sec_flags & TDFU_SEC_NORWR_OFF) ? "disabled under secure boot" : "allowed");
        } else {
            DIAG_APP("  Extra restrict:  %s\n", (d->sec_flags & TDFU_SEC_SD_OFF) ? "yes (a boot source blocked under secure boot)" : "none");
        }
        DIAG_APP("  RSA exponent:    %s\n", (d->sec_flags & TDFU_SEC_RSA_E3) ? "e=3" : "e=65537");
        if (d->key_hash_present) {
            DIAG_APP("  RSA key hash:    ");
            for (int i = 0; i < 32; i++)
                DIAG_APP("%02x", d->key_hash[i]);
            DIAG_APP("\n");
        } else {
            DIAG_APP("  RSA key hash:    (not provisioned)\n");
        }
    } else {
        DIAG_APP("Secure boot:  unknown (no secure-boot fuses on this SoC family)\n");
    }

    if (d->efuse_len > 0) {
        DIAG_APP("eFuse window (phys 0x%08X):\n", d->efuse_base);
        for (int i = 0; i < d->efuse_len; i += 16) {
            DIAG_APP("  %08X:", d->efuse_base + i);
            for (int j = 0; j < 16 && i + j < d->efuse_len; j++)
                DIAG_APP(" %02x", d->efuse[i + j]);
            DIAG_APP("\n");
        }
    }

    return (int)n;
}
