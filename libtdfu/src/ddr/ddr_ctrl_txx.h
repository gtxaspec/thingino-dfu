#ifndef DDR_CTRL_TXX_H
#define DDR_CTRL_TXX_H

#include <stdint.h>
#include "ddr_types.h"

/**
 * Generate TXX-specific DDRC hardware registers for DDR2
 * Populates obj[0x7c-0xcc] with DDRC hardware register values
 *
 * @param config DDR configuration
 * @param obj_buffer Object buffer (must be initialized with input parameters at obj[0x154+])
 * @return 0 on success, -1 on error
 */
int ddr_generate_ddrc_txx_ddr2(const ddr_config_t *config, uint8_t *obj_buffer);

/**
 * Generate TXX-specific DDRC timing registers for DDR2
 * Populates obj[0xac-0xc4] with DDRC timing values
 * Called internally by ddr_generate_ddrc_txx_ddr2
 *
 * @param config DDR configuration
 * @param obj_buffer Object buffer
 * @return 0 on success, -1 on error
 */
int ddr_generate_ddrc_timing_txx_ddr2(const ddr_config_t *config, uint8_t *obj_buffer);

#endif // DDR_CTRL_TXX_H
