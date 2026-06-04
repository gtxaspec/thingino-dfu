/**
 * DDR Binary Builder - Matches Python script format
 * Builds FIDB (platform config) + RDD (DDR PHY params) format
 */

#include "ddr_binary_builder.h"
#include "ddr_config_database.h"
#include "ddr_reg_data.h"
#include "thingino.h"
#include "cloner/constants.h"
#include <string.h>
#include <stdio.h>

/**
 * Get default platform configuration from embedded database
 */
int ddr_get_platform_config(const char *platform_name, platform_config_t *config) {
    if (!platform_name || !config)
        return -1;

    const processor_config_t *proc_cfg = processor_config_get(platform_name);
    if (!proc_cfg) {
        // Default to T31 config if not found
        proc_cfg = processor_config_get("t31");
        if (!proc_cfg)
            return -1;
    }

    config->name = proc_cfg->name;
    config->crystal_freq = proc_cfg->crystal_freq;
    config->cpu_freq = proc_cfg->cpu_freq;
    config->ddr_freq = proc_cfg->ddr_freq;
    config->uart_baud = proc_cfg->uart_baud;
    config->mem_size = proc_cfg->mem_size;
    config->uart_idx = proc_cfg->uart_idx;
    config->ginfo_ddr_params_size = proc_cfg->ginfo_ddr_params_size;
    config->use_fidb_header = proc_cfg->use_fidb_header;
    config->use_inno_phy_rdd = proc_cfg->use_inno_phy_rdd;
    config->is_xburst2 = proc_cfg->is_xburst2;
    config->ddr_bank8 = proc_cfg->ddr_bank8;
    config->ddr_dw32 = proc_cfg->ddr_dw32;
    config->ddr_cs0 = proc_cfg->ddr_cs0;
    config->ddr_cs1 = proc_cfg->ddr_cs1;

    return 0;
}

/**
 * Get default platform configuration by processor variant
 *
 * Wrapper around ddr_get_platform_config() that accepts tdfu_variant_t enum.
 * Maps variant enums to platform name strings.
 */
int ddr_get_platform_config_by_variant(int variant, platform_config_t *config) {
    if (!config)
        return -1;

    // Map processor variant enum to platform name
    // These values match the tdfu_variant_t enum from thingino.h:
    // TDFU_VARIANT_T20 = 0, TDFU_VARIANT_T21 = 1, TDFU_VARIANT_T30 = 3, TDFU_VARIANT_T31X = 5,
    // TDFU_VARIANT_T31ZX = 6, TDFU_VARIANT_T41 = 9
    const char *platform_name;

    switch (variant) {
    case TDFU_VARIANT_T10:
        platform_name = "t10";
        break;
    case TDFU_VARIANT_T20:
        platform_name = "t20";
        break;
    case TDFU_VARIANT_T21:
        platform_name = "t21";
        break;
    case TDFU_VARIANT_T23:
        platform_name = "t23";
        break;
    case TDFU_VARIANT_T23DL:
        platform_name = "t23dl";
        break;
    case TDFU_VARIANT_T32:
        platform_name = "t32";
        break;
    case TDFU_VARIANT_T30:
        platform_name = "t30";
        break;
    case TDFU_VARIANT_T31X:
    case TDFU_VARIANT_T31ZX:
    case TDFU_VARIANT_T31:
        platform_name = "t31";
        break;
    case TDFU_VARIANT_T31A:
        platform_name = "t31a";
        break;
    case TDFU_VARIANT_T31AL:
        platform_name = "t31al";
        break;
    case TDFU_VARIANT_T40:
        platform_name = "t40";
        break;
    case TDFU_VARIANT_T40XP:
        platform_name = "t40xp";
        break;
    case TDFU_VARIANT_T41:
        platform_name = "t41";
        break;
    case TDFU_VARIANT_A1:
        platform_name = "a1";
        break;
    default:
        platform_name = "t31";
        break;
    }

    return ddr_get_platform_config(platform_name, config);
}

