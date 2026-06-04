#include "ddr_generator.h"
#include "ddr_controller.h"
#include "ddr_phy.h"
#include "tdfu/tdfu.h"
#include <string.h>
#include <stdio.h>

// Internal: Generate DDR registers using shared object buffer
// The vendor tool uses a DDRBaseParam object that both DDRC and DDRP generators populate
static int ddr_generate_with_shared_object(const ddr_config_t *config, uint8_t *ddrc_regs, uint8_t *ddrp_regs) {
    // Create a shared object buffer (0x300 bytes to be safe)
    // This mimics the DDRBaseParam object structure from vendor tool
    uint8_t obj_buffer[0x300];
    memset(obj_buffer, 0, sizeof(obj_buffer));

    // Initialize object buffer with config values at vendor source offsets
    ddr_init_object_buffer(config, obj_buffer);

    // Both DDRC and DDRP generators populate this object buffer
    // Then ddr_convert_param copies values from object to output

    int result = ddr_generate_ddrc_with_object(config, obj_buffer, ddrc_regs);
    if (result < 0) {
        return -1;
    }

    result = ddr_generate_ddrp_with_object(config, obj_buffer, ddrp_regs);
    if (result < 0) {
        return -1;
    }

    return 0;
}

// Generate complete DDR binary from configuration
int ddr_generate_binary(const ddr_config_t *config, uint8_t *output, size_t output_size) {
    if (output_size < 324) {
        LOG_INFO("[ERROR] Output buffer too small (need 324 bytes, got %zu)\n", output_size);
        return -1;
    }

    // Create binary structure
    ddr_binary_t *binary = (ddr_binary_t *)output;

    // Add FIDB marker and size
    memcpy(binary->fidb_sig, "FIDB", 4);
    binary->fidb_size = 0xb8; // 184 bytes

    // Generate both DDRC and DDRP using shared object buffer
    if (ddr_generate_with_shared_object(config, binary->ddrc, binary->ddrp) < 0) {
        LOG_INFO("[ERROR] Failed to generate DDR registers\n");
        return -1;
    }

    // Add RDD marker and size (stored as uint32_t: "\0RDD" = 0x44445200 in little-endian)
    binary->rdd_sig = 0x44445200;
    binary->rdd_size = 0x7c; // 124 bytes

    LOG_INFO("[DDR] Generated binary: 324 bytes\n");
    return 0; // Success
}

// Generate DDR binary and compare with reference
int ddr_test_against_reference(const ddr_config_t *config, const uint8_t *reference, size_t ref_size) {
    uint8_t generated[324];

    if (ref_size != 324) {
        LOG_INFO("[ERROR] Reference binary wrong size: %zu (expected 324)\n", ref_size);
        return -1;
    }

    // Generate binary
    if (ddr_generate_binary(config, generated, 324) < 0) {
        return -1;
    }

    // Compare byte by byte
    int mismatches = 0;
    for (int i = 0; i < 324; i++) {
        if (generated[i] != reference[i]) {
            if (mismatches < 20) { // Only print first 20 mismatches
                LOG_INFO("[DIFF] Offset 0x%02x: Generated 0x%02x, Reference 0x%02x\n", i, generated[i], reference[i]);
            }
            mismatches++;
        }
    }

    if (mismatches == 0) {
        LOG_INFO("[SUCCESS] ✓ Generated binary matches reference exactly!\n");
        return 0;
    } else {
        LOG_INFO("[FAILURE] ✗ Found %d byte differences\n", mismatches);
        return 1;
    }
}