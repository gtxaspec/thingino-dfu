/**
 * Public API shared types.
 *
 * The canonical tdfu_error_t / tdfu_variant_t / tdfu_stage_t /
 * tdfu_device_info_t are defined in thingino.h. This header adds the
 * discovery list, progress callback, and opaque handle used at the
 * public API boundary.
 */

#ifndef TDFU_TYPES_H
#define TDFU_TYPES_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "thingino.h"

/* Device list (returned by discovery) */
typedef struct {
    tdfu_device_info_t *devices;
    int count;
} tdfu_device_list_t;

/* Progress callback */
typedef void (*tdfu_progress_cb)(int percent, const char *stage, const char *message, void *user_data);

/* Opaque device handle */
typedef struct tdfu_device tdfu_device_t;

#endif /* TDFU_TYPES_H */
