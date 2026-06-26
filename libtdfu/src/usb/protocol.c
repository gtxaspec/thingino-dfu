#include "tdfu/tdfu.h"
#include "platform.h"
#include "tdfu/constants.h"
#include <string.h>

// ============================================================================
// PROTOCOL IMPLEMENTATION
// ============================================================================

tdfu_error_t protocol_set_data_address(usb_device_t *device, uint32_t addr) {
    if (!device) {
        return TDFU_ERROR_INVALID_PARAMETER;
    }

    DEBUG_PRINT("SetDataAddress: 0x%08x\n", addr);

    int response_length;
    tdfu_error_t result =
        usb_device_vendor_request(device, REQUEST_TYPE_OUT, VR_SET_DATA_ADDR, (uint16_t)(addr >> 16),
                                  (uint16_t)(addr & 0xFFFF), NULL, 0, NULL, &response_length);

    if (result != TDFU_SUCCESS) {
        DEBUG_PRINT("SetDataAddress error: %s\n", tdfu_error_to_string(result));
        return result;
    }

    DEBUG_PRINT("SetDataAddress OK\n");

    // Platform-specific sleep
    platform_sleep_ms(CMD_RESPONSE_DELAY_MS);

    return TDFU_SUCCESS;
}

tdfu_error_t protocol_set_data_length(usb_device_t *device, uint32_t length) {
    if (!device) {
        return TDFU_ERROR_INVALID_PARAMETER;
    }

    DEBUG_PRINT("SetDataLength: %d (0x%08x)\n", length, length);

    int response_length;
    tdfu_error_t result =
        usb_device_vendor_request(device, REQUEST_TYPE_OUT, VR_SET_DATA_LEN, (uint16_t)(length >> 16),
                                  (uint16_t)(length & 0xFFFF), NULL, 0, NULL, &response_length);

    if (result != TDFU_SUCCESS) {
        DEBUG_PRINT("SetDataLength error: %s\n", tdfu_error_to_string(result));
        return result;
    }

    DEBUG_PRINT("SetDataLength OK\n");

    // Platform-specific sleep
    platform_sleep_ms(CMD_RESPONSE_DELAY_MS);

    return TDFU_SUCCESS;
}

tdfu_error_t protocol_flush_cache(usb_device_t *device) {
    if (!device) {
        return TDFU_ERROR_INVALID_PARAMETER;
    }

    DEBUG_PRINT("FlushCache: executing\n");

    int response_length;
    tdfu_error_t result =
        usb_device_vendor_request(device, REQUEST_TYPE_OUT, VR_FLUSH_CACHE, 0, 0, NULL, 0, NULL, &response_length);

    if (result != TDFU_SUCCESS) {
        DEBUG_PRINT("FlushCache error: %s\n", tdfu_error_to_string(result));
        return result;
    }

    DEBUG_PRINT("FlushCache OK\n");

    // Platform-specific sleep
    platform_sleep_ms(CMD_RESPONSE_DELAY_MS);

    return TDFU_SUCCESS;
}

tdfu_error_t protocol_prog_stage1(usb_device_t *device, uint32_t addr) {
    if (!device) {
        return TDFU_ERROR_INVALID_PARAMETER;
    }

    DEBUG_PRINT("ProgStage1: addr=0x%08x\n", addr);

    int response_length;
    tdfu_error_t result =
        usb_device_vendor_request(device, REQUEST_TYPE_OUT, VR_PROG_STAGE1, (uint16_t)(addr >> 16),
                                  (uint16_t)(addr & 0xFFFF), NULL, 0, NULL, &response_length);

    if (result != TDFU_SUCCESS) {
        DEBUG_PRINT("ProgStage1 error: %s\n", tdfu_error_to_string(result));
        return result;
    }

    DEBUG_PRINT("ProgStage1 OK\n");

    // Platform-specific sleep
    platform_sleep_ms(CMD_RESPONSE_DELAY_MS);

    return TDFU_SUCCESS;
}

tdfu_error_t protocol_prog_stage2(usb_device_t *device, uint32_t addr) {
    if (!device) {
        return TDFU_ERROR_INVALID_PARAMETER;
    }

    DEBUG_PRINT("ProgStage2: addr=0x%08x\n", addr);

    int response_length;
    tdfu_error_t result =
        usb_device_vendor_request(device, REQUEST_TYPE_OUT, VR_PROG_STAGE2, (uint16_t)(addr >> 16),
                                  (uint16_t)(addr & 0xFFFF), NULL, 0, NULL, &response_length);

    if (result != TDFU_SUCCESS) {
        // It's expected for ProgStage2 to fail with timeout or pipe error
        // because device is re-enumerating after executing U-Boot
        DEBUG_PRINT("ProgStage2 sent (timeout/pipe error during re-enumeration is expected): %s\n",
                    tdfu_error_to_string(result));
        return TDFU_SUCCESS; // Treat as success - device is re-enumerating
    }

    DEBUG_PRINT("ProgStage2 OK\n");

    // Platform-specific sleep
    platform_sleep_ms(CMD_RESPONSE_DELAY_MS);

    return TDFU_SUCCESS;
}

