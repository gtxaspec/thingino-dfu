#include "tdfu/tdfu.h"
#include "platform.h"
#include "tdfu/constants.h"
#include "tdfu/platform_profile.h"

// ============================================================================
// FIRMWARE HANDSHAKE PROTOCOL (40-byte chunk transfers)
// ============================================================================

/**
 * Handshake Structure (8 bytes total - 4 u16 values)
 * Used to verify status of firmware read/write operations
 */
typedef struct {
    uint16_t result_low;  // Lower 16 bits of result code
    uint16_t result_high; // Upper 16 bits of result code
    uint16_t reserved;    // Reserved field
    uint16_t status;      // Device status
} firmware_handshake_t;

/**
 * Parse handshake response from device
 * Returns 0x0000 for success, 0xFFFF for CRC failure
 */
static uint32_t parse_handshake_result(const firmware_handshake_t *hs) {
    if (!hs) {
        return 0xFFFFFFFF;
    }
    return (uint32_t)hs->result_low | ((uint32_t)hs->result_high << 16);
}

// Compute CRC32 over a buffer (matches standard Ethernet CRC32)
static uint32_t firmware_crc32(const uint8_t *data, uint32_t length) {
    if (!data || length == 0) {
        return 0;
    }

    uint32_t crc = CRC32_INITIAL;

    for (uint32_t i = 0; i < length; ++i) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; ++bit) {
            if (crc & 1) {
                crc = (crc >> 1) ^ CRC32_POLYNOMIAL;
            } else {
                crc >>= 1;
            }
        }
    }

    return crc ^ 0xFFFFFFFF;
}

// Drain log messages from bulk IN endpoint(s) after a write chunk.
// Vendor tool issues many IN transfers on 0x81/0x82 between chunks; we
// approximate this by reading small chunks with short timeouts and
// ignoring the contents.
__attribute__((unused)) static void firmware_drain_logs(usb_device_t *device, uint8_t endpoint, int max_reads) {
    if (!device) {
        return;
    }

    uint8_t buf[FW_LOG_BUFFER_SIZE];
    int transferred = 0;

    for (int i = 0; i < max_reads; ++i) {
        tdfu_error_t res = usb_device_bulk_transfer(device, endpoint, buf, sizeof(buf), &transferred, 10);
        if (res != TDFU_SUCCESS || transferred <= 0) {
            break;
        }

        DEBUG_PRINT("FW log: ep=0x%02X, %d bytes\n", endpoint, transferred);
    }
}

/**
 * Firmware read with 40-byte handshake protocol (vendor uint64_t layout)
 *
 * VR_READ (0x13) 40-byte struct matching the vendor host tool:
 *   [0-7]   uint64_t offset    (flash read offset, LE)
 *   [8-15]  uint64_t remaining (remaining bytes to read, LE)
 *   [16-23] uint64_t size      (this chunk's read size, LE)
 *   [24-27] uint32_t ops       = OPS(SPI,RAW) = 0x00060000
 *   [28-39] zeros
 *
 * Protocol per chunk:
 *   1. Send VR_READ (0x13) control OUT with 40-byte struct
 *   2. Read VR_FW_READ_STATUS2 (0x19) for 8-byte status
 *   3. Bulk IN for chunk data
 *   4. VR_FW_READ (0x10) 4-byte status read (ack)
 */
