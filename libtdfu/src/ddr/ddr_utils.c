#include "ddr_utils.h"
#include "tdfu/tdfu.h"
#include <stdio.h>

// Convert nanoseconds to DDR clock cycles (rounds up)
uint32_t ddr_ns_to_cycles(uint32_t ns, uint32_t clock_mhz) {
    if (clock_mhz == 0)
        return 0;
    // Formula: cycles = (ns * clock_mhz + 1000 - 1) / 1000
    // Simplifies to: (ns + 1000/clock_mhz - 1) / (1000/clock_mhz)
    // But we use MHz directly: (ns * clock_mhz + 1000 - 1) / 1000
    return (ns * clock_mhz + 1000 - 1) / 1000;
}

// Get DDR type field for DDRP register
uint8_t ddr_get_phy_type_field(ddr_type_t type) {
    switch (type) {
    case DDR_TYPE_DDR2:
        return 3;
    case DDR_TYPE_DDR3:
        return 0;
    case DDR_TYPE_LPDDR:
        return 4;
    case DDR_TYPE_LPDDR2:
        return 4;
    case DDR_TYPE_LPDDR3:
        return 2;
    default:
        return 0;
    }
}

// Get DDR type field for DDRC register
uint8_t ddr_get_ctrl_type_field(ddr_type_t type) {
    switch (type) {
    case DDR_TYPE_DDR2:
        return 0x2; // 10 in binary (bits 2-3)
    case DDR_TYPE_DDR3:
        return 0x1; // 01 in binary
    case DDR_TYPE_LPDDR:
        return 0x3; // 11 in binary
    case DDR_TYPE_LPDDR2:
        return 0x3; // 11 in binary
    case DDR_TYPE_LPDDR3:
        return 0x3; // 11 in binary
    default:
        return 0;
    }
}

// Validate timing parameter is within bounds
int ddr_validate_timing(const char *param_name, uint32_t value, uint32_t min_val, uint32_t max_val) {
    if (value < min_val || value > max_val) {
        LOG_INFO("[ERROR] [DDR] %s out of bounds: %u (min:%u, max:%u)\n", param_name, value, min_val, max_val);
        return 0;
    }
    return 1;
}