/**
 * Read memory from device in bootrom stage.
 * Uses: VR_SET_DATA_ADDR(0x01) + VR_SET_DATA_LEN(0x02) + Bulk IN(0x81)
 */
tdfu_error_t protocol_read_memory(usb_device_t *device, uint32_t addr, uint32_t len, uint8_t *out) {
    if (!device || !out || len == 0) {
        return TDFU_ERROR_INVALID_PARAMETER;
    }

    tdfu_error_t result = protocol_set_data_address(device, addr);
    if (result != TDFU_SUCCESS)
        return result;

    result = protocol_set_data_length(device, len);
    if (result != TDFU_SUCCESS)
        return result;

    int transferred = 0;
    int rc = libusb_bulk_transfer(device->handle, ENDPOINT_IN, out, (int)len, &transferred, 2000);
    if (rc != 0 || transferred != (int)len) {
        DEBUG_PRINT("Memory read failed at 0x%08X: %s (got %d/%u)\n", addr, libusb_error_name(rc), transferred, len);
        return TDFU_ERROR_TRANSFER_FAILED;
    }

    return TDFU_SUCCESS;
}

/**
 * Auto-detect SoC by reading hardware ID registers from bootrom.
 *
 * Strategy: upload a tiny MIPS program that reads the SoC ID registers
 * (which the CPU can access but the USB DMA engine cannot) and stores
 * the results in TCSM. Execute via VR_PROG_STAGE2 (0x05) which has no
 * one-shot restriction, keeping VR_PROG_STAGE1 available for SPL.
 * Then read results from TCSM via bulk IN (DMA can read TCSM fine).
 *
 * Key bootrom findings (from bootrom analysis):
 *   - VR_PROG_STAGE1 (0x04): one-shot flag at _DAT_8000005c, can't reuse
 *   - VR_PROG_STAGE2 (0x05): NO one-shot flag, returns to USB handler loop
 *   - Both call the program as a function (jalr), so jr ra returns cleanly
 *   - USB DMA cannot access peripheral registers (0x1300xxxx) — hangs
 *   - CPU load instructions (lw) CAN access peripheral registers
 *
 * Register map (same across xburst1 and xburst2):
 *   0x1300002C — SoC ID (bits 12-27 = CPU family)
 *   0x13540238 — Sub-SoC type 1 (EFUSE, bits 16-31)
 *   0x13540250 — Sub-SoC type 2 (EFUSE, bits 16-31)
 *
 * CPU family IDs (from thingino cmd_socinfo.c):
 *   T10=0x0005, T20=0x2000, T21=0x0021, T23=0x0023,
 *   T30=0x0030, T31=0x0031, T40/T41=0x0040, A1=0x0001
 */
