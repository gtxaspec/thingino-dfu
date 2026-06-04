#include "tdfu/tdfu.h"
#include "tdfu/constants.h"
#include "ddr_binary_builder.h"
#include "ddr_config_database.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// ============================================================================
// FIRMWARE LOADER IMPLEMENTATION
// ============================================================================
// Loads SPL/U-Boot from files at <firmware_dir>/cloner/<platform>/spl.bin etc.
// DDR configuration generated from chip database or reference binaries.

// Default firmware directory
#define DEFAULT_FIRMWARE_DIR "./firmware"

// ============================================================================
// VARIANT TO FIRMWARE DIRECTORY MAPPING
// ============================================================================

/**
 * Map processor variant to firmware directory name.
 * Most variants map 1:1, but some share firmware directories.
 */
static const char *variant_to_firmware_dir(tdfu_variant_t variant) {
    switch (variant) {
    case TDFU_VARIANT_A1:
        return "a1_n_ne_x";
    case TDFU_VARIANT_T31ZX:
    case TDFU_VARIANT_T31AL:
        return "t31x";
    case TDFU_VARIANT_T23DL:
        return "t23";
    case TDFU_VARIANT_T40XP:
        return "t40";
    default:
        return tdfu_variant_to_string(variant);
    }
}

// ============================================================================
// DDR CONFIGURATION
// ============================================================================

/**
 * Generate DDR configuration binary.
 * Uses dynamic generation for T31A (DDR3), reference binaries for others.
 */
static tdfu_error_t firmware_generate_ddr_config(tdfu_variant_t variant, const char *firmware_dir,
                                                     uint8_t **config_buffer, size_t *config_size) {

    /* firmware_dir used for ddr.bin file fallback */

    if (!config_buffer || !config_size) {
        return TDFU_ERROR_INVALID_PARAMETER;
    }

    const char *variant_name = tdfu_variant_to_string(variant);
    LOG_DEBUG("firmware_generate_ddr_config: variant=%d (%s)\n", variant, variant_name);

    // Try loading DDR binary from file first (dynamic generation may not
    // produce correct values for all platforms yet - e.g. T20 timing)
    const char *fw_dir_name = variant_to_firmware_dir(variant);
    const char *base = firmware_dir ? firmware_dir : DEFAULT_FIRMWARE_DIR;
    char ddr_path[512];
    snprintf(ddr_path, sizeof(ddr_path), "%s/cloner/%s/ddr.bin", base, fw_dir_name);

    tdfu_error_t result = load_file(ddr_path, config_buffer, config_size);
    if (result == TDFU_SUCCESS) {
        LOG_INFO("DDR config loaded from %s: %zu bytes\n", ddr_path, *config_size);
        return TDFU_SUCCESS;
    }

    // No reference file - generate dynamically
    platform_config_t platform;
    if (ddr_get_platform_config_by_variant(variant, &platform) != 0) {
        LOG_ERROR("Failed to get platform config for %s\n", variant_name);
        return TDFU_ERROR_INVALID_PARAMETER;
    }

    const ddr_chip_config_t *ddr_chip = ddr_chip_config_get_default(variant_name);
    if (!ddr_chip) {
        LOG_ERROR("No default DDR chip for %s\n", variant_name);
        return TDFU_ERROR_INVALID_PARAMETER;
    }

    ddr_phy_params_t params;
    ddr_chip_to_phy_params(ddr_chip, platform.ddr_freq, &params);

    *config_buffer = (uint8_t *)malloc(DDR_BINARY_SIZE_MAX);
    if (!*config_buffer)
        return TDFU_ERROR_MEMORY;
    *config_size = ddr_build_binary(&platform, &params, *config_buffer);

    LOG_INFO("DDR config generated for %s (%s): %zu bytes\n", variant_name, ddr_chip->name, *config_size);
    return TDFU_SUCCESS;
}

// ============================================================================
// GENERIC FIRMWARE LOADER
// ============================================================================

tdfu_error_t firmware_load_from_dir(tdfu_variant_t variant, const char *firmware_dir,
                                        firmware_files_t *firmware);

tdfu_error_t firmware_load(tdfu_variant_t variant, firmware_files_t *firmware) {
    return firmware_load_from_dir(variant, NULL, firmware);
}

tdfu_error_t firmware_load_from_dir(tdfu_variant_t variant, const char *firmware_dir,
                                        firmware_files_t *firmware) {
    if (!firmware)
        return TDFU_ERROR_INVALID_PARAMETER;

    firmware->config = NULL;
    firmware->config_size = 0;
    firmware->spl = NULL;
    firmware->spl_size = 0;
    firmware->uboot = NULL;
    firmware->uboot_size = 0;

    const char *fw_dir_name = variant_to_firmware_dir(variant);
    const char *base_dir = firmware_dir ? firmware_dir : DEFAULT_FIRMWARE_DIR;
    tdfu_error_t result;

    // Generate DDR config
    result = firmware_generate_ddr_config(variant, base_dir, &firmware->config, &firmware->config_size);
    if (result != TDFU_SUCCESS) {
        LOG_ERROR("Failed to generate DDR configuration\n");
        return result;
    }

    // Build paths: <base_dir>/cloner/<platform>/spl.bin
    char spl_path[512], uboot_path[512];
    snprintf(spl_path, sizeof(spl_path), "%s/cloner/%s/spl.bin", base_dir, fw_dir_name);
    snprintf(uboot_path, sizeof(uboot_path), "%s/cloner/%s/uboot.bin", base_dir, fw_dir_name);

    // Load SPL
    result = load_file(spl_path, &firmware->spl, &firmware->spl_size);
    if (result != TDFU_SUCCESS) {
        LOG_ERROR("Failed to load SPL: %s\n", spl_path);
        firmware_cleanup(firmware);
        return result;
    }

    // Load U-Boot
    result = load_file(uboot_path, &firmware->uboot, &firmware->uboot_size);
    if (result != TDFU_SUCCESS) {
        LOG_ERROR("Failed to load U-Boot: %s\n", uboot_path);
        firmware_cleanup(firmware);
        return result;
    }

    DEBUG_PRINT("Firmware loaded: DDR=%zu SPL=%zu U-Boot=%zu bytes\n", firmware->config_size, firmware->spl_size,
                firmware->uboot_size);
    return TDFU_SUCCESS;
}