/**
 * Write 32-bit little-endian value
 */
void write_u32_le(uint8_t *buf, uint32_t value) {
    buf[0] = value & 0xFF;
    buf[1] = (value >> 8) & 0xFF;
    buf[2] = (value >> 16) & 0xFF;
    buf[3] = (value >> 24) & 0xFF;
}

/**
 * Calculate FIDB data size for a platform.
 *
 * struct global_info layout (CONFIG_BURNER):
 *   0x00: extal, cpufreq, ddrfreq, ddr_div, uart_idx, baud_rate (24 bytes)
 *   0x18: [ddr_params] - present on A1/T40/T41, commented out on T20/T21/T23/T30/T31
 *   0x18 + ddr_params_size: nr_gpio_func (4 bytes)
 *   0x1c + ddr_params_size: gpio[] (0 entries)
 *
 * We need enough space for nr_gpio_func at the correct offset.
 */
static uint32_t fidb_data_size(const platform_config_t *platform) {
    uint32_t nr_gpio_offset = 0x18 + platform->ginfo_ddr_params_size;
    /* Need at least nr_gpio_offset + 4 bytes, rounded up */
    uint32_t min_size = nr_gpio_offset + 4;
    if (min_size < DDR_FIDB_DATA_SIZE_MIN)
        return DDR_FIDB_DATA_SIZE_MIN;
    /* Round up to 8-byte boundary */
    return (min_size + 7) & ~7u;
}

/**
 * Build FIDB section (global_info data for SPL)
 *
 * xburst1 (T20/T21/T23/T30/T31): SPL uses find_param() to scan for "FIDB" magic.
 *   Output = 8-byte header ("FIDB" + size) + struct global_info data.
 *
 * xburst2 (A1/T40/T41): SPL reads struct global_info directly from CONFIG_SPL_GINFO_BASE.
 *   Output = raw struct global_info data (no header).
 */
