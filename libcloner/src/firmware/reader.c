#include "thingino.h"
#include "cloner/constants.h"
#include "platform.h"

// ============================================================================
// FIRMWARE READER - chunk loop only
//
// The descriptor setup, JEDEC detection, and handshake init are done by
// the CLI (main.c) before calling firmware_read_full(). This module only
// performs the 128KB chunk read loop using firmware_handshake_read_chunk().
// ============================================================================

#define READ_CHUNK_SIZE (1024 * 1024) /* 1MB default for reads */

/**
 * Read full firmware from device.
 *
 * Caller is responsible for:
 *   1. Sending partition marker (ILOP)
 *   2. Reading JEDEC ID and looking up flash chip
 *   3. Generating and sending the READ descriptor (no erase)
 *   4. Calling firmware_handshake_init()
 *
 * This function only does the chunk loop:
 *   For each 128KB chunk: VR_READ (0x13) control + Bulk IN
 *
 * @param device      USB device handle (must be in firmware stage)
 * @param read_size   Total bytes to read (e.g. chip->size)
 * @param data        Output: allocated buffer with firmware data
 * @param actual_size Output: actual bytes read
 */
tdfu_error_t firmware_read_full(usb_device_t *device, uint32_t read_size, uint8_t **data, uint32_t *actual_size) {
    if (!device || !data || !actual_size || read_size == 0) {
        return TDFU_ERROR_INVALID_PARAMETER;
    }

    uint32_t chunk_size = READ_CHUNK_SIZE; /* 1MB for all platforms */

    LOG_INFO("Reading %u bytes (%.2f MB) in %uKB chunks...\n", read_size, (float)read_size / (1024 * 1024),
             chunk_size / 1024);

    uint8_t *firmware_buffer = (uint8_t *)malloc(read_size);
    if (!firmware_buffer) {
        LOG_ERROR("Failed to allocate %u bytes for firmware buffer\n", read_size);
        return TDFU_ERROR_MEMORY;
    }

    uint32_t total_read = 0;
    uint32_t chunk_count = (read_size + chunk_size - 1) / chunk_size;

    for (uint32_t i = 0; i < chunk_count; i++) {
        uint32_t offset = i * chunk_size;
        uint32_t this_chunk = chunk_size;
        if (offset + this_chunk > read_size) {
            this_chunk = read_size - offset;
        }

        uint8_t *chunk_data = NULL;
        int chunk_len = 0;

        tdfu_error_t result =
            firmware_handshake_read_chunk(device, i, offset, this_chunk, read_size, &chunk_data, &chunk_len);

        if (result != TDFU_SUCCESS) {
            LOG_ERROR("Read chunk %u/%u failed at offset 0x%08X: %s\n", i + 1, chunk_count, offset,
                      tdfu_error_to_string(result));
            free(firmware_buffer);
            return result;
        }

        if (chunk_data && chunk_len > 0) {
            uint32_t copy_len = (uint32_t)chunk_len;
            if (copy_len > this_chunk)
                copy_len = this_chunk;
            memcpy(firmware_buffer + offset, chunk_data, copy_len);
            total_read += copy_len;
            free(chunk_data);
        }

        int pct = (int)((uint64_t)(i + 1) * 100 / chunk_count);
        LOG_INFO("\r  Read progress: %u/%u chunks (%d%%), %.2f MB read", i + 1, chunk_count, pct,
                 (float)total_read / (1024 * 1024));
        fflush(stderr);
    }

    LOG_INFO("\n");
    LOG_INFO("Read complete: %u bytes\n", total_read);

    *data = firmware_buffer;
    *actual_size = total_read;

    return TDFU_SUCCESS;
}
