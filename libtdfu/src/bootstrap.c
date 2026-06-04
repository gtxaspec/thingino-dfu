#include "tdfu/tdfu.h"
#include "ddr_config_database.h"
#include "platform.h"
#include "tdfu/constants.h"
#include "tdfu/platform_profile.h"

// ============================================================================
// BOOTSTRAP IMPLEMENTATION
// ============================================================================

tdfu_error_t bootstrap_device(usb_device_t *device, const bootstrap_config_t *config) {
    if (!device || !config) {
        return TDFU_ERROR_INVALID_PARAMETER;
    }

    // Only bootstrap if device is in bootrom stage
    if (device->info.stage != TDFU_STAGE_BOOTROM) {
        if (config->verbose) {
            LOG_INFO("Device already in firmware stage, skipping bootstrap\n");
        }
        return TDFU_SUCCESS;
    }

    const char *variant_str = tdfu_variant_to_string(device->info.variant);
    LOG_INFO("Starting bootstrap sequence for %s\n", variant_str);

    // Reopen device to clean USB state from enumeration probes.
    // The device manager does GET_CPU_INFO during discovery which can
    // leave the bootrom's USB state dirty. Reopening gives a fresh handle.
    DEBUG_PRINT("Reopening device for clean USB state\n");
    usb_device_reopen(device);

    // Get CPU info to understand current device state
    DEBUG_PRINT("Getting CPU info...\n");
    cpu_info_t cpu_info;
    tdfu_error_t result = usb_device_get_cpu_info(device, &cpu_info);
    if (result != TDFU_SUCCESS) {
        LOG_INFO("Warning: failed to get CPU info: %s\n", tdfu_error_to_string(result));
        LOG_INFO("Continuing with bootstrap anyway - device may not be ready\n");
        // Don't exit - continue with bootstrap and let it fail gracefully if needed
    } else {
        // Show raw hex bytes for debugging
        LOG_INFO("CPU magic (raw hex): ");
        for (int i = 0; i < 8; i++) {
            LOG_INFO("%02X ", cpu_info.magic[i]);
        }
        LOG_INFO("\n");

        LOG_INFO("CPU info: stage=%s, magic='%.8s'\n", tdfu_stage_to_string(cpu_info.stage), cpu_info.magic);

        // Detect and display processor variant
        tdfu_variant_t detected_variant = detect_variant_from_magic(cpu_info.clean_magic);
        LOG_INFO("Detected processor variant: %s (from magic: '%s')\n", tdfu_variant_to_string(detected_variant),
                 cpu_info.clean_magic);

        // Update device stage based on actual CPU info
        if (cpu_info.stage == TDFU_STAGE_FIRMWARE) {
            device->info.stage = TDFU_STAGE_FIRMWARE;
            LOG_INFO("Device stage updated to firmware based on CPU info\n");
        }
    }

    // Load firmware files
    DEBUG_PRINT("Loading firmware files...\n");
    firmware_files_t fw;

    // Check if custom files are provided
    if (config->config_file || config->spl_file || config->uboot_file) {
        DEBUG_PRINT("Using custom firmware files:\n");
        if (config->config_file)
            DEBUG_PRINT("  Config: %s\n", config->config_file);
        if (config->spl_file)
            DEBUG_PRINT("  SPL: %s\n", config->spl_file);
        if (config->uboot_file)
            DEBUG_PRINT("  U-Boot: %s\n", config->uboot_file);

        result = firmware_load_from_files(device->info.variant, config->config_file, config->spl_file,
                                          config->uboot_file, &fw);
    } else {
        DEBUG_PRINT("Using firmware from: %s\n", config->firmware_dir ? config->firmware_dir : "./firmware");
        result = firmware_load_from_dir(device->info.variant, config->firmware_dir, &fw);
    }

    if (result != TDFU_SUCCESS) {
        DEBUG_PRINT("Firmware load failed: %s\n", tdfu_error_to_string(result));
        return result;
    }

    LOG_INFO("Firmware loaded - Config: %zu bytes, SPL: %zu bytes, U-Boot: %zu bytes\n", fw.config_size, fw.spl_size,
             fw.uboot_size);

    // Get platform config for memory addresses
    const char *platform_name = tdfu_variant_to_string(device->info.variant);
    const processor_config_t *plat = processor_config_get(platform_name);
    uint32_t ginfo_addr = plat ? plat->ginfo_addr : 0x80001000;
    uint32_t spl_addr = plat ? plat->spl_addr : 0x80001800;
    uint32_t d2i_len = plat ? plat->d2i_len : 0x7000;

    // Step 1: Load DDR configuration to memory (NOT executed yet)
    if (!config->skip_ddr) {
        LOG_INFO("Loading DDR configuration\n");
        result = bootstrap_load_data_to_memory(device, fw.config, fw.config_size, ginfo_addr);
        if (result != TDFU_SUCCESS) {
            firmware_cleanup(&fw);
            return result;
        }
        LOG_INFO("DDR configuration loaded\n");
    } else {
        LOG_INFO("Skipping DDR configuration (SkipDDR flag set)\n");
    }

    // Step 2: Load SPL to memory (NOT executed yet)
    LOG_INFO("Loading SPL (Stage 1 bootloader)\n");
    result = bootstrap_load_data_to_memory(device, fw.spl, fw.spl_size, spl_addr);
    if (result != TDFU_SUCCESS) {
        firmware_cleanup(&fw);
        return result;
    }
    LOG_INFO("SPL loaded\n");

    // Step 3: Set execution size (d2i_len) and execute SPL
    DEBUG_PRINT("Setting execution size (d2i_len) to 0x%x for %s\n", d2i_len, platform_name);
    result = protocol_set_data_length(device, d2i_len);
    if (result != TDFU_SUCCESS) {
        firmware_cleanup(&fw);
        return result;
    }

    DEBUG_PRINT("Executing SPL from entry point 0x%x\n", spl_addr);
    result = protocol_prog_stage1(device, spl_addr);
    if (result != TDFU_SUCCESS) {
        firmware_cleanup(&fw);
        return result;
    }
    LOG_INFO("SPL execution started\n");

    // IMPORTANT: Unlike T31X, the vendor's T20 implementation does NOT close/reopen the device
    // The USB device address stays the same (verified in pcap: address 106 throughout)
    // We just wait for SPL to complete DDR initialization
    DEBUG_PRINT("Waiting for SPL to complete DDR initialization (keeping device handle open)...\n");

    const platform_profile_t *profile = platform_get_profile(device->info.variant);

    DEBUG_PRINT("Waiting %u ms for DDR init (%s)...\n", profile->ddr_init_wait_ms, profile->name);
    platform_sleep_ms(profile->ddr_init_wait_ms);
    DEBUG_PRINT("SPL should have completed, device handle remains valid\n");

    if (profile->poll_spl_after_ddr) {
        DEBUG_PRINT("Polling GET_CPU_INFO after SPL wait (%s vendor pattern)...\n", profile->name);
        cpu_info_t poll_info;
        bool spl_ready = false;
        for (int attempt = 0; attempt < SPL_POLL_MAX_ATTEMPTS; attempt++) {
            tdfu_error_t poll_result = usb_device_get_cpu_info(device, &poll_info);
            if (poll_result == TDFU_SUCCESS) {
                DEBUG_PRINT("SPL ready after %d attempt(s): stage=%s, magic='%s'\n", attempt + 1,
                            tdfu_stage_to_string(poll_info.stage), poll_info.clean_magic);
                spl_ready = true;
                break;
            }
            platform_sleep_ms(20);
        }
        if (!spl_ready) {
            DEBUG_PRINT("Warning: GET_CPU_INFO polling after SPL failed for %s\n", profile->name);
        }
    }

    if (profile->reopen_usb_after_spl) {
        DEBUG_PRINT("Reopening USB device handle after SPL for %s\n", profile->name);
        tdfu_error_t reopen_result = usb_device_reopen(device);
        if (reopen_result != TDFU_SUCCESS) {
            LOG_INFO("Error: failed to re-open USB device after SPL: %s\n", tdfu_error_to_string(reopen_result));
            firmware_cleanup(&fw);
            return reopen_result;
        }
        DEBUG_PRINT("Waiting %dms after USB reopen for device to be ready...\n", USB_REOPEN_WAIT_MS);
        platform_sleep_ms(USB_REOPEN_WAIT_MS);
    }

    // Step 4: Load and program U-Boot (Stage 2 bootloader)
    LOG_INFO("Loading U-Boot (Stage 2 bootloader)\n");
    result = bootstrap_program_stage2(device, fw.uboot, fw.uboot_size);
    if (result != TDFU_SUCCESS) {
        firmware_cleanup(&fw);
        return result;
    }
    LOG_INFO("U-Boot loaded\n");

    // Vendor does GET_CPU_INFO immediately after PROG_START2 (verified in pcap)
    // This might be necessary to "wake up" the device or trigger the transition
    DEBUG_PRINT("Checking CPU info immediately after PROG_START2 (vendor sequence)...\n");
    cpu_info_t cpu_info_after;
    result = usb_device_get_cpu_info(device, &cpu_info_after);
    if (result == TDFU_SUCCESS) {
        DEBUG_PRINT("CPU info after PROG_START2: stage=%s, magic='%s'\n", tdfu_stage_to_string(cpu_info_after.stage),
                    cpu_info_after.clean_magic);
    } else {
        DEBUG_PRINT("GET_CPU_INFO after PROG_START2 failed (may be expected): %s\n", tdfu_error_to_string(result));
    }

    // NOTE (T31 doorbell): Factory T31 burner U-Boot logs show that sending
    // VR_FW_HANDSHAKE/VR_FW_READ immediately after PROG_STAGE2 results in
    // cloner->ack = -22 and a trap exception when no flash descriptor has
    // been provided yet. For this device we therefore perform FW_HANDSHAKE
    // only in the higher-level read/write flows, *after* the 172-byte
    // partition marker and 972-byte flash descriptor have been sent.

    LOG_INFO("Bootstrap sequence completed successfully\n");

    firmware_cleanup(&fw);
    return TDFU_SUCCESS;
}