size_t ddr_build_fidb(const platform_config_t *platform, uint8_t *output) {
    if (!platform || !output)
        return 0;

    uint32_t data_size = fidb_data_size(platform);
    uint32_t header_size = platform->use_fidb_header ? DDR_FIDB_HEADER_SIZE : 0;
    uint32_t total_size = header_size + data_size;

    /* Clear output buffer */
    memset(output, 0, total_size);

    /* Write header only for xburst1 platforms that use find_param() */
    if (platform->use_fidb_header) {
        output[0] = 'F';
        output[1] = 'I';
        output[2] = 'D';
        output[3] = 'B';
        write_u32_le(output + 4, data_size);
    }

    /* struct global_info data */
    uint8_t *d = output + header_size;

    /* struct global_info fields - offsets verified against vendor reference binary.
     *
     * NOTE: The vendor reference binary (ddr_extracted.bin) has ddr_div=0 at 0x0c
     * and uart_idx at 0x10 with value 1. We match that layout exactly.
     *
     * After baud_rate (0x14), the layout depends on CONFIG_BURNER:
     * - For T31 (ddr_params commented out): nr_gpio_func at 0x18, gpio[] at 0x1c
     * - For A1/T40/T41 (ddr_params embedded): ddr_params at 0x18, then nr_gpio_func
     *
     * The vendor reference had non-zero values at offsets 0x20-0x30 which the SPL
     * reads but which are harmless for bootstrap. We replicate those values to match
     * the vendor binary exactly.
     */
    write_u32_le(d + 0x00, platform->crystal_freq);
    write_u32_le(d + 0x04, platform->cpu_freq);
    write_u32_le(d + 0x08, platform->ddr_freq);

    /* ddr_div: vendor reference binary has 0 here.
     * SPL computes its own divider when this is 0. */
    write_u32_le(d + 0x0c, 0);

    write_u32_le(d + 0x10, platform->uart_idx);
    write_u32_le(d + 0x14, platform->uart_baud);

    /* After baud_rate (0x14), the struct layout depends on the SPL build.
     *
     * T31 vendor SPL: ddr_params is commented out in global_info.
     *   Offset 0x18 is nr_gpio_func, followed by gpio[] and ddr_test.
     *   The vendor reference binary has specific values that work.
     *   VERIFIED: T31X UART and bootstrap work with these exact values.
     *
     * T20/T40/T41/A1 vendor SPLs: ddr_params is embedded in global_info.
     *   Offset 0x18 starts struct ddr_params (geometry, sizes, timing).
     *   nr_gpio_func follows after ddr_params.
     */
    /* Defined in ddr_fidb_xb1.c and ddr_fidb_xb2.c */
    extern void ddr_fidb_fill_xb1(uint8_t *d, const platform_config_t *platform);
    extern void ddr_fidb_fill_xb2(uint8_t *d, const platform_config_t *platform);

    /* A1 has a unique xb2 FIDB layout. T40/T41 are xburst2 but use xb1-style FIDB.
     * Detect A1 specifically by uart_idx=3 (only A1 uses uart3). */
    if (platform->is_xburst2 && platform->uart_idx == 3) {
        ddr_fidb_fill_xb2(d, platform);
    } else if (platform->ginfo_ddr_params_size == 0) {
        ddr_fidb_fill_xb1(d, platform);
    } else {
        /* Platforms with ddr_params embedded: fill geometry for SPL.
         * The SPL reads row, col, bank8, dw32, cs0, cs1, chip sizes
         * from ddr_params in CONFIG_BURNER mode. */
        const char *pname = NULL;
        size_t count;
        const processor_config_t *procs = processor_config_list(&count);
        for (size_t i = 0; i < count; i++) {
            if (procs[i].cpu_freq == platform->cpu_freq && procs[i].ddr_freq == platform->ddr_freq &&
                procs[i].mem_size == platform->mem_size) {
                pname = procs[i].name;
                break;
            }
        }
        const ddr_chip_config_t *chip = ddr_chip_config_get_default(pname);

        if (chip) {
            uint8_t bank8 = platform->ddr_bank8;
            uint8_t dw32 = platform->ddr_dw32;
            uint32_t memsize = (1u << (chip->row + chip->col)) * (dw32 ? 4 : 2) * (bank8 ? 8 : 4);

            /* struct ddr_params at FIDB data offset 0x18 */
            write_u32_le(d + 0x18, chip->ddr_type);     /* type */
            write_u32_le(d + 0x1c, platform->ddr_freq); /* freq */
            write_u32_le(d + 0x20, 0);                  /* div */
            write_u32_le(d + 0x24, 1);                  /* cs0 */
            write_u32_le(d + 0x28, 0);                  /* cs1 */
            write_u32_le(d + 0x2c, dw32);               /* dw32 */
            write_u32_le(d + 0x30, chip->cl);           /* cl */
            write_u32_le(d + 0x34, chip->bl);           /* bl */
            write_u32_le(d + 0x38, chip->col);          /* col */
            write_u32_le(d + 0x3c, chip->row);          /* row */
            write_u32_le(d + 0x40, chip->col1 > 0 ? chip->col1 : chip->col);
            write_u32_le(d + 0x44, chip->row1 > 0 ? chip->row1 : chip->row);
            write_u32_le(d + 0x48, bank8);   /* bank8 */
            write_u32_le(d + 0x4c, memsize); /* size.chip0 */
            write_u32_le(d + 0x50, 0);       /* size.chip1 */
        }

        /* nr_gpio_func = 0 after ddr_params */
        uint32_t nr_gpio_offset = 0x18 + platform->ginfo_ddr_params_size;
        write_u32_le(d + nr_gpio_offset, 0);
    }

    return total_size;
}

/**
 * Build RDD section (132 bytes: 8 header + 124 data)
 *
 * Shared fields (DDRC_CFG, MMAP, chip size, remap) are filled here.
 * DDR2/DDR3-specific fields (timings, MR registers) are filled by
 * ddr_rdd_fill_ddr2() or ddr_rdd_fill_ddr3() in separate files.
 */