tdfu_error_t protocol_detect_soc(usb_device_t *device, tdfu_variant_t *variant) {
    if (!device || !variant) {
        return TDFU_ERROR_INVALID_PARAMETER;
    }

    /* Set USB configuration 1 and claim interface — required for bulk transfers.
     * Without this, bulk transfers fail with LIBUSB_ERROR_IO. */
    int current_cfg = -1;
    libusb_get_configuration(device->handle, &current_cfg);
    if (current_cfg != 1) {
        int cfg_rc = libusb_set_configuration(device->handle, 1);
        if (cfg_rc != LIBUSB_SUCCESS) {
            DEBUG_PRINT("set_configuration(1) failed: %s (non-fatal)\n", libusb_error_name(cfg_rc));
        }
    }
    libusb_detach_kernel_driver(device->handle, 0);
    {
        int claim_rc = libusb_claim_interface(device->handle, 0);
        if (claim_rc != LIBUSB_SUCCESS) {
            DEBUG_PRINT("claim_interface(0) failed: %s (non-fatal)\n", libusb_error_name(claim_rc));
        }
    }

    /* MIPS32 LE program to read SoC ID registers and bypass one-shot flag.
     *
     * Register reads use kseg1 (0xBxxxxxxx) for uncached HW access.
     * Result stores use kseg0 (0x8000xxxx) D-cache scratchpad (TCSM).
     *
     * ALL Ingenic bootroms enforce a one-shot flag on VR_PROG_STAGE1:
     *   case 4: if (flag == 0) { FUN_exec(code); flag = 1; }
     * The flag is set AFTER FUN_exec returns, so clearing it from within
     * our code doesn't help. Instead, we overwrite the saved return address
     * in FUN_exec's stack frame (ra at sp+0x1C, frame size 0x20) to jump
     * directly to the event loop continuation, skipping the flag-set.
     *
     * Per-SoC continuation addresses:
     *   T20 (0x2000): 0xBFC02D20
     *   T21 (0x0021): 0xBFC02BEC  — clears ErrCtl bit 29, ra at sp+0x24
     *   T23 (0x0023): 0xBFC03024  — clears ErrCtl bit 29
     *   T30 (0x0030): 0xBFC02D0C (T30L) / 0xBFC02D5C (T30X, +0x50 shift) — picked by subtype1
     *   T31 (0x0031): 0xBFC02DD4  — also clears ErrCtl bit 29
     *   T32 (0x0032): 0xBFC03ADC  — clears ErrCtl bit 29, unwinds frame
     *   T40NN (0x0040, sub2=0x1111/0x8888): 0xBFC036F0
     *   T40XP (0x0040, sub2=0x7777): 0xBFC03604
     *   T41 (0x0040, sub2=other):         0xBFC03ACC
     *   A1  (0x0001): 0xBFC0387C
     *
     * One-shot flag addresses: T20=0x5C T21=0x5C T23=0x5C T30=0x5C T31=0x5C T41=0x60 T32=0x68 T40=0x78 A1=0x7C
     *
     * Result layout at 0x80001100 (16 bytes):
     *   [0]  soc_id       from 0xB300002C
     *   [4]  subsoctype1  from 0xB3540238
     *   [8]  subsoctype2  from 0xB3540250
     *   [12] 0xDEADBEEF   execution marker
     */
    static const uint8_t mips_prog[] = {
        /* lui  t2, 0x8000 */
        0x00, 0x80, 0x0A, 0x3C,
        /* lui  t0, 0xB300 */
        0x00, 0xB3, 0x08, 0x3C,
        /* lw   t1, 0x002C(t0) = soc_id */
        0x2C, 0x00, 0x09, 0x8D,
        /* sw   t1, 0x1100(t2) */
        0x00, 0x11, 0x49, 0xAD,
        /* move t3, t1 */
        0x25, 0x58, 0x20, 0x01,
        /* lui  t0, 0xB354 */
        0x54, 0xB3, 0x08, 0x3C,
        /* lw   t1, 0x0238(t0) = sub1 */
        0x38, 0x02, 0x09, 0x8D,
        /* sw   t1, 0x1104(t2) */
        0x04, 0x11, 0x49, 0xAD,
        /* lw   t4, 0x0250(t0) = sub2 */
        0x50, 0x02, 0x0C, 0x8D,
        /* sw   t4, 0x1108(t2) */
        0x08, 0x11, 0x4C, 0xAD,
        /* lui  t1, 0xDEAD */
        0xAD, 0xDE, 0x09, 0x3C,
        /* ori  t1, t1, 0xBEEF */
        0xEF, 0xBE, 0x29, 0x35,
        /* sw   t1, 0x110C(t2) = marker */
        0x0C, 0x11, 0x49, 0xAD,
        /* srl  t0, t3, 12 */
        0x02, 0x43, 0x0B, 0x00,
        /* andi t0, t0, 0xFFFF */
        0xFF, 0xFF, 0x08, 0x31,
        /* li   t1, 0x0032 ; T32 */
        0x32, 0x00, 0x09, 0x24,
        /* beq  t0, t1, bypass_t32 */
        0x18, 0x00, 0x09, 0x11,
        /* nop */
        0x00, 0x00, 0x00, 0x00,
        /* li   t1, 0x0031 ; T31 */
        0x31, 0x00, 0x09, 0x24,
        /* beq  t0, t1, bypass_t31 */
        0x20, 0x00, 0x09, 0x11,
        /* nop */
        0x00, 0x00, 0x00, 0x00,
        /* li   t1, 0x0040 ; T40/T41 */
        0x40, 0x00, 0x09, 0x24,
        /* beq  t0, t1, bypass_t40_t41 */
        0x3B, 0x00, 0x09, 0x11,
        /* nop */
        0x00, 0x00, 0x00, 0x00,
        /* li   t1, 0x0001 ; A1 */
        0x01, 0x00, 0x09, 0x24,
        /* beq  t0, t1, bypass_a1 */
        0x51, 0x00, 0x09, 0x11,
        /* nop */
        0x00, 0x00, 0x00, 0x00,
        /* li   t1, 0x0021 ; T21 */
        0x21, 0x00, 0x09, 0x24,
        /* beq  t0, t1, bypass_t21 */
        0x2B, 0x00, 0x09, 0x11,
        /* nop */
        0x00, 0x00, 0x00, 0x00,
        /* li   t1, 0x0023 ; T23 */
        0x23, 0x00, 0x09, 0x24,
        /* beq  t0, t1, bypass_t23 */
        0x1E, 0x00, 0x09, 0x11,
        /* nop */
        0x00, 0x00, 0x00, 0x00,
        /* li   t1, 0x0030 ; T30 */
        0x30, 0x00, 0x09, 0x24,
        /* beq  t0, t1, bypass_t30 */
        0x52, 0x00, 0x09, 0x11,
        /* nop */
        0x00, 0x00, 0x00, 0x00,
        /* ori  t1, zero, 0x2000 ; T20 */
        0x00, 0x20, 0x09, 0x34,
        /* beq  t0, t1, bypass_t20 */
        0x4A, 0x00, 0x09, 0x11,
        /* nop */
        0x00, 0x00, 0x00, 0x00,
        /* jr   ra ; unknown SoC, return without bypass */
        0x08, 0x00, 0xE0, 0x03,
        /* nop */
        0x00, 0x00, 0x00, 0x00,
        /* === bypass_t32: clear ErrCtl, unwind frame, direct jump === */
        /* mfc0 t0, ErrCtl */
        0x00, 0xD0, 0x08, 0x40,
        /* lui  t1, 0xDFFF */
        0xFF, 0xDF, 0x09, 0x3C,
        /* ori  t1, t1, 0xFFFF */
        0xFF, 0xFF, 0x29, 0x35,
        /* and  t0, t0, t1 */
        0x24, 0x40, 0x09, 0x01,
        /* mtc0 t0, ErrCtl */
        0x00, 0xD0, 0x88, 0x40,
        /* lw   s0, 0x18(sp) */
        0x18, 0x00, 0xB0, 0x8F,
        /* addiu sp, sp, 0x20 */
        0x20, 0x00, 0xBD, 0x27,
        /* lui  t0, 0xBFC0 */
        0xC0, 0xBF, 0x08, 0x3C,
        /* ori  t0, t0, 0x3ADC ; T32 cont */
        0xDC, 0x3A, 0x08, 0x35,
        /* jr   t0 */
        0x08, 0x00, 0x00, 0x01,
        /* nop */
        0x00, 0x00, 0x00, 0x00,
        /* === bypass_t31: clear ErrCtl, overwrite ra === */
        /* mfc0 t0, ErrCtl */
        0x00, 0xD0, 0x08, 0x40,
        /* lui  t1, 0xDFFF */
        0xFF, 0xDF, 0x09, 0x3C,
        /* ori  t1, t1, 0xFFFF */
        0xFF, 0xFF, 0x29, 0x35,
        /* and  t0, t0, t1 */
        0x24, 0x40, 0x09, 0x01,
        /* mtc0 t0, ErrCtl */
        0x00, 0xD0, 0x88, 0x40,
        /* lui  t0, 0xBFC0 */
        0xC0, 0xBF, 0x08, 0x3C,
        /* ori  t0, t0, 0x2DD4 ; T31 cont */
        0xD4, 0x2D, 0x08, 0x35,
        /* sw   t0, 0x1C(sp) */
        0x1C, 0x00, 0xA8, 0xAF,
        /* jr   ra */
        0x08, 0x00, 0xE0, 0x03,
        /* nop */
        0x00, 0x00, 0x00, 0x00,
        /* === bypass_t23: clear ErrCtl, overwrite ra === */
        /* mfc0 t0, ErrCtl */
        0x00, 0xD0, 0x08, 0x40,
        /* lui  t1, 0xDFFF */
        0xFF, 0xDF, 0x09, 0x3C,
        /* ori  t1, t1, 0xFFFF */
        0xFF, 0xFF, 0x29, 0x35,
        /* and  t0, t0, t1 */
        0x24, 0x40, 0x09, 0x01,
        /* mtc0 t0, ErrCtl */
        0x00, 0xD0, 0x88, 0x40,
        /* lui  t0, 0xBFC0 */
        0xC0, 0xBF, 0x08, 0x3C,
        /* ori  t0, t0, 0x3024 ; T23 cont */
        0x24, 0x30, 0x08, 0x35,
        /* sw   t0, 0x1C(sp) */
        0x1C, 0x00, 0xA8, 0xAF,
        /* jr   ra */
        0x08, 0x00, 0xE0, 0x03,
        /* nop */
        0x00, 0x00, 0x00, 0x00,
        /* === bypass_t21: clear ErrCtl, overwrite ra at sp+0x24 === */
        /* mfc0 t0, ErrCtl */
        0x00, 0xD0, 0x08, 0x40,
        /* lui  t1, 0xDFFF */
        0xFF, 0xDF, 0x09, 0x3C,
        /* ori  t1, t1, 0xFFFF */
        0xFF, 0xFF, 0x29, 0x35,
        /* and  t0, t0, t1 */
        0x24, 0x40, 0x09, 0x01,
        /* mtc0 t0, ErrCtl */
        0x00, 0xD0, 0x88, 0x40,
        /* lui  t0, 0xBFC0 */
        0xC0, 0xBF, 0x08, 0x3C,
        /* ori  t0, t0, 0x2BEC ; T21 cont */
        0xEC, 0x2B, 0x08, 0x35,
        /* sw   t0, 0x24(sp) ; ra at sp+0x24 (40-byte frame) */
        0x24, 0x00, 0xA8, 0xAF,
        /* jr   ra */
        0x08, 0x00, 0xE0, 0x03,
        /* nop */
        0x00, 0x00, 0x00, 0x00,
        /* === bypass_t40_t41: check sub2 to distinguish variants === */
        /* srl  t0, t4, 16 */
        0x02, 0x44, 0x0C, 0x00,
        /* ori  t1, zero, 0x8888 ; T40NN */
        0x88, 0x88, 0x09, 0x34,
        /* beq  t0, t1, bypass_t40nn */
        0x0C, 0x00, 0x09, 0x11,
        /* nop */
        0x00, 0x00, 0x00, 0x00,
        /* ori  t1, zero, 0x1111 ; T40N */
        0x11, 0x11, 0x09, 0x34,
        /* beq  t0, t1, bypass_t40nn */
        0x09, 0x00, 0x09, 0x11,
        /* nop */
        0x00, 0x00, 0x00, 0x00,
        /* ori  t1, zero, 0x7777 ; T40XP */
        0x77, 0x77, 0x09, 0x34,
        /* beq  t0, t1, bypass_t40xp */
        0x0B, 0x00, 0x09, 0x11,
        /* nop */
        0x00, 0x00, 0x00, 0x00,
        /* T41 (default for 0x0040 family) */
        /* lui  t0, 0xBFC0 */
        0xC0, 0xBF, 0x08, 0x3C,
        /* ori  t0, t0, 0x3ACC ; T41 cont */
        0xCC, 0x3A, 0x08, 0x35,
        /* sw   t0, 0x1C(sp) */
        0x1C, 0x00, 0xA8, 0xAF,
        /* jr   ra */
        0x08, 0x00, 0xE0, 0x03,
        /* nop */
        0x00, 0x00, 0x00, 0x00,
        /* === bypass_t40nn === */
        /* lui  t0, 0xBFC0 */
        0xC0, 0xBF, 0x08, 0x3C,
        /* ori  t0, t0, 0x36F0 ; T40NN cont */
        0xF0, 0x36, 0x08, 0x35,
        /* sw   t0, 0x1C(sp) */
        0x1C, 0x00, 0xA8, 0xAF,
        /* jr   ra */
        0x08, 0x00, 0xE0, 0x03,
        /* nop */
        0x00, 0x00, 0x00, 0x00,
        /* === bypass_t40xp === */
        /* lui  t0, 0xBFC0 */
        0xC0, 0xBF, 0x08, 0x3C,
        /* ori  t0, t0, 0x3604 ; T40XP cont */
        0x04, 0x36, 0x08, 0x35,
        /* sw   t0, 0x1C(sp) */
        0x1C, 0x00, 0xA8, 0xAF,
        /* jr   ra */
        0x08, 0x00, 0xE0, 0x03,
        /* nop */
        0x00, 0x00, 0x00, 0x00,
        /* === bypass_a1 === */
        /* lui  t0, 0xBFC0 */
        0xC0, 0xBF, 0x08, 0x3C,
        /* ori  t0, t0, 0x387C ; A1 cont */
        0x7C, 0x38, 0x08, 0x35,
        /* sw   t0, 0x1C(sp) */
        0x1C, 0x00, 0xA8, 0xAF,
        /* jr   ra */
        0x08, 0x00, 0xE0, 0x03,
        /* nop */
        0x00, 0x00, 0x00, 0x00,
        /* === bypass_t20 === */
        /* lui  t0, 0xBFC0 */
        0xC0, 0xBF, 0x08, 0x3C,
        /* ori  t0, t0, 0x2D20 ; T20 cont */
        0x20, 0x2D, 0x08, 0x35,
        /* sw   t0, 0x1C(sp) */
        0x1C, 0x00, 0xA8, 0xAF,
        /* jr   ra */
        0x08, 0x00, 0xE0, 0x03,
        /* nop */
        0x00, 0x00, 0x00, 0x00,
        /* === bypass_t30: T30L and T30X ship DIFFERENT mask ROMs. T30X inserts
         *     SFC-clock-override code in the USB-boot path that shifts the whole
         *     event loop by +0x50, so the continuation differs:
         *       T30L = 0xBFC02D0C   T30X = 0xBFC02D5C
         *     subtype1 was clobbered by the DEADBEEF marker store, so re-read it
         *     from 0xB3540238 and branch: T30X (0x2222) -> 0x2D5C, else 0x2D0C.
         *     (Keep this LAST in the array - growing it shifts nothing.) === */
        /* lui  t0, 0xB354 */
        0x54, 0xB3, 0x08, 0x3C,
        /* lw   t1, 0x0238(t0) = sub1 */
        0x38, 0x02, 0x09, 0x8D,
        /* srl  t1, t1, 16  -> subtype1 */
        0x02, 0x4C, 0x09, 0x00,
        /* ori  t0, zero, 0x2222 ; T30X subtype1 */
        0x22, 0x22, 0x08, 0x34,
        /* bne  t1, t0, +5 (t30l_cont) */
        0x05, 0x00, 0x28, 0x15,
        /* nop */
        0x00, 0x00, 0x00, 0x00,
        /* lui  t0, 0xBFC0 ; T30X path */
        0xC0, 0xBF, 0x08, 0x3C,
        /* ori  t0, t0, 0x2D5C */
        0x5C, 0x2D, 0x08, 0x35,
        /* b    +3 (t30_set_ra) */
        0x03, 0x00, 0x00, 0x10,
        /* nop */
        0x00, 0x00, 0x00, 0x00,
        /* t30l_cont: lui t0, 0xBFC0 */
        0xC0, 0xBF, 0x08, 0x3C,
        /* ori  t0, t0, 0x2D0C */
        0x0C, 0x2D, 0x08, 0x35,
        /* t30_set_ra: sw t0, 0x1C(sp) */
        0x1C, 0x00, 0xA8, 0xAF,
        /* jr   ra */
        0x08, 0x00, 0xE0, 0x03,
        /* nop */
        0x00, 0x00, 0x00, 0x00,
    };
    /* The stub is uploaded into `padded` below; keep it within that buffer. */
    _Static_assert(sizeof(mips_prog) <= 1024, "detect stub exceeds upload buffer (bump padded[])");

    const uint32_t prog_addr = 0x80001000;
    const uint32_t result_addr = 0x80001100;
    tdfu_error_t result;
    int transferred = 0;

    /* Step 1: Upload MIPS program to TCSM at 0x80001000 */
    result = protocol_set_data_address(device, prog_addr);
    if (result != TDFU_SUCCESS)
        goto fail;

    /* Pad to a 512-byte multiple (bootrom bulk transfer min). 1024 leaves
     * headroom — the stub grew past 512 with the per-variant T30 bypass. */
    uint8_t padded[1024];
    memset(padded, 0, sizeof(padded));
    memcpy(padded, mips_prog, sizeof(mips_prog));

    result = protocol_set_data_length(device, sizeof(padded));
    if (result != TDFU_SUCCESS)
        goto fail;

    result = usb_device_bulk_transfer(device, ENDPOINT_OUT, padded, sizeof(padded), &transferred, 5000);
    if (result != TDFU_SUCCESS || transferred != (int)sizeof(padded)) {
        DEBUG_PRINT("Failed to upload detect program: %d (transferred %d)\n", result, transferred);
        goto fail;
    }
    DEBUG_PRINT("Uploaded %d-byte detect program to 0x%08X\n", transferred, prog_addr);

    /* Step 2: Execute via VR_PROG_STAGE1 (0x04).
     * STAGE1 is one-shot on ALL Ingenic bootroms — the case 4 handler sets
     * a flag after execution. Our MIPS program bypasses this by overwriting
     * the saved ra in the execution function's stack frame to jump past the
     * flag-set instruction, allowing STAGE1 to be used again for SPL.
     * On T31/T32, the execution function also sets ErrCtl bit 29 (SPRAM mode),
     * which our program clears before returning.
     *
     * Set d2i_len before STAGE1 — T20/T21 bootroms use this value to determine
     * how much code to flush from D-cache before execution. Without it, the
     * program may not be visible to I-cache and won't execute. */
    result = protocol_set_data_length(device, 0x7000);
    if (result != TDFU_SUCCESS)
        goto fail;
    result = protocol_prog_stage1(device, prog_addr);
    if (result != TDFU_SUCCESS) {
        DEBUG_PRINT("Failed to execute detect program\n");
        goto fail;
    }
    device->stage1_consumed = true;

    platform_sleep_ms(50); /* Let program run and return */

    /* Step 3: Read results from TCSM (24 bytes: 16 results + 4 ra + 4 sp). */
    uint8_t results[24] = {0};
    result = protocol_read_memory(device, result_addr, sizeof(results), results);

    if (result != TDFU_SUCCESS) {
        DEBUG_PRINT("Failed to read detect results from 0x%08X\n", result_addr);
        goto fail;
    }

    libusb_release_interface(device->handle, 0);

    uint32_t soc_id_raw, subsoctype1_raw, subsoctype2_raw, marker;
    memcpy(&soc_id_raw, &results[0], 4);
    memcpy(&subsoctype1_raw, &results[4], 4);
    memcpy(&subsoctype2_raw, &results[8], 4);
    memcpy(&marker, &results[12], 4);

    uint32_t orig_ra, sp_val;
    memcpy(&orig_ra, &results[16], 4);
    memcpy(&sp_val, &results[20], 4);
    DEBUG_PRINT("Detect results: soc_id=0x%08X sub1=0x%08X sub2=0x%08X marker=0x%08X ra=0x%08X sp=0x%08X\n", soc_id_raw,
                subsoctype1_raw, subsoctype2_raw, marker, orig_ra, sp_val);

    if (marker != 0xDEADBEEF) {
        LOG_WARN("  Detect program did not execute (marker=0x%08X)\n", marker);
        return TDFU_ERROR_PROTOCOL;
    }

    if (soc_id_raw == 0) {
        LOG_WARN("  SoC ID register returned 0\n");
        return TDFU_ERROR_PROTOCOL;
    }

    /* Clear the VR_PROG_STAGE1 one-shot flag via host-side bulk OUT.
     * The flag is set by the case 4 handler AFTER our program returns,
     * so we can't clear it from inside the program. Instead, we overwrite
     * it from the host using SET_DATA_ADDR + bulk OUT (PIO write).
     *
     * Flag addresses (from bootrom analysis of each SoC):
     *   T31: 0x8000005C, T41: 0x80000060, T32: 0x80000068,
     *   T40: 0x80000078, A1: 0x8000007C */
    uint16_t cpu_id = (soc_id_raw >> 12) & 0xFFFF;
    uint32_t flag_addr = 0;
    switch (cpu_id) {
    case 0x0023:
    case 0x0031:
        flag_addr = 0x8000005C;
        break;
    case 0x0032:
        flag_addr = 0x80000068;
        break;
    case 0x0040: {
        uint16_t sub2 = (subsoctype2_raw >> 16) & 0xFFFF;
        flag_addr = (sub2 == 0x1111 || sub2 == 0x8888 || sub2 == 0x7777) ? 0x80000078  /* T40 */
                                                                         : 0x80000060; /* T41 */
        break;
    }
    case 0x0001:
        flag_addr = 0x8000007C;
        break;
    }
    /* Note: flag clearing is not needed. On T31/T40/T41/A1, the one-shot
     * flag doesn't prevent STAGE1 reuse in practice. On T32, see below. */
    (void)flag_addr;
    uint16_t subtype1 = (subsoctype1_raw >> 16) & 0xFFFF;
    uint16_t subtype2 = (subsoctype2_raw >> 16) & 0xFFFF;
    DEBUG_PRINT("SoC ID: 0x%08X (CPU: 0x%04X, sub1: 0x%04X, sub2: 0x%04X)\n", soc_id_raw, cpu_id, subtype1, subtype2);

    /* Match CPU family */
    switch (cpu_id) {
    case 0x0005:
        *variant = TDFU_VARIANT_T10N; /* `soc -m`: 0x0000 -> t10 (base = t10n) */
        break;
    case 0x2000:
        if (subtype1 == 0x2222)
            *variant = TDFU_VARIANT_T20X; /* 128 MB */
        else if (subtype1 == 0x3333)
            *variant = TDFU_VARIANT_T20L;
        else
            *variant = TDFU_VARIANT_T20N; /* 64 MB (incl 0x1111, ax, z) */
        break;
    case 0x0021:
        *variant = TDFU_VARIANT_T21N; /* `soc -m` n/l/z all -> t21n loader */
        break;
    case 0x0023:
        if (subtype1 == 0x3333)
            *variant = TDFU_VARIANT_T23DL; /* DDR2, 32 MB */
        else if (subtype1 == 0x2222)
            *variant = TDFU_VARIANT_T23X;
        else if (subtype1 == 0x7777)
            *variant = TDFU_VARIANT_T23ZN;
        else
            *variant = TDFU_VARIANT_T23N; /* DDR2 64 MB base (incl 0x1111) */
        break;
    case 0x0030:
        if (subtype1 == 0x2222)
            *variant = TDFU_VARIANT_T30X; /* 128 MB */
        else if (subtype1 == 0x3333)
            *variant = TDFU_VARIANT_T30L;
        else if (subtype1 == 0x4444)
            *variant = TDFU_VARIANT_T30A; /* 128 MB */
        else
            *variant = TDFU_VARIANT_T30N; /* 64 MB base (incl 0x1111, z) */
        break;
    case 0x0032:
        /* T32: the sub1 grade code selects the DDR type (mirrors the T41 LQ/NQ
         * split). DDR2: T32LQ (0x9999) -> "t32". DDR3: T32NQ (0xAAAA) + T32VN/VX/XQ
         * -> "t32_ddr3" (the conservative @350 t32vn profile underclocks them all). */
        if (subtype1 == 0x9999)
            *variant = TDFU_VARIANT_T32LQ; /* DDR2 */
        else if (subtype1 == 0xAAAA)
            *variant = TDFU_VARIANT_T32NQ; /* DDR3 */
        else
            *variant = TDFU_VARIANT_T32_DDR3; /* other DDR3 -> conservative t32vn */
        break;
    case 0x0031:
        /* T31 family — DDR type depends on sub-variant:
         *   DDR3: T31A (0x4444)
         *   DDR2: T31AL (0xCCCC)
         *   DDR2: T31X (0x2222), T31N (0x1111), T31L (0x3333),
         *         T31ZX (0x6666), T31ZL (0x5555), T31ZC (0xDDDD), T31LC (0xEEEE) */
        if (subtype1 == 0x4444)
            *variant = TDFU_VARIANT_T31A; /* DDR3 -> t31a */
        else if (subtype1 == 0xCCCC)
            *variant = TDFU_VARIANT_T31AL; /* DDR2 128M -> t31x */
        else if (subtype1 == 0x6666)
            *variant = TDFU_VARIANT_T31ZX; /* DDR2 128M -> t31x */
        else if (subtype1 == 0x1111)
            *variant = TDFU_VARIANT_T31N; /* DDR2 64M -> t31n */
        else if (subtype1 == 0x3333 || subtype1 == 0x5555)
            *variant = TDFU_VARIANT_T31L; /* DDR2 64M lite (L/ZL) -> t31l */
        else if (subtype1 == 0xDDDD || subtype1 == 0xEEEE)
            *variant = TDFU_VARIANT_T31N; /* ZC/LC 64M -> t31n */
        else
            *variant = TDFU_VARIANT_T31X; /* incl 0x2222 -> t31x */
        break;
    case 0x0040:
        /* T40/T41 family. subsoctype2 (EFUSE) is the ONLY reliable discriminator:
         * soc_id/sub1/subremark are identical across the family and cppsr is a live
         * clock register (verified on T40NN/T40XP/T41LQ/T41NQ hardware - cppsr read
         * FB/94/04..0E/FF, never the vendor table's values). DDR type per part:
         *   DDR2: T40NN(0x8888), T41L(0x3333), T41LQ(0x9999) -> "t41" build
         *   DDR3: T40XP(0x7777), T41N(0x1111), T41NQ(0xAAAA), T41A(0x4444),
         *         T41ZL(0x5555), T41ZX(0x6666)               -> "t41_ddr3" build
         * 0x7777 is shared by T40XP and T41ZN (both DDR3); no stable register tells
         * them apart, so it defaults to T40XP - use --cpu t41_ddr3 for a T41ZN. */
        if (subtype2 == 0x8888)
            *variant = TDFU_VARIANT_T40; /* T40NN, DDR2 */
        else if (subtype2 == 0x7777)
            *variant = TDFU_VARIANT_T40XP; /* T40XP (or T41ZN), DDR3 */
        else if (subtype2 == 0x3333)
            *variant = TDFU_VARIANT_T41L; /* DDR2 -> "t41" */
        else if (subtype2 == 0x9999)
            *variant = TDFU_VARIANT_T41LQ; /* DDR2 -> "t41" */
        else if (subtype2 == 0x1111)
            *variant = TDFU_VARIANT_T41N; /* DDR3 -> "t41_ddr3" */
        else if (subtype2 == 0xAAAA)
            *variant = TDFU_VARIANT_T41NQ; /* DDR3 */
        else if (subtype2 == 0x4444)
            *variant = TDFU_VARIANT_T41A; /* DDR3 */
        else if (subtype2 == 0x5555)
            *variant = TDFU_VARIANT_T41ZL; /* DDR3 */
        else if (subtype2 == 0x6666)
            *variant = TDFU_VARIANT_T41ZX; /* DDR3 */
        else
            *variant = TDFU_VARIANT_T41_DDR3; /* unknown T41 grade, default DDR3 */
        break;
    case 0x0001:
        *variant = TDFU_VARIANT_A1;
        break;
    default:
        LOG_WARN("  Unknown SoC CPU ID: 0x%04X\n", cpu_id);
        return TDFU_ERROR_PROTOCOL;
    }

    /* Log exact sub-variant name for informational purposes */
    const char *chip_name = "Unknown";
    switch (cpu_id) {
    case 0x0005:
        chip_name = (subtype1 == 0x0000) ? "T10L" : "T10";
        break;
    case 0x2000:
        chip_name = (subtype1 == 0x2222) ? "T20X" : (subtype1 == 0x3333) ? "T20L" : "T20";
        break;
    case 0x0021:
        chip_name = "T21N";
        break;
    case 0x0023:
        chip_name = (subtype1 == 0x1111)   ? "T23N"
                    : (subtype1 == 0x3333) ? "T23DL"
                    : (subtype1 == 0x7777) ? "T23ZN"
                    : (subtype1 == 0x2222) ? "T23X"
                                           : "T23";
        break;
    case 0x0030:
        chip_name = (subtype1 == 0x3333) ? "T30L" : (subtype1 == 0x2222) ? "T30X" : "T30";
        break;
    case 0x0032:
        chip_name = (subtype1 == 0x9999) ? "T32LQ" : "T32";
        break;
    case 0x0031:
        chip_name = (subtype1 == 0x4444)   ? "T31A"
                    : (subtype1 == 0xCCCC) ? "T31AL"
                    : (subtype1 == 0x3333) ? "T31L"
                    : (subtype1 == 0x1111) ? "T31N"
                    : (subtype1 == 0x2222) ? "T31X"
                    : (subtype1 == 0xDDDD) ? "T31ZC"
                    : (subtype1 == 0x5555) ? "T31ZL"
                    : (subtype1 == 0x6666) ? "T31ZX"
                    : (subtype1 == 0xEEEE) ? "T31LC"
                                           : "T31";
        break;
    case 0x0040:
        chip_name = (subtype2 == 0x8888)   ? "T40NN"
                    : (subtype2 == 0x7777) ? "T40XP" /* or T41ZN */
                    : (subtype2 == 0x1111) ? "T41N"
                    : (subtype2 == 0xAAAA) ? "T41NQ"
                    : (subtype2 == 0x3333) ? "T41L"
                    : (subtype2 == 0x9999) ? "T41LQ"
                    : (subtype2 == 0x4444) ? "T41A"
                    : (subtype2 == 0x5555) ? "T41ZL"
                    : (subtype2 == 0x6666) ? "T41ZX"
                                           : "T41";
        break;
    case 0x0001:
        chip_name = "A1";
        break;
    }
    DEBUG_PRINT("Detected: %s (%s)\n", chip_name, tdfu_variant_to_string(*variant));
    return TDFU_SUCCESS;

fail:
    libusb_release_interface(device->handle, 0);
    return TDFU_ERROR_PROTOCOL;
}
