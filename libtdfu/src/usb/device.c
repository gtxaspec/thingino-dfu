#include "tdfu/tdfu.h"
#include "platform.h"

#if defined(__linux__) && !defined(__EMSCRIPTEN__)
#include <sys/ioctl.h>
#include <linux/usbdevice_fs.h>
#include <fcntl.h>
#include <errno.h>
#endif
#ifndef _WIN32
#include <time.h>
#endif

tdfu_error_t usb_device_get_cpu_info(usb_device_t *device, cpu_info_t *info) {
    if (!device || !info || device->closed) {
        DEBUG_PRINT("GetCPUInfo: Invalid parameters or device closed\n");
        return TDFU_ERROR_INVALID_PARAMETER;
    }

    DEBUG_PRINT("GetCPUInfo: Starting CPU info request (VID:0x%04X, PID:0x%04X)\n", device->info.vendor,
                device->info.product);

    uint8_t data[8] = {0};
    int transferred;
    DEBUG_PRINT("GetCPUInfo: Sending vendor request VR_GET_CPU_INFO (0x%02X)\n", VR_GET_CPU_INFO);

    // Direct control transfer without claiming interface first (like Go version)
    int result = libusb_control_transfer(device->handle, REQUEST_TYPE_VENDOR, VR_GET_CPU_INFO, 0, 0, data, 8, 5000);

    if (result < 0) {
        DEBUG_PRINT("GetCPUInfo: Direct control transfer failed: %d (%s), trying with interface claim\n", result,
                    libusb_error_name(result));

        // Fall back to interface claiming approach
        tdfu_error_t claim_result = usb_device_claim_interface(device);
        if (claim_result != TDFU_SUCCESS) {
            DEBUG_PRINT("GetCPUInfo: Failed to claim interface: %s\n", tdfu_error_to_string(claim_result));
            return claim_result;
        }

        result =
            usb_device_vendor_request(device, REQUEST_TYPE_VENDOR, VR_GET_CPU_INFO, 0, 0, NULL, 8, data, &transferred);

        // Release interface after communication
        usb_device_release_interface(device);

        if (result != TDFU_SUCCESS) {
            DEBUG_PRINT("GetCPUInfo: Vendor request failed: %s\n", tdfu_error_to_string(result));
            return result;
        }
    } else {
        transferred = result;
        DEBUG_PRINT("GetCPUInfo: Direct control transfer succeeded: %d bytes\n", transferred);
    }

    if (transferred < 8) {
        DEBUG_PRINT("GetCPUInfo: Invalid response length: %d (expected 8)\n", transferred);
        return TDFU_ERROR_PROTOCOL;
    }

    DEBUG_PRINT("GetCPUInfo: Got %d bytes of response data\n", transferred);

    // Copy magic bytes
    memcpy(info->magic, data, 8);

    // Clean CPU string - extract only valid ASCII characters
    char cpu_str[9] = {0};
    int write_pos = 0;
    for (int read_pos = 0; read_pos < 8; read_pos++) {
        uint8_t byte = data[read_pos];
        if (byte >= 0x20 && byte <= 0x7E) { // Printable ASCII including space
            if (byte != ' ') {              // Skip spaces
                cpu_str[write_pos++] = byte;
            }
        }
    }
    cpu_str[write_pos] = '\0';

    // Also create a clean version for variant detection (preserve spaces for pattern matching)
    char clean_cpu_str[9] = {0};
    write_pos = 0;
    for (int read_pos = 0; read_pos < 8; read_pos++) {
        uint8_t byte = data[read_pos];
        if (byte >= 0x20 && byte <= 0x7E) { // Printable ASCII including space
            clean_cpu_str[write_pos++] = byte;
        }
    }
    clean_cpu_str[write_pos] = '\0';

    DEBUG_PRINT("GetCPUInfo: CPU magic bytes: ");
    for (int i = 0; i < 8; i++) {
        DEBUG_PRINT("0x%02X ", data[i]);
    }
    DEBUG_PRINT("-> string = '%s' -> clean = '%s'\n", cpu_str, clean_cpu_str);

    // Store clean string in cpu_info for variant detection
    strcpy(info->clean_magic, clean_cpu_str);

    // Determine boot stage based on CPU info
    // Firmware stage is normally indicated by a "Boot"/"BOOT" prefix, but some
    // boards report a short CPU string (e.g. "X2580" for T41N/XBurst2, "A1" for
    // A1-series NVRs) once the vendor firmware is running. Treat these as
    // firmware stage too so that post-bootstrap detection works correctly.
    bool is_firmware_stage = false;
    if (strncmp(cpu_str, "Boot", 4) == 0 || strncmp(cpu_str, "BOOT", 4) == 0) {
        is_firmware_stage = true;
    } else if (strncmp(cpu_str, "X2", 2) == 0 || strncmp(cpu_str, "x2", 2) == 0) {
        /* X2500 (T40), X2580 (T41), X2600 etc. — xburst2 vendor burner U-Boot */
        DEBUG_PRINT("GetCPUInfo: Detected %s magic, treating as firmware stage (xburst2)\n", cpu_str);
        is_firmware_stage = true;
    } else if (strncmp(cpu_str, "A1", 2) == 0 || strncmp(cpu_str, "a1", 2) == 0) {
        DEBUG_PRINT("GetCPUInfo: Detected A1 magic, treating as firmware stage (A1/T31VX board)\n");
        is_firmware_stage = true;
    }

    if (is_firmware_stage) {
        info->stage = TDFU_STAGE_FIRMWARE;
        device->info.stage = TDFU_STAGE_FIRMWARE;
        DEBUG_PRINT("GetCPUInfo: Device is in firmware stage\n");
    } else {
        info->stage = TDFU_STAGE_BOOTROM;
        device->info.stage = TDFU_STAGE_BOOTROM;
        DEBUG_PRINT("GetCPUInfo: Device is in bootrom stage\n");
    }

    return TDFU_SUCCESS;
}

