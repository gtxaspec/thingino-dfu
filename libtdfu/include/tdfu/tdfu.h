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
// FORWARD DECLARATIONS
// ============================================================================

// Forward declare firmware_binary_t (defined in firmware/firmware_database.h)
typedef struct {
    const char *processor;
    const uint8_t *spl_data;
    size_t spl_size;
    const uint8_t *uboot_data;
    size_t uboot_size;
} firmware_binary_t;

// ============================================================================
// CONSTANTS
// ============================================================================

// USB Vendor IDs and Product IDs for Ingenic devices
#define VENDOR_ID_INGENIC     0x601A // X series VID (X1000/X1600/X2000...) - future DFU
#define VENDOR_ID_INGENIC_ALT 0xA108 // T/A/C series VID (T10-T41, A1) - current bootrom
#define PRODUCT_ID_BOOTROM    0x4770 // X series bootrom (601a:4770) - future
#define PRODUCT_ID_BOOTROM2   0xC309 // T/A/C series bootrom (a108:c309)
#define PRODUCT_ID_BOOTROM3   0x601A // Alternative bootrom ID
#define PRODUCT_ID_FIRMWARE   0x8887 // firmware-stage PID (vestigial - not seen in production; the stage is read via VR_GET_CPU_INFO while the device keeps its bootrom VID:PID)
#define PRODUCT_ID_FIRMWARE2  0x601E // firmware-stage PID (vestigial)
#define PRODUCT_ID_DFU        0x4D44 // U-Boot DFU gadget (device already in DFU mode)

// Command codes - Bootrom stage (0x00-0x05)
#define VR_GET_CPU_INFO  0x00
#define VR_SET_DATA_ADDR 0x01
#define VR_SET_DATA_LEN  0x02
#define VR_FLUSH_CACHE   0x03
#define VR_PROG_STAGE1   0x04
#define VR_PROG_STAGE2   0x05

// Firmware stage commands (0x10-0x26)
#define VR_FW_READ         0x10
#define VR_FW_HANDSHAKE    0x11
#define VR_FW_WRITE2       0x14
#define VR_REBOOT          0x16 /* Also used as FW_READ_STATUS1 in some contexts */
#define VR_FW_READ_STATUS2 0x19
#define VR_FW_READ_STATUS3 0x25
#define VR_FW_READ_STATUS4 0x26

// Traditional firmware operations
#define VR_WRITE 0x12
#define VR_READ  0x13 /* Also used as FW_WRITE1 (sends a read/write command) */

// NAND operations (available in bootloader)
#define VR_NAND_OPS          0x07
#define NAND_OPERATION_READ  0x05 // NAND read subcommand
#define NAND_OPERATION_WRITE 0x06 // NAND write subcommand

// USB Configuration constants
#define DEFAULT_BUFFER_SIZE (1024 * 1024) // 1MB default buffer
#define REQUEST_TYPE_VENDOR 0xC0          // USB vendor request type for device-to-host
#define REQUEST_TYPE_OUT    0x40          // USB vendor request type for host-to-device

// Bootstrap constants
#define BOOTLOADER_ADDRESS_SDRAM   0x80000000
#define BOOTSTRAP_TIMEOUT_SECONDS  30
#define BOOTSTRAP_POLL_INTERVAL_MS 500
#define CRC32_POLYNOMIAL           0xEDB88320
#define CRC32_INITIAL              0xFFFFFFFF

// Endpoints
#define ENDPOINT_IN  0x81 // Bulk IN
#define ENDPOINT_OUT 0x01 // Bulk OUT

// Error codes
#define ACK_SUCCESS 0x00
#define ACK_ERROR   0x01

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
} tdfu_device_info_t;

// CPU information structure
typedef struct {
    uint8_t magic[8];    // "BOOT47XX" or similar
    uint8_t unknown[8];  // Additional info
    char clean_magic[9]; // Clean ASCII string for variant detection
    tdfu_stage_t stage;
} cpu_info_t;

// Write command structure
typedef struct {
    uint32_t partition;
    uint32_t offset;
    uint32_t length;
    uint32_t crc32;
} write_command_t;