tdfu_error_t firmware_load_from_files(tdfu_variant_t variant, const char *config_file, const char *spl_file,
                                          const char *uboot_file, firmware_files_t *firmware) {

    if (!firmware)
        return TDFU_ERROR_INVALID_PARAMETER;

    firmware->config = NULL;
    firmware->config_size = 0;
    firmware->spl = NULL;
    firmware->spl_size = 0;
    firmware->uboot = NULL;
    firmware->uboot_size = 0;

    // DDR config: custom file or generate
    if (config_file) {
        tdfu_error_t result = load_file(config_file, &firmware->config, &firmware->config_size);
        if (result != TDFU_SUCCESS) {
            firmware_cleanup(firmware);
            return result;
        }
    } else {
        tdfu_error_t result =
            firmware_generate_ddr_config(variant, DEFAULT_FIRMWARE_DIR, &firmware->config, &firmware->config_size);
        if (result != TDFU_SUCCESS) {
            firmware->config = NULL;
            firmware->config_size = 0;
        }
    }

    // SPL: custom file or default path
    if (spl_file) {
        tdfu_error_t result = load_file(spl_file, &firmware->spl, &firmware->spl_size);
        if (result != TDFU_SUCCESS) {
            firmware_cleanup(firmware);
            return result;
        }
    } else {
        const char *fw_dir_name = variant_to_firmware_dir(variant);
        char path[512];
        snprintf(path, sizeof(path), "%s/cloner/%s/spl.bin", DEFAULT_FIRMWARE_DIR, fw_dir_name);
        tdfu_error_t result = load_file(path, &firmware->spl, &firmware->spl_size);
        if (result != TDFU_SUCCESS) {
            LOG_ERROR("Failed to load SPL: %s\n", path);
            firmware_cleanup(firmware);
            return result;
        }
    }

    // U-Boot: custom file or default path
    if (uboot_file) {
        tdfu_error_t result = load_file(uboot_file, &firmware->uboot, &firmware->uboot_size);
        if (result != TDFU_SUCCESS) {
            firmware_cleanup(firmware);
            return result;
        }
    } else {
        const char *fw_dir_name = variant_to_firmware_dir(variant);
        char path[512];
        snprintf(path, sizeof(path), "%s/cloner/%s/uboot.bin", DEFAULT_FIRMWARE_DIR, fw_dir_name);
        tdfu_error_t result = load_file(path, &firmware->uboot, &firmware->uboot_size);
        if (result != TDFU_SUCCESS) {
            LOG_ERROR("Failed to load U-Boot: %s\n", path);
            firmware_cleanup(firmware);
            return result;
        }
    }

    return TDFU_SUCCESS;
}

// ============================================================================
// UTILITIES
// ============================================================================

void firmware_cleanup(firmware_files_t *firmware) {
    if (!firmware)
        return;
    free(firmware->config);
    firmware->config = NULL;
    firmware->config_size = 0;
    free(firmware->spl);
    firmware->spl = NULL;
    firmware->spl_size = 0;
    free(firmware->uboot);
    firmware->uboot = NULL;
    firmware->uboot_size = 0;
}

tdfu_error_t load_file(const char *filename, uint8_t **data, size_t *size) {
    if (!filename || !data || !size)
        return TDFU_ERROR_INVALID_PARAMETER;

    FILE *file = fopen(filename, "rb");
    if (!file)
        return TDFU_ERROR_FILE_IO;

    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return TDFU_ERROR_FILE_IO;
    }
    long file_size = ftell(file);
    if (file_size < 0) {
        fclose(file);
        return TDFU_ERROR_FILE_IO;
    }
    if (fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return TDFU_ERROR_FILE_IO;
    }

    *data = (uint8_t *)malloc(file_size);
    if (!*data) {
        fclose(file);
        return TDFU_ERROR_MEMORY;
    }

    size_t bytes_read = fread(*data, 1, file_size, file);
    fclose(file);

    if (bytes_read != (size_t)file_size) {
        free(*data);
        *data = NULL;
        return TDFU_ERROR_FILE_IO;
    }

    *size = bytes_read;
    return TDFU_SUCCESS;
}

tdfu_error_t firmware_validate(const firmware_files_t *firmware) {
    if (!firmware)
        return TDFU_ERROR_INVALID_PARAMETER;

    if (firmware->config && firmware->config_size > 0) {
        tdfu_error_t result = ddr_validate_binary(firmware->config, firmware->config_size);
        if (result != TDFU_SUCCESS)
            return result;
    }

    if (firmware->spl && firmware->spl_size < 1024)
        return TDFU_ERROR_PROTOCOL;

    if (firmware->uboot && firmware->uboot_size < 4096)
        return TDFU_ERROR_PROTOCOL;

    return TDFU_SUCCESS;
}

tdfu_error_t firmware_file_check_readable(const char *path) {
    if (!path)
        return TDFU_ERROR_INVALID_PARAMETER;

    FILE *f = fopen(path, "rb");
    if (!f)
        return TDFU_ERROR_FILE_IO;

    fclose(f);
    return TDFU_SUCCESS;
}