tdfu_error_t firmware_handshake_read_chunk(usb_device_t *device, uint32_t chunk_index, uint32_t chunk_offset,
                                               uint32_t chunk_size, uint32_t total_size, uint8_t **out_data,
                                               int *out_len) {
    if (!device || !out_data || !out_len || chunk_size == 0) {
        return TDFU_ERROR_INVALID_PARAMETER;
    }

    uint32_t remaining = total_size - chunk_offset;

    DEBUG_PRINT("FirmwareHandshakeReadChunk: index=%u, offset=0x%08X, size=%u, remaining=%u\n", chunk_index,
                chunk_offset, chunk_size, remaining);

    tdfu_error_t result;

    /* Build 40-byte read handshake — platform dependent layout.
     *
     * A1/xburst2 (from a1_vendor_read.pcap):
     *   [8-11]  ops = OPS(SPI,RAW) = 0x60000
     *   [12-15] offset = chunk flash offset
     *   [16-19] length = chunk size
     *
     * T31/xburst1 (SHA256-verified):
     *   [8-11]  offset = chunk flash offset
     *   [16-19] length = chunk size
     */
    uint8_t handshake_cmd[FW_HANDSHAKE_SIZE] = {0};
    const platform_profile_t *profile = platform_get_profile(device->info.variant);

    /* [0-7] partition = 0 (already zeroed) */

    if (profile->crc_format == CRC_FMT_A1) {
        /* A1 only: OPS at [8-11], offset at [12-15] (from a1_vendor_read.pcap) */
        handshake_cmd[8] = 0x00;
        handshake_cmd[9] = 0x00;
        handshake_cmd[10] = 0x06;
        handshake_cmd[11] = 0x00;

        handshake_cmd[12] = (chunk_offset >> 0) & 0xFF;
        handshake_cmd[13] = (chunk_offset >> 8) & 0xFF;
        handshake_cmd[14] = (chunk_offset >> 16) & 0xFF;
        handshake_cmd[15] = (chunk_offset >> 24) & 0xFF;
    } else {
        /* T31/T20/xburst1: offset at [8-11], OPS at [24-27] */
        handshake_cmd[8] = (chunk_offset >> 0) & 0xFF;
        handshake_cmd[9] = (chunk_offset >> 8) & 0xFF;
        handshake_cmd[10] = (chunk_offset >> 16) & 0xFF;
        handshake_cmd[11] = (chunk_offset >> 24) & 0xFF;

        /* [24-27] OPS(SPI,RAW) = 0x00060000 */
        handshake_cmd[24] = 0x00;
        handshake_cmd[25] = 0x00;
        handshake_cmd[26] = 0x06;
        handshake_cmd[27] = 0x00;
    }

    /* [16-19] length = chunk size */
    handshake_cmd[16] = (chunk_size >> 0) & 0xFF;
    handshake_cmd[17] = (chunk_size >> 8) & 0xFF;
    handshake_cmd[18] = (chunk_size >> 16) & 0xFF;
    handshake_cmd[19] = (chunk_size >> 24) & 0xFF;

    /* [20-39] zeros (already zeroed) */

    DEBUG_PRINT("Read handshake bytes:");
    for (int i = 0; i < FW_HANDSHAKE_SIZE; i++) {
        if (i % 8 == 0)
            DEBUG_PRINT("\n  ");
        DEBUG_PRINT("%02X ", handshake_cmd[i]);
    }
    DEBUG_PRINT("\n");

    /* Send via VR_READ (0x13) */
    uint8_t handshake_cmd_code = VR_READ; /* 0x13 */
    DEBUG_PRINT("Sending read command 0x%02X (40 bytes)...\n", handshake_cmd_code);

    int response_len = 0;
    result = usb_device_vendor_request(device, REQUEST_TYPE_OUT, handshake_cmd_code, 0, 0, handshake_cmd,
                                       FW_HANDSHAKE_SIZE, NULL, &response_len);

    if (result != TDFU_SUCCESS) {
        DEBUG_PRINT("Failed to send read handshake: %s\n", tdfu_error_to_string(result));
        return result;
    }

    DEBUG_PRINT("Read handshake sent, waiting for status...\n");

    // Small delay to allow device to process
    platform_sleep_ms(50);

    // Read status handshake from device (8 bytes)
    uint8_t status_buffer[8] = {0};
    int status_len = 0;
    result = usb_device_vendor_request(device, REQUEST_TYPE_VENDOR, VR_FW_READ_STATUS2, 0, 0, NULL, 8, status_buffer,
                                       &status_len);

    if (result != TDFU_SUCCESS) {
        DEBUG_PRINT("Failed to read status handshake: %s\n", tdfu_error_to_string(result));
        return result;
    }

    if (status_len < 8) {
        DEBUG_PRINT("Warning: Incomplete status handshake (%d/8 bytes)\n", status_len);
    }

    // Debug: Print status buffer content
    DEBUG_PRINT("Status buffer: %02X %02X %02X %02X %02X %02X %02X %02X\n", status_buffer[0], status_buffer[1],
                status_buffer[2], status_buffer[3], status_buffer[4], status_buffer[5], status_buffer[6],
                status_buffer[7]);

    // Parse handshake response via memcpy to avoid strict aliasing violation
    firmware_handshake_t hs;
    memcpy(&hs, status_buffer, sizeof(hs));
    uint32_t hs_result = parse_handshake_result(&hs);

    DEBUG_PRINT("Handshake result: 0x%08X (low=0x%04X, high=0x%04X, status=0x%04X)\n", hs_result, hs.result_low,
                hs.result_high, hs.status);

    // Check for CRC failure indication (0xFFFF in result fields)
    // NOTE: Device may return 0xFFFF legitimately, so we log but don't fail
    if (hs.result_low == 0xFFFF || hs.result_high == 0xFFFF) {
        DEBUG_PRINT("Warning: Device handshake shows 0xFFFF (may not indicate failure)\n");
    }

    // Wait for device to prepare data for bulk transfer
    platform_sleep_ms(50);

    // Now perform bulk-in transfer to read the actual data
    DEBUG_PRINT("Reading %u bytes of data via bulk-in...\n", chunk_size);

    uint8_t *data_buffer = (uint8_t *)malloc(chunk_size);
    if (!data_buffer) {
        return TDFU_ERROR_MEMORY;
    }

    int transferred = 0;
    int timeout = HANDSHAKE_BULK_TIMEOUT_MS;

    result = usb_device_bulk_transfer(device, ENDPOINT_IN, data_buffer, chunk_size, &transferred, timeout);

    if (result != TDFU_SUCCESS) {
        DEBUG_PRINT("Bulk-in transfer failed: %s\n", tdfu_error_to_string(result));
        free(data_buffer);
        return result;
    }

    DEBUG_PRINT("Data received: %d/%u bytes\n", transferred, chunk_size);
    DEBUG_PRINT("DEBUG: transferred value after bulk transfer = %d\n", transferred);
    if (transferred >= 32) {
        DEBUG_PRINT("First 32 bytes: %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X "
                    "%02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X\n",
                    data_buffer[0], data_buffer[1], data_buffer[2], data_buffer[3], data_buffer[4], data_buffer[5],
                    data_buffer[6], data_buffer[7], data_buffer[8], data_buffer[9], data_buffer[10], data_buffer[11],
                    data_buffer[12], data_buffer[13], data_buffer[14], data_buffer[15], data_buffer[16], data_buffer[17],
                    data_buffer[18], data_buffer[19], data_buffer[20], data_buffer[21], data_buffer[22], data_buffer[23],
                    data_buffer[24], data_buffer[25], data_buffer[26], data_buffer[27], data_buffer[28], data_buffer[29],
                    data_buffer[30], data_buffer[31]);
    }

    // CRITICAL: After bulk IN completes, we must tickle the firmware with
    // VR_FW_READ (0x10). Factory tool analysis shows this is required to
    // acknowledge the transfer and prepare the device for the next operation.
    // Vendor trace shows bmRequestType=0xC0 and wLength=4.
    DEBUG_PRINT("Sending final VR_FW_READ (0x10) with 4-byte status...\n");
    uint8_t final_status[4] = {0};
    int final_status_len = 0;

    int ctrl_result = libusb_control_transfer(device->handle, REQUEST_TYPE_VENDOR, VR_FW_READ, 0, 0, final_status,
                                              sizeof(final_status), 5000);

    if (ctrl_result < 0) {
        DEBUG_PRINT("Warning: VR_FW_READ after read chunk failed: %d (%s)\n", ctrl_result,
                    libusb_error_name(ctrl_result));
        // Don't fail the operation - the data was already received
    } else {
        final_status_len = ctrl_result;
        DEBUG_PRINT("Final FW_READ status: len=%d, bytes=%02X %02X %02X %02X\n", final_status_len, final_status[0],
                    final_status[1], final_status[2], final_status[3]);
    }

    DEBUG_PRINT("DEBUG: transferred value before assignment = %d\n", transferred);

    *out_data = data_buffer;
    *out_len = transferred;

    DEBUG_PRINT("firmware_handshake_read_chunk returning: transferred=%d, *out_len=%d\n", transferred, *out_len);

    return TDFU_SUCCESS;
}

