#include "tdfu/tdfu.h"

// ============================================================================
// USB MANAGER IMPLEMENTATION
// ============================================================================

tdfu_error_t usb_manager_init(usb_manager_t *manager) {
    if (!manager)
        return TDFU_ERROR_INVALID_PARAMETER;

    DEBUG_PRINT("Initializing USB manager...\n");

    int result = libusb_init(&manager->context);
    if (result < 0) {
        DEBUG_PRINT("libusb_init failed: %d\n", result);
        return TDFU_ERROR_INIT_FAILED;
    }

    DEBUG_PRINT("libusb initialized successfully\n");
    manager->initialized = true;
    return TDFU_SUCCESS;
}

// Helper: check if a USB device is an Ingenic cloner device
static bool is_ingenic_device(const struct libusb_device_descriptor *desc, bool *out_bootrom, bool *out_firmware,
                              bool *out_dfu) {
    bool is_ingenic = (desc->idVendor == VENDOR_ID_INGENIC || desc->idVendor == VENDOR_ID_INGENIC_ALT);
    if (!is_ingenic)
        return false;

    *out_bootrom = (desc->idProduct == PRODUCT_ID_BOOTROM || desc->idProduct == PRODUCT_ID_BOOTROM2 ||
                    desc->idProduct == PRODUCT_ID_BOOTROM3);
    *out_firmware = (desc->idProduct == PRODUCT_ID_FIRMWARE || desc->idProduct == PRODUCT_ID_FIRMWARE2);
    /* A device already sitting in the U-Boot DFU gadget (e.g. mainline U-Boot
     * that boots straight to DFU, or a previously-bootstrapped device). It has
     * no bootrom CPU-info protocol, so it is reported as TDFU_STAGE_DFU and the
     * DFU read/write path operates on it directly (no bootstrap). */
    *out_dfu = (desc->idProduct == PRODUCT_ID_DFU);
    return *out_bootrom || *out_firmware || *out_dfu;
}

tdfu_error_t usb_manager_find_devices(usb_manager_t *manager, tdfu_device_info_t **devices, int *count) {

    if (!manager || !devices || !count)
        return TDFU_ERROR_INVALID_PARAMETER;
    if (!manager->initialized)
        return TDFU_ERROR_INIT_FAILED;

    *devices = NULL;
    *count = 0;

    libusb_device **device_list;
    ssize_t device_count = libusb_get_device_list(manager->context, &device_list);
    if (device_count < 0)
        return TDFU_ERROR_DEVICE_NOT_FOUND;

    DEBUG_PRINT("Processing %zd devices\n", device_count);

    int ingenic_count = 0;
    int capacity = 0;

    for (ssize_t i = 0; i < device_count; i++) {
        if (!device_list[i])
            break;

        struct libusb_device_descriptor desc;
        if (libusb_get_device_descriptor(device_list[i], &desc) < 0)
            continue;

        DEBUG_PRINT("Device %zd: VID=0x%04X, PID=0x%04X\n", i, desc.idVendor, desc.idProduct);

        bool is_bootrom, is_firmware, is_dfu;
        if (!is_ingenic_device(&desc, &is_bootrom, &is_firmware, &is_dfu))
            continue;
        (void)is_bootrom;

        DEBUG_PRINT("Found Ingenic device %zd (VID:0x%04X, PID:0x%04X)\n", i, desc.idVendor, desc.idProduct);

        // Grow array as needed
        if (ingenic_count >= capacity) {
            capacity = capacity ? capacity * 2 : 4;
            tdfu_device_info_t *tmp = realloc(*devices, capacity * sizeof(tdfu_device_info_t));
            if (!tmp) {
                free(*devices);
                *devices = NULL;
                libusb_free_device_list(device_list, 1);
                return TDFU_ERROR_MEMORY;
            }
            *devices = tmp;
        }

        tdfu_device_info_t *info = &(*devices)[ingenic_count];
        info->bus = libusb_get_bus_number(device_list[i]);
        info->address = libusb_get_device_address(device_list[i]);
        info->vendor = desc.idVendor;
        info->product = desc.idProduct;
        info->stage = is_dfu ? TDFU_STAGE_DFU : (is_firmware ? TDFU_STAGE_FIRMWARE : TDFU_STAGE_BOOTROM);
        info->variant = TDFU_VARIANT_T31X;

        // Query CPU info for bootrom devices
        if (is_bootrom) {
            DEBUG_PRINT("Checking CPU info for device %d to determine actual stage\n", ingenic_count);
            usb_device_t *test_device;
            if (usb_manager_open_device(manager, info, &test_device) == TDFU_SUCCESS) {
                cpu_info_t cpu_info;
                if (usb_device_get_cpu_info(test_device, &cpu_info) == TDFU_SUCCESS) {
                    info->stage = cpu_info.stage;
                    DEBUG_PRINT("Device %d is in %s stage (CPU magic: %.8s)\n", ingenic_count,
                                cpu_info.stage == TDFU_STAGE_FIRMWARE ? "firmware" : "bootrom", cpu_info.magic);

                    tdfu_variant_t detected = detect_variant_from_magic(cpu_info.clean_magic);
                    info->variant = detected;
                    DEBUG_PRINT("Updated device %d variant to %s (%d) based on CPU magic\n", ingenic_count,
                                tdfu_variant_to_string(detected), detected);
                } else {
                    DEBUG_PRINT("Failed to get CPU info for device %d\n", ingenic_count);
                }
                usb_device_close(test_device);
                free(test_device);
            }
        }

        ingenic_count++;
    }

    libusb_free_device_list(device_list, 1);

    DEBUG_PRINT("Found %d Ingenic devices\n", ingenic_count);
    *count = ingenic_count;
    return TDFU_SUCCESS;
}

