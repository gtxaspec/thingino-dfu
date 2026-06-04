/**
 * DDR Register Data - matches u-boot burner SPL format
 *
 * Source: thingino-uboot/arch/mips/cpu/xburst/ddr_reg_data.h
 *
 * The RDD section of the DDR binary (after CRC32) is a serialized
 * instance of this struct. The burner SPL reads it via g_ddr_param
 * and writes each field directly to the hardware register.
 */

#ifndef DDR_REG_DATA_H
#define DDR_REG_DATA_H

#include <stdint.h>

struct ddr_registers {
    uint32_t ddrc_cfg;
    uint32_t ddrc_ctrl;
    uint32_t ddrc_mmap[2];
    uint32_t ddrc_refcnt;
    uint32_t ddrc_timing1;
    uint32_t ddrc_timing2;
    uint32_t ddrc_timing3;
    uint32_t ddrc_timing4;
    uint32_t ddrc_timing5;
    uint32_t ddrc_timing6;
    uint32_t ddrc_autosr;
    uint32_t ddrp_dcr;
    uint32_t ddrp_mr0;
    uint32_t ddrp_mr1;
    uint32_t ddrp_mr2;
    uint32_t ddrp_mr3;
    uint32_t ddrp_ptr0;
    uint32_t ddrp_ptr1;
    uint32_t ddrp_ptr2;
    uint32_t ddrp_dtpr0;
    uint32_t ddrp_dtpr1;
    uint32_t ddrp_dtpr2;
    uint32_t ddrp_pgcr;
    uint32_t ddrp_odtcr;
    uint32_t ddrp_dxngcrt[4];
    uint32_t ddrp_zqncr1;
    uint32_t ddrp_impedance[2];
    uint32_t ddrp_odt_impedance[2];
    uint8_t ddrp_rzq[32];
    uint32_t ddr_chip0_size;
    uint32_t ddr_chip1_size;
    uint32_t remap_array[5];
};

/**
 * Compute DDR register values from chip config and platform frequency.
 * Uses the same algorithm as u-boot's ddr_params_creator tool.
 */
#include "ddr_config_database.h"
void ddr_compute_registers(const ddr_chip_config_t *chip, uint32_t ddr_freq_hz, uint8_t row, uint8_t col, uint8_t bank8,
                           uint8_t dw32, struct ddr_registers *regs);

#endif
