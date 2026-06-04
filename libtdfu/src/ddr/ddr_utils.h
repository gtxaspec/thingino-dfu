#ifndef DDR_UTILS_H
#define DDR_UTILS_H

#include <stdint.h>
#include "ddr_types.h"

// Convert nanoseconds to DDR clock cycles (rounds up)
uint32_t ddr_ns_to_cycles(uint32_t ns, uint32_t clock_mhz);

// Get DDR type field for DDRP register
uint8_t ddr_get_phy_type_field(ddr_type_t type);

// Get DDR type field for DDRC register
uint8_t ddr_get_ctrl_type_field(ddr_type_t type);

// Validate timing parameter is within bounds
int ddr_validate_timing(const char *param_name, uint32_t value, uint32_t min_val, uint32_t max_val);

#endif // DDR_UTILS_H