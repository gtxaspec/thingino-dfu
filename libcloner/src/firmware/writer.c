/**
 * Firmware Writer Implementation
 *
 * Based on USB capture analysis of vendor cloner write operation.
 *
 * Write sequence discovered:
 * 1. DDR configuration (324 bytes)
 * 2. SPL bootloader (~10KB)
 * 3. U-Boot bootloader (~240KB)
 * 4. Partition marker ("ILOP", 172 bytes)
 * 5. Metadata (972 bytes)
 * 6. Firmware data in 128KB chunks
 */

#include "thingino.h"
#include "platform.h"
#include "cloner/constants.h"
#include "cloner/platform_profile.h"
#include <unistd.h>
#include <string.h>

// Wait for NOR erase to complete in firmware stage using VR_FW_READ_STATUS2.
//
// The vendor T31x write flow issues status checks (0x16/0x19/0x25/0x26) while
// the burner is erasing and programming. Here we implement a conservative
// polling loop around VR_FW_READ_STATUS2 (0x19) to avoid starting the first
// VR_WRITE/firmware chunk while a full-chip erase is still in progress.
//
// Strategy:
//   - Always wait at least min_wait_ms (default 5s) to give the chip time to
//     begin/finish erase.
//   - Poll VR_FW_READ_STATUS2 every 500ms and log the raw 32-bit status value.
//   - After the minimum wait, proceed once the status has been stable for a
//     few polls or when max_wait_ms is reached.
//   - Any protocol errors are treated as "device busy"; we keep waiting up to
//     max_wait_ms but do not fail the write purely due to status polling.
//
// This mirrors the vendor behavior ("wait on status before writes") without
// depending on undocumented status bit semantics.
static void firmware_wait_for_erase_ready(usb_device_t *device, int min_wait_ms, int max_wait_ms) {
    const int poll_interval_ms = 500; // 0.5s between polls

    if (!device) {
        return;
    }

    // Only do firmware-stage polling for variants that use it.
    // Platforms with ERASE_WAIT_FIXED never reach this function.
    if (device->info.stage != STAGE_FIRMWARE) {
        platform_sleep_ms(min_wait_ms);
        return;
    }

    if (min_wait_ms < 0)
        min_wait_ms = 0;
    if (max_wait_ms < min_wait_ms)
        max_wait_ms = min_wait_ms;

    LOG_INFO("Waiting for device to prepare flash (erase) using status polling...\n");

    int elapsed_ms = 0;
    uint32_t last_status = 0;
    int stable_count = 0;
    int have_status = 0;

    while (elapsed_ms < max_wait_ms) {
        uint32_t status = 0;
        thingino_error_t st = protocol_fw_read_status(device, VR_FW_READ_STATUS2, &status);

        if (st == THINGINO_SUCCESS) {
            DEBUG_PRINT("Erase status (VR_FW_READ_STATUS2) at %d ms: 0x%08X\n", elapsed_ms, status);

            if (elapsed_ms >= min_wait_ms) {
                if (!have_status) {
                    have_status = 1;
                    last_status = status;
                    stable_count = 1;
                } else if (status == last_status) {
                    stable_count++;
                } else {
                    // Status changed after minimum wait; assume erase state
                    // transitioned (e.g., from busy to ready).
                    DEBUG_PRINT("Erase status changed from 0x%08X to 0x%08X at %d ms; "
                                "assuming erase complete\n",
                                last_status, status, elapsed_ms);
                    break;
                }

                // If we've seen the same status value a few times after the
                // minimum wait, treat the device as ready.
                if (stable_count >= 3) {
                    DEBUG_PRINT("Erase status stable at 0x%08X for %d polls after %d ms; "
                                "proceeding with write\n",
                                status, stable_count, elapsed_ms);
                    break;
                }
            }
        } else {
            DEBUG_PRINT("Erase status poll error at %d ms: %s\n", elapsed_ms, thingino_error_to_string(st));
        }

        platform_sleep_ms(poll_interval_ms);
        elapsed_ms += poll_interval_ms;
    }

    if (elapsed_ms >= max_wait_ms) {
        LOG_INFO("[WARN] Timed out waiting for firmware erase status after %d ms; "
                 "continuing with write anyway.\n",
                 elapsed_ms);
    }
}