// Read command structure
typedef struct {
    uint32_t partition;
    uint32_t offset;
    uint32_t length;
} read_command_t;

// Flash memory bank structure
typedef struct {
    uint32_t offset;
    uint32_t size;
    char label[16];
    bool enabled;
} flash_bank_t;

// Firmware read configuration
typedef struct {
    uint32_t total_size;
    int bank_count;
    flash_bank_t *banks;
    uint32_t block_size;
} firmware_read_config_t;

// Firmware files structure
typedef struct {
    uint8_t *config;
    size_t config_size;
    uint8_t *spl;
    size_t spl_size;
    uint8_t *uboot;
    size_t uboot_size;
} firmware_files_t;

// Bootstrap configuration
typedef struct {
    uint32_t sdram_address;
    int timeout;
    bool verbose;
    bool skip_ddr;
    const char *config_file;  // Custom DDR config file path (NULL = use default)
    const char *spl_file;     // Custom SPL file path (NULL = use default)
    const char *uboot_file;   // Custom U-Boot file path (NULL = use default)
    const char *firmware_dir; // Firmware root directory (NULL = "./firmware")
} bootstrap_config_t;

// Bootstrap progress
typedef struct {
    char stage[32];
    int current;
    int total;
    char description[128];
} bootstrap_progress_t;

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

// Protocol functions
tdfu_error_t protocol_set_data_address(usb_device_t *device, uint32_t addr);
tdfu_error_t protocol_set_data_length(usb_device_t *device, uint32_t length);
tdfu_error_t protocol_flush_cache(usb_device_t *device);
tdfu_error_t protocol_read_status(usb_device_t *device, uint8_t *status_buffer, int buffer_size, int *status_len);
tdfu_error_t protocol_prog_stage1(usb_device_t *device, uint32_t addr);
tdfu_error_t protocol_prog_stage2(usb_device_t *device, uint32_t addr);
tdfu_error_t protocol_get_ack(usb_device_t *device, int32_t *status);
tdfu_error_t protocol_init(usb_device_t *device);
tdfu_error_t protocol_read_memory(usb_device_t *device, uint32_t addr, uint32_t len, uint8_t *out);
tdfu_error_t protocol_detect_soc(usb_device_t *device, tdfu_variant_t *variant);
tdfu_error_t protocol_nand_read(usb_device_t *device, uint32_t offset, uint32_t size, uint8_t **data,
                                    int *transferred);

// Firmware functions
tdfu_error_t firmware_load(tdfu_variant_t variant, firmware_files_t *firmware);
tdfu_error_t firmware_load_from_dir(tdfu_variant_t variant, const char *firmware_dir,
                                        firmware_files_t *firmware);
tdfu_error_t firmware_load_from_files(tdfu_variant_t variant, const char *config_file, const char *spl_file,
                                          const char *uboot_file, firmware_files_t *firmware);
void firmware_cleanup(firmware_files_t *firmware);
tdfu_error_t load_file(const char *filename, uint8_t **data, size_t *size);
tdfu_error_t firmware_file_check_readable(const char *path);
tdfu_error_t firmware_validate(const firmware_files_t *firmware);

// DDR functions
tdfu_error_t ddr_validate_binary(const uint8_t *data, size_t size);

// Bootstrap functions
tdfu_error_t bootstrap_device(usb_device_t *device, const bootstrap_config_t *config);
tdfu_error_t bootstrap_ensure_bootstrapped(usb_device_t *device, const bootstrap_config_t *config);
tdfu_error_t bootstrap_load_data_to_memory(usb_device_t *device, const uint8_t *data, size_t size,
                                               uint32_t address);
tdfu_error_t bootstrap_program_stage2(usb_device_t *device, const uint8_t *data, size_t size);
tdfu_error_t bootstrap_transfer_data(usb_device_t *device, const uint8_t *data, size_t size);

