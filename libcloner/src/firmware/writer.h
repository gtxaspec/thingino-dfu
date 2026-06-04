/**
 * Firmware Writer Header
 *
 * Provides firmware writing functionality for Ingenic SoC devices.
 */

#ifndef FIRMWARE_WRITER_H
#define FIRMWARE_WRITER_H

#include <stdint.h>
#include "../usb/device.h"
#include "thingino.h"

/**
 * Write firmware to device
 *
 * @param device USB device handle
 * @param firmware_file Path to firmware file
 * @param fw_binary Firmware binary configuration for the target SoC
 * @param force_erase Force erase flag (currently unused)
 * @param is_a1_board True if device is an A1 board (uses 1MB chunks)
 * @return TDFU_SUCCESS on success, error code otherwise
 */
tdfu_error_t write_firmware_to_device(usb_device_t *device, const char *firmware_file,
                                          const firmware_binary_t *fw_binary, bool no_erase, bool is_a1_board,
                                          uint32_t chunk_size);

/**
 * Send bulk data to device
 *
 * @param device USB device handle
 * @param endpoint USB endpoint address
 * @param data Data to send
 * @param size Size of data in bytes
 * @return TDFU_SUCCESS on success, error code otherwise
 */
tdfu_error_t send_bulk_data(usb_device_t *device, uint8_t endpoint, const uint8_t *data, uint32_t size);

#endif // FIRMWARE_WRITER_H