// Initialize USB device
tdfu_error_t usb_device_init(usb_device_t *device, uint8_t bus, uint8_t address) {
    if (!device) {
        return TDFU_ERROR_INVALID_PARAMETER;
    }

    // Find the device by bus and address
    libusb_device **devices;
    ssize_t count = libusb_get_device_list(NULL, &devices);
    if (count < 0) {
        return TDFU_ERROR_DEVICE_NOT_FOUND;
    }

    libusb_device *found_device = NULL;
    for (ssize_t i = 0; i < count; i++) {
        uint8_t dev_bus = libusb_get_bus_number(devices[i]);
        uint8_t dev_address = libusb_get_device_address(devices[i]);

        if (dev_bus == bus && dev_address == address) {
            found_device = devices[i];
            break;
        }
    }

    if (!found_device) {
        libusb_free_device_list(devices, 1);
        return TDFU_ERROR_DEVICE_NOT_FOUND;
    }

    // Open the device
    int result = libusb_open(found_device, &device->handle);
    if (result != LIBUSB_SUCCESS) {
        libusb_free_device_list(devices, 1);
        return TDFU_ERROR_OPEN_FAILED;
    }

    // Get device descriptor
    struct libusb_device_descriptor desc;
    result = libusb_get_device_descriptor(found_device, &desc);
    if (result != LIBUSB_SUCCESS) {
        libusb_close(device->handle);
        libusb_free_device_list(devices, 1);
        return TDFU_ERROR_OPEN_FAILED;
    }

    // Initialize device structure
    device->device = found_device;
    libusb_ref_device(found_device);
    // Preserve context if already set by manager, otherwise set to NULL
    // (context is set before usb_device_init is called by the manager)
    // DEBUG_PRINT("usb_device_init: context before init = %p\n", device->context);
    device->closed = false;
    device->info.bus = bus;
    device->info.address = address;
    device->info.vendor = desc.idVendor;
    device->info.product = desc.idProduct;
    // IMPORTANT: Do NOT override device->info.stage here; it is set by the manager
    // based on CPU magic (bootrom vs firmware stage).
    // Don't set default variant - preserve whatever was set by manager
    DEBUG_PRINT("usb_device_init: preserving variant %d, stage=%d, context=%p\n", device->info.variant,
                device->info.stage, device->context);

    libusb_free_device_list(devices, 1);

    DEBUG_PRINT("Device initialized: VID:0x%04X, PID:0x%04X, Bus:%d, Addr:%d\n", device->info.vendor,
                device->info.product, bus, address);

    return TDFU_SUCCESS;
}

