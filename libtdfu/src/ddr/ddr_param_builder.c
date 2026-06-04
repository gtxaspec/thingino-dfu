#include "ddr_param_builder.h"
#include "ddr_types.h"
#include "tdfu/tdfu.h"
#include <stdio.h>

/**
 * Reference DDR Configurations extracted from vendor binary
 * Generated from vendor tool reference binary analysis
 *
 * These values are extracted from reference binary files 8-16 and vendor tool analysis:
 * - DDRC[0x7c] = memory address remap low
 * - DDRC[0x80] = DDR controller timing base
 * - DDRC[0x90] = memory configuration low (rows/columns)
 * - DDRC[0x94] = memory configuration high (banks/refresh)
 */

// T31X Configuration (Reference: ddr_extracted.bin, files 8-16)
// Standard DDR2 configuration, 512MB, 400MHz
static const ddr_txx_chip_config_t t31x_config = {
    .chip_type = CHIP_T31X,
    .ddr_type = DDR_TYPE_DDR2,

    // DDRC output values from reference binary
    .ddrc_0x7c = 0x000000b8, // Address remap
    .ddrc_0x80 = 0x016e3600, // Timing base (derived from tCKE, tXP, tREFI)
    .ddrc_0x90 = 0x22551000, // Memory config: row/col configuration
    .ddrc_0x94 = 0x17d78400, // Memory config: bank/refresh configuration

    // Memory configuration
    .dram_width = 8, // 32-bit data width
    .chip_count = 1, // Single chip
    .banks = 8,      // 8 banks

    // Frequency
    .ddr_freq = 400000000, // 400 MHz

    // Timing parameters (in picoseconds)
    // Converted from tCO/tDL specs for M14D5121632A DDR2
    .tWR = 15000,     // Write recovery: 15ns
    .tRL = 12000,     // Read latency: 12ns (CL=3)
    .tRP = 12000,     // Precharge: 12ns
    .tRCD = 12000,    // RAS-to-CAS: 12ns
    .tRAS = 42000,    // Row active: 42ns
    .tRC = 54000,     // Row cycle: 54ns
    .tRRD = 10000,    // Row to row: 10ns
    .tCKE = 3000,     // CKE setup: 3ns
    .tXP = 2000,      // Exit power-down: 2ns
    .tRFC = 70000,    // Refresh cycle: 70ns
    .tREFI = 7800000, // Refresh interval: 7.8us
};

// T31NL Configuration (Reference: configs/t31nl, same DDR2 as T31X)
// Likely identical to T31X for DDR generation
static const ddr_txx_chip_config_t t31nl_config = {
    .chip_type = CHIP_T31L,
    .ddr_type = DDR_TYPE_DDR2,

    // Should match T31X unless vendor has variant
    .ddrc_0x7c = 0x000000b8,
    .ddrc_0x80 = 0x016e3600,
    .ddrc_0x90 = 0x22551000,
    .ddrc_0x94 = 0x17d78400,

    .dram_width = 8,
    .chip_count = 1,
    .banks = 8,

    .ddr_freq = 400000000,

    .tWR = 15000,
    .tRL = 12000,
    .tRP = 12000,
    .tRCD = 12000,
    .tRAS = 42000,
    .tRC = 54000,
    .tRRD = 10000,
    .tCKE = 3000,
    .tXP = 2000,
    .tRFC = 70000,
    .tREFI = 7800000,
};

// T23N Configuration (Reference: configs/t23, same DDR2 as T31X)
// Likely identical to T31X for DDR generation
static const ddr_txx_chip_config_t t23n_config = {
    .chip_type = CHIP_T23N,
    .ddr_type = DDR_TYPE_DDR2,

    // Should match T31X unless vendor has variant
    .ddrc_0x7c = 0x000000b8,
    .ddrc_0x80 = 0x016e3600,
    .ddrc_0x90 = 0x22551000,
    .ddrc_0x94 = 0x17d78400,

    .dram_width = 8,
    .chip_count = 1,
    .banks = 8,

    .ddr_freq = 400000000,

    .tWR = 15000,
    .tRL = 12000,
    .tRP = 12000,
    .tRCD = 12000,
    .tRAS = 42000,
    .tRC = 54000,
    .tRRD = 10000,
    .tCKE = 3000,
    .tXP = 2000,
    .tRFC = 70000,
    .tREFI = 7800000,
};

int ddr_get_chip_config(chip_type_t chip, ddr_txx_chip_config_t *config) {
    if (!config)
        return -1;

    switch (chip) {
    case CHIP_T31X:
        memcpy(config, &t31x_config, sizeof(ddr_txx_chip_config_t));
        return 0;
    case CHIP_T31L:
        memcpy(config, &t31nl_config, sizeof(ddr_txx_chip_config_t));
        return 0;
    case CHIP_T23N:
        memcpy(config, &t23n_config, sizeof(ddr_txx_chip_config_t));
        return 0;
    default:
        LOG_ERROR("Unsupported chip type: 0x%x\n", chip);
        return -1;
    }
}

int ddr_build_params(const ddr_txx_chip_config_t *config, ddr_params_t *params) {
    if (!config || !params)
        return -1;

    // Initialize parameter structure
    memset(params, 0, sizeof(ddr_params_t));

    // Set DDR type and memory configuration
    params->ddr_type = config->ddr_type;
    params->dram_width = config->dram_width;
    params->chip_count = config->chip_count;
    params->banks = config->banks;

    // Set timing parameters (in picoseconds)
    params->tWR = config->tWR;
    params->tRL = config->tRL;
    params->tRP = config->tRP;
    params->tRCD = config->tRCD;
    params->tRAS = config->tRAS;
    params->tRC = config->tRC;
    params->tRRD = config->tRRD;
    params->tREFI = config->tREFI;

    // Set memory sizes (512MB typical for these chips)
    params->row_size = 512 * 1024 * 1024; // Total 512MB
    params->col_size = 512 * 1024 * 1024; // Per-chip size

    return 0;
}

/**
 * Print DDR configuration for debugging
 */
void ddr_print_config(const ddr_txx_chip_config_t *config) {
    if (!config)
        return;

    LOG_INFO("\n=== DDR Configuration ===\n");
    LOG_INFO("Chip Type: 0x%02x\n", config->chip_type);
    LOG_INFO("DDR Type: %u\n", config->ddr_type);
    LOG_INFO("Frequency: %u MHz\n", config->ddr_freq / 1000000);
    LOG_INFO("Data Width: %u-bit\n", config->dram_width * 4);
    LOG_INFO("Chip Count: %u\n", config->chip_count);
    LOG_INFO("Banks: %u\n", config->banks);
    LOG_INFO("\nDDRC Values:\n");
    LOG_INFO("  0x7c = 0x%08x\n", config->ddrc_0x7c);
    LOG_INFO("  0x80 = 0x%08x\n", config->ddrc_0x80);
    LOG_INFO("  0x90 = 0x%08x\n", config->ddrc_0x90);
    LOG_INFO("  0x94 = 0x%08x\n", config->ddrc_0x94);
    LOG_INFO("\nTiming (ps):\n");
    LOG_INFO("  tWR:  %u\n", config->tWR);
    LOG_INFO("  tRL:  %u\n", config->tRL);
    LOG_INFO("  tRP:  %u\n", config->tRP);
    LOG_INFO("  tRCD: %u\n", config->tRCD);
    LOG_INFO("  tRAS: %u\n", config->tRAS);
    LOG_INFO("  tRC:  %u\n", config->tRC);
    LOG_INFO("  tRRD: %u\n", config->tRRD);
    LOG_INFO("  tREFI: %u\n", config->tREFI);
}