/* Defined in ddr_rdd_ddr2.c / ddr_rdd_ddr3.c */
extern void ddr_rdd_fill_ddr2(uint8_t *rdd_data, const platform_config_t *platform, const ddr_phy_params_t *params,
                              uint32_t memsize);
extern void ddr_rdd_fill_ddr3(uint8_t *rdd_data, const platform_config_t *platform, const ddr_phy_params_t *params,
                              uint32_t memsize);

size_t ddr_build_rdd(const platform_config_t *platform, const ddr_phy_params_t *params, uint8_t *output) {
    if (!platform || !params || !output)
        return 0;

    memset(output, 0, DDR_RDD_SIZE);

    uint8_t rdd_data[DDR_RDD_DATA_SIZE];
    memset(rdd_data, 0, DDR_RDD_DATA_SIZE);

    /* --- Shared fields (DDR2 and DDR3) --- */

    uint8_t bank8 = platform->ddr_bank8;
    uint8_t cs0 = platform->ddr_cs0;
    uint8_t cs1 = platform->ddr_cs1;
    uint8_t dw32 = platform->ddr_dw32;

    /* [0x00] DDRC_CFG */
    uint8_t ddrc_type = 4; /* DDR2 */
    if (params->ddr_type == 0 || params->ddr_type == 3)
        ddrc_type = 6; /* DDR3 */
    else if (params->ddr_type == 2 || params->ddr_type == 4)
        ddrc_type = 5; /* LPDDR2 */

    uint32_t ddrc_cfg = dw32 | (bank8 << 1) | (0 << 2) /* CL=0 (INNO PHY reads CL from MR0) */
                        | (cs0 << 6) | (cs1 << 7) | ((uint32_t)(params->col_bits - 8) << 8) |
                        ((uint32_t)(params->row_bits - 12) << 11) | (1u << 15)                           /* MISPE */
                        | ((uint32_t)ddrc_type << 17) | ((params->bl == 8 ? 1u : 0u) << 21) | (1u << 22) /* IMBA */
                        | ((uint32_t)bank8 << 23) | ((uint32_t)(params->col_bits - 8) << 24) |
                        ((uint32_t)(params->row_bits - 12) << 27);
    write_u32_le(rdd_data + 0x00, ddrc_cfg);

    /* [0x04] DDR type: INNO format uses 2 for both DDR2 and DDR3 */
    uint32_t inno_type = 2;
    if (params->ddr_type == 2 || params->ddr_type == 4)
        inno_type = 4;
    write_u32_le(rdd_data + 0x04, inno_type);

    /* [0x10-0x14] MMAP */
    uint32_t memsize = (1u << (params->row_bits + params->col_bits)) * (dw32 ? 4 : 2) * (bank8 ? 8 : 4);
    uint32_t mem_base0 = 0x20;
    uint32_t mem_mask0 = ~((memsize >> 24) - 1) & 0xFF;
    write_u32_le(rdd_data + 0x10, (mem_base0 << 8) | mem_mask0);
    write_u32_le(rdd_data + 0x14, 0x00002800);

    /* [0x18] Config constant */
    write_u32_le(rdd_data + 0x18, 0x00c20001);

    /* --- DDR type-specific fields --- */
    if (params->ddr_type == 0) {
        ddr_rdd_fill_ddr3(rdd_data, platform, params, memsize);
    } else {
        ddr_rdd_fill_ddr2(rdd_data, platform, params, memsize);
    }

    /* --- Shared tail fields --- */

    /* [0x60-0x64] Chip sizes */
    write_u32_le(rdd_data + 0x60, memsize);
    write_u32_le(rdd_data + 0x64, 0);

    /* [0x68-0x7B] Remap array */
    {
        uint8_t remap[20];
        for (int i = 0; i < 20; i++)
            remap[i] = i;
        int bank_bits = bank8 ? 3 : 2;
        int bit_width = dw32 ? 2 : 1;
        int addr_bits = bit_width + params->col_bits + params->row_bits + bank_bits;
        int swap_bits = bank_bits;
        int startA = (bit_width + params->col_bits > 12) ? (bit_width + params->col_bits) : 12;
        int startB = addr_bits - swap_bits - startA;
        startA -= 12;
        for (int i = 0; i < swap_bits && (startA + i) < 20 && (startB + i) < 20; i++) {
            uint8_t tmp = remap[startA + i];
            remap[startA + i] = remap[startB + i];
            remap[startB + i] = tmp;
        }
        memcpy(rdd_data + 0x68, remap, 20);
    }

    /* RDD header */
    output[0] = 0x00;
    output[1] = 'R';
    output[2] = 'D';
    output[3] = 'D';
    write_u32_le(output + 4, DDR_RDD_DATA_SIZE);
    memcpy(output + 8, rdd_data, DDR_RDD_DATA_SIZE);

    return DDR_RDD_SIZE;
}

