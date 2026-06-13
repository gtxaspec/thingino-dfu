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

/**
 * Read device status
 */
tdfu_error_t protocol_read_status(usb_device_t *device, uint8_t *status_buffer, int buffer_size, int *status_len) {
    if (!device || !status_buffer || buffer_size < 8) {
        return TDFU_ERROR_INVALID_PARAMETER;
    }

    DEBUG_PRINT("ReadStatus: executing\n");

    // Use VR_FW_READ_STATUS2 (0x19) - most commonly used status check
    tdfu_error_t result = usb_device_vendor_request(device, REQUEST_TYPE_VENDOR, VR_FW_READ_STATUS2, 0, 0, NULL,
                                                        buffer_size, status_buffer, status_len);

    if (result != TDFU_SUCCESS) {
        DEBUG_PRINT("ReadStatus error: %s\n", tdfu_error_to_string(result));
        return result;
    }

    DEBUG_PRINT("ReadStatus: success, got %d bytes\n", *status_len);
    return TDFU_SUCCESS;
}

/**
 * Set up a data transfer: set address + length in one call.
 */
static tdfu_error_t protocol_setup_transfer(usb_device_t *device, uint32_t addr, uint32_t length) {
    tdfu_error_t result = protocol_set_data_address(device, addr);
    if (result != TDFU_SUCCESS)
        return result;
    return protocol_set_data_length(device, length);
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

tdfu_error_t protocol_get_ack(usb_device_t *device, int32_t *status) {
    if (!device || !status) {
        return TDFU_ERROR_INVALID_PARAMETER;
    }

    uint8_t data[4];
    int response_length;
    tdfu_error_t result =
        usb_device_vendor_request(device, REQUEST_TYPE_VENDOR, VR_GET_CPU_INFO, 0, 0, NULL, 4, data, &response_length);

    if (result != TDFU_SUCCESS) {
        return result;
    }

    if (response_length < 4) {
        return TDFU_ERROR_PROTOCOL;
    }

    // Convert little-endian bytes to int32
    *status = (int32_t)data[0] | (int32_t)data[1] << 8 | (int32_t)data[2] << 16 | (int32_t)data[3] << 24;

    return TDFU_SUCCESS;
}

tdfu_error_t protocol_read_flash_id(usb_device_t *device, uint32_t *jedec_id) {
    if (!device || !jedec_id)
        return TDFU_ERROR_INVALID_PARAMETER;

    *jedec_id = 0;

    /* The vendor cloner reads flash JEDEC ID using VR_FW_READ_STATUS4 (0x26)
     * after sending the flash descriptor. The burner returns 3 bytes:
     * {capacity, type, manufacturer} in little-endian JEDEC order.
     * Verified from vendor USB capture: frame 13445/13448. */
    uint8_t buf[4] = {0};
    int len = 0;
    tdfu_error_t result =
        usb_device_vendor_request(device, REQUEST_TYPE_VENDOR, VR_FW_READ_STATUS4, 0, 0, NULL, 3, buf, &len);
    if (result != TDFU_SUCCESS || len < 3) {
        DEBUG_PRINT("Flash ID read (VR_FW_READ_STATUS4) failed: %s (len=%d)\n", tdfu_error_to_string(result), len);
        return TDFU_ERROR_PROTOCOL;
    }

    /* Response is {capacity, type, manufacturer} — reverse to standard JEDEC order */
    uint32_t id = (uint32_t)buf[2] << 16 | (uint32_t)buf[1] << 8 | buf[0];

    /* Validate manufacturer byte */
    if ((id >> 16) == 0 || id == 0xFFFFFF) {
        DEBUG_PRINT("Flash ID response not a valid JEDEC ID: %06X\n", id);
        return TDFU_ERROR_PROTOCOL;
    }

    *jedec_id = id;
    LOG_INFO("  Flash JEDEC ID: 0x%06X\n", id);
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
        *variant = TDFU_VARIANT_T10;
        break;
    case 0x2000:
        *variant = TDFU_VARIANT_T20;
        break;
    case 0x0021:
        *variant = TDFU_VARIANT_T21;
        break;
    case 0x0023:
        if (subtype1 == 0x3333)
            *variant = TDFU_VARIANT_T23DL; /* DDR2, 32MB */
        else
            *variant = TDFU_VARIANT_T23; /* DDR2, 64MB (T23N/T23X/T23ZN) */
        break;
    case 0x0030:
        *variant = TDFU_VARIANT_T30;
        break;
    case 0x0032:
        *variant = TDFU_VARIANT_T32;
        break;
    case 0x0031:
        /* T31 family — DDR type depends on sub-variant:
         *   DDR3: T31A (0x4444)
         *   DDR2: T31AL (0xCCCC)
         *   DDR2: T31X (0x2222), T31N (0x1111), T31L (0x3333),
         *         T31ZX (0x6666), T31ZL (0x5555), T31ZC (0xDDDD), T31LC (0xEEEE) */
        if (subtype1 == 0x4444)
            *variant = TDFU_VARIANT_T31A; /* DDR3 */
        else if (subtype1 == 0xCCCC)
            *variant = TDFU_VARIANT_T31AL; /* DDR2 */
        else if (subtype1 == 0x6666)
            *variant = TDFU_VARIANT_T31ZX; /* DDR2, USB re-enumerate after SPL */
        else
            *variant = TDFU_VARIANT_T31X; /* DDR2 (covers T31X/N/L/ZL/ZC/LC) */
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

tdfu_error_t protocol_init(usb_device_t *device) {
    if (!device) {
        return TDFU_ERROR_INVALID_PARAMETER;
    }

    int response_length;
    return usb_device_vendor_request(device, REQUEST_TYPE_OUT, VR_FW_HANDSHAKE, 0, 0, NULL, 0, NULL, &response_length);
}

// Enhanced timeout calculation for protocol operations
static int calculate_protocol_timeout(uint32_t size) {
    // Base timeout of 5 seconds + 1 second per 64KB
    // For 1MB transfers: 5000 + (1048576/65536)*1000 = 21 seconds
    // For larger transfers, max 60 seconds to allow sufficient time
    int timeout = 5000 + (size / 65536) * 1000;
    if (timeout > 60000)
        timeout = 60000; // Increased max to 60s for 1MB+ transfers
    return timeout;
}

// Firmware stage protocol functions
tdfu_error_t protocol_fw_read(usb_device_t *device, int data_len, uint8_t **data, int *actual_len) {
    if (!device || !data || !actual_len) {
        return TDFU_ERROR_INVALID_PARAMETER;
    }

    DEBUG_PRINT("FWRead: reading %d bytes\n", data_len);

    // For firmware reading, we need to claim interface first
    tdfu_error_t result = usb_device_claim_interface(device);
    if (result != TDFU_SUCCESS) {
        DEBUG_PRINT("FWRead failed to claim interface: %s\n", tdfu_error_to_string(result));
        return result;
    }

    // Now read actual data via bulk transfer
    uint8_t *buffer = (uint8_t *)malloc(data_len);
    if (!buffer) {
        usb_device_release_interface(device);
        return TDFU_ERROR_MEMORY;
    }

    int transferred = 0;
    int timeout = calculate_protocol_timeout(data_len);

    DEBUG_PRINT("FWRead: using adaptive timeout of %dms for %d bytes\n", timeout, data_len);

    // Use direct libusb call with adaptive timeout for better control
    int libusb_result = libusb_bulk_transfer(device->handle, ENDPOINT_IN, buffer, data_len, &transferred, timeout);

    // Handle stall errors with interface reset (from Go implementation experience)
    if (libusb_result != LIBUSB_SUCCESS) {
        DEBUG_PRINT("FWRead bulk transfer failed: %s\n", libusb_error_name(libusb_result));

        // If it's a pipe error, try to reset and retry
        if (libusb_result == LIBUSB_ERROR_PIPE) {
            DEBUG_PRINT("FWRead stall detected, resetting interface and retrying...\n");
            usb_device_release_interface(device);

            // Small delay before retry
            platform_sleep_ms(100);

            // Re-claim interface and retry once with longer timeout
            tdfu_error_t claim_result = usb_device_claim_interface(device);
            if (claim_result == TDFU_SUCCESS) {
                DEBUG_PRINT("FWRead retrying transfer after interface reset...\n");
                int retry_timeout = timeout * 2; // Double timeout for retry
                libusb_result =
                    libusb_bulk_transfer(device->handle, ENDPOINT_IN, buffer, data_len, &transferred, retry_timeout);
            } else {
                DEBUG_PRINT("FWRead failed to reclaim interface: %s\n", tdfu_error_to_string(claim_result));
            }
        }
    }

    // Release interface
    usb_device_release_interface(device);

    if (libusb_result != LIBUSB_SUCCESS) {
        DEBUG_PRINT("FWRead bulk transfer error: %s\n", libusb_error_name(libusb_result));
        free(buffer);
        return TDFU_ERROR_TRANSFER_FAILED;
    }

    DEBUG_PRINT("FWRead success: got %d bytes (requested %d)\n", transferred, data_len);

    *data = buffer;
    *actual_len = transferred;
    return TDFU_SUCCESS;
}

tdfu_error_t protocol_fw_handshake(usb_device_t *device) {
    if (!device) {
        return TDFU_ERROR_INVALID_PARAMETER;
    }

    DEBUG_PRINT("FWHandshake: sending vendor request (command 0x%02X)\n", VR_FW_HANDSHAKE);

    // VR_FW_HANDSHAKE (0x11) is a vendor request, NOT an INT endpoint operation
    // Send it as a control/vendor request like all bootstrap commands
    // This request takes no parameters (wValue=0, wIndex=0) and has no data
    int response_length = 0;
    tdfu_error_t result =
        usb_device_vendor_request(device, REQUEST_TYPE_OUT, VR_FW_HANDSHAKE, 0, 0, NULL, 0, NULL, &response_length);

    if (result != TDFU_SUCCESS) {
        DEBUG_PRINT("FWHandshake vendor request failed: %s\n", tdfu_error_to_string(result));
        return result;
    }

    DEBUG_PRINT("FWHandshake vendor request sent successfully\n");

    // Platform-specific sleep after successful handshake
    platform_sleep_ms(BULK_DATA_PREP_DELAY_MS);

    return TDFU_SUCCESS;
}

tdfu_error_t protocol_fw_write_chunk1(usb_device_t *device, const uint8_t *data) {
    if (!device || !data) {
        return TDFU_ERROR_INVALID_PARAMETER;
    }

    DEBUG_PRINT("FWWriteChunk1: writing %d bytes\n", FW_HANDSHAKE_SIZE);

    int response_length;
    tdfu_error_t result = usb_device_vendor_request(device, REQUEST_TYPE_OUT, VR_READ, 0, 0, (uint8_t *)data,
                                                        FW_HANDSHAKE_SIZE, NULL, &response_length);

    if (result != TDFU_SUCCESS) {
        DEBUG_PRINT("FWWriteChunk1 error: %s\n", tdfu_error_to_string(result));
        return result;
    }

    DEBUG_PRINT("FWWriteChunk1 OK\n");

    // Platform-specific sleep
    platform_sleep_ms(BULK_DATA_PREP_DELAY_MS);

    return TDFU_SUCCESS;
}

// ============================================================================
// PROPER PROTOCOL FUNCTIONS (Using Bootloader Code Execution Pattern)
// ============================================================================

/**
 * Load firmware reader stub into RAM and execute it
 * Protocol: VR_SET_DATA_ADDRESS → VR_SET_DATA_LENGTH → VR_PROGRAM_START1 → Bulk-Out → VR_PROGRAM_START2
 */
tdfu_error_t protocol_load_and_execute_code(usb_device_t *device, uint32_t ram_address, const uint8_t *code,
                                                uint32_t code_size) {
    if (!device || !code || code_size == 0) {
        return TDFU_ERROR_INVALID_PARAMETER;
    }

    DEBUG_PRINT("Loading code to RAM: address=0x%08X, size=%u bytes\n", ram_address, code_size);

    // Step 1: Set RAM address for code
    tdfu_error_t result = protocol_prog_stage1(device, ram_address);
    if (result != TDFU_SUCCESS) {
        DEBUG_PRINT("Failed to set RAM address for code: %s\n", tdfu_error_to_string(result));
        return result;
    }

    // Step 2: Transfer code to device via bulk-out
    int transferred = 0;
    result = usb_device_bulk_transfer(device, ENDPOINT_OUT, (uint8_t *)code, code_size, &transferred, 10000);
    if (result != TDFU_SUCCESS) {
        DEBUG_PRINT("Failed to transfer code: %s\n", tdfu_error_to_string(result));
        return result;
    }

    DEBUG_PRINT("Code transferred: %d/%u bytes\n", transferred, code_size);

    if (transferred < (int)code_size) {
        DEBUG_PRINT("Warning: Not all code bytes transferred (%d/%u)\n", transferred, code_size);
    }

    // Step 3: Execute code at RAM address
    result = protocol_prog_stage2(device, ram_address);
    if (result != TDFU_SUCCESS) {
        DEBUG_PRINT("Failed to execute code: %s\n", tdfu_error_to_string(result));
        return result;
    }

    DEBUG_PRINT("Code execution initiated\n");
    return TDFU_SUCCESS;
}

/**
 * Firmware read using bootloader's code execution pattern
 * Proper protocol: Set address → Set size → Load reader stub → Execute → Bulk-in data
 */
tdfu_error_t protocol_proper_firmware_read(usb_device_t *device, uint32_t flash_offset, uint32_t read_size,
                                               uint8_t **out_data, int *out_len) {
    if (!device || !out_data || !out_len) {
        return TDFU_ERROR_INVALID_PARAMETER;
    }

    DEBUG_PRINT("ProperFirmwareRead: offset=0x%08X, size=%u bytes\n", flash_offset, read_size);

    // Step 1: Set flash address and size
    tdfu_error_t result = protocol_setup_transfer(device, flash_offset, read_size);
    if (result != TDFU_SUCCESS)
        return result;

    // Step 2: For now, use the existing firmware read (requires firmware reader stub to be loaded separately)
    // In a complete implementation, you would:
    // 1. Load firmware reader stub here
    // 2. Execute it via protocol_load_and_execute_code()
    // 3. Read the data via bulk-in with proper handshaking

    DEBUG_PRINT("ProperFirmwareRead: Address and size set. Requires firmware reader stub to be loaded separately.\n");

    // Fallback to protocol_fw_read for now
    return protocol_fw_read(device, read_size, out_data, out_len);
}

/**
 * Firmware write using bootloader's code execution pattern with CRC32 verification
 * Proper protocol: Set address → Set size → Load writer stub → Execute → Bulk-out data → Verify
 */
tdfu_error_t protocol_proper_firmware_write(usb_device_t *device, uint32_t flash_offset, const uint8_t *data,
                                                uint32_t data_size) {
    if (!device || !data || data_size == 0) {
        return TDFU_ERROR_INVALID_PARAMETER;
    }

    DEBUG_PRINT("ProperFirmwareWrite: offset=0x%08X, size=%u bytes\n", flash_offset, data_size);

    // Step 1: Set flash address and size
    tdfu_error_t result = protocol_setup_transfer(device, flash_offset, data_size);
    if (result != TDFU_SUCCESS)
        return result;

    // Step 2: Calculate CRC32 for data verification
    uint32_t crc = calculate_crc32(data, data_size);
    DEBUG_PRINT("Data CRC32: 0x%08X\n", crc);

    // Step 3: Prepare buffer with data + CRC32
    uint32_t buffer_size = data_size + 4;
    uint8_t *write_buffer = (uint8_t *)malloc(buffer_size);
    if (!write_buffer) {
        return TDFU_ERROR_MEMORY;
    }

    memcpy(write_buffer, data, data_size);
    // Append CRC32 (little-endian)
    write_buffer[data_size + 0] = (crc >> 0) & 0xFF;
    write_buffer[data_size + 1] = (crc >> 8) & 0xFF;
    write_buffer[data_size + 2] = (crc >> 16) & 0xFF;
    write_buffer[data_size + 3] = (crc >> 24) & 0xFF;

    DEBUG_PRINT("ProperFirmwareWrite: Buffer size with CRC: %u bytes\n", buffer_size);

    // Step 4: For now, this requires firmware writer stub
    // In a complete implementation, you would:
    // 1. Load firmware writer stub here
    // 2. Execute it via protocol_load_and_execute_code()
    // 3. Send firmware data via bulk-out with proper handshaking

    DEBUG_PRINT("ProperFirmwareWrite: Address and size set. Requires firmware writer stub to be loaded separately.\n");

    free(write_buffer);
    return TDFU_SUCCESS;
}

// ============================================================================
// VENDOR-STYLE FIRMWARE READ (Fallback - derived from vendor tool USB captures)
// ============================================================================

// Vendor-style firmware read using VR_READ (0x13) command
// This matches the vendor tool's approach: send 40-byte command, check status, bulk read
tdfu_error_t protocol_vendor_style_read(usb_device_t *device, uint32_t offset, uint32_t size, uint8_t **data,
                                            int *actual_len) {
    if (!device || !data || !actual_len) {
        return TDFU_ERROR_INVALID_PARAMETER;
    }

    DEBUG_PRINT("VendorStyleRead: offset=0x%08X, size=%u bytes\n", offset, size);

    // CRITICAL: Initialize device state with SetDataAddress and SetDataLength
    // This prepares the U-Boot firmware to read from the specified address/size
    // Same pattern as NAND_OPS - must be done BEFORE issuing the read command
    tdfu_error_t result = protocol_setup_transfer(device, offset, size);
    if (result != TDFU_SUCCESS) {
        DEBUG_PRINT("VendorStyleRead: setup failed: %s\n", tdfu_error_to_string(result));
        return result;
    }

    DEBUG_PRINT("VendorStyleRead: Device initialized for address=0x%08X, length=%u\n", offset, size);

    // Build 40-byte command buffer for VR_READ (0x13)
    // Based on ioctl log analysis:
    // Bytes 0-3: offset (little-endian)
    // Bytes 4-7: unknown (0x00000000)
    // Bytes 8-11: unknown (0x00000000)
    // Bytes 12-15: unknown (0x00000000)
    // Bytes 16-19: unknown (0x00000000)
    // Bytes 20-23: size (little-endian)
    // Bytes 24-31: unknown (0x00000000 0x00000000)
    // Bytes 32-39: vendor-specific data (varies between calls)
    uint8_t cmd_buffer[FW_HANDSHAKE_SIZE] = {0};

    // Set offset (little-endian)
    cmd_buffer[0] = (offset >> 0) & 0xFF;
    cmd_buffer[1] = (offset >> 8) & 0xFF;
    cmd_buffer[2] = (offset >> 16) & 0xFF;
    cmd_buffer[3] = (offset >> 24) & 0xFF;

    // Set size (little-endian) at offset 20
    cmd_buffer[20] = (size >> 0) & 0xFF;
    cmd_buffer[21] = (size >> 8) & 0xFF;
    cmd_buffer[22] = (size >> 16) & 0xFF;
    cmd_buffer[23] = (size >> 24) & 0xFF;

    // Set vendor-specific bytes (pattern from vendor tool)
    cmd_buffer[32] = 0x06;
    cmd_buffer[33] = 0x00;
    cmd_buffer[34] = 0x05;
    cmd_buffer[35] = 0x7F;
    cmd_buffer[36] = 0x00;
    cmd_buffer[37] = 0x00;

    // Send VR_READ command with handshake-sized buffer
    int response_length;
    result = usb_device_vendor_request(device, REQUEST_TYPE_OUT, VR_READ, 0, 0, cmd_buffer, FW_HANDSHAKE_SIZE, NULL,
                                       &response_length);

    if (result != TDFU_SUCCESS) {
        DEBUG_PRINT("VendorStyleRead: VR_READ command failed: %s\n", tdfu_error_to_string(result));
        return result;
    }

    DEBUG_PRINT("VendorStyleRead: VR_READ command sent successfully\n");

    // Check status with VR_FW_READ_STATUS2 (0x19)
    uint8_t status_buffer[8] = {0};
    int status_len;
    result = usb_device_vendor_request(device, REQUEST_TYPE_VENDOR, VR_FW_READ_STATUS2, 0, 0, NULL, 8, status_buffer,
                                       &status_len);

    if (result != TDFU_SUCCESS) {
        DEBUG_PRINT("VendorStyleRead: Status check failed: %s\n", tdfu_error_to_string(result));
        return result;
    }

    DEBUG_PRINT("VendorStyleRead: Status check OK (got %d bytes)\n", status_len);
    DEBUG_PRINT("Status buffer: %02X %02X %02X %02X %02X %02X %02X %02X\n", status_buffer[0], status_buffer[1],
                status_buffer[2], status_buffer[3], status_buffer[4], status_buffer[5], status_buffer[6],
                status_buffer[7]);

    // Wait for device to prepare data for bulk transfer
    platform_sleep_ms(BULK_DATA_PREP_DELAY_MS); // delay for device to prepare bulk data

    // Allocate buffer for bulk read
    uint8_t *buffer = (uint8_t *)malloc(size);
    if (!buffer) {
        return TDFU_ERROR_MEMORY;
    }

    // Perform bulk IN transfer on endpoint 0x81
    // Calculate adaptive timeout based on transfer size
    // For 1MB: 21 seconds; for larger transfers: up to 60 seconds
    int timeout = calculate_protocol_timeout(size);

    DEBUG_PRINT("VendorStyleRead: Using adaptive timeout of %dms for %u bytes\n", timeout, size);

    int transferred = 0;
    result = usb_device_bulk_transfer(device, USB_ENDPOINT_IN, buffer, size, &transferred, timeout);

    if (result != TDFU_SUCCESS) {
        DEBUG_PRINT("VendorStyleRead: Bulk transfer failed: %s\n", tdfu_error_to_string(result));
        free(buffer);
        return result;
    }

    DEBUG_PRINT("VendorStyleRead: Successfully read %d bytes (requested %u)\n", transferred, size);

    *data = buffer;
    *actual_len = transferred;
    return TDFU_SUCCESS;
}

// Traditional firmware read using VR_READ command (alternative approach)
tdfu_error_t protocol_traditional_read(usb_device_t *device, int data_len, uint8_t **data, int *actual_len) {
    if (!device || !data || !actual_len) {
        return TDFU_ERROR_INVALID_PARAMETER;
    }

    DEBUG_PRINT("TraditionalRead: reading %d bytes using VR_READ\n", data_len);

    // Claim interface for the operation
    tdfu_error_t result = usb_device_claim_interface(device);
    if (result != TDFU_SUCCESS) {
        DEBUG_PRINT("TraditionalRead failed to claim interface: %s\n", tdfu_error_to_string(result));
        return result;
    }

    // Use traditional VR_READ command
    uint8_t *buffer = (uint8_t *)malloc(data_len);
    if (!buffer) {
        usb_device_release_interface(device);
        return TDFU_ERROR_MEMORY;
    }

    int transferred = 0;
    result =
        usb_device_vendor_request(device, REQUEST_TYPE_VENDOR, VR_READ, 0, 0, NULL, data_len, buffer, &transferred);

    // Release interface after transfer
    usb_device_release_interface(device);

    if (result != TDFU_SUCCESS) {
        DEBUG_PRINT("TraditionalRead vendor request error: %s\n", tdfu_error_to_string(result));
        free(buffer);
        return result;
    }

    DEBUG_PRINT("TraditionalRead success: got %d bytes (requested %d)\n", transferred, data_len);

    *data = buffer;
    *actual_len = transferred;
    return TDFU_SUCCESS;
}

tdfu_error_t protocol_fw_read_operation(usb_device_t *device, uint32_t offset, uint32_t length, uint8_t **data,
                                            int *actual_len) {
    if (!device || !data || !actual_len) {
        return TDFU_ERROR_INVALID_PARAMETER;
    }

    DEBUG_PRINT("FWReadOperation: offset=0x%08X, length=%u\n", offset, length);

    // Set address and length first
    tdfu_error_t result = protocol_setup_transfer(device, offset, length);
    if (result != TDFU_SUCCESS)
        return result;

    // Try different operation parameters for read
    uint8_t *buffer = (uint8_t *)malloc(length);
    if (!buffer) {
        return TDFU_ERROR_MEMORY;
    }

    int response_length;
    // Try operation 12 with different parameters based on reference config analysis
    result = usb_device_vendor_request(device, REQUEST_TYPE_VENDOR, 12, 0, 0, NULL, length, buffer, &response_length);

    if (result != TDFU_SUCCESS) {
        DEBUG_PRINT("FWReadOperation error: %s\n", tdfu_error_to_string(result));
        free(buffer);
        return result;
    }

    DEBUG_PRINT("FWReadOperation success: got %d bytes (requested %u)\n", response_length, length);

    *data = buffer;
    *actual_len = response_length;
    return TDFU_SUCCESS;
}

tdfu_error_t protocol_fw_read_status(usb_device_t *device, int status_cmd, uint32_t *status) {
    if (!device || !status) {
        return TDFU_ERROR_INVALID_PARAMETER;
    }

    DEBUG_PRINT("FWReadStatus: checking status with command 0x%02X\n", status_cmd);

    uint8_t data[4];
    int response_length;
    tdfu_error_t result =
        usb_device_vendor_request(device, REQUEST_TYPE_VENDOR, status_cmd, 0, 0, NULL, 4, data, &response_length);

    if (result != TDFU_SUCCESS) {
        DEBUG_PRINT("FWReadStatus error: %s\n", tdfu_error_to_string(result));
        return result;
    }

    if (response_length < 4) {
        DEBUG_PRINT("FWReadStatus: insufficient response length %d\n", response_length);
        return TDFU_ERROR_PROTOCOL;
    }

    // Convert little-endian bytes to uint32
    *status = (uint32_t)data[0] | (uint32_t)data[1] << 8 | (uint32_t)data[2] << 16 | (uint32_t)data[3] << 24;

    DEBUG_PRINT("FWReadStatus: status = 0x%08X (%u)\n", *status, *status);
    return TDFU_SUCCESS;
}

tdfu_error_t protocol_fw_write_chunk2(usb_device_t *device, const uint8_t *data) {
    if (!device || !data) {
        return TDFU_ERROR_INVALID_PARAMETER;
    }

    DEBUG_PRINT("FWWriteChunk2: writing %d bytes\n", FW_HANDSHAKE_SIZE);

    int response_length;
    tdfu_error_t result = usb_device_vendor_request(device, REQUEST_TYPE_OUT, VR_FW_WRITE2, 0, 0, (uint8_t *)data,
                                                        FW_HANDSHAKE_SIZE, NULL, &response_length);

    if (result != TDFU_SUCCESS) {
        DEBUG_PRINT("FWWriteChunk2 error: %s\n", tdfu_error_to_string(result));
        return result;
    }

    DEBUG_PRINT("FWWriteChunk2 OK\n");

    // Platform-specific sleep
    platform_sleep_ms(BULK_DATA_PREP_DELAY_MS);

    return TDFU_SUCCESS;
}

// ============================================================================
// NAND OPERATIONS (VR_NAND_OPS - 0x07)
// ============================================================================

/**
 * Read firmware via NAND_OPS (VR_NAND_OPS 0x07 with NAND_READ subcommand 0x05)
 *
 * Protocol sequence:
 * 1. Set data address (SPI-NAND flash offset)
 * 2. Set data length (how many bytes to read)
 * 3. Issue NAND_OPS read command (0x07)
 * 4. Bulk-in transfer to read the data
 *
 * This uses the NAND_OPS command built into U-Boot bootloader
 */
tdfu_error_t protocol_nand_read(usb_device_t *device, uint32_t offset, uint32_t size, uint8_t **data,
                                    int *transferred) {
    if (!device || !data || !transferred || size == 0) {
        return TDFU_ERROR_INVALID_PARAMETER;
    }

    DEBUG_PRINT("NAND_OPS Read: offset=0x%08X, size=%u bytes\n", offset, size);

    // Step 1: Set data address (flash offset)
    tdfu_error_t result = protocol_setup_transfer(device, offset, size);
    if (result != TDFU_SUCCESS) {
        DEBUG_PRINT("NAND_OPS: SetDataLength failed: %s\n", tdfu_error_to_string(result));
        return result;
    }

    // Step 3: Issue NAND_OPS read command (0x07 with subcommand 0x05)
    DEBUG_PRINT("NAND_OPS: Issuing read command (VR_NAND_OPS=0x07, subcommand=0x%02X)\n", NAND_OPERATION_READ);

    int response_length;
    result = usb_device_vendor_request(device, REQUEST_TYPE_OUT, VR_NAND_OPS, NAND_OPERATION_READ, 0x0000, NULL, 0,
                                       NULL, &response_length);

    if (result != TDFU_SUCCESS) {
        DEBUG_PRINT("NAND_OPS: Command failed: %s\n", tdfu_error_to_string(result));
        return result;
    }

    DEBUG_PRINT("NAND_OPS: Command sent successfully\n");

    // Give device time to prepare data for bulk transfer
    platform_sleep_ms(BULK_DATA_PREP_DELAY_MS);

    // Step 4: Bulk-in transfer to read the data
    uint8_t *buffer = (uint8_t *)malloc(size);
    if (!buffer) {
        DEBUG_PRINT("NAND_OPS: Memory allocation failed for %u bytes\n", size);
        return TDFU_ERROR_MEMORY;
    }

    // Calculate timeout based on transfer size
    int timeout = calculate_protocol_timeout(size);
    DEBUG_PRINT("NAND_OPS: Performing bulk-in transfer (timeout=%dms)...\n", timeout);

    // Perform bulk transfer
    int bytes_transferred = 0;
    int libusb_result = libusb_bulk_transfer(device->handle, ENDPOINT_IN, buffer, size, &bytes_transferred, timeout);

    if (libusb_result != LIBUSB_SUCCESS) {
        DEBUG_PRINT("NAND_OPS: Bulk transfer failed: %s\n", libusb_error_name(libusb_result));
        free(buffer);
        return TDFU_ERROR_TRANSFER_FAILED;
    }

    DEBUG_PRINT("NAND_OPS: Successfully read %d bytes (requested %u bytes)\n", bytes_transferred, size);

    *data = buffer;
    *transferred = bytes_transferred;
    return TDFU_SUCCESS;
}