// Close USB device
tdfu_error_t usb_device_close(usb_device_t *device) {
    if (!device) {
        return TDFU_ERROR_INVALID_PARAMETER;
    }

    if (!device->closed && device->handle) {
        libusb_close(device->handle);
        device->handle = NULL;
    }

    device->closed = true;
    return TDFU_SUCCESS;
}

// Reopen USB device after possible reset or re-enumeration
tdfu_error_t usb_device_reopen(usb_device_t *device) {
    if (!device) {
        return TDFU_ERROR_INVALID_PARAMETER;
    }

#ifdef ANDROID
    /* On Android, the USB fd is managed by the Java UsbDeviceConnection.
     * We cannot enumerate or reopen devices — the existing handle stays valid.
     * Just return success and keep using the current handle. */
    DEBUG_PRINT("usb_device_reopen: skipping on Android (fd managed by Java)\n");
    return TDFU_SUCCESS;
#endif

    DEBUG_PRINT("usb_device_reopen: attempting to reopen device VID:0x%04X PID:0x%04X (old bus=%d addr=%d)\n",
                device->info.vendor, device->info.product, device->info.bus, device->info.address);

    // Close existing handle if still open
    if (!device->closed && device->handle) {
        libusb_close(device->handle);
        device->handle = NULL;
    }
    device->closed = true;

    // Enumerate devices on the same libusb context (if available)
    libusb_device **list = NULL;
    ssize_t count = libusb_get_device_list(device->context, &list);
    if (count < 0) {
        DEBUG_PRINT("usb_device_reopen: libusb_get_device_list failed: %zd\n", count);
        return TDFU_ERROR_DEVICE_NOT_FOUND;
    }

    libusb_device *found = NULL;
    uint8_t new_bus = 0;
    uint8_t new_addr = 0;

    for (ssize_t i = 0; i < count; i++) {
        struct libusb_device_descriptor desc;
        if (libusb_get_device_descriptor(list[i], &desc) != LIBUSB_SUCCESS) {
            continue;
        }

        uint8_t bus = libusb_get_bus_number(list[i]);
        uint8_t addr = libusb_get_device_address(list[i]);

        if (desc.idVendor == device->info.vendor && desc.idProduct == device->info.product) {
            /* Prefer exact bus+address match to avoid opening the wrong device
             * when multiple devices share the same VID:PID */
            if (bus == device->info.bus && addr == device->info.address) {
                found = list[i];
                new_bus = bus;
                new_addr = addr;
                break;
            }
            /* Fall back to first VID:PID match if address changed (re-enumeration) */
            if (!found) {
                found = list[i];
                new_bus = bus;
                new_addr = addr;
            }
        }
    }

    if (!found) {
        libusb_free_device_list(list, 1);
        DEBUG_PRINT("usb_device_reopen: matching device not found after re-enumeration\n");
        return TDFU_ERROR_DEVICE_NOT_FOUND;
    }

    int result = libusb_open(found, &device->handle);
    if (result != LIBUSB_SUCCESS) {
        libusb_free_device_list(list, 1);
        DEBUG_PRINT("usb_device_reopen: libusb_open failed: %s\n", libusb_error_name(result));
        return TDFU_ERROR_OPEN_FAILED;
    }

    device->device = found;
    libusb_ref_device(found);
    device->info.bus = new_bus;
    device->info.address = new_addr;
    device->closed = false;

    libusb_free_device_list(list, 1);

    DEBUG_PRINT("usb_device_reopen: reopened on bus=%d addr=%d\n", device->info.bus, device->info.address);

    return TDFU_SUCCESS;
}