/**
 * Build RDD section from struct ddr_registers (u-boot register format).
 *
 * Used by T20/T40/T41 SPLs which read RDD as struct ddr_registers via g_ddr_param.
 * The T31 SPL uses a different byte-packed INNO PHY format (ddr_build_rdd above).
 */
static size_t ddr_build_rdd_regs(const platform_config_t *platform, const ddr_chip_config_t *chip, uint8_t *output) {
    if (!platform || !chip || !output)
        return 0;

    struct ddr_registers regs;
    ddr_compute_registers(chip, platform->ddr_freq, chip->row, chip->col, platform->ddr_bank8, platform->ddr_dw32,
                          &regs);

    uint32_t data_size = sizeof(struct ddr_registers);
    uint32_t total_size = DDR_RDD_HEADER_SIZE + data_size;

    memset(output, 0, total_size);

    /* Header: "\0RDD" + size */
    output[0] = 0x00;
    output[1] = 'R';
    output[2] = 'D';
    output[3] = 'D';
    write_u32_le(output + 4, data_size);

    /* Copy struct as-is (already little-endian on MIPS target) */
    memcpy(output + DDR_RDD_HEADER_SIZE, &regs, sizeof(regs));

    return total_size;
}

/**
 * Build complete DDR binary (FIDB + RDD, size depends on platform)
 */
size_t ddr_build_binary(const platform_config_t *platform, const ddr_phy_params_t *params, uint8_t *output) {
    if (!platform || !params || !output)
        return 0;

    /* Build FIDB section (platform-dependent size) */
    size_t fidb_size = ddr_build_fidb(platform, output);

    /* Build RDD section - format depends on architecture and DDR init path:
     * xburst2 (A1/T40/T41): chip name + params (184 bytes)
     * xburst1 INNO PHY (T21/T23/T30/T31): byte-packed RDD (124 bytes)
     * xburst1 DDRP (T10/T20): struct ddr_registers (196 bytes)
     */
    extern size_t ddr_build_rdd_xb2(const platform_config_t *platform, const ddr_phy_params_t *params, uint8_t *output);
    size_t rdd_size;

    if (platform->is_xburst2) {
        rdd_size = ddr_build_rdd_xb2(platform, params, output + fidb_size);
    } else if (platform->use_inno_phy_rdd) {
        rdd_size = ddr_build_rdd(platform, params, output + fidb_size);
    } else {
        const char *pname = NULL;
        size_t count;
        const processor_config_t *procs = processor_config_list(&count);
        for (size_t i = 0; i < count; i++) {
            if (procs[i].cpu_freq == platform->cpu_freq && procs[i].ddr_freq == platform->ddr_freq &&
                procs[i].mem_size == platform->mem_size) {
                pname = procs[i].name;
                break;
            }
        }
        const ddr_chip_config_t *chip = ddr_chip_config_get_default(pname);
        if (chip) {
            rdd_size = ddr_build_rdd_regs(platform, chip, output + fidb_size);
        } else {
            rdd_size = ddr_build_rdd(platform, params, output + fidb_size);
        }
    }

    return fidb_size + rdd_size;
}