/**
 * Firmware write with 40-byte handshake protocol
 *
 * Protocol (as observed in vendor T31 doorbell capture):
 * 1. Set total firmware size with VR_SET_DATA_LEN (once, before first chunk)
 * 2. For each chunk:
 *    - Send VR_WRITE (0x12) with 40-byte handshake structure
 *    - Bulk-out transfer firmware data chunk
 *    - Device logs progress via bulk-IN and FW_READ
 */
tdfu_error_t firmware_handshake_write_chunk(usb_device_t *device, uint32_t chunk_index, uint32_t chunk_offset,
                                                const uint8_t *data, uint32_t data_size) {
    if (!device || !data || data_size == 0) {
        return TDFU_ERROR_INVALID_PARAMETER;
    }

    DEBUG_PRINT("FirmwareHandshakeWriteChunk: index=%u, offset=0x%08X, size=%u\n", chunk_index, chunk_offset,
                data_size);

    // Build 40-byte handshake command for write
    // Layout derived from vendor T31 write capture vendor_write_real_20251118_122703.pcap
    // and extended with T41N/T41 (XBurst2) trailer from t41_full_write_20251119_185651.pcap:
    //   Bytes  0-9 : zeros
    //   Bytes 10-11: Chunk offset in 64KB units (little-endian)
    //   Bytes 12-17: zeros
    //   Bytes 18-19: Chunk size in 64KB units (for 128KB: 0x0002, for 64KB: 0x0001)
    //   Bytes 20-23: zeros
    //   Bytes 24-27: 0x00000600 (00 00 06 00)
    //   Bytes 28-31: ~CRC32(chunk_data) (little-endian)
    //   Bytes 32-39: Constant trailer (T31: 20 FB 00 08 A2 77 00 00,
    //                                   T41N: F0 17 00 44 70 7A 00 00)
    //
    // Verified pattern from complete capture with 128 chunks on T31 and
    // from t41_full_write_20251119_185651.pcap on T41N.
    /* Build handshake matching the burner's union cmd / struct write:
     *   [0-7]   partition (uint64_t) - always 0
     *   [8-11]  ops (uint32_t) - flash offset in bytes
     *   [12-15] offset (uint32_t) - always 0
     *   [16-19] length (uint32_t) - chunk size in bytes
     *   [20-23] crc (uint32_t) - CRC32 of chunk data
     *   [24-39] extra trailer data (platform-specific)
     *
     * Source: f_jz_cloner.c lines 152-173 (union cmd / struct write)
     *         Line 777: write_req->length = cmd->write.length
     */
    uint8_t handshake_cmd[FW_HANDSHAKE_SIZE] = {0};

    const platform_profile_t *profile = platform_get_profile(device->info.variant);
    uint32_t crc = firmware_crc32(data, data_size);

    /* Layout depends on platform's struct write format */
    switch (profile->crc_format) {
    case CRC_FMT_T20:
        /* T20: [8-11]=offset [16-19]=length [20-23]=0 [24-27]=OPS [28-31]=~CRC */
        handshake_cmd[8] = (chunk_offset >> 0) & 0xFF;
        handshake_cmd[9] = (chunk_offset >> 8) & 0xFF;
        handshake_cmd[10] = (chunk_offset >> 16) & 0xFF;
        handshake_cmd[11] = (chunk_offset >> 24) & 0xFF;
        handshake_cmd[16] = (data_size >> 0) & 0xFF;
        handshake_cmd[17] = (data_size >> 8) & 0xFF;
        handshake_cmd[18] = (data_size >> 16) & 0xFF;
        handshake_cmd[19] = (data_size >> 24) & 0xFF;
        handshake_cmd[24] = 0x00;
        handshake_cmd[25] = 0x00;
        handshake_cmd[26] = 0x06;
        handshake_cmd[27] = 0x00;
        {
            uint32_t raw_crc = ~crc;
            handshake_cmd[28] = (raw_crc >> 0) & 0xFF;
            handshake_cmd[29] = (raw_crc >> 8) & 0xFF;
            handshake_cmd[30] = (raw_crc >> 16) & 0xFF;
            handshake_cmd[31] = (raw_crc >> 24) & 0xFF;
        }
        break;

    case CRC_FMT_A1:
        /* A1 uses firmware_handshake_write_chunk_a1() instead.
         * Fall through to standard if reached unexpectedly. */
    case CRC_FMT_STANDARD:
    default:
        /* T31: [8-11]=offset [16-19]=length [20-23]=CRC [24-27]=OPS [28-31]=~CRC */
        handshake_cmd[8] = (chunk_offset >> 0) & 0xFF;
        handshake_cmd[9] = (chunk_offset >> 8) & 0xFF;
        handshake_cmd[10] = (chunk_offset >> 16) & 0xFF;
        handshake_cmd[11] = (chunk_offset >> 24) & 0xFF;
        handshake_cmd[16] = (data_size >> 0) & 0xFF;
        handshake_cmd[17] = (data_size >> 8) & 0xFF;
        handshake_cmd[18] = (data_size >> 16) & 0xFF;
        handshake_cmd[19] = (data_size >> 24) & 0xFF;
        handshake_cmd[20] = (crc >> 0) & 0xFF;
        handshake_cmd[21] = (crc >> 8) & 0xFF;
        handshake_cmd[22] = (crc >> 16) & 0xFF;
        handshake_cmd[23] = (crc >> 24) & 0xFF;
        handshake_cmd[24] = 0x00;
        handshake_cmd[25] = 0x00;
        handshake_cmd[26] = 0x06;
        handshake_cmd[27] = 0x00;
        {
            uint32_t crc_inv = ~crc;
            handshake_cmd[28] = (crc_inv >> 0) & 0xFF;
            handshake_cmd[29] = (crc_inv >> 8) & 0xFF;
            handshake_cmd[30] = (crc_inv >> 16) & 0xFF;
            handshake_cmd[31] = (crc_inv >> 24) & 0xFF;
        }
        break;
    }

    /* Bytes 32-39: platform trailer from profile */
    memcpy(&handshake_cmd[32], profile->trailer, 8);

    // Send handshake using VR_WRITE (0x12), as seen in vendor write capture
    // VR_READ/VR_FW_WRITE2 (0x13/0x14) are used for other initialization commands
    uint8_t handshake_cmd_code = VR_WRITE;

    DEBUG_PRINT("Sending write handshake with command 0x%02X...\n", handshake_cmd_code);

    // Debug: dump handshake bytes for analysis
    DEBUG_PRINT("Handshake bytes:");
    for (int i = 0; i < FW_HANDSHAKE_SIZE; i++) {
        if (i % 8 == 0) {
            DEBUG_PRINT("\n  ");
        }
        DEBUG_PRINT("%02X ", handshake_cmd[i]);
    }
    DEBUG_PRINT("\n");

    int response_len = 0;
    tdfu_error_t result = usb_device_vendor_request(device, REQUEST_TYPE_OUT, handshake_cmd_code, 0, 0,
                                                        handshake_cmd, FW_HANDSHAKE_SIZE, NULL, &response_len);

    if (result != TDFU_SUCCESS) {
        DEBUG_PRINT("Failed to send write handshake: %s\n", tdfu_error_to_string(result));
        return result;
    }

    platform_sleep_ms(10);

    // Send actual data via bulk-out
    DEBUG_PRINT("Sending %u bytes of data via bulk-out...\n", data_size);

    int transferred = 0;
    result = usb_device_bulk_transfer(device, ENDPOINT_OUT, (uint8_t *)data, data_size, &transferred, 6000);

    if (result != TDFU_SUCCESS) {
        DEBUG_PRINT("Bulk-out transfer failed: %s\n", tdfu_error_to_string(result));
        return result;
    }

    DEBUG_PRINT("Data sent: %d/%u bytes\n", transferred, data_size);

    // After bulk write, drain device response from bulk IN endpoint.
    // Vendor T20 capture shows reads from EP 0x81 (log) and EP 0x80
    // (status) between chunks. Without draining, T20 crashes on next WRITE.
    {
        uint8_t drain_buf[512];
        int drain_xfer = 0;
        // Read from EP 0x81 (bulk IN) - device log/ack
        usb_device_bulk_transfer(device, 0x81, drain_buf, sizeof(drain_buf), &drain_xfer, 200);
        DEBUG_PRINT("Post-chunk drain EP 0x81: %d bytes\n", drain_xfer);
    }
    platform_sleep_ms(50);

    // Some platforms need a VR_FW_READ (0x10) after each chunk.
    // On T31 this times out, so it's gated by the profile flag.
    if (profile->per_chunk_status_read) {
        DEBUG_PRINT("Sending per-chunk VR_FW_READ (0x10)...\n");

        // For T41/T41N, vendor traces show a 4-byte VR_FW_READ (0x10) after
        // each write chunk. We issue this directly via libusb to avoid the
        // generic usb_device_vendor_request() retry logic, which can turn a
        // simple timeout into a long sequence of retries.
        uint8_t status[4] = {0};
        int ctrl_result = libusb_control_transfer(device->handle, REQUEST_TYPE_VENDOR, VR_FW_READ, 0, 0, status,
                                                  sizeof(status), 1000);

        if (ctrl_result < 0) {
            DEBUG_PRINT("Warning: per-chunk VR_FW_READ for T41 failed: %s\n", libusb_error_name(ctrl_result));
            // Don't fail the operation here; the data chunk was already sent.
        } else {
            DEBUG_PRINT("Per-chunk VR_FW_READ status: len=%d, bytes=%02X %02X %02X %02X\n", ctrl_result, status[0],
                        status[1], status[2], status[3]);
        }
    }

    // Drain log traffic from bulk-IN endpoint 0x81
    // The vendor capture shows many bulk-IN transfers happen after status check
    DEBUG_PRINT("Draining logs from bulk-IN endpoint 0x81...\n");

    int total_drained = 0;
    // Limit to a small number of quick polls to avoid slowing down the write
    for (int i = 0; i < 16; i++) {
        uint8_t log_buf[FW_LOG_BUFFER_SIZE];
        int log_transferred = 0;

        int log_result = libusb_bulk_transfer(device->handle, ENDPOINT_IN, log_buf, sizeof(log_buf), &log_transferred,
                                              5); // 5ms timeout

        if (log_result == LIBUSB_ERROR_TIMEOUT || log_transferred == 0) {
            break;
        }

        if (log_result == 0 && log_transferred > 0) {
            total_drained += log_transferred;
        }
    }

    if (total_drained > 0) {
        DEBUG_PRINT("Drained %d bytes of logs\n", total_drained);
    }

    // Give device more time to finish processing chunk before next handshake
    // Tightened from 1000ms to 300ms to speed up full-image writes while
    // still giving firmware time to progress internal state.
    DEBUG_PRINT("Waiting 300ms for device to finish processing chunk...\n");
    platform_sleep_ms(300);

    return TDFU_SUCCESS;
}