// Reset USB device
tdfu_error_t usb_device_reset(usb_device_t *device) {
    if (!device || !device->handle || device->closed) {
        return TDFU_ERROR_INVALID_PARAMETER;
    }

#ifdef ANDROID
    /* USB reset not supported on Android — fd is managed by Java */
    DEBUG_PRINT("usb_device_reset: skipping on Android\n");
    return TDFU_SUCCESS;
#endif

    int result = libusb_reset_device(device->handle);
    if (result != LIBUSB_SUCCESS) {
        DEBUG_PRINT("Reset device failed: %s\n", libusb_error_name(result));
        return TDFU_ERROR_TRANSFER_FAILED;
    }

    return TDFU_SUCCESS;
}

// Claim USB interface
tdfu_error_t usb_device_claim_interface(usb_device_t *device) {
    if (!device || !device->handle || device->closed) {
        return TDFU_ERROR_INVALID_PARAMETER;
    }

    // Ensure configuration is set before claiming interface
    int current_config = -1;
    libusb_get_configuration(device->handle, &current_config);
    if (current_config != 1) {
        DEBUG_PRINT("Setting USB configuration 1 (was %d)\n", current_config);
        int cfg_result = libusb_set_configuration(device->handle, 1);
        if (cfg_result != LIBUSB_SUCCESS && cfg_result != LIBUSB_ERROR_BUSY) {
            DEBUG_PRINT("Set configuration failed: %s\n", libusb_error_name(cfg_result));
        }
    }

    // Detach kernel driver if attached
    if (libusb_kernel_driver_active(device->handle, 0) == 1) {
        DEBUG_PRINT("Detaching kernel driver from interface 0\n");
        libusb_detach_kernel_driver(device->handle, 0);
    }

    int result = libusb_claim_interface(device->handle, 0);
    if (result != LIBUSB_SUCCESS) {
        DEBUG_PRINT("Claim interface failed: %s\n", libusb_error_name(result));
        return TDFU_ERROR_TRANSFER_FAILED;
    }

    return TDFU_SUCCESS;
}

// Release USB interface
tdfu_error_t usb_device_release_interface(usb_device_t *device) {
    if (!device || !device->handle || device->closed) {
        return TDFU_ERROR_INVALID_PARAMETER;
    }

    int result = libusb_release_interface(device->handle, 0);
    if (result != LIBUSB_SUCCESS) {
        DEBUG_PRINT("Release interface failed: %s\n", libusb_error_name(result));
        return TDFU_ERROR_TRANSFER_FAILED;
    }

    return TDFU_SUCCESS;
}

// Control transfer
tdfu_error_t usb_device_control_transfer(usb_device_t *device, uint8_t request_type, uint8_t request,
                                             uint16_t value, uint16_t index, uint8_t *data, uint16_t length,
                                             int *transferred) {

    if (!device || !device->handle || device->closed) {
        return TDFU_ERROR_INVALID_PARAMETER;
    }

    int result = libusb_control_transfer(device->handle, request_type, request, value, index, data, length, 5000);

    if (result < 0) {
        DEBUG_PRINT("Control transfer failed: %s\n", libusb_error_name(result));
        return TDFU_ERROR_TRANSFER_FAILED;
    }

    if (transferred) {
        *transferred = result;
    }

    return TDFU_SUCCESS;
}

// Helper to get current time in milliseconds
#ifndef _WIN32
#endif

// Async transfer removed - protocol requires direct, synchronous transfers per trace file

// Direct ioctl removed - protocol requires synchronous libusb transfers per trace file

