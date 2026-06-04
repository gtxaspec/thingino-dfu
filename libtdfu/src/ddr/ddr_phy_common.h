#ifndef DDR_PHY_COMMON_H
#define DDR_PHY_COMMON_H

#include <stdint.h>

/**
 * Helper function: Convert nanoseconds to clock cycles (ceiling)
 * Formula: cycles = (ns * clock_mhz + 999) / 1000
 *
 * Uses uint64_t intermediate to prevent overflow for large ns values
 * (e.g., 200000000 ns * 400 MHz would overflow uint32_t).
 *
 * @param ns Timing value in nanoseconds
 * @param clock_mhz Clock frequency in MHz
 * @return Number of clock cycles (rounded up)
 */
static inline uint32_t ns2cycle_ceil(uint32_t ns, uint32_t clock_mhz) {
    if (clock_mhz == 0)
        return 0;

    // Use uint64_t to prevent overflow on large ns values
    uint64_t product = (uint64_t)ns * clock_mhz;
    return (uint32_t)((product + 999) / 1000);
}

/* Legacy alias — parameter was always ns despite the old name */
#define ps2cycle_ceil ns2cycle_ceil

/**
 * Impedance lookup table for output drivers
 * Used in DDR2/DDR3 PHY configuration
 */
typedef struct {
    uint32_t impedance; // Impedance value in ohms
    uint8_t code;       // Register encoding
} impedance_entry_t;

/**
 * Output impedance lookup table
 * Output impedance values from vendor tool
 */
static const impedance_entry_t out_impedance_table[] = {{27500, 0x00}, {30000, 0x01}, {34300, 0x02}, {40000, 0x03},
                                                        {48000, 0x04}, {60000, 0x05}, {80000, 0x06}, {120000, 0x07}};

/**
 * ODT (On-Die Termination) impedance lookup table
 * ODT impedance values from vendor tool
 */
static const impedance_entry_t odt_impedance_table[] = {{0, 0x00}, // Disabled
                                                        {50000, 0x01},  {75000, 0x02},  {100000, 0x03}, {120000, 0x04},
                                                        {150000, 0x05}, {200000, 0x06}, {240000, 0x07}, {300000, 0x08},
                                                        {400000, 0x09}, {600000, 0x0a}, {1200000, 0x0b}};

/**
 * Find nearest impedance value in lookup table
 *
 * @param table Impedance lookup table
 * @param table_size Number of entries in table
 * @param target_impedance Target impedance in ohms
 * @return Index of nearest entry
 */
static inline int find_nearest_impedance(const impedance_entry_t *table, int table_size, uint32_t target_impedance) {
    int best_idx = 0;
    uint32_t best_diff = UINT32_MAX;

    for (int i = 0; i < table_size; i++) {
        uint32_t diff;
        if (table[i].impedance > target_impedance) {
            diff = table[i].impedance - target_impedance;
        } else {
            diff = target_impedance - table[i].impedance;
        }

        if (diff < best_diff) {
            best_diff = diff;
            best_idx = i;
        }
    }

    return best_idx;
}

/**
 * DDR2/DDR3 PHY register offsets in object buffer
 * These are the offsets where type-specific algorithms write
 */
#define DDR_PHY_REG_WIDTH_CAS   0xd0 // Memory width + CAS latency
#define DDR_PHY_REG_TWR         0xd1 // Write recovery timing
#define DDR_PHY_REG_ODT1        0xd4 // ODT configuration 1
#define DDR_PHY_REG_ODT2        0xd5 // ODT configuration 2
#define DDR_PHY_REG_IMPEDANCE   0xe0 // Register impedance (DWORD)
#define DDR_PHY_REG_EXT_TIMING1 0xe8 // Extended timing field 1 (DWORD)
#define DDR_PHY_REG_EXT_TIMING2 0xea // Extended timing field 2 (WORD)
#define DDR_PHY_REG_EXT_TIMING3 0xec // Extended timing field 3 (DWORD)
#define DDR_PHY_REG_EXT_TIMING4 0xee // Extended timing field 4 (WORD)
#define DDR_PHY_REG_BASE_START  0xf0 // Base class registers start

/**
 * Input parameter offsets in object buffer
 * These are where the config parameters are stored
 */
#define DDR_PARAM_TYPE           0x154 // DDR type
#define DDR_PARAM_DATA_WIDTH     0x174 // Data width (4, 8, 16)
#define DDR_PARAM_CAS_LATENCY    0x16c // CAS latency
#define DDR_PARAM_CLOCK_MHZ      0x26c // Clock frequency
#define DDR_PARAM_EXT_BIT        0x27c // Extended timing bit
#define DDR_PARAM_ODT1           0x280 // ODT parameter 1
#define DDR_PARAM_ODT2           0x288 // ODT parameter 2
#define DDR_PARAM_ODT3           0x28c // ODT parameter 3
#define DDR_PARAM_ODT4           0x290 // ODT parameter 4
#define DDR_PARAM_TRL            0x1a4 // tRL timing
#define DDR_PARAM_TRFC           0x1b4 // tRFC timing
#define DDR_PARAM_TRCD_ALT       0x1b8 // Alternative tRCD
#define DDR_PARAM_TWR_ALT        0x1c8 // Alternative tWR
#define DDR_PARAM_TRP            0x1e0 // tRP timing
#define DDR_PARAM_IMPEDANCE_LOW  0x160 // Impedance field low
#define DDR_PARAM_IMPEDANCE_HIGH 0x164 // Impedance field high

#endif // DDR_PHY_COMMON_H
