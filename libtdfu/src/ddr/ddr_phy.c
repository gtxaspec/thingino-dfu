#include "ddr_phy.h"
#include "ddr_phy_ddr2.h"
#include "ddr_utils.h"
#include "tdfu/tdfu.h"
#include <string.h>
#include <stdio.h>

// Vendor tool DDR PHY register generation algorithm (ddrp_generate_register)
// This creates a 136-byte DDR PHY register block with timing parameters
// The vendor tool uses a C++ object structure with offsets to store intermediate values

int ddr_generate_ddrp(const ddr_config_t *config, uint8_t *ddrp_regs) {
    uint32_t cycles;
    uint32_t clock_mhz = config->clock_mhz;
    int errors = 0;

    // Create intermediate object buffer (minimal needed for DDRP generation)
    // The vendor tool uses offsets relative to DDRBaseParam object
    // Need at least 0xe4 + 0x88 (228) bytes for DDRP register generation
    uint8_t obj_buffer[0x200]; // 512 bytes to be safe
    memset(obj_buffer, 0, sizeof(obj_buffer));

    memset(ddrp_regs, 0, 0x80); // 128 bytes total

    // ========================================
    // 1. CALCULATE ALL TIMING PARAMETERS
    // ========================================

    // DDR Type encoding (from switch statement at 0x154)
    uint8_t type_field;
    if (config->type == DDR_TYPE_DDR2) {
        type_field = 3;
    } else if (config->type == DDR_TYPE_DDR3) {
        type_field = 0;
    } else if (config->type == DDR_TYPE_LPDDR || config->type == DDR_TYPE_LPDDR2) {
        type_field = 4;
    } else if (config->type == DDR_TYPE_LPDDR3) {
        type_field = 2;
    } else {
        LOG_INFO("[DDR] DDR type not supported for PHY: %d\n", config->type);
        return -1;
    }

    // tWTR (Write to Read Delay) - PHY constraints: 1-6 cycles
    cycles = ddr_ns_to_cycles(config->tWTR, clock_mhz);
    if (cycles < 1 || cycles > 6) {
        LOG_INFO("[DDR] tWTR out of range for PHY: %u (valid: 1-6)\n", cycles);
        errors++;
    }
    uint8_t phy_tWTR = cycles & 0x07; // 3-bit field

    // tRP (Row Precharge) - PHY constraints: 2-11 cycles
    cycles = ddr_ns_to_cycles(config->tRP, clock_mhz);
    if (cycles < 2 || cycles > 11) {
        LOG_INFO("[DDR] tRP out of range for PHY: %u (valid: 2-11)\n", cycles);
        errors++;
    }
    uint8_t phy_tRP = cycles & 0x0f; // 4-bit field

    // tRCD (Row to Column Delay) - PHY constraints: 2-11 cycles
    cycles = ddr_ns_to_cycles(config->tRCD, clock_mhz);
    if (cycles < 2 || cycles > 11) {
        LOG_INFO("[DDR] tRCD out of range for PHY: %u (valid: 2-11)\n", cycles);
        errors++;
    }
    uint8_t phy_tRCD = cycles & 0x0f; // 4-bit field

    // tRAS (Row Active Time) - PHY constraints: 2-31 cycles
    cycles = ddr_ns_to_cycles(config->tRAS, clock_mhz);
    if (cycles < 2 || cycles > 31) {
        LOG_INFO("[DDR] tRAS out of range for PHY: %u (valid: 2-31)\n", cycles);
        errors++;
    }
    uint8_t phy_tRAS = cycles & 0x1f; // 5-bit field

    // tRRD (Row to Row Delay) - PHY constraints: 1-8 cycles
    cycles = ddr_ns_to_cycles(config->tRRD, clock_mhz);
    if (cycles < 1 || cycles > 8) {
        LOG_INFO("[DDR] tRRD out of range for PHY: %u (valid: 1-8)\n", cycles);
        errors++;
    }
    uint16_t phy_tRRD = cycles & 0x0f; // 4-bit field (but crosses byte boundary)

    // tRC (Row Cycle) - PHY constraints: 2-42 cycles
    cycles = ddr_ns_to_cycles(config->tRC, clock_mhz);
    if (cycles < 2 || cycles > 42) {
        LOG_INFO("[DDR] tRC out of range for PHY: %u (valid: 2-42)\n", cycles);
        errors++;
    }
    uint8_t phy_tRC = cycles & 0x3f; // 6-bit field

    // tRFC (Refresh to Active) - PHY constraints: 0-255 cycles
    cycles = ddr_ns_to_cycles(config->tRFC, clock_mhz);
    if (cycles > 255) {
        LOG_INFO("[DDR] tRFC out of range for PHY: %u (valid: 0-255)\n", cycles);
        errors++;
    }
    uint8_t phy_tRFC = cycles & 0xff; // 8-bit field

    if (errors > 0) {
        return -1;
    }

    // ========================================
    // 2. PACK TIMING INTO OBJECT OFFSETS
    // ========================================
    // These match the object structure offsets from the vendor tool
    // Following vendor tool ddrp_generate_register() algorithm

    // Offset 0xcc: CAS Latency (bits 3+) | Type (bits 0-2)
    *(uint32_t *)(obj_buffer + 0xcc) = (config->cas_latency << 3) | type_field;

    // Offset 0xf0: tWTR stored at bits 5-7 (shifted left 5, mask lower bits)
    obj_buffer[0xf0] = (obj_buffer[0xf0] & 0x1f) | (phy_tWTR << 5);

    // Offset 0xf1: tRP in bits 0-3, tRCD in bits 4-7
    uint8_t f1_val = (obj_buffer[0xf1] & 0xf0) | (phy_tRP & 0x0f);
    f1_val = (f1_val & 0x0f) | (phy_tRCD << 4);
    obj_buffer[0xf1] = f1_val;

    // Offset 0xf2-0xf3 bit layout (16-bit LE word):
    //   Byte 0xf2: [4:0] = tRAS (5 bits), [7:5] = tRRD low 3 bits
    //   Byte 0xf3: [0]   = tRRD bit 3 (MSB), [6:1] = tRC (6 bits), [7] = reserved
    //
    // tRAS is written first (bits 0-4 of 0xf2).
    // tRRD spans bits 5-8 (across byte boundary): bits 5-7 of 0xf2 and bit 0 of 0xf3.
    // tRC occupies bits 1-6 of 0xf3 (mask 0x81 preserves bit 0 = tRRD MSB, and bit 7).
    obj_buffer[0xf2] = (obj_buffer[0xf2] & 0xe0) | (phy_tRAS & 0x1f);
    uint16_t f2_word = obj_buffer[0xf2] | (obj_buffer[0xf3] << 8);
    f2_word = (f2_word & 0xfe1f) | ((phy_tRRD & 0x0f) << 5);
    obj_buffer[0xf2] = f2_word & 0xff;
    obj_buffer[0xf3] = (f2_word >> 8) & 0xff;

    // tRC in bits 1-6 of 0xf3. Mask 0x81 preserves bit 0 (tRRD MSB) and bit 7.
    obj_buffer[0xf3] = (obj_buffer[0xf3] & 0x81) | ((phy_tRC & 0x3f) << 1);

    // Offset 0xf6: tRFC (full byte)
    obj_buffer[0xf6] = phy_tRFC;

    // Offsets 0xe4, 0xe5, 0xe6: PHY-specific calculations
    // Clock-based calculations

    // e4[0]: max(8, tCK_parameter) & 0x3f where tCK_parameter = (clock + 49999) / clock
    uint32_t tck_param = (clock_mhz + 49999) / clock_mhz;
    if (tck_param < 8)
        tck_param = 8;
    obj_buffer[0xe4] = (obj_buffer[0xe4] & 0xc0) | (tck_param & 0x3f);

    // e4 bits 6-17: PHY timing = (clock + 0x4e1fff) / clock
    uint32_t phy_timing = (clock_mhz + 0x4e1fff) / clock_mhz;
    uint32_t e4_val = obj_buffer[0xe4] | (obj_buffer[0xe5] << 8) | (obj_buffer[0xe6] << 16);
    e4_val = (e4_val & 0xfffc003f) | ((phy_timing & 0xfff) << 6);
    obj_buffer[0xe4] = e4_val & 0xff;
    obj_buffer[0xe5] = (e4_val >> 8) & 0xff;
    obj_buffer[0xe6] = (e4_val >> 16) & 0xff;

    // e6: set bits 5 (mask 0xc3 | 0x20)
    obj_buffer[0xe6] = (obj_buffer[0xe6] & 0xc3) | 0x20;

    // fa: timing value (word at offset 0xfa-0xfb)
    uint16_t fa_val = obj_buffer[0xfa] | (obj_buffer[0xfb] << 8);
    fa_val = (fa_val & 0xe007) | 0x1000;
    obj_buffer[0xfa] = fa_val & 0xff;
    obj_buffer[0xfb] = (fa_val >> 8) & 0xff;

    // ========================================
    // 3. COPY OBJECT OFFSETS TO OUTPUT BINARY
    // ========================================
    // The DDRP section (128 bytes) is filled from object buffer
    // Based on the vendor tool's ddr_convert_param() function:
    // The DDRP output layout copies from object offsets to output offsets

    // Copy main DDRP data starting from object offset 0xe4 (128 bytes = 0x80)
    // This includes all the PHY timing calculations
    memcpy(ddrp_regs, obj_buffer + 0xe4, 0x80);

    return 0;
}

