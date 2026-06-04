/**
 * Test DDR Integration - Verify DDR generation works with new binary builder
 */

#include "ddr_binary_builder.h"
#include "cloner/thingino.h"
#include "cloner/constants.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void) {
    printf("=== DDR Integration Test ===\n\n");

    // Test T31X variant
    printf("Testing T31X DDR generation...\n");

    platform_config_t platform_cfg;
    if (ddr_get_platform_config_by_variant(TDFU_VARIANT_T31X, &platform_cfg) != 0) {
        printf("[FAIL] Failed to get platform config\n");
        return 1;
    }

    printf("[OK] Platform config retrieved\n");
    printf("  Crystal: %u Hz\n", platform_cfg.crystal_freq);
    printf("  CPU: %u Hz\n", platform_cfg.cpu_freq);
    printf("  DDR: %u Hz\n", platform_cfg.ddr_freq);
    printf("  UART: %u baud\n", platform_cfg.uart_baud);
    printf("  Memory: %u bytes\n", platform_cfg.mem_size);

    // Create DDR PHY parameters (M14D1G1664A DDR2 @ 400MHz)
    ddr_phy_params_t phy_params = {
        .ddr_type = 1,      // DDR2
        .row_bits = 13,
        .col_bits = 10,
        .cl = 7,
        .bl = 8,
        .tRAS = 18,
        .tRC = 23,
        .tRCD = 6,
        .tRP = 6,
        .tRFC = 52,
        .tRTP = 3,
        .tFAW = 18,
        .tRRD = 4,
        .tWTR = 3
    };

    // Generate DDR binary
    uint8_t *ddr_binary = (uint8_t*)malloc(DDR_BINARY_SIZE_MAX);
    if (!ddr_binary) {
        printf("[FAIL] Failed to allocate memory\n");
        return 1;
    }

    size_t result = ddr_build_binary(&platform_cfg, &phy_params, ddr_binary);
    if (result == 0) {
        printf("[FAIL] Failed to build DDR binary (returned 0)\n");
        free(ddr_binary);
        return 1;
    }

    printf("[OK] DDR binary generated: %zu bytes\n", result);

    // Verify FIDB header
    if (memcmp(ddr_binary, "FIDB", 4) == 0) {
        printf("[OK] FIDB header found\n");
    } else {
        printf("[FAIL] FIDB header not found\n");
        free(ddr_binary);
        return 1;
    }

    // Verify RDD header (at offset 0xC0 = 192)
    if (ddr_binary[0xC1] == 'R' && ddr_binary[0xC2] == 'D' && ddr_binary[0xC3] == 'D') {
        printf("[OK] RDD header found at offset 0xC0\n");
    } else {
        printf("[FAIL] RDD header not found at offset 0xC0\n");
        free(ddr_binary);
        return 1;
    }

    // Display first 16 bytes
    printf("\nFirst 16 bytes:\n");
    for (int i = 0; i < 16; i++) {
        printf("%02x ", ddr_binary[i]);
        if ((i + 1) % 8 == 0) printf("\n");
    }

    // Display RDD section start (offset 0xC0)
    printf("\nRDD section start (offset 0xC0):\n");
    for (int i = 0xC0; i < 0xD0; i++) {
        printf("%02x ", ddr_binary[i]);
        if ((i + 1) % 8 == 0) printf("\n");
    }

    free(ddr_binary);
    printf("\n[SUCCESS] DDR integration test passed!\n");
    return 0;
}

