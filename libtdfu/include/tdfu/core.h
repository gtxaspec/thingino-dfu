/**
 * tdfu Public API
 *
 * High-level interface to libtdfu, used by the CLI (local mode), the daemon,
 * and the web facade. All operations are synchronous.
 */

#ifndef TDFU_CORE_H
#define TDFU_CORE_H

#include "tdfu/types.h"

/**
 * Initialize the library. Must be called before any other function.
 * Returns TDFU_SUCCESS on success.
 */
tdfu_error_t tdfu_init(void);

/**
 * Shut down the library. Releases USB resources.
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