// Bulk transfer with timeout parameter
// According to the trace file, protocol requires successful transfer
// Fail immediately if transfer doesn't succeed
tdfu_error_t usb_device_bulk_transfer(usb_device_t *device, uint8_t endpoint, uint8_t *data, int length,
                                          int *transferred, int timeout) {

    if (!device || !device->handle || device->closed) {
        return TDFU_ERROR_INVALID_PARAMETER;
    }

    // Determine direction from endpoint (bit 7: 0=OUT, 1=IN)
    const char *direction = (endpoint & 0x80) ? "read" : "write";

    DEBUG_PRINT("Bulk transfer: %s %d bytes, timeout=%dms, endpoint=0x%02X\n", direction, length, timeout, endpoint);

    // Use libusb for bulk transfer
    int result = libusb_bulk_transfer(device->handle, endpoint, data, length, transferred, timeout);

    if (result == LIBUSB_SUCCESS) {
        DEBUG_PRINT("Bulk transfer success: %d bytes transferred\n", transferred ? *transferred : -1);
        return TDFU_SUCCESS;
    }

    // Special handling for timeouts
    if (result == LIBUSB_ERROR_TIMEOUT) {
        // If libusb reports a timeout but also reports that the full payload
        // was transferred, treat this as success. This handles controller
        // quirks where completion is delayed but data is already on the bus.
        if (transferred && *transferred == length) {
            DEBUG_PRINT(
                "Bulk transfer reported timeout but full length (%d bytes) was transferred; treating as success\n",
                *transferred);
            return TDFU_SUCCESS;
        }

        DEBUG_PRINT("Bulk transfer timeout (%s): endpoint=0x%02X, length=%d, timeout=%dms, transferred=%d\n", direction,
                    endpoint, length, timeout, transferred ? *transferred : -1);
        return TDFU_ERROR_TIMEOUT;
    }

    LOG_INFO("[ERROR] Bulk transfer failed: %s (endpoint=0x%02X, length=%d, timeout=%dms, transferred=%d)\n",
             libusb_error_name(result), endpoint, length, timeout, transferred ? *transferred : -1);
    return TDFU_ERROR_TRANSFER_FAILED;
}

// Interrupt transfer with timeout parameter
// Used for INT endpoint communication (e.g., EP 0x00 handshaking)
tdfu_error_t usb_device_interrupt_transfer(usb_device_t *device, uint8_t endpoint, uint8_t *data, int length,
                                               int *transferred, int timeout) {

    if (!device || !device->handle || device->closed) {
        return TDFU_ERROR_INVALID_PARAMETER;
    }

    // Determine direction from endpoint (bit 7: 0=OUT, 1=IN)
    const char *direction = (endpoint & 0x80) ? "read" : "write";

    DEBUG_PRINT("Interrupt transfer: %s %d bytes, timeout=%dms, endpoint=0x%02X\n", direction, length, timeout,
                endpoint);

    // Use libusb interrupt transfer
    int result = libusb_interrupt_transfer(device->handle, endpoint, data, length, transferred, timeout);

    if (result == LIBUSB_SUCCESS) {
        DEBUG_PRINT("Interrupt transfer success (%s): %d bytes transferred\n", direction, *transferred);
        return TDFU_SUCCESS;
    }

    if (result == LIBUSB_ERROR_TIMEOUT) {
        DEBUG_PRINT("Interrupt transfer timeout (%s): endpoint=0x%02X, length=%d, timeout=%dms\n", direction, endpoint,
                    length, timeout);
        return TDFU_ERROR_TIMEOUT;
    }

    DEBUG_PRINT("Interrupt transfer failed (%s): %s (endpoint=0x%02X, length=%d, timeout=%dms)\n", direction,
                libusb_error_name(result), endpoint, length, timeout);
    return TDFU_ERROR_TRANSFER_FAILED;
}