/**
 * Firmware write with 40-byte handshake protocol for A1 boards.
 *
 * A1 uses a different handshake layout than T31/T41, with 1MB chunks and
 * a unique trailer. Pattern derived from a1_full_write_20251119_221121.pcap:
 *   Bytes  0-7 : zeros
 *   Bytes  8-11: Constant 0x00000600 (00 00 06 00)
 *   Bytes 12-15: Chunk offset in bytes (little-endian)
 *   Bytes 16-19: Chunk size 0x00100000 (00 00 10 00) = 1MB
 *   Bytes 20-23: ~CRC32(chunk_data) (little-endian)
 *   Bytes 24-31: zeros
 *   Bytes 32-39: A1 trailer (30 24 00 D4 02 75 00 00)
 */
tdfu_error_t firmware_handshake_write_chunk_a1(usb_device_t *device, uint32_t chunk_index, uint32_t chunk_offset,
                                                   const uint8_t *data, uint32_t data_size) {
    if (!device || !data || data_size == 0) {
        return TDFU_ERROR_INVALID_PARAMETER;
    }

    DEBUG_PRINT("FirmwareHandshakeWriteChunkA1: index=%u, offset=0x%08X, size=%u\n", chunk_index, chunk_offset,
                data_size);

    // Build 40-byte handshake command for write (A1-specific layout)
    // Pattern from a1_full_write_20251119_221121.pcap showing 1MB chunks:
    //   Bytes  0-7 : zeros
    //   Bytes  8-11: Constant 0x00000600 (00 00 06 00)
    //   Bytes 12-15: Chunk offset in bytes (little-endian)
    //   Bytes 16-19: Chunk size 0x00100000 (00 00 10 00) = 1MB
    //   Bytes 20-23: ~CRC32(chunk_data) (little-endian)
    //   Bytes 24-31: zeros
    //   Bytes 32-39: A1 trailer (30 24 00 D4 02 75 00 00)
    uint8_t handshake_cmd[FW_HANDSHAKE_SIZE] = {0};

    // Bytes 8-11: Constant pattern 0x00000600
    handshake_cmd[8] = 0x00;
    handshake_cmd[9] = 0x00;
    handshake_cmd[10] = 0x06;
    handshake_cmd[11] = 0x00;

    // Bytes 12-15: Chunk offset in bytes (little-endian)
    handshake_cmd[12] = (chunk_offset >> 0) & 0xFF;
    handshake_cmd[13] = (chunk_offset >> 8) & 0xFF;
    handshake_cmd[14] = (chunk_offset >> 16) & 0xFF;
    handshake_cmd[15] = (chunk_offset >> 24) & 0xFF;

    // Bytes 16-19: Chunk size in bytes (little-endian)
    // A1 uses 1MB (0x100000) chunks
    handshake_cmd[16] = (data_size >> 0) & 0xFF;
    handshake_cmd[17] = (data_size >> 8) & 0xFF;
    handshake_cmd[18] = (data_size >> 16) & 0xFF;
    handshake_cmd[19] = (data_size >> 24) & 0xFF;

    // Compute inverted CRC32 of chunk data
    uint32_t crc = firmware_crc32(data, data_size);
    uint32_t crc_inv = ~crc;

    // Bytes 20-23: Inverted CRC32 of chunk data (little-endian)
    handshake_cmd[20] = (crc_inv >> 0) & 0xFF;
    handshake_cmd[21] = (crc_inv >> 8) & 0xFF;
    handshake_cmd[22] = (crc_inv >> 16) & 0xFF;
    handshake_cmd[23] = (crc_inv >> 24) & 0xFF;

    // Bytes 24-31: zeros (already initialized)

    // Bytes 32-39: A1-specific trailer from vendor capture
    handshake_cmd[32] = 0x30;
    handshake_cmd[33] = 0x24;
    handshake_cmd[34] = 0x00;
    handshake_cmd[35] = 0xD4;
    handshake_cmd[36] = 0x02;
    handshake_cmd[37] = 0x75;
    handshake_cmd[38] = 0x00;
    handshake_cmd[39] = 0x00;

    // Send handshake using VR_WRITE (0x12)
    uint8_t handshake_cmd_code = VR_WRITE;

    DEBUG_PRINT("Sending A1 write handshake with command 0x%02X...\n", handshake_cmd_code);

    // Debug: dump handshake bytes for analysis
    DEBUG_PRINT("A1 Handshake bytes:");
    for (int i = 0; i < FW_HANDSHAKE_SIZE; i++) {
        if (i % 8 == 0) {
            DEBUG_PRINT("\n  ");
        }
        DEBUG_PRINT("%02X ", handshake_cmd[i]);
    }
    DEBUG_PRINT("\n");

    int response_len = 0;
    tdfu_error_t result = usb_device_vendor_request(device, REQUEST_TYPE_OUT, handshake_cmd_code, 0, 0,
                                                        handshake_cmd, FW_HANDSHAKE_SIZE, NULL, &response_len);

    if (result != TDFU_SUCCESS) {
        DEBUG_PRINT("Failed to send A1 write handshake: %s\n", tdfu_error_to_string(result));
        return result;
    }

    platform_sleep_ms(10);

    // Send actual data via bulk-out (interface claimed by writer)
    DEBUG_PRINT("[A1] Sending %u bytes of data via bulk-out...\n", data_size);

    int transferred = 0;
    result = usb_device_bulk_transfer(device, ENDPOINT_OUT, (uint8_t *)data, data_size, &transferred, 6000);

    if (result != TDFU_SUCCESS) {
        DEBUG_PRINT("[A1] Bulk-out transfer failed: %s\n", tdfu_error_to_string(result));
        return result;
    }

    DEBUG_PRINT("[A1] Data sent: %d/%u bytes\n", transferred, data_size);

    // Give device time to start and finish processing the chunk.
    DEBUG_PRINT("[A1] Waiting 300ms for device to process chunk...\n");
    platform_sleep_ms(300);

    return TDFU_SUCCESS;
}

