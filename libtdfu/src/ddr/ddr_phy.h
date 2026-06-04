#ifndef DDR_PHY_H
#define DDR_PHY_H

#include <stdint.h>
#include "ddr_types.h"

// Generate DDRP (DDR PHY) register configuration
int ddr_generate_ddrp(const ddr_config_t *config, uint8_t *ddrp_regs);

// Internal: Generate DDRP using shared object buffer (for ddr_convert_param emulation)
int ddr_generate_ddrp_with_object(const ddr_config_t *config, uint8_t *obj_buffer, uint8_t *ddrp_regs);

#endif // DDR_PHY_H