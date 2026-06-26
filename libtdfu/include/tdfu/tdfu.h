#ifndef TDFU_H
#define TDFU_H

#include <stdint.h>
#include <libusb-1.0/libusb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include "platform_compat.h"

// ============================================================================
// GLOBAL DEBUG FLAG
// ============================================================================

extern bool g_debug_enabled;

// ============================================================================
// LOG HOOK - allows redirecting log output (e.g. daemon → TCP client)
// ============================================================================

typedef void (*tdfu_log_fn)(const char *msg, size_t len);
extern tdfu_log_fn g_tdfu_log_hook;

#ifdef __GNUC__
void tdfu_log_output(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
#else
void tdfu_log_output(const char *fmt, ...);
#endif

// Logging macros — route through hook when set, else stderr
#define LOG_DEBUG(fmt, ...)                                   \
    do {                                                      \
        if (g_debug_enabled)                                  \
            tdfu_log_output("[DEBUG] " fmt, ##__VA_ARGS__); \
    } while (0)

#define LOG_INFO(fmt, ...)  tdfu_log_output(fmt, ##__VA_ARGS__)
#define LOG_WARN(fmt, ...)  tdfu_log_output("[WARN] " fmt, ##__VA_ARGS__)
#define LOG_ERROR(fmt, ...) tdfu_log_output("[ERROR] " fmt, ##__VA_ARGS__)

// Legacy alias
#define DEBUG_PRINT LOG_DEBUG

// ============================================================================
// CONSTANTS
// ============================================================================

// USB Vendor IDs and Product IDs for Ingenic devices
#define VENDOR_ID_INGENIC     0x601A // X series VID (X1000/X1600/X2000...) - future DFU
#define VENDOR_ID_INGENIC_ALT 0xA108 // T/A/C series VID (T10-T41, A1) - current bootrom
#define PRODUCT_ID_BOOTROM    0x4770 // X series bootrom (601a:4770) - future
#define PRODUCT_ID_BOOTROM2   0xC309 // T/A/C series bootrom (a108:c309)
#define PRODUCT_ID_BOOTROM3   0x601A // Alternative bootrom ID
#define PRODUCT_ID_FIRMWARE   0x8887 // firmware-stage PID (vestigial; device keeps its bootrom VID:PID)
#define PRODUCT_ID_FIRMWARE2  0x601E // firmware-stage PID (vestigial)
#define PRODUCT_ID_DFU        0x4D44 // U-Boot DFU gadget (device already in DFU mode)

// Command codes - Bootrom stage (0x00-0x05)
#define VR_GET_CPU_INFO  0x00
#define VR_SET_DATA_ADDR 0x01
#define VR_SET_DATA_LEN  0x02
#define VR_FLUSH_CACHE   0x03
#define VR_PROG_STAGE1   0x04
#define VR_PROG_STAGE2   0x05

// USB vendor request types
#define REQUEST_TYPE_VENDOR 0xC0 // device-to-host vendor request
#define REQUEST_TYPE_OUT    0x40 // host-to-device vendor request

// Endpoints
#define ENDPOINT_IN  0x81 // Bulk IN
#define ENDPOINT_OUT 0x01 // Bulk OUT

// ============================================================================
// TYPE DEFINITIONS
// ============================================================================

// Processor variants
typedef enum tdfu_variant {
    TDFU_VARIANT_T10,
    TDFU_VARIANT_T20,
    TDFU_VARIANT_T21,
    TDFU_VARIANT_T23,
    TDFU_VARIANT_T30,
    TDFU_VARIANT_T31,
    TDFU_VARIANT_T31X,
    TDFU_VARIANT_T31ZX,
    TDFU_VARIANT_T31A, // T31A (DDR3, like A1)
    TDFU_VARIANT_A1,   // A1 (T31-based but with DDR3, different handling)
    TDFU_VARIANT_T40,
    TDFU_VARIANT_T41,
    TDFU_VARIANT_T32,
    TDFU_VARIANT_X1000,
    TDFU_VARIANT_X1600,
    TDFU_VARIANT_X1700,
    TDFU_VARIANT_X2000,
    TDFU_VARIANT_X2100,
    TDFU_VARIANT_X2600,
    TDFU_VARIANT_T31AL, // T31AL (DDR2, distinct from T31A which is DDR3)
    TDFU_VARIANT_T40XP, // T40XP (DDR3, dw32=1, different bootrom from T40NN)
    TDFU_VARIANT_T23DL, // T23DL (DDR2, 32MB, M14D2561616A)
    TDFU_VARIANT_T41_DDR3, // generic T41 DDR3 fallback (also T41ZN, indistinct from T40XP)
    // Per-chip T41 models (exact reporting). DDR2: T41L/T41LQ -> "t41" build.
    // DDR3: T41N/T41NQ/T41A/T41ZL/T41ZX -> "t41_ddr3" build (see dfu_variant_dir).
    TDFU_VARIANT_T41N,
    TDFU_VARIANT_T41NQ,
    TDFU_VARIANT_T41L,
    TDFU_VARIANT_T41LQ,
    TDFU_VARIANT_T41A,
    TDFU_VARIANT_T41ZL,
    TDFU_VARIANT_T41ZX,
    TDFU_VARIANT_T32_DDR3, // T32 DDR3 (T32NQ/VN/VX/XQ); base T32 = DDR2 (T32LQ). -> "t32_ddr3"
    // Per-variant loaders: 1:1 with firmware/dfu/<dir> + isvp_<v>_usbboot. The
    // detect signature (`soc -m` table) resolves these; dfu_variant_dir is 1:1.
    TDFU_VARIANT_T10N, TDFU_VARIANT_T10L,
    TDFU_VARIANT_T20N, TDFU_VARIANT_T20X, TDFU_VARIANT_T20L,
    TDFU_VARIANT_T21N, TDFU_VARIANT_T21HP,
    TDFU_VARIANT_T23N, TDFU_VARIANT_T23DN, TDFU_VARIANT_T23NHP,
    TDFU_VARIANT_T23NLP, TDFU_VARIANT_T23X, TDFU_VARIANT_T23ZN,
    TDFU_VARIANT_T30N, TDFU_VARIANT_T30X, TDFU_VARIANT_T30L, TDFU_VARIANT_T30A,
    TDFU_VARIANT_T31N, TDFU_VARIANT_T31L,
    TDFU_VARIANT_T32LQ, TDFU_VARIANT_T32NQ, TDFU_VARIANT_T32VN,
    TDFU_VARIANT_T32VNP, TDFU_VARIANT_T32VX, TDFU_VARIANT_T32XQ,
    TDFU_VARIANT_T33,
    TDFU_VARIANT_T40N,
    TDFU_VARIANT_A1N,
} tdfu_variant_t;
#define TDFU_VARIANT_DEFINED

// Device stages
typedef enum { TDFU_STAGE_BOOTROM, TDFU_STAGE_FIRMWARE, TDFU_STAGE_DFU } tdfu_stage_t;

// Error codes
typedef enum {
    TDFU_SUCCESS = 0,
    TDFU_ERROR_INIT_FAILED = -1,
    TDFU_ERROR_DEVICE_NOT_FOUND = -2,
    TDFU_ERROR_OPEN_FAILED = -3,
    TDFU_ERROR_TRANSFER_FAILED = -4,
    TDFU_ERROR_TIMEOUT = -5,
    TDFU_ERROR_INVALID_PARAMETER = -6,
    TDFU_ERROR_MEMORY = -7,
    TDFU_ERROR_FILE_IO = -8,
    TDFU_ERROR_PROTOCOL = -9,
    TDFU_ERROR_TRANSFER_TIMEOUT = -10
} tdfu_error_t;

// Device information structure
typedef struct {
    uint8_t bus;
    uint8_t address;
    uint16_t vendor;
    uint16_t product;
    tdfu_stage_t stage;
    tdfu_variant_t variant;
    uint8_t port_numbers[7]; // USB port path (physical), stable across the bootrom->DFU re-enum
    uint8_t port_depth;      // valid entries in port_numbers; 0 if unavailable
} tdfu_device_info_t;

// CPU information structure
typedef struct {
    uint8_t magic[8];    // "BOOT47XX" or similar
    uint8_t unknown[8];  // Additional info
    char clean_magic[9]; // Clean ASCII string for variant detection
    tdfu_stage_t stage;
} cpu_info_t;

// USB device structure
typedef struct {
    libusb_device_handle *handle;
    libusb_context *context;
    libusb_device *device;
    tdfu_device_info_t info;
    bool closed;
    bool stage1_consumed; // Set by protocol_detect_soc after STAGE1 execution
} usb_device_t;

// USB manager structure
typedef struct {
    libusb_context *context;
    bool initialized;
} usb_manager_t;

// ============================================================================
// FUNCTION DECLARATIONS
// ============================================================================

// Manager functions
tdfu_error_t usb_manager_init(usb_manager_t *manager);
tdfu_error_t usb_manager_find_devices(usb_manager_t *manager, tdfu_device_info_t **devices, int *count);
tdfu_error_t usb_manager_find_devices_fast(usb_manager_t *manager, tdfu_device_info_t **devices, int *count);
tdfu_error_t usb_manager_open_device(usb_manager_t *manager, const tdfu_device_info_t *info, usb_device_t **device);
void usb_manager_cleanup(usb_manager_t *manager);

// Device functions
tdfu_error_t usb_device_init(usb_device_t *device, uint8_t bus, uint8_t address);
tdfu_error_t usb_device_close(usb_device_t *device);
tdfu_error_t usb_device_reopen(usb_device_t *device);

tdfu_error_t usb_device_reset(usb_device_t *device);
tdfu_error_t usb_device_claim_interface(usb_device_t *device);
tdfu_error_t usb_device_release_interface(usb_device_t *device);
tdfu_error_t usb_device_get_cpu_info(usb_device_t *device, cpu_info_t *info);

// Transfer functions
tdfu_error_t usb_device_control_transfer(usb_device_t *device, uint8_t request_type, uint8_t request,
                                             uint16_t value, uint16_t index, uint8_t *data, uint16_t length,
                                             int *transferred);
tdfu_error_t usb_device_bulk_transfer(usb_device_t *device, uint8_t endpoint, uint8_t *data, int length,
                                          int *transferred, int timeout);
tdfu_error_t usb_device_interrupt_transfer(usb_device_t *device, uint8_t endpoint, uint8_t *data, int length,
                                               int *transferred, int timeout);
tdfu_error_t usb_device_vendor_request(usb_device_t *device, uint8_t request_type, uint8_t request, uint16_t value,
                                           uint16_t index, uint8_t *data, uint16_t length, uint8_t *response,
                                           int *response_length);

// Protocol functions (bootrom stage + SoC auto-detect)
tdfu_error_t protocol_set_data_address(usb_device_t *device, uint32_t addr);
tdfu_error_t protocol_set_data_length(usb_device_t *device, uint32_t length);
tdfu_error_t protocol_flush_cache(usb_device_t *device);
tdfu_error_t protocol_prog_stage1(usb_device_t *device, uint32_t addr);
tdfu_error_t protocol_prog_stage2(usb_device_t *device, uint32_t addr);
tdfu_error_t protocol_read_memory(usb_device_t *device, uint32_t addr, uint32_t len, uint8_t *out);
tdfu_error_t protocol_detect_soc(usb_device_t *device, tdfu_variant_t *variant);

// File helpers
tdfu_error_t load_file(const char *filename, uint8_t **data, size_t *size);
tdfu_error_t firmware_file_check_readable(const char *path);

// Bootrom data-upload helpers (used by the DFU bootstrap path)
tdfu_error_t bootstrap_load_data_to_memory(usb_device_t *device, const uint8_t *data, size_t size,
                                               uint32_t address);
tdfu_error_t bootstrap_transfer_data(usb_device_t *device, const uint8_t *data, size_t size);

// Utility functions
tdfu_variant_t detect_variant_from_magic(const char *magic);
const char *tdfu_variant_to_string(tdfu_variant_t variant);
tdfu_variant_t tdfu_variant_from_string(const char *str);
const char *tdfu_stage_to_string(tdfu_stage_t stage);
const char *tdfu_error_to_string(tdfu_error_t error);

#endif // TDFU_H