// Vendor request with retry logic for device re-enumeration
tdfu_error_t usb_device_vendor_request(usb_device_t *device, uint8_t request_type, uint8_t request, uint16_t value,
                                           uint16_t index, uint8_t *data, uint16_t length, uint8_t *response,
                                           int *response_length) {

    if (!device || !device->handle || device->closed) {
        return TDFU_ERROR_INVALID_PARAMETER;
    }

    // Special handling for firmware-stage VR_WRITE (0x12) handshakes.
    // On some T31x devices the control transfer for VR_WRITE may time out
    // even though the device has accepted the 40-byte handshake and will
    // process the subsequent bulk-out chunk. To avoid spamming multiple
    // identical VR_WRITE requests (which diverges from the vendor trace),
    // send it once and treat a timeout as success.
    if (request_type == REQUEST_TYPE_OUT && request == VR_WRITE && device->info.stage == TDFU_STAGE_FIRMWARE) {

        uint8_t *buffer = response ? response : data;
        int result = libusb_control_transfer(device->handle, request_type, request, value, index, buffer, length, 5000);

        if (result >= 0) {
            if (response_length) {
                *response_length = result;
            }
            return TDFU_SUCCESS;
        }

        if (result == LIBUSB_ERROR_TIMEOUT) {
            DEBUG_PRINT("Vendor request VR_WRITE timed out; assuming device is still processing handshake (treating as "
                        "success)\n");
            if (response_length) {
                *response_length = 0;
            }
            return TDFU_SUCCESS;
        }

        DEBUG_PRINT("Vendor request VR_WRITE failed: %s\n", libusb_error_name(result));
        return TDFU_ERROR_TRANSFER_FAILED;
    }

    // Special handling for firmware-stage VR_SET_DATA_ADDR (0x01) and VR_SET_DATA_LEN (0x02) during NOR
    // writer_full operations. When the cloner starts a full-image burn with
    // force_erase enabled, the first SetDataAddress/SetDataLength pair
    // triggers a full chip erase. While the erase is in progress the
    // firmware often stops responding to control transfers on the same
    // endpoint and libusb reports timeouts, even though the command has
    // been accepted and the erase is progressing.
    //
    // To avoid aborting the write with "Failed to set flash base address"
    // or "Failed to set firmware write length" in this situation, treat a
    // timeout on VR_SET_DATA_ADDR or VR_SET_DATA_LEN in firmware stage as
    // "device busy but OK" and let subsequent operations (status checks,
    // handshakes, data transfers) detect any real failures.
    if (request_type == REQUEST_TYPE_OUT && (request == VR_SET_DATA_ADDR || request == VR_SET_DATA_LEN) &&
        device->info.stage == TDFU_STAGE_FIRMWARE) {

        uint8_t *buffer = response ? response : data;
        int result = libusb_control_transfer(device->handle, request_type, request, value, index, buffer, length, 5000);

        if (result >= 0) {
            if (response_length) {
                *response_length = result;
            }
            return TDFU_SUCCESS;
        }

        if (result == LIBUSB_ERROR_TIMEOUT) {
            const char *req_name = (request == VR_SET_DATA_ADDR) ? "VR_SET_DATA_ADDR" : "VR_SET_DATA_LEN";
            DEBUG_PRINT("Vendor request %s timed out in firmware stage; "
                        "assuming device is busy (chip erase in progress) and treating as success\n",
                        req_name);
            if (response_length) {
                *response_length = 0;
            }
            return TDFU_SUCCESS;
        }

        const char *req_name = (request == VR_SET_DATA_ADDR) ? "VR_SET_DATA_ADDR" : "VR_SET_DATA_LEN";
        DEBUG_PRINT("Vendor request %s failed: %s\n", req_name, libusb_error_name(result));
        return TDFU_ERROR_TRANSFER_FAILED;
    }

    // Retry logic for device re-enumeration issues
    int max_retries = 5;
    int retry_count = 0;
    int retry_delays[] = {500000, 1000000, 2000000, 3000000, 5000000}; // microseconds: 0.5s, 1s, 2s, 3s, 5s

    while (retry_count < max_retries) {
        uint8_t *buffer = response ? response : data;
        int result = libusb_control_transfer(device->handle, request_type, request, value, index, buffer, length, 5000);

        if (result >= 0) {
            // Success
            if (response_length) {
                *response_length = result;
            }
            return TDFU_SUCCESS;
        }

        // Check if this is a timeout or pipe error (device disconnected)
        if (result == LIBUSB_ERROR_TIMEOUT || result == LIBUSB_ERROR_PIPE || result == LIBUSB_ERROR_NO_DEVICE) {
            retry_count++;
            if (retry_count < max_retries) {
                DEBUG_PRINT("Vendor request failed with %s, retrying in %d ms (attempt %d/%d)...\n",
                            libusb_error_name(result), retry_delays[retry_count - 1] / 1000, retry_count, max_retries);
                platform_sleep_ms(retry_delays[retry_count - 1] / 1000);
            } else {
                DEBUG_PRINT("Vendor request failed after %d retries: %s\n", max_retries, libusb_error_name(result));
                return TDFU_ERROR_TRANSFER_FAILED;
            }
        } else {
            // Non-recoverable error
            DEBUG_PRINT("Vendor request failed: %s\n", libusb_error_name(result));
            return TDFU_ERROR_TRANSFER_FAILED;
        }
    }

    return TDFU_ERROR_TRANSFER_FAILED;
}