tdfu_error_t bootstrap_ensure_bootstrapped(usb_device_t *device, const bootstrap_config_t *config) {
    if (!device || !config) {
        return TDFU_ERROR_INVALID_PARAMETER;
    }

    // If device is already in firmware stage, no bootstrap needed
    if (device->info.stage == TDFU_STAGE_FIRMWARE) {
        return TDFU_SUCCESS;
    }

    // Device needs bootstrap
    return bootstrap_device(device, config);
}

tdfu_error_t bootstrap_load_data_to_memory(usb_device_t *device, const uint8_t *data, size_t size,
                                               uint32_t address) {

    if (!device || !data || size == 0) {
        return TDFU_ERROR_INVALID_PARAMETER;
    }

    // Step 1: Set target address
    DEBUG_PRINT("Setting data address to 0x%08x\n", address);
    tdfu_error_t result = protocol_set_data_address(device, address);
    if (result != TDFU_SUCCESS) {
        return result;
    }

    // Step 2: Set data length
    DEBUG_PRINT("Setting data length to %zu bytes\n", size);
    result = protocol_set_data_length(device, (uint32_t)size);
    if (result != TDFU_SUCCESS) {
        return result;
    }

    // Step 3: Transfer data
    DEBUG_PRINT("Transferring data (%zu bytes)...\n", size);
    result = bootstrap_transfer_data(device, data, size);
    if (result != TDFU_SUCCESS) {
        return result;
    }

    return TDFU_SUCCESS;
}