/**
 * Firmware write with vendor-format 40-byte handshake (T40/T41).
 *
 * Matches the vendor host tool layout, verified against T40 vendor
 * USB capture (t40_vendor_write.pcap):
 *   [0-7]   uint64_t partition = 0  (full image)
 *   [8-15]  uint64_t offset = chunk flash offset
 *   [16-23] uint64_t size = chunk size
 *   [24-27] uint32_t ops = OPS(SPI,RAW) = 0x00060000
 *   [28-31] uint32_t crc = CRC32 (raw, no final XOR)
 *   [32-39] ignored by device
 */
tdfu_error_t firmware_handshake_write_chunk_vendor(usb_device_t *device, uint32_t chunk_index,
                                                       uint32_t chunk_offset, const uint8_t *data, uint32_t data_size) {
    if (!device || !data || data_size == 0) {
        return TDFU_ERROR_INVALID_PARAMETER;
    }

    DEBUG_PRINT("FirmwareHandshakeWriteChunkVendor: index=%u, offset=0x%08X, size=%u\n", chunk_index, chunk_offset,
                data_size);

    uint8_t handshake_cmd[FW_HANDSHAKE_SIZE] = {0};

    /* Compute CRC32 without final XOR (raw vendor format) */
    uint32_t crc = firmware_crc32(data, data_size);
    uint32_t raw_crc = ~crc; /* undo final XOR to get raw CRC */

    /* [0-7] partition = 0 (already zeroed) */

    /* [8-15] offset as uint64_t LE (upper 32 = 0 for <4GB) */
    handshake_cmd[8] = (chunk_offset >> 0) & 0xFF;
    handshake_cmd[9] = (chunk_offset >> 8) & 0xFF;
    handshake_cmd[10] = (chunk_offset >> 16) & 0xFF;
    handshake_cmd[11] = (chunk_offset >> 24) & 0xFF;
    /* [12-15] = 0 (upper 32, already zeroed) */

    /* [16-23] size as uint64_t LE */
    handshake_cmd[16] = (data_size >> 0) & 0xFF;
    handshake_cmd[17] = (data_size >> 8) & 0xFF;
    handshake_cmd[18] = (data_size >> 16) & 0xFF;
    handshake_cmd[19] = (data_size >> 24) & 0xFF;
    /* [20-23] = 0 (upper 32, already zeroed) */

    /* [24-27] ops = OPS(SPI, RAW) = (6 << 16) | 0 = 0x00060000 */
    handshake_cmd[24] = 0x00;
    handshake_cmd[25] = 0x00;
    handshake_cmd[26] = 0x06;
    handshake_cmd[27] = 0x00;

    /* [28-31] CRC32 raw (no final XOR) */
    handshake_cmd[28] = (raw_crc >> 0) & 0xFF;
    handshake_cmd[29] = (raw_crc >> 8) & 0xFF;
    handshake_cmd[30] = (raw_crc >> 16) & 0xFF;
    handshake_cmd[31] = (raw_crc >> 24) & 0xFF;

    /* [32-39] padding, zeroed (vendor has stack residue here, device ignores) */

    DEBUG_PRINT("Vendor handshake bytes:");
    for (int i = 0; i < FW_HANDSHAKE_SIZE; i++) {
        if (i % 8 == 0)
            DEBUG_PRINT("\n  ");
        DEBUG_PRINT("%02X ", handshake_cmd[i]);
    }
    DEBUG_PRINT("\n");

    /* Send via VR_WRITE (0x12) with vendor timeout (200s) */
    int response_len = 0;
    tdfu_error_t result = usb_device_vendor_request(device, REQUEST_TYPE_OUT, VR_WRITE, 0, 0, handshake_cmd,
                                                        FW_HANDSHAKE_SIZE, NULL, &response_len);

    if (result != TDFU_SUCCESS) {
        DEBUG_PRINT("Failed to send vendor write handshake: %s\n", tdfu_error_to_string(result));
        return result;
    }

    platform_sleep_ms(10);

    /* Bulk OUT: send chunk data */
    DEBUG_PRINT("[Vendor] Sending %u bytes via bulk-out...\n", data_size);

    int transferred = 0;
    result = usb_device_bulk_transfer(device, ENDPOINT_OUT, (uint8_t *)data, data_size, &transferred, 20000);

    if (result != TDFU_SUCCESS) {
        DEBUG_PRINT("[Vendor] Bulk-out failed: %s\n", tdfu_error_to_string(result));
        return result;
    }

    DEBUG_PRINT("[Vendor] Data sent: %d/%u bytes\n", transferred, data_size);

    /* Brief settle time */
    platform_sleep_ms(50);

    return TDFU_SUCCESS;
}

