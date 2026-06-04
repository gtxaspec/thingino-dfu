/**
 * tdfu Public API
 *
 * High-level interface to libtdfu. Used by CLI (local mode)
 * and daemon. All operations are synchronous with optional
 * progress callbacks.
 */

#ifndef TDFU_CORE_H
#define TDFU_CORE_H

#include "tdfu/types.h"

/**
 * Initialize the cloner library. Must be called before any other function.
 * Returns TDFU_SUCCESS on success.
 */
tdfu_error_t tdfu_init(void);

/**
 * Shut down the cloner library. Releases USB resources.
 */
void tdfu_cleanup(void);

/**
 * Discover connected Ingenic USB devices.
 * Caller must free the returned list with tdfu_free_device_list().
 */
tdfu_error_t tdfu_discover_devices(tdfu_device_list_t *list);

/**
 * Free a device list returned by tdfu_discover_devices().
 */
void tdfu_free_device_list(tdfu_device_list_t *list);

/**
 * Bootstrap a device: load DDR config, SPL, and U-Boot.
 *
 * @param device_index  Index in most recent discovery result
 * @param variant       Processor variant (determines firmware paths)
 * @param firmware_dir  Firmware root directory, contains cloner/<platform>/
 *                      (NULL = "./firmware")
 * @param progress      Optional progress callback (NULL = no progress)
 * @param user_data     Passed to progress callback
 */
tdfu_error_t tdfu_bootstrap(int device_index, tdfu_variant_t variant, const char *firmware_dir,
                                tdfu_progress_cb progress, void *user_data);

/**
 * Bootstrap with pre-loaded firmware binaries (for remote/daemon use).
 * The caller provides the DDR, SPL, and U-Boot data directly.
 */
tdfu_error_t tdfu_bootstrap_with_data(int device_index, tdfu_variant_t variant, const uint8_t *ddr,
                                          size_t ddr_len, const uint8_t *spl, size_t spl_len, const uint8_t *uboot,
                                          size_t uboot_len, tdfu_progress_cb progress, void *user_data);

/**
 * Write firmware to a device's flash.
 *
 * @param device_index  Index in most recent discovery result
 * @param firmware      Firmware binary data
 * @param len           Length of firmware data
 * @param progress      Optional progress callback
 * @param user_data     Passed to progress callback
 */
tdfu_error_t tdfu_write_firmware(int device_index, const uint8_t *firmware, size_t len, tdfu_progress_cb progress,
                                     void *user_data);

/**
 * Read firmware from a device's flash.
 * Caller must free *firmware with free().
 *
 * @param device_index  Index in most recent discovery result
 * @param firmware      Output: allocated firmware data
 * @param len           Output: length of firmware data
 * @param progress      Optional progress callback
 * @param user_data     Passed to progress callback
 */
tdfu_error_t tdfu_read_firmware(int device_index, uint8_t **firmware, size_t *len, tdfu_progress_cb progress,
                                    void *user_data);

/**
 * Look up a DDR chip by name from the compiled-in database.
 * Returns internal pointer (do not free). Opaque to consumers
 * that don't include ddr_config_database.h.
 */
const void *tdfu_find_ddr_chip(const char *name);

/**
 * Generate a DDR configuration binary for a processor.
 *
 * @param processor_name  Processor name (e.g., "t31x", "a1")
 * @param out             Output buffer (must be at least 324 bytes)
 * @param out_len         Output: actual bytes written
 */
tdfu_error_t tdfu_generate_ddr(const char *processor_name, uint8_t *out, size_t *out_len);

/**
 * Override the variant for a discovered device.
 * Use after bootstrap when re-enumeration loses the original variant.
 */
tdfu_error_t tdfu_set_device_variant(int device_index, tdfu_variant_t variant);

/**
 * Variant name conversion utilities.
 */
const char *tdfu_variant_to_string(tdfu_variant_t variant);
tdfu_variant_t tdfu_variant_from_string(const char *name);

#endif /* TDFU_CORE_H */