tdfu_error_t bootstrap_program_stage2(usb_device_t *device, const uint8_t *data, size_t size) {

    if (!device || !data || size == 0) {
        return TDFU_ERROR_INVALID_PARAMETER;
    }

    // Step 1: Set target address for U-Boot from platform config
    const char *uboot_plat_name = tdfu_variant_to_string(device->info.variant);
    const processor_config_t *uboot_plat = processor_config_get(uboot_plat_name);
    uint32_t uboot_address = uboot_plat ? uboot_plat->uboot_addr : 0x80100000;
    DEBUG_PRINT("Setting U-Boot data address to 0x%08x\n", uboot_address);
    tdfu_error_t result = protocol_set_data_address(device, uboot_address);
    if (result != TDFU_SUCCESS) {
        return result;
    }

    // Step 2: Set data length
    DEBUG_PRINT("Setting U-Boot data length to %zu bytes\n", size);
    result = protocol_set_data_length(device, (uint32_t)size);
    if (result != TDFU_SUCCESS) {
        return result;
    }

    // Step 3: Transfer data
    DEBUG_PRINT("Transferring U-Boot data (%zu bytes)...\n", size);
    result = bootstrap_transfer_data(device, data, size);
    if (result != TDFU_SUCCESS) {
        return result;
    }

    // After large U-Boot transfer, give device time to process
    DEBUG_PRINT("Waiting for device to process U-Boot transfer...\n");
    platform_sleep_ms(UBOOT_TRANSFER_WAIT_MS);

    // Step 4: Flush cache before executing U-Boot
    // Step 4: Flush cache before executing U-Boot
    DEBUG_PRINT("Flushing cache before U-Boot execution\n");
    result = protocol_flush_cache(device);
    if (result != TDFU_SUCCESS) {
        DEBUG_PRINT("FlushCache error (non-fatal): %s\n", tdfu_error_to_string(result));
    }

    // Step 5: Execute U-Boot using ProgStage2 (vendor protocol)
    DEBUG_PRINT("Executing U-Boot using ProgStage2 at 0x%08x\n", uboot_address);
    result = protocol_prog_stage2(device, uboot_address);

    DEBUG_PRINT("U-Boot execution command sent - device should now be in firmware stage\n");

    // Platform-specific sleep
    platform_sleep_ms(1000);

    return TDFU_SUCCESS;
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

    // Chunk size for bulk transfers.
    // Vendor cloner uses: 512, 32768, 65536, 131072, 262144, 524288, 1048576
    // Large single transfers can fail on some bootrom USB stacks.
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