/**
 * Populate DDR PHY params from chip config and platform frequency.
 * Converts timing values from ps to clock cycles.
 */
void ddr_chip_to_phy_params(const ddr_chip_config_t *chip, uint32_t ddr_freq_hz, ddr_phy_params_t *params) {

    memset(params, 0, sizeof(*params));

    uint32_t ps_per_tck = 1000000000 / (ddr_freq_hz / 1000);

    params->ddr_type = chip->ddr_type;
    params->row_bits = chip->row;
    params->col_bits = chip->col;
    params->cl = chip->cl;
    params->bl = chip->bl;
    params->rl = chip->rl;
    params->wl = chip->wl;

/* Core timing: ps to cycles (ceiling). Values < 100 are already
 * in cycles (DDR3 chip configs use cycle counts for some fields). */
#define PS2CYC(ps)   (uint32_t)(((uint32_t)(ps) + ps_per_tck - 1) / ps_per_tck)
#define CONVERT(val) ((val) > 100 ? PS2CYC(val) : (uint32_t)(val))

    params->tRAS = (chip->tRAS > 0) ? PS2CYC(chip->tRAS) : 0;
    params->tRC = (chip->tRC > 0) ? PS2CYC(chip->tRC) : 0;
    params->tRCD = (chip->tRCD > 0) ? PS2CYC(chip->tRCD) : 0;
    params->tRP = (chip->tRP > 0) ? PS2CYC(chip->tRP) : 0;
    params->tRFC = (chip->tRFC > 0) ? PS2CYC(chip->tRFC) : 0;
    params->tRTP = (chip->tRTP > 0) ? CONVERT(chip->tRTP) : 0;
    params->tFAW = (chip->tFAW > 0) ? PS2CYC(chip->tFAW) : 0;
    params->tRRD = (chip->tRRD > 0) ? CONVERT(chip->tRRD) : 0;
    params->tWTR = (chip->tWTR > 0) ? CONVERT(chip->tWTR) : 0;
    params->tWR = (chip->tWR > 0) ? PS2CYC(chip->tWR) : 0;

    /* Extended timing - small values are cycles, large values are ps */
    params->tCKE = (chip->tCKE > 0) ? CONVERT(chip->tCKE) : 0;
    params->tXP = (chip->tXP > 0) ? CONVERT(chip->tXP) : 0;
    params->tCCD = (chip->tCCD > 0) ? (uint8_t)chip->tCCD : 0;
    params->tMOD = (chip->tMOD > 0) ? (uint8_t)chip->tMOD : 0;
    params->tXPDLL = (chip->tXPDLL > 0) ? (uint8_t)PS2CYC(chip->tXPDLL) : 0xFF;
    params->tXS = (chip->tXS > 0) ? (uint8_t)chip->tXS : 0;
    params->tXSDLL = (chip->tXSDLL > 0) ? (uint8_t)chip->tXSDLL : 0;
    params->tCKESR = (chip->tCKESR > 0) ? (uint32_t)chip->tCKESR : 0;
    params->tREFI = (chip->tREFI > 0) ? (uint32_t)chip->tREFI / ps_per_tck : 0;
    params->tXSR = (chip->tXSR > 0) ? PS2CYC(chip->tXSR) : 0;
    params->tXSRD = (chip->tXSRD > 0) ? (uint32_t)chip->tXSRD : 200;

#undef PS2CYC
}

tdfu_error_t ddr_validate_binary(const uint8_t *data, size_t size) {
    if (!data)
        return TDFU_ERROR_INVALID_PARAMETER;
    /* Minimum: FIDB header(8) + some data + RDD(132) */
    if (size < DDR_FIDB_HEADER_SIZE + 24 + DDR_RDD_SIZE)
        return TDFU_ERROR_PROTOCOL;
    if (data[0] != 'F' || data[1] != 'I' || data[2] != 'D' || data[3] != 'B')
        return TDFU_ERROR_PROTOCOL;
    return TDFU_SUCCESS;
}