// Additional protocol functions
tdfu_error_t protocol_fw_read(usb_device_t *device, int data_len, uint8_t **data, int *actual_len);
tdfu_error_t protocol_fw_handshake(usb_device_t *device);
tdfu_error_t protocol_fw_write_chunk1(usb_device_t *device, const uint8_t *data);
tdfu_error_t protocol_fw_write_chunk2(usb_device_t *device, const uint8_t *data);
tdfu_error_t protocol_traditional_read(usb_device_t *device, int data_len, uint8_t **data, int *actual_len);
tdfu_error_t protocol_fw_read_operation(usb_device_t *device, uint32_t offset, uint32_t length, uint8_t **data,
                                            int *actual_len);
tdfu_error_t protocol_fw_read_status(usb_device_t *device, int status_cmd, uint32_t *status);
tdfu_error_t protocol_vendor_style_read(usb_device_t *device, uint32_t offset, uint32_t size, uint8_t **data,
                                            int *actual_len);

// Proper bootloader protocol functions (using code execution pattern)
tdfu_error_t protocol_load_and_execute_code(usb_device_t *device, uint32_t ram_address, const uint8_t *code,
                                                uint32_t code_size);
tdfu_error_t protocol_proper_firmware_read(usb_device_t *device, uint32_t flash_offset, uint32_t read_size,
                                               uint8_t **out_data, int *out_len);
tdfu_error_t protocol_proper_firmware_write(usb_device_t *device, uint32_t flash_offset, const uint8_t *data,
                                                uint32_t data_size);

// Firmware read functions
tdfu_error_t firmware_read_full(usb_device_t *device, uint32_t read_size, uint8_t **data, uint32_t *actual_size);

// Firmware handshake protocol functions (40-byte chunk transfers)
tdfu_error_t firmware_handshake_read_chunk(usb_device_t *device, uint32_t chunk_index, uint32_t chunk_offset,
                                               uint32_t chunk_size, uint32_t total_size, uint8_t **out_data,
                                               int *out_len);
tdfu_error_t firmware_handshake_write_chunk(usb_device_t *device, uint32_t chunk_index, uint32_t chunk_offset,
                                                const uint8_t *data, uint32_t data_size);
tdfu_error_t firmware_handshake_write_chunk_a1(usb_device_t *device, uint32_t chunk_index, uint32_t chunk_offset,
                                                   const uint8_t *data, uint32_t data_size);
tdfu_error_t firmware_handshake_write_chunk_vendor(usb_device_t *device, uint32_t chunk_index,
                                                       uint32_t chunk_offset, const uint8_t *data, uint32_t data_size);
tdfu_error_t firmware_handshake_init(usb_device_t *device);

// Firmware writer functions
tdfu_error_t write_firmware_to_device(usb_device_t *device, const char *firmware_file,
                                          const firmware_binary_t *fw_binary, bool no_erase, bool is_a1_board,
                                          uint32_t chunk_size);
tdfu_error_t send_bulk_data(usb_device_t *device, uint8_t endpoint, const uint8_t *data, uint32_t size);

// Utility functions (additional)
tdfu_variant_t detect_variant_from_magic(const char *magic);

// Utility functions
uint32_t calculate_crc32(const uint8_t *data, size_t length);
const char *tdfu_variant_to_string(tdfu_variant_t variant);
tdfu_variant_t tdfu_variant_from_string(const char *str);
const char *tdfu_stage_to_string(tdfu_stage_t stage);
const char *tdfu_error_to_string(tdfu_error_t error);

// High-level device operations (libtdfu/src/operations.c)
const char *tdfu_get_last_detected_variant(void);
tdfu_error_t tdfu_op_bootstrap(usb_manager_t *manager, int index, const char *force_cpu, bool verbose,
                                     bool skip_ddr, const char *config_file, const char *spl_file,
                                     const char *uboot_file, const char *firmware_dir);

tdfu_error_t tdfu_op_read_firmware(usb_manager_t *manager, int index, const char *output_file,
                                         const char *force_cpu, const char *flash_chip_name);

tdfu_error_t tdfu_op_write_firmware(usb_manager_t *manager, int device_index, const char *firmware_file,
                                          const char *force_cpu, const char *flash_chip_name, bool no_erase,
                                          bool reboot_after, bool do_bootstrap, bool verbose, bool skip_ddr,
                                          const char *config_file, const char *spl_file, const char *uboot_file,
                                          const char *firmware_dir, uint32_t chunk_size);

#endif // TDFU_H