// Fast enumeration - skips CPU info queries (for bootstrap re-detection)
tdfu_error_t usb_manager_find_devices_fast(usb_manager_t *manager, tdfu_device_info_t **devices, int *count) {

    if (!manager || !devices || !count)
        return TDFU_ERROR_INVALID_PARAMETER;
    if (!manager->initialized)
        return TDFU_ERROR_INIT_FAILED;

    *devices = NULL;
    *count = 0;

    libusb_device **device_list;
    ssize_t device_count = libusb_get_device_list(manager->context, &device_list);
    if (device_count < 0)
        return TDFU_ERROR_DEVICE_NOT_FOUND;

    int ingenic_count = 0;
    int capacity = 0;

    for (ssize_t i = 0; i < device_count; i++) {
        if (!device_list[i])
            break;

        struct libusb_device_descriptor desc;
        if (libusb_get_device_descriptor(device_list[i], &desc) < 0)
            continue;

        bool is_bootrom, is_firmware, is_dfu;
        if (!is_ingenic_device(&desc, &is_bootrom, &is_firmware, &is_dfu))
            continue;
        (void)is_bootrom;

        if (ingenic_count >= capacity) {
            capacity = capacity ? capacity * 2 : 4;
            tdfu_device_info_t *tmp = realloc(*devices, capacity * sizeof(tdfu_device_info_t));
            if (!tmp) {
                free(*devices);
                *devices = NULL;
                libusb_free_device_list(device_list, 1);
                return TDFU_ERROR_MEMORY;
            }
            *devices = tmp;
        }

        tdfu_device_info_t *info = &(*devices)[ingenic_count];
        info->bus = libusb_get_bus_number(device_list[i]);
        info->address = libusb_get_device_address(device_list[i]);
        info->vendor = desc.idVendor;
        info->product = desc.idProduct;
        info->stage = is_dfu ? TDFU_STAGE_DFU : (is_firmware ? TDFU_STAGE_FIRMWARE : TDFU_STAGE_BOOTROM);
        info->variant = TDFU_VARIANT_T31X;

        DEBUG_PRINT("Fast enumeration: found Ingenic device %d (VID:0x%04X, PID:0x%04X)\n", ingenic_count,
                    desc.idVendor, desc.idProduct);

        ingenic_count++;
    }

    libusb_free_device_list(device_list, 1);

    *count = ingenic_count;
    return TDFU_SUCCESS;
}

tdfu_error_t usb_manager_open_device(usb_manager_t *manager, const tdfu_device_info_t *info, usb_device_t **device) {

    if (!manager || !info || !device)
        return TDFU_ERROR_INVALID_PARAMETER;
    if (!manager->initialized)
        return TDFU_ERROR_INIT_FAILED;

    DEBUG_PRINT("Allocating device structure...\n");
    *device = (usb_device_t *)calloc(1, sizeof(usb_device_t));
    if (!*device)
        return TDFU_ERROR_MEMORY;

    DEBUG_PRINT("Setting device info and context...\n");
    (*device)->info = *info;
    (*device)->context = manager->context;
    DEBUG_PRINT("Manager device variant: %d (%s)\n", info->variant, tdfu_variant_to_string(info->variant));

    DEBUG_PRINT("Initializing device (bus=%d, addr=%d)...\n", info->bus, info->address);
    tdfu_error_t result = usb_device_init(*device, info->bus, info->address);
    if (result != TDFU_SUCCESS) {
        DEBUG_PRINT("Device init failed: %s\n", tdfu_error_to_string(result));
        free(*device);
        *device = NULL;
        return result;
    }

    DEBUG_PRINT("Device initialized successfully\n");
    return TDFU_SUCCESS;
}

void usb_manager_cleanup(usb_manager_t *manager) {
    if (manager && manager->initialized && manager->context) {
        libusb_exit(manager->context);
        manager->context = NULL;
        manager->initialized = false;
    }
}
