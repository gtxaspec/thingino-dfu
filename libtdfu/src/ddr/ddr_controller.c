#include "ddr_controller.h"
#include "ddr_utils.h"
#include <string.h>
#include <stdio.h>

// Helper: write a uint32_t to an unaligned offset in a byte buffer (LE)
static inline void write_u32_le(uint8_t *buf, size_t offset, uint32_t val) {
    memcpy(buf + offset, &val, sizeof(val));
}

// Helper: Initialize object buffer with config values at vendor source offsets
// Based on vendor tool DDR controller register generation algorithm
void ddr_init_object_buffer(const ddr_config_t *config, uint8_t *obj_buffer) {
    // Store config values at the offsets that vendor functions read from
    // These are input values used by ddrc_generate_register and ddrp_generate_register

    // From ddrc_generate_register algorithm:
    write_u32_le(obj_buffer, 0x1a4, (uint32_t)config->tWR);   // tWR (Write Recovery)
    write_u32_le(obj_buffer, 0x1c0, (uint32_t)config->tWL);   // WL (Write Latency)
    write_u32_le(obj_buffer, 0x194, (uint32_t)config->tRAS);  // tRAS (Row Active Time)
    write_u32_le(obj_buffer, 0x19c, (uint32_t)config->tRCD);  // tRCD (Row to Column)
    write_u32_le(obj_buffer, 0x1bc, (uint32_t)config->tRL);   // tRL (Read Latency)
    write_u32_le(obj_buffer, 0x198, (uint32_t)config->tRP);   // tRP (Row Precharge)
    write_u32_le(obj_buffer, 0x1a8, (uint32_t)config->tRRD);  // tRRD (Row to Row Delay)
    write_u32_le(obj_buffer, 0x1a0, (uint32_t)config->tRC);   // tRC (Row Cycle)
    write_u32_le(obj_buffer, 0x1b0, (uint32_t)config->tRFC);  // tRFC (Refresh to Active)
    write_u32_le(obj_buffer, 0x1b8, (uint32_t)config->tCKE);  // tCKE (Clock Enable)
    write_u32_le(obj_buffer, 0x1b4, (uint32_t)config->tXP);   // tXP (Power Down Exit)
    write_u32_le(obj_buffer, 0x1c4, (uint32_t)config->tREFI); // tREFI (Refresh Interval in ns)

    // From ddrp_generate_register algorithm:
    write_u32_le(obj_buffer, 0x26c, config->clock_mhz);       // Clock MHz
    write_u32_le(obj_buffer, 0x188, (uint32_t)config->cas_latency); // CAS Latency
    write_u32_le(obj_buffer, 0x154, config->type);             // DDR Type

    // Clock period in picoseconds (for ps2cycle calculations)
    // clock_period_ps = 1,000,000 / clock_mhz
    uint32_t clock_period_ps = 1000000 / config->clock_mhz;
    write_u32_le(obj_buffer, 0x22c, clock_period_ps);

    // Note: 0x270 is INI config (we set to 0, meaning use defaults)
    write_u32_le(obj_buffer, 0x270, 0);

    // Initialize ddr_params structure at obj[0x118+]
    // Initialize ddr_params structure - param_3 in ddrc_config_creator points to class offset 0x118
    uint32_t *params = (uint32_t *)(obj_buffer + 0x118);
    uint32_t data_width = config->data_width;
    uint32_t row_bits = config->row_bits;
    uint32_t col_bits = config->col_bits;

    // DDR type: 0=LPDDR, 1=DDR, 4=DDR2, etc.
    uint32_t ddr_type = (config->type == DDR_TYPE_DDR2) ? 4 : (uint32_t)config->type;

    params[0] = ddr_type;                   // obj[0x118 + 0x00] - DDR type
    params[1] = 0;                          // obj[0x118 + 0x04] - Reserved
    params[2] = 0;                          // obj[0x118 + 0x08] - Reserved
    params[3] = 1;                          // obj[0x118 + 0x0c] = obj[0x124] - CS0 enable
    params[4] = 0;                          // obj[0x118 + 0x10] = obj[0x128] - CS1 enable
    params[5] = (data_width == 32) ? 1 : 0; // obj[0x118 + 0x14] = obj[0x12c] - Data width (0=16-bit, 1=32-bit)
    params[6] = 0;                          // obj[0x118 + 0x18] = obj[0x130] - Reserved
    params[7] = 0;                          // obj[0x118 + 0x1c] = obj[0x134] - Reserved
    params[8] = 8;                          // obj[0x118 + 0x20] = obj[0x138] - Burst length (4 or 8)
    // TXX-specific encoding: COL0 = col_bits - 4, ROW0 = row_bits - 11
    // The formula (row0 * 8 + 0x20) & 0x38 transforms row0 to the output ROW0 field
    // This is different from standard U-Boot (COL0 = col_bits - 8, ROW0 = row_bits - 12)
    params[9] = col_bits - 4;    // obj[0x118 + 0x24] = obj[0x13c] - COL0 (col_bits - 4 for TXX)
    params[10] = row_bits - 11;  // obj[0x118 + 0x28] = obj[0x140] - ROW0 (row_bits - 11 for TXX)
    params[0xb] = col_bits - 4;  // obj[0x118 + 0x2c] = obj[0x144] - COL1 (same as COL0 for single CS)
    params[0xc] = row_bits - 11; // obj[0x118 + 0x30] = obj[0x148] - ROW1 (same as ROW0 for single CS)
    params[0xd] = 1;             // obj[0x118 + 0x34] = obj[0x14c] - Bank bits (0=4 banks, 1=8 banks)
    params[0xe] = 0;             // obj[0x118 + 0x38] = obj[0x150] - CS0 memory size (will be calculated)
    params[0xf] = 0;             // obj[0x118 + 0x3c] = obj[0x154] - CS1 memory size

    // Calculate CS0 and CS1 memory sizes in MB (not bytes!)
    // memsize = (1 << row_bits) * (1 << col_bits) * (1 << bank_bits) * data_width / 8 / 1024 / 1024
    uint32_t cs0_size_mb = (1 << row_bits) * (1 << col_bits) * 8 * (data_width / 8) / 1024 / 1024;
    uint32_t cs1_size_mb = 0; // Assume single CS for now

    params[0xe] = cs0_size_mb; // obj[0x118 + 0x38] = obj[0x150] - CS0 size in MB
    params[0xf] = cs1_size_mb; // obj[0x118 + 0x3c] = obj[0x154] - CS1 size in MB
}

