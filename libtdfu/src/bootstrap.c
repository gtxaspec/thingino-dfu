#include "tdfu/tdfu.h"
#include "tdfu/constants.h"
#include "platform.h"
#include <stdlib.h>
#include <string.h>

/*
 * XBurst1 gen-1 mask ROMs (T10/T20/T21/T30) stage the first-stage image into
 * cache-as-RAM and prime the I-cache with a fill whose extent is the data
 * length we hand them. A length that is not a multiple of the 32-byte cache
 * line leaves that fill mis-bounded and silently corrupts the cached image,
 * so the loaded SPL hangs before its first instruction (deterministic; it
 * looks like a bad SPL but is purely a size quirk). Round every cache-as-RAM
 * load up to a cache line and zero-pad: the padding lands after the image in
 * unused cache-as-RAM and is never executed, and it is inert on every other
 * SoC, so it is applied unconditionally.
 */
#define TDFU_STAGE1_ALIGN 32u

// ============================================================================
// BOOTSTRAP IMPLEMENTATION
// ============================================================================

tdfu_error_t bootstrap_load_data_to_memory(usb_device_t *device, const uint8_t *data, size_t size,
                                               uint32_t address) {

    if (!device || !data || size == 0) {
        return TDFU_ERROR_INVALID_PARAMETER;
    }

    // Round the stage1 length up to a cache line (see TDFU_STAGE1_ALIGN) and
    // zero-pad, so the mask ROM's cache-as-RAM I-cache fill stays in bounds.
    const uint8_t *xfer = data;
    size_t xfer_size = size;
    uint8_t *padbuf = NULL;
    if (size & (TDFU_STAGE1_ALIGN - 1u)) {
        xfer_size = (size + TDFU_STAGE1_ALIGN - 1u) & ~(size_t)(TDFU_STAGE1_ALIGN - 1u);
        padbuf = malloc(xfer_size);
        if (padbuf) {
            memcpy(padbuf, data, size);
            memset(padbuf + size, 0, xfer_size - size);
            xfer = padbuf;
        } else {
            xfer_size = size; // OOM: fall back to the unpadded transfer
        }
    }

    // Step 1: Set target address
    DEBUG_PRINT("Setting data address to 0x%08x\n", address);
    tdfu_error_t result = protocol_set_data_address(device, address);

    // Step 2: Set data length (cache-line aligned)
    if (result == TDFU_SUCCESS) {
        DEBUG_PRINT("Setting data length to %zu bytes (from %zu)\n", xfer_size, size);
        result = protocol_set_data_length(device, (uint32_t)xfer_size);
    }

    // Step 3: Transfer data
    if (result == TDFU_SUCCESS) {
        DEBUG_PRINT("Transferring data (%zu bytes)...\n", xfer_size);
        result = bootstrap_transfer_data(device, xfer, xfer_size);
    }

    free(padbuf);
    return result;
}

tdfu_error_t bootstrap_transfer_data(usb_device_t *device, const uint8_t *data, size_t size) {

    if (!device || !data || size == 0) {
        return TDFU_ERROR_INVALID_PARAMETER;
    }

    DEBUG_PRINT("TransferData starting: %zu bytes total\n", size);

    // Claim interface before bulk transfers (required on Linux)
    tdfu_error_t claim_result = usb_device_claim_interface(device);
    if (claim_result != TDFU_SUCCESS) {
        DEBUG_PRINT("TransferData: failed to claim interface, trying with kernel driver detach\n");
        libusb_detach_kernel_driver(device->handle, 0);
        claim_result = usb_device_claim_interface(device);
        if (claim_result != TDFU_SUCCESS) {
            DEBUG_PRINT("TransferData: still failed to claim interface: %s\n", tdfu_error_to_string(claim_result));
            return claim_result;
        }
    }

    // Chunk size for bulk transfers. Large single transfers can fail on some
    // bootrom USB stacks, so cap each bulk-OUT at 64 KiB.
    size_t chunk_size = CHUNK_SIZE_64KB;
    size_t total_written = 0;
    size_t offset = 0;
    int max_retries = 3;

    while (offset < size) {
        // Determine chunk size for this iteration
        size_t remaining = size - offset;
        size_t current_chunk_size = chunk_size;
        if (remaining < current_chunk_size) {
            current_chunk_size = remaining;
        }

        DEBUG_PRINT("TransferData chunk: offset=%zu, size=%zu, remaining=%zu\n", offset, current_chunk_size, remaining);

        // Try to write this chunk with retries
        bool chunk_written = false;
        for (int retry = 0; retry < max_retries && !chunk_written; retry++) {
            int transferred;
            // Calculate timeout: 5s base + 1s per 64KB, max 30s
            int timeout = 5000 + ((int)current_chunk_size / 65536) * 1000;
            if (timeout > 30000)
                timeout = 30000;
            if (timeout < 5000)
                timeout = 5000; // Minimum 5 seconds

            tdfu_error_t result = usb_device_bulk_transfer(device, ENDPOINT_OUT, (uint8_t *)&data[offset],
                                                               (int)current_chunk_size, &transferred, timeout);

            if (result == TDFU_SUCCESS && transferred > 0) {
                // Success - at least some bytes written
                DEBUG_PRINT("TransferData chunk written: %d bytes (attempt %d)\n", transferred, retry + 1);
                total_written += transferred;
                offset += transferred;

                // If we wrote the expected amount, move to next chunk
                if (transferred == (int)current_chunk_size) {
                    chunk_written = true;
                    break;
                } else if (transferred < (int)current_chunk_size) {
                    // Partial write - adjust chunk size and retry
                    current_chunk_size -= transferred;
                    DEBUG_PRINT("Partial write, retrying remaining %zu bytes\n", current_chunk_size);
                    continue;
                }
            }

            // Handle write error
            if (result != TDFU_SUCCESS) {
                DEBUG_PRINT("TransferData error on attempt %d: %s\n", retry + 1, tdfu_error_to_string(result));

                // For other errors or if retry limit reached
                if (retry < max_retries - 1) {
                    DEBUG_PRINT("Retrying write after brief delay (attempt %d/%d)\n", retry + 2, max_retries);

                    // Platform-specific sleep
                    platform_sleep_ms(INTER_CHUNK_DELAY_MS);
                    continue;
                }

                // Out of retries - this is a real failure
                usb_device_release_interface(device);
                return result;
            }

            // No error but no bytes written - shouldn't happen
            if (retry == max_retries - 1) {
                DEBUG_PRINT("Bulk write returned 0 bytes and no error at offset %zu\n", offset);
                usb_device_release_interface(device);
                return TDFU_ERROR_TRANSFER_FAILED;
            }
        }

        // Small delay between chunks for large transfers to prevent overwhelming device
        if (size > 100 * 1024 && offset < size) {
            // Platform-specific sleep
            platform_sleep_ms(10);
        }
    }

    /* Release interface after bulk transfer (matches old working binary behavior).
     * This is critical for T20 - without release, subsequent control transfers
     * (FlushCache, ProgStage2) timeout. */
    usb_device_release_interface(device);

    DEBUG_PRINT("TransferData complete: %zu bytes written successfully\n", total_written);

    return TDFU_SUCCESS;
}