/**
 * Initialize firmware stage with handshake protocol.
 *
 * Vendor flow (stage2_init behavior from USB capture analysis):
 *   1. Send VR_FW_HANDSHAKE (0x11) with up to 5 retries
 *   2. Poll VR_FW_READ (0x10) for ACK with up to 50s timeout
 * The handshake triggers tdfu_init() on the device which probes
 * the SFC flash and performs chip erase. The ACK poll waits for
 * erase completion before proceeding to write chunks.
 */
tdfu_error_t firmware_handshake_init(usb_device_t *device) {
    if (!device) {
        return TDFU_ERROR_INVALID_PARAMETER;
    }

    DEBUG_PRINT("Initializing firmware handshake protocol...\n");

    /* Send VR_FW_HANDSHAKE (0x11) with retries (vendor does up to 5) */
    tdfu_error_t result = TDFU_ERROR_TIMEOUT;
    for (int retry = 0; retry < 5; retry++) {
        result = protocol_fw_handshake(device);
        if (result == TDFU_SUCCESS)
            break;
        DEBUG_PRINT("FW handshake retry %d/5: %s\n", retry + 1, tdfu_error_to_string(result));
        platform_sleep_ms(100);
    }
    if (result != TDFU_SUCCESS) {
        DEBUG_PRINT("Firmware handshake failed after retries: %s\n", tdfu_error_to_string(result));
        return result;
    }

    /* Poll VR_FW_READ (0x10) for ACK — waits for erase completion.
     * Only needed for xburst2/vendor platforms (T40/T41/A1) where the
     * VR_FW_HANDSHAKE triggers a long chip erase. On xburst1 (T31 etc.),
     * polling VR_FW_READ during erase interferes with the SFC controller
     * and causes erase failure. Those platforms use status polling in
     * writer.c instead. */
    const platform_profile_t *profile = platform_get_profile(device->info.variant);
    if (profile->crc_format == CRC_FMT_VENDOR) {
        /* T40/T41 vendor format: poll ACK to wait for erase.
         * A1 cannot be polled during erase (ep0-control timeout). */
        /* Try ACK polling first. If the first poll times out, the device
         * doesn't support polling during erase (T41) — fall back to a
         * fixed 60s silent wait like A1. T40 responds to polls. */
        LOG_INFO("Waiting for device initialization (erase)...\n");
        int ack_timeout_ms = 120000;
        int elapsed_ms = 0;
        int32_t ack_val = -110;
        bool polling_works = false;

        while (elapsed_ms < ack_timeout_ms) {
            uint8_t ack_buf[4] = {0};
            int rc = libusb_control_transfer(device->handle, REQUEST_TYPE_VENDOR, VR_FW_READ, 0, 0, ack_buf,
                                             sizeof(ack_buf), 5000);

            if (rc == 4) {
                polling_works = true;
                ack_val = (int32_t)(ack_buf[0] | (ack_buf[1] << 8) | (ack_buf[2] << 16) | (ack_buf[3] << 24));
                if (ack_val != -110) {
                    LOG_INFO("Device ready (ack=%d, %dms)\n", ack_val, elapsed_ms);
                    break;
                }
            } else if (!polling_works && elapsed_ms > 0) {
                /* First two polls both timed out — device can't be polled.
                 * Fall back to silent wait. */
                LOG_INFO("Device does not respond to ACK poll, waiting silently...\n");
                int remaining_ms = 60000 - elapsed_ms;
                if (remaining_ms > 0) {
                    for (int s = 0; s < remaining_ms / 1000; s++) {
                        LOG_INFO("\r  Erase progress: %d/%d seconds...", (elapsed_ms / 1000) + s + 1, 60);
                        fflush(stdout);
                        platform_sleep_ms(1000);
                    }
                    LOG_INFO("\n");
                }
                break;
            }
            DEBUG_PRINT("Init ACK poll: rc=%d val=%d elapsed=%dms\n", rc, ack_val, elapsed_ms);
            platform_sleep_ms(1000);
            elapsed_ms += 1000 + 5000; /* 1s sleep + 5s USB transfer timeout */
        }

        if (ack_val == -110 && polling_works) {
            LOG_WARN("Init ACK timeout after %dms (proceeding anyway)\n", elapsed_ms);
        }
    } else {
        /* xburst1 (T31 etc.): just wait briefly, erase polling happens later */
        platform_sleep_ms(100);
    }

    return TDFU_SUCCESS;
}