// Internal: Generate DDRP using shared object buffer
// This version takes a shared object buffer that's populated by both DDRC and DDRP generators
// Dispatches to type-specific implementations based on DDR type
int ddr_generate_ddrp_with_object(const ddr_config_t *config, uint8_t *obj_buffer, uint8_t *ddrp_regs) {
    // Dispatch to type-specific implementation
    switch (config->type) {
    case DDR_TYPE_DDR2:
        return ddr_generate_ddrp_ddr2(config, obj_buffer, ddrp_regs);

    case DDR_TYPE_DDR3:
        // TODO: Implement DDR3-specific algorithm
        LOG_INFO("[DDR] DDR3 DDRP generation not yet implemented\n");
        return -1;

    case DDR_TYPE_LPDDR:
    case DDR_TYPE_LPDDR2:
    case DDR_TYPE_LPDDR3:
        // Fall through to base class implementation below
        break;

    default:
        LOG_INFO("[DDR] Unsupported DDR type: %d\n", config->type);
        return -1;
    }

    // ========================================
    // BASE CLASS IMPLEMENTATION (for LPDDR types)
    // ========================================
    // This is the original DDRBaseParam::ddrp_generate_register algorithm
    // from the vendor tool's DDRBaseParam::ddrp_generate_register algorithm

    uint32_t cycles;
    uint32_t clock_mhz = config->clock_mhz;
    int errors = 0;

    memset(ddrp_regs, 0, 0x80); // 128 bytes total

    // DDR Type encoding (from switch statement at 0x154)
    uint8_t type_field;
    if (config->type == DDR_TYPE_LPDDR || config->type == DDR_TYPE_LPDDR2) {
        type_field = 4;
    } else if (config->type == DDR_TYPE_LPDDR3) {
        type_field = 2;
    } else {
        LOG_INFO("[DDR] DDR type not supported for base PHY: %d\n", config->type);
        return -1;
    }

    // tWTR (Write to Read Delay) - PHY constraints: 1-6 cycles
    cycles = ddr_ns_to_cycles(config->tWTR, clock_mhz);
    if (cycles < 1 || cycles > 6) {
        LOG_INFO("[DDR] tWTR out of range for PHY: %u (valid: 1-6)\n", cycles);
        errors++;
    }
    uint8_t phy_tWTR = cycles & 0x07; // 3-bit field

    // tRP (Row Precharge) - PHY constraints: 2-11 cycles
    cycles = ddr_ns_to_cycles(config->tRP, clock_mhz);
    if (cycles < 2 || cycles > 11) {
        LOG_INFO("[DDR] tRP out of range for PHY: %u (valid: 2-11)\n", cycles);
        errors++;
    }
    uint8_t phy_tRP = cycles & 0x0f; // 4-bit field

    // tRCD (Row to Column Delay) - PHY constraints: 2-11 cycles
    cycles = ddr_ns_to_cycles(config->tRCD, clock_mhz);
    if (cycles < 2 || cycles > 11) {
        LOG_INFO("[DDR] tRCD out of range for PHY: %u (valid: 2-11)\n", cycles);
        errors++;
    }
    uint8_t phy_tRCD = cycles & 0x0f; // 4-bit field

    // tRAS (Row Active Time) - PHY constraints: 2-31 cycles
    cycles = ddr_ns_to_cycles(config->tRAS, clock_mhz);
    if (cycles < 2 || cycles > 31) {
        LOG_INFO("[DDR] tRAS out of range for PHY: %u (valid: 2-31)\n", cycles);
        errors++;
    }
    uint8_t phy_tRAS = cycles & 0x1f; // 5-bit field

    // tRRD (Row to Row Delay) - PHY constraints: 1-8 cycles
    cycles = ddr_ns_to_cycles(config->tRRD, clock_mhz);
    if (cycles < 1 || cycles > 8) {
        LOG_INFO("[DDR] tRRD out of range for PHY: %u (valid: 1-8)\n", cycles);
        errors++;
    }
    uint16_t phy_tRRD = cycles & 0x0f; // 4-bit field (but crosses byte boundary)

    // tRC (Row Cycle) - PHY constraints: 2-42 cycles
    cycles = ddr_ns_to_cycles(config->tRC, clock_mhz);
    if (cycles < 2 || cycles > 42) {
        LOG_INFO("[DDR] tRC out of range for PHY: %u (valid: 2-42)\n", cycles);
        errors++;
    }
    uint8_t phy_tRC = cycles & 0x3f; // 6-bit field

    // tRFC (Refresh to Active) - PHY constraints: 0-255 cycles
    cycles = ddr_ns_to_cycles(config->tRFC, clock_mhz);
    if (cycles > 255) {
        LOG_INFO("[DDR] tRFC out of range for PHY: %u (valid: 0-255)\n", cycles);
        errors++;
    }
    uint8_t phy_tRFC = cycles & 0xff; // 8-bit field

    if (errors > 0) {
        return -1;
    }

    // ========================================
    // 2. PACK TIMING INTO SHARED OBJECT BUFFER
    // ========================================
    // These match the object structure offsets from the vendor tool
    // Following vendor tool ddrp_generate_register() algorithm

    // Offset 0xcc: CAS Latency (bits 3+) | Type (bits 0-2)
    // NOTE: DDRC also sets this, DDRP writes the same value (CAS latency from config)
    *(uint32_t *)(obj_buffer + 0xcc) = (config->cas_latency << 3) | type_field;

    // Offset 0xf0: tWTR stored at bits 5-7 (shifted left 5, mask lower bits)
    obj_buffer[0xf0] = (obj_buffer[0xf0] & 0x1f) | (phy_tWTR << 5);

    // Offset 0xf1: tRP in bits 0-3, tRCD in bits 4-7
    uint8_t f1_val = (obj_buffer[0xf1] & 0xf0) | (phy_tRP & 0x0f);
    f1_val = (f1_val & 0x0f) | (phy_tRCD << 4);
    obj_buffer[0xf1] = f1_val;

    // Offset 0xf2-0xf3 bit layout (16-bit LE word):
    //   Byte 0xf2: [4:0] = tRAS (5 bits), [7:5] = tRRD low 3 bits
    //   Byte 0xf3: [0]   = tRRD bit 3 (MSB), [6:1] = tRC (6 bits), [7] = reserved
    //
    // tRAS is written first (bits 0-4 of 0xf2).
    // tRRD spans bits 5-8 (across byte boundary): bits 5-7 of 0xf2 and bit 0 of 0xf3.
    // tRC occupies bits 1-6 of 0xf3 (mask 0x81 preserves bit 0 = tRRD MSB, and bit 7).
    obj_buffer[0xf2] = (obj_buffer[0xf2] & 0xe0) | (phy_tRAS & 0x1f);
    uint16_t f2_word = obj_buffer[0xf2] | (obj_buffer[0xf3] << 8);
    f2_word = (f2_word & 0xfe1f) | ((phy_tRRD & 0x0f) << 5);
    obj_buffer[0xf2] = f2_word & 0xff;
    obj_buffer[0xf3] = (f2_word >> 8) & 0xff;

    // tRC in bits 1-6 of 0xf3. Mask 0x81 preserves bit 0 (tRRD MSB) and bit 7.
    obj_buffer[0xf3] = (obj_buffer[0xf3] & 0x81) | ((phy_tRC & 0x3f) << 1);

    // Offset 0xf6: tRFC (full byte)
    obj_buffer[0xf6] = phy_tRFC;

    // Offsets 0xe4, 0xe5, 0xe6: PHY-specific calculations
    // Clock-based calculations

    // e4[0]: max(8, tCK_parameter) & 0x3f where tCK_parameter = (clock + 49999) / clock
    uint32_t tck_param = (clock_mhz + 49999) / clock_mhz;
    if (tck_param < 8)
        tck_param = 8;
    obj_buffer[0xe4] = (obj_buffer[0xe4] & 0xc0) | (tck_param & 0x3f);

    // e4 bits 6-17: PHY timing = (clock + 0x4e1fff) / clock
    uint32_t phy_timing = (clock_mhz + 0x4e1fff) / clock_mhz;
    uint32_t e4_val = obj_buffer[0xe4] | (obj_buffer[0xe5] << 8) | (obj_buffer[0xe6] << 16);
    e4_val = (e4_val & 0xfffc003f) | ((phy_timing & 0xfff) << 6);
    obj_buffer[0xe4] = e4_val & 0xff;
    obj_buffer[0xe5] = (e4_val >> 8) & 0xff;
    obj_buffer[0xe6] = (e4_val >> 16) & 0xff;

    // e6: set bits 5 (mask 0xc3 | 0x20)
    obj_buffer[0xe6] = (obj_buffer[0xe6] & 0xc3) | 0x20;

    // fa: timing value (word at offset 0xfa-0xfb)
    uint16_t fa_val = obj_buffer[0xfa] | (obj_buffer[0xfb] << 8);
    fa_val = (fa_val & 0xe007) | 0x1000;
    obj_buffer[0xfa] = fa_val & 0xff;
    obj_buffer[0xfb] = (fa_val >> 8) & 0xff;

    // ========================================
    // 3. COPY OBJECT OFFSETS TO OUTPUT BINARY
    // ========================================
    // The DDRP section has a specific structure:
    // [0x00-0x03]: Size marker = 0x7c (124 bytes of data following)
    // [0x04-0x7F]: PHY timing data (124 bytes)
    //
    // NOTE: This is the BASE CLASS algorithm. For DDR2/DDR3, the vendor tool
    // uses type-specific implementations that write to obj_buffer[0xd0-0xec]
    // instead of 0xe4-0xfc. See .zencoder/DDR_ISSUE_RESOLVED.md for details.
    //
    // TODO: Implement ddr_generate_ddrp_ddr2() and ddr_generate_ddrp_ddr3()
    // based on vendor DDR2/DDR3-specific algorithms.

    // Write size marker at the beginning
    uint32_t size_marker = 0x0000007c;
    memcpy(ddrp_regs + 0x00, &size_marker, sizeof(size_marker));

    // Copy PHY timing data from object buffer
    // For DDR2/DDR3: should copy from obj_buffer[0xd0]
    // For base class (LPDDR): copies from obj_buffer[0xe4]
    // Currently using base class algorithm - this is WRONG for DDR2/DDR3!
    memcpy(ddrp_regs + 0x04, obj_buffer + 0xd0, 0x7c);

    return 0;
}