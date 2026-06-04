#ifndef DDR_PHY_DDR2_H
#define DDR_PHY_DDR2_H

#include "ddr_types.h"
#include <stdint.h>

/**
 * Generate DDR2-specific DDRP (PHY) registers
 *
 * This implements the DDR2Param::ddrp_generate_register algorithm
 * derived from protocol analysis and reference binary comparison.
 *
 * @param config DDR configuration parameters
 * @param obj_buffer Intermediate object buffer (at least 0x200 bytes)
 * @param ddrp_regs Output DDRP register buffer (128 bytes)
 * @return 0 on success, -1 on error
 */
int ddr_generate_ddrp_ddr2(const ddr_config_t *config, uint8_t *obj_buffer, uint8_t *ddrp_regs);

#endif // DDR_PHY_DDR2_H
