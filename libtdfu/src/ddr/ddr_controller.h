#ifndef DDR_CONTROLLER_H
#define DDR_CONTROLLER_H

#include <stdint.h>
#include "ddr_types.h"

// Generate DDRC (DDR Controller) register configuration
int ddr_generate_ddrc(const ddr_config_t *config, uint8_t *ddrc_regs);

// Internal: Generate DDRC using shared object buffer (for ddr_convert_param emulation)
int ddr_generate_ddrc_with_object(const ddr_config_t *config, uint8_t *obj_buffer, uint8_t *ddrc_regs);

// Helper: Initialize object buffer with config values at vendor source offsets
void ddr_init_object_buffer(const ddr_config_t *config, uint8_t *obj_buffer);

#endif // DDR_CONTROLLER_H