int ddr_generate_ddrc_with_object(const ddr_config_t *config, uint8_t *obj_buffer, uint8_t *ddrc_regs) {
    (void)config; /* Used only for TXX ddr_convert_param mapping below */

    // Initialize DDRC register buffer (188 bytes = 0xbc)
    // We'll populate this with calculated register values
    memset(ddrc_regs, 0, 0xbc);

    // Ensure buffer is large enough for all intermediate offsets
    // The vendor object is at least 0x20c bytes, but we're only filling what's needed
    if (obj_buffer == NULL)
        return -1;

    // ========================================
    // DDRC SECTION GENERATION
    // ========================================
    // The DDRC section (file 0x08-0xBF, 184 bytes) should be generated using
    // the U-Boot ddrc_config_creator algorithm, NOT the TXX mapping!
    // The TXX mapping only applies to the DDRP section.
    //
    // Based on Ingenic U-Boot tools/ingenic-tools/ddr_params_creator.c
    // The DDRC section contains a serialized struct ddrc_reg:
    //   - cfg (4 bytes)
    //   - ctrl (4 bytes)
    //   - refcnt (4 bytes)
    //   - mmap[2] (8 bytes)
    //   - remap[5] (20 bytes)
    //   - timing1-6 (24 bytes)
    //   - autosr_en (4 bytes)
    //   - clkstp_cfg (4 bytes)
    //   Total: 72 bytes, rest is padding/zeros

    // For now, generate TXX DDRC registers in obj_buffer for DDRP section use
    if (config->type == DDR_TYPE_DDR2) {
        extern int ddr_generate_ddrc_txx_ddr2(const ddr_config_t *config, uint8_t *obj_buffer);
        if (ddr_generate_ddrc_txx_ddr2(config, obj_buffer) != 0) {
            return -1;
        }
    }

    // ========================================
    // Apply TXX ddr_convert_param mapping
    // Based on TXX ddr_convert_param mapping algorithm
    // ========================================
    uint32_t *ddrc_out = (uint32_t *)ddrc_regs;
    uint32_t *obj = (uint32_t *)obj_buffer;

    ddrc_out[0] = obj[0x7c / 4];   // obj[0x7c]
    ddrc_out[1] = obj[0x80 / 4];   // obj[0x80]
    ddrc_out[2] = obj[0x8c / 4];   // obj[0x8c]
    ddrc_out[3] = obj[0x84 / 4];   // obj[0x84]
    ddrc_out[4] = obj[0x90 / 4];   // obj[0x90]
    ddrc_out[5] = obj[0x94 / 4];   // obj[0x94]
    ddrc_out[6] = obj[0x88 / 4];   // obj[0x88]
    ddrc_out[7] = obj[0xac / 4];   // obj[0xac]
    ddrc_out[8] = obj[0xb0 / 4];   // obj[0xb0]
    ddrc_out[9] = obj[0xb4 / 4];   // obj[0xb4]
    ddrc_out[10] = obj[0xb8 / 4];  // obj[0xb8]
    ddrc_out[11] = obj[0xbc / 4];  // obj[0xbc]
    ddrc_out[12] = obj[0xc0 / 4];  // obj[0xc0]
    ddrc_out[13] = obj[0xc4 / 4];  // obj[0xc4]
    ddrc_out[14] = obj[0xd0 / 4];  // obj[0xd0]
    ddrc_out[15] = obj[0xd8 / 4];  // obj[0xd8]
    ddrc_out[16] = obj[0xdc / 4];  // obj[0xdc]
    ddrc_out[17] = obj[0x1d4 / 4]; // obj[0x1d4]
    ddrc_out[18] = obj[0x1dc / 4]; // obj[0x1dc]
    ddrc_out[19] = obj[0x1e4 / 4]; // obj[0x1e4]
    ddrc_out[20] = obj[0x1e8 / 4]; // obj[0x1e8]
    ddrc_out[21] = obj[0x1ec / 4]; // obj[0x1ec]
    ddrc_out[22] = obj[0x1f0 / 4]; // obj[0x1f0]
    ddrc_out[23] = obj[0x1f4 / 4]; // obj[0x1f4]
    ddrc_out[24] = obj[0x150 / 4]; // obj[0x150]
    ddrc_out[25] = obj[0x154 / 4]; // obj[0x154]
    ddrc_out[26] = obj[0x1c0 / 4]; // obj[0x1c0]
    ddrc_out[27] = obj[0x1c4 / 4]; // obj[0x1c4]
    ddrc_out[28] = obj[0x1c8 / 4]; // obj[0x1c8]
    ddrc_out[29] = obj[0x1cc / 4]; // obj[0x1cc]
    ddrc_out[30] = obj[0x1d0 / 4]; // obj[0x1d0]

    return 0;
}