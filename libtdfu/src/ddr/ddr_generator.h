#ifndef DDR_GENERATOR_H
#define DDR_GENERATOR_H

#include <stdint.h>
#include <stddef.h>
#include "ddr_types.h"

// Generate complete DDR binary from configuration
int ddr_generate_binary(const ddr_config_t *config, uint8_t *output, size_t output_size);

// Generate DDR binary and compare with reference
int ddr_test_against_reference(const ddr_config_t *config, const uint8_t *reference, size_t ref_size);

#endif // DDR_GENERATOR_H