/**
 * Write firmware to device
 *
 * This implements the complete write sequence as observed from vendor cloner:
 * - Bootstrap device (DDR + SPL + U-Boot)
 * - Send partition marker
 * - Send metadata
 * - Send firmware in 128KB chunks (T31x) or 1MB chunks (A1)
 */
thingino_error_t write_firmware_to_device(usb_device_t *device, const char *firmware_file,
                                          const firmware_binary_t *fw_binary, bool no_erase, bool is_a1_board,
                                          uint32_t chunk_size_arg) {

    if (!device || !firmware_file) {
        return THINGINO_ERROR_INVALID_PARAMETER;
    }

    (void)no_erase; // Erase control is in the flash descriptor, not the writer

    LOG_INFO("Writing firmware to device...\n");
    LOG_INFO("  Firmware file: %s\n", firmware_file);
    if (fw_binary) {
        LOG_INFO("  SoC: %s\n", fw_binary->processor);
    }

    (void)is_a1_board; // Superseded by platform profile

    // Step 1: Load firmware file
    FILE *file = fopen(firmware_file, "rb");
    if (!file) {
        LOG_ERROR("Cannot open firmware file: %s\n", firmware_file);
        return THINGINO_ERROR_FILE_IO;
    }

    // Get file size
    fseek(file, 0, SEEK_END);
    long firmware_size = ftell(file);
    fseek(file, 0, SEEK_SET);

    if (firmware_size <= 0) {
        LOG_ERROR("Invalid firmware file size\n");
        fclose(file);
        return THINGINO_ERROR_FILE_IO;
    }
    if ((unsigned long)firmware_size > (unsigned long)UINT32_MAX) {
        LOG_ERROR("Firmware file too large (%ld bytes)\n", firmware_size);
        fclose(file);
        return THINGINO_ERROR_INVALID_PARAMETER;
    }

    uint32_t firmware_size_u = (uint32_t)firmware_size;
    LOG_INFO("  Firmware size: %u bytes (%.1f KB)\n", firmware_size_u, firmware_size_u / 1024.0);

    // Allocate buffer for firmware
    uint8_t *firmware_data = (uint8_t *)malloc(firmware_size_u);
    if (!firmware_data) {
        LOG_ERROR("Cannot allocate memory for firmware\n");
        fclose(file);
        return THINGINO_ERROR_MEMORY;
    }

    // Read firmware
    size_t bytes_read = fread(firmware_data, 1, firmware_size_u, file);
    fclose(file);

    if (bytes_read != (size_t)firmware_size_u) {
        LOG_ERROR("Failed to read firmware file\n");
        free(firmware_data);
        return THINGINO_ERROR_FILE_IO;
    }

    // Step 2: Prepare flash address and length for firmware write
    thingino_error_t result;
    const platform_profile_t *profile = platform_get_profile(device->info.variant);

    LOG_INFO("\nStep 1: Preparing firmware write (address/length)...\n");

    uint32_t flash_base_address = 0x00008010;

    if (!profile->skip_set_data_addr) {
        DEBUG_PRINT("Setting flash base address with SetDataAddress: 0x%08lX\n", (unsigned long)flash_base_address);
        int addr_resp_len = 0;
        result = usb_device_vendor_request(device, REQUEST_TYPE_OUT, VR_SET_DATA_ADDR,
                                           (uint16_t)(flash_base_address & 0xFFFF), 0, NULL, 0, NULL, &addr_resp_len);
        if (result != THINGINO_SUCCESS) {
            LOG_ERROR("Failed to set flash base address: %s\n", thingino_error_to_string(result));
            free(firmware_data);
            return result;
        }
    }

    // Wait for erase to complete.
    if (profile->skip_set_data_addr && profile->crc_format == CRC_FMT_A1) {
        /* A1 only: descriptor triggered chip erase. Wait silently —
         * do NOT send USB transfers during erase or the burner
         * reports ep0-control timeout. */
        int secs = 60;
        LOG_INFO("Waiting for chip erase (~%d seconds)...\n", secs);
        for (int i = 0; i < secs; i++) {
            LOG_INFO("\r  Erase progress: %d/%d seconds...", i + 1, secs);
            fflush(stderr);
            platform_sleep_ms(1000);
        }
        LOG_INFO("\n");
    } else if (profile->erase_wait == ERASE_WAIT_FIXED) {
        int secs = profile->erase_delay_seconds;
        LOG_INFO("Waiting for %s flash erase (~%d seconds)...\n", profile->name, secs);
        for (int i = 0; i < secs; i++) {
            LOG_INFO("\r  Erase progress: %d/%d seconds...", i + 1, secs);
            fflush(stderr);
            platform_sleep_ms(1000);
        }
        LOG_INFO("\n");
    } else {
        firmware_wait_for_erase_ready(device, 5000, 60000);
    }

    // Set data length after erase wait, before first chunk
    {
        uint32_t set_length = profile->set_data_len_per_chunk ? profile->default_chunk_size : (uint32_t)firmware_size;

        DEBUG_PRINT("Setting firmware write length with SetDataLength: %lu bytes\n", (unsigned long)set_length);
        result = protocol_set_data_length(device, set_length);
        if (result != THINGINO_SUCCESS) {
            LOG_ERROR("Failed to set firmware write length: %s\n", thingino_error_to_string(result));
            free(firmware_data);
            return result;
        }
    }

    // Claim interface for the entire write operation
    {
        int claim_rc = libusb_claim_interface(device->handle, 0);
        if (claim_rc != 0) {
            LOG_INFO("Interface claim failed: %s, trying detach+claim\n", libusb_error_name(claim_rc));
            libusb_detach_kernel_driver(device->handle, 0);
            claim_rc = libusb_claim_interface(device->handle, 0);
            LOG_INFO("After detach: claim %s\n", claim_rc == 0 ? "OK" : libusb_error_name(claim_rc));
        } else {
            LOG_INFO("Interface 0 claimed OK\n");
        }
    }

    // Step 3: Send firmware with variant-specific protocol
    LOG_INFO("\nStep 2: Writing firmware data...\n");

    uint32_t bytes_written = 0;
    uint32_t chunk_num = 0;
    result = THINGINO_SUCCESS;

    while (bytes_written < (uint32_t)firmware_size) {
        uint32_t chunk_size = chunk_size_arg ? chunk_size_arg : profile->default_chunk_size;
        if (bytes_written + chunk_size > (uint32_t)firmware_size) {
            chunk_size = (uint32_t)firmware_size - bytes_written;
        }

        chunk_num++;
        uint32_t chunk_offset = bytes_written;
        uint32_t current_flash_addr = flash_base_address + chunk_offset;

        LOG_INFO("  Chunk %u: Writing %u bytes at 0x%08X (%.1f%%)...\n", chunk_num, chunk_size, current_flash_addr,
                 (bytes_written + chunk_size) * 100.0 / firmware_size);

        // Send chunk. Vendor T21 capture shows GET_ACK check + retry loop:
        // the burner's CRC check is non-deterministic on some platforms,
        // so retries eventually succeed.
        int max_attempts = 3;
        for (int attempt = 0; attempt < max_attempts; attempt++) {
            if (profile->crc_format == CRC_FMT_VENDOR) {
                result = firmware_handshake_write_chunk_vendor(device, chunk_num - 1, chunk_offset,
                                                               firmware_data + bytes_written, chunk_size);
            } else if (profile->use_a1_handshake) {
                result = firmware_handshake_write_chunk_a1(device, chunk_num - 1, chunk_offset,
                                                           firmware_data + bytes_written, chunk_size);
            } else {
                result = firmware_handshake_write_chunk(device, chunk_num - 1, chunk_offset,
                                                        firmware_data + bytes_written, chunk_size);
            }
            if (result != THINGINO_SUCCESS)
                break;

            // Check GET_ACK - vendor does this after each chunk.
            // Vendor uses VR_FW_READ (0x10) with up to 50s timeout
            // and backoff polling for pending (-110) responses.
            {
                uint8_t ack_buf[4] = {0};
                int32_t ack_val = -110; /* pending sentinel */
                int ack_timeout_ms = (profile->crc_format == CRC_FMT_VENDOR) ? 50000 : 5000;
                int elapsed_ms = 0;
                int poll_interval_ms = 500;

                while (elapsed_ms < ack_timeout_ms) {
                    memset(ack_buf, 0, sizeof(ack_buf));
                    int rc = libusb_control_transfer(device->handle, REQUEST_TYPE_VENDOR, VR_FW_READ, 0, 0, ack_buf,
                                                     sizeof(ack_buf), 5000);
                    if (rc == 4) {
                        ack_val = (int32_t)(ack_buf[0] | (ack_buf[1] << 8) | (ack_buf[2] << 16) | (ack_buf[3] << 24));
                        if (ack_val != -110)
                            break; /* got real response */
                    }
                    DEBUG_PRINT("  ACK poll: rc=%d val=%d elapsed=%dms\n", rc, ack_val, elapsed_ms);
                    platform_sleep_ms(poll_interval_ms);
                    elapsed_ms += poll_interval_ms + 5000; /* poll + timeout */
                }

                if (ack_val == 0) {
                    break; /* success */
                }
                if (ack_val == -110) {
                    DEBUG_PRINT("  Chunk %u: ACK timeout after %dms\n", chunk_num, elapsed_ms);
                }
                if (attempt < max_attempts - 1) {
                    DEBUG_PRINT("  Chunk %u: ack=%d, retrying (%d/%d)...\n", chunk_num, ack_val, attempt + 1,
                                max_attempts);
                }
            }
        }
        if (result != THINGINO_SUCCESS) {
            LOG_ERROR("Failed to write chunk %u\n", chunk_num);
            libusb_release_interface(device->handle, 0);
            free(firmware_data);
            return result;
        }

        bytes_written += chunk_size;
    }

    // Release interface after write loop
    libusb_release_interface(device->handle, 0);

    // Flush cache after all writes
    LOG_INFO("\nFlushing cache...\n");
    result = protocol_flush_cache(device);
    if (result != THINGINO_SUCCESS) {
        LOG_WARN("Failed to flush cache\n");
        // Don't fail on flush error
    }

    LOG_INFO("\nFirmware write complete!\n");
    LOG_INFO("  Total written: %u bytes in %u chunks\n", bytes_written, chunk_num);

    free(firmware_data);
    return THINGINO_SUCCESS;
}

/**
 * Send bulk data to device
 */
thingino_error_t send_bulk_data(usb_device_t *device, uint8_t endpoint, const uint8_t *data, uint32_t size) {
    if (!device || !data || size == 0) {
        return THINGINO_ERROR_INVALID_PARAMETER;
    }

    int transferred = 0;
    int result =
        libusb_bulk_transfer(device->handle, endpoint, (uint8_t *)data, size, &transferred, 5000); // 5 second timeout

    if (result != LIBUSB_SUCCESS) {
        LOG_ERROR("Bulk transfer failed: %s\n", libusb_error_name(result));
        return THINGINO_ERROR_TRANSFER_FAILED;
    }

    if (transferred != (int)size) {
        LOG_ERROR("Incomplete transfer: sent %d of %u bytes\n", transferred, size);
        return THINGINO_ERROR_TRANSFER_FAILED;
    }

    return THINGINO_SUCCESS;
}
