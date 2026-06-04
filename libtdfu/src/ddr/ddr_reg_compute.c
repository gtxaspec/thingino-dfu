/**
 * DDR Register Computation
 *
 * Implements the same register value computation as u-boot's
 * ddr_params_creator tool. Produces struct ddr_registers that
 * the burner SPL reads directly.
 *
 * Source reference:
 *   thingino-uboot/tools/ingenic-tools/ddr_params_creator.c
 *   thingino-uboot/tools/ingenic-tools/ddr2_params.c
 *   thingino-uboot/include/ddr/ddr_params.h
 */

#include "ddr_reg_data.h"
#include "ddr_config_database.h"
#include <string.h>

/* ps2cycle_ceil from u-boot ddr_params_creator.c */
static int ps2cyc(int ps, int ps_per_tck, int div_tck) {
    if (ps <= 0)
        return 0;
    return (ps + div_tck * ps_per_tck - 1) / ps_per_tck;
}

static int ps2floor(int ps, int ps_per_tck) {
    if (ps <= 0)
        return 0;
    return ps / ps_per_tck;
}

static int clamp(int val, int lo, int hi) {
    if (val < lo)
        return lo;
    if (val > hi)
        return hi;
    return val;
}

/* Compute tREFI divider (from u-boot ddr_refi_div) */

/* Memory size calculation (from u-boot sdram_size) */
static uint32_t calc_mem_size(int row, int col, int bank8, int dw32) {
    int banks = bank8 ? 8 : 4;
    int dw = dw32 ? 4 : 2;
    return (1u << (row + col)) * dw * banks;
}

void ddr_compute_registers(const ddr_chip_config_t *chip, uint32_t ddr_freq_hz, uint8_t row, uint8_t col, uint8_t bank8,
                           uint8_t dw32, struct ddr_registers *regs) {

    memset(regs, 0, sizeof(*regs));

    int pstck = 1000000000 / (ddr_freq_hz / 1000);

    /* Determine DDR type encodings */
    int is_ddr2 = (chip->ddr_type == 1); /* our database: DDR2=1 */
    int is_ddr3 = (chip->ddr_type == 0); /* our database: DDR3=0 */
    (void)is_ddr3;

    /* Timing in cycles */
    int tWR = ps2cyc(chip->tWR, pstck, 1);
    int tWL = chip->wl;
    int tRL = chip->rl;
    int tRAS = ps2cyc(chip->tRAS, pstck, 1);
    int tRCD = ps2cyc(chip->tRCD, pstck, 1);
    int tRP = ps2cyc(chip->tRP, pstck, 1);
    int tRC = ps2cyc(chip->tRC, pstck, 1);
    int tRRD = ps2cyc(chip->tRRD, pstck, 1);
    int tWTR = ps2cyc(chip->tWTR, pstck, 1);
    int tRFC = ps2cyc(chip->tRFC, pstck, 2) / 2 - 1;
    int tXP = (chip->tXP > 0) ? chip->tXP : 3;
    int tCKE = (chip->tCKE > 0) ? chip->tCKE : 3;
    int tRTP = ps2cyc(chip->tRTP, pstck, 1);
    int tCCD = (chip->tCCD > 0) ? chip->tCCD : 2;
    int tFAW = ps2cyc(chip->tFAW, pstck, 1);
    int tMRD = (chip->tMRD > 0) ? chip->tMRD : 2;
    int tREFI = ps2floor(chip->tREFI, pstck);
    int tXSRD = (chip->tXSRD > 0) ? chip->tXSRD : 200;

    if (tRFC < 0)
        tRFC = 0;
    if (tRFC > 63)
        tRFC = 63;

    /* ===== DDRC registers ===== */

    /* DDRC_CFG - bit layout from ddrc.h:
     * [0]DW [1]BA0 [5:2]CL [6]CS0EN [7]CS1EN [10:8]COL0 [13:11]ROW0
     * [14]rsv [15]MISPE [16]ODTEN [19:17]TYPE [20]rsv [21]BSL
     * [22]IMBA [23]BA1 [26:24]COL1 [29:27]ROW1 */
    int ddrc_type = is_ddr2 ? 4 : 6;                     /* DDR2=4, DDR3=6 in DDRC */
    regs->ddrc_cfg = dw32                                /* [0] DW */
                     | (bank8 << 1)                      /* [1] BA0 */
                     | ((uint32_t)(chip->cl & 0xF) << 2) /* [5:2] CL */
                     | (1u << 6)                         /* [6] CS0EN */
                     | ((uint32_t)(col - 8) << 8)        /* [10:8] COL0 */
                     | ((uint32_t)(row - 12) << 11)      /* [13:11] ROW0 */
                     | (1u << 15)                        /* [15] MISPE */
                     | ((uint32_t)ddrc_type << 17)       /* [19:17] TYPE */
                     | ((chip->bl == 8 ? 1u : 0u) << 21) /* [21] BSL */
                     | (1u << 22)                        /* [22] IMBA */
                     | ((uint32_t)bank8 << 23)           /* [23] BA1 */
                     | ((uint32_t)(col - 8) << 24)       /* [26:24] COL1 */
                     | ((uint32_t)(row - 12) << 27);     /* [29:27] ROW1 */

    /* DDRC_CTRL - vendor uses 0xc91a for T20 */
    regs->ddrc_ctrl = is_ddr2 ? 0x0000c91a : 0x0000d91e;

    /* DDRC_MMAP */
    uint32_t memsize = calc_mem_size(row, col, bank8, dw32);
    uint32_t mem_base0 = 0x20; /* DDR_MEM_PHY_BASE >> 24 */
    uint32_t mem_mask0 = ~((memsize >> 24) - 1) & 0xFF;
    regs->ddrc_mmap[0] = (mem_base0 << 8) | mem_mask0;
    /* MMAP1: CS1 disabled. Base = chip0 end address >> 24 */
    uint32_t cs1_base = mem_base0 + (memsize >> 24);
    regs->ddrc_mmap[1] = (cs1_base << 8);

    /* DDRC_REFCNT: [0]REF_EN [23:16]refcnt_value
     * refcnt_value = (tREFI_tck / 16) - 1 */
    regs->ddrc_refcnt = ((tREFI / 16 - 1) << 16) | 1;

    /* DDRC_TIMING1-6: 6-bit fields with 2-bit reserved gaps (8-bit spacing).
     * Field order from include/ddr/ddrc.h bitfield structs. */

    /* DDRC_TIMING1-6: layouts from include/ddr/ddrc.h bitfield structs */

    /* TIMING1: [5:0]tWL [13:8]tWR [21:16]tWTR [29:24]tRTP
     * Note: u-boot sets tWTR = tWL + ceil(tWTR_ps/tck) + BL/2 for DDR2 */
    int tWTR_reg = is_ddr2 ? (tWL + tWTR + chip->bl / 2) : tWTR;
    regs->ddrc_timing1 =
        clamp(tWL, 0, 63) | (clamp(tWR, 0, 63) << 8) | (clamp(tWTR_reg, 0, 63) << 16) | (clamp(tRTP, 0, 63) << 24);

    /* TIMING2: [5:0]tRL [13:8]tRCD [21:16]tRAS [29:24]tCCD */
    regs->ddrc_timing2 =
        clamp(tRL, 0, 63) | (clamp(tRCD, 0, 63) << 8) | (clamp(tRAS, 0, 63) << 16) | (clamp(tCCD, 0, 63) << 24);

    /* TIMING3: [5:0]tRC [13:8]tRRD [21:16]tRP [26:24]tCKSRE [30:27]ONUM */
    regs->ddrc_timing3 =
        clamp(tRC, 0, 63) | (clamp(tRRD, 0, 63) << 8) | (clamp(tRP, 0, 63) << 16) | (4u << 27); /* ONUM=4, tCKSRE=0 */

    /* TIMING4: [1:0]tMRD [6:4]tXP [11:8]tMINSR [18:16]tCKE
     *          [20:19]tRWCOV [23:21]tEXTRW [29:24]tRFC */
    regs->ddrc_timing4 = clamp(tMRD - 1, 0, 3) | (clamp(tXP, 0, 7) << 4) | (clamp(tCKE + 1, 0, 7) << 16) |
                         (3u << 21)                    /* tEXTRW=3 */
                         | (clamp(tRFC, 0, 63) << 24); /* tRFC already halved */

    /* TIMING5: [5:0]tWDLAT [13:8]tRDLAT [21:16]tRTW [31:24]tCTLUPD */
    int tRTW = is_ddr2 ? (tRL + tCCD + 2 - tWL + 1) : 0; /* +1 per vendor */
    int tRDLAT = tRL - 2;
    int tWDLAT = tWL - 1;
    regs->ddrc_timing5 = clamp(tWDLAT, 0, 63) | (clamp(tRDLAT, 0, 63) << 8) | (clamp(tRTW, 0, 63) << 16) |
                         (0xFFu << 24); /* tCTLUPD=0xFF default */

    /* TIMING6: [5:0]tCFGR [13:8]tCFGW [21:16]tFAW [29:24]tXSRD */
    int tXSRD_t6 = clamp(tXSRD / 4, 0, 63);
    regs->ddrc_timing6 = 5          /* tCFGR=5 */
                         | (5 << 8) /* tCFGW=5 */
                         | (clamp(tFAW, 0, 63) << 16) | (tXSRD_t6 << 24);

    /* DDRC_AUTOSR_EN */
    regs->ddrc_autosr = 0;

    /* ===== DDRP registers ===== */

    /* DDRP_DCR: DDR2=2, DDR3=3 in DDRP encoding */
    int ddrp_type = is_ddr2 ? 2 : 3;
    regs->ddrp_dcr = ddrp_type | (bank8 << 3);

    /* DDRP_MR0 (DDR2 mode register) */
    if (is_ddr2) {
        int mr0_wr = clamp(tWR - 1, 0, 7);
        int mr0_cl = chip->cl & 0x7;
        int mr0_bl = (chip->bl == 8) ? 3 : 2;
        regs->ddrp_mr0 = (mr0_bl) | (mr0_cl << 4) | (mr0_wr << 9);
    }

    /* DDRP_PTR0: [5:0]tDLLSRST [9:6]tITMSRST [21:10]tDLLLOCK */
    int tDLLSRST = ps2cyc(50000, pstck, 1); /* 50ns in ps */
    if (tDLLSRST < 8)
        tDLLSRST = 8;
    int tDLLLOCK = ps2cyc(5440000, pstck, 1);              /* 5.44us = 2176 * 2.5ns */
    regs->ddrp_ptr0 = (tDLLSRST & 0x3F) | ((0 & 0xF) << 6) /* tITMSRST=0 */
                      | ((tDLLLOCK & 0xFFF) << 10);

    /* DDRP_PTR1: same as PTR0 */
    regs->ddrp_ptr1 = regs->ddrp_ptr0;

    /* DDRP_DTPR0: [1:0]tMRD [4:2]tRTP [8:5]tWTR [11:8]tRP [15:12]tRCD
     *             [20:16]tRAS [24:21]tRRD [30:25]tRC [31]tCCD */
    int dtpr0_tRTP = is_ddr2 ? clamp(tRL - 1, 1, 7) : clamp(tRTP, 1, 7);
    regs->ddrp_dtpr0 = (clamp(tMRD, 0, 3)) | (clamp(dtpr0_tRTP, 0, 7) << 2) | (clamp(tWTR, 1, 15) << 5) |
                       (clamp(tRP, 2, 15) << 8) | (clamp(tRCD, 2, 15) << 12) | (clamp(tRAS, 2, 39) << 16) |
                       (clamp(tRRD, 1, 9) << 21) | (clamp(tRC, 2, 58) << 25) | (0 << 31); /* tCCD=0 for DDR2 */

    /* DDRP_DTPR1: [1:0]tAOND [8:3]tFAW [21:16]tRFC */
    int dtpr1_tFAW = is_ddr2 ? clamp(tFAW, 2, 31) : clamp(tFAW, 2, 63);
    int dtpr1_tRFC = clamp(ps2cyc(chip->tRFC, pstck, 1), 0, 63);
    regs->ddrp_dtpr1 = (0 << 0) /* tAOND=0 for DDR2 */
                       | (dtpr1_tFAW << 3) | (dtpr1_tRFC << 16);

    /* DDRP_DTPR2: [9:0]tXS [14:10]tXP [18:15]tCKE [28:19]tDLLK */
    int dtpr2_tXS = (chip->tXSRD > 0) ? chip->tXSRD : 200;
    /* DDR2 uses tXARDS for DTPR2.tXP, not the tXP field */
    int dtpr2_tXP = is_ddr2 ? (chip->tXARDS > 0 ? chip->tXARDS : tXP) : tXP;
    regs->ddrp_dtpr2 = (clamp(dtpr2_tXS, 0, 1023)) | (clamp(dtpr2_tXP, 0, 31) << 10) | (clamp(tCKE, 0, 15) << 15) |
                       (512 << 19); /* tDLLK=512 */

    /* DDRP_PGCR */
    regs->ddrp_pgcr = 0x01842e02;

    /* DDRP_DXnGCR (byte lane configs) */
    regs->ddrp_dxngcrt[0] = 0x00090881; /* lanes 0,1 enabled */
    regs->ddrp_dxngcrt[1] = 0x00090881;
    regs->ddrp_dxngcrt[2] = 0x00090e80; /* lanes 2,3 default */
    regs->ddrp_dxngcrt[3] = 0x00090e80;

    /* DDRP_ZQNCR1 */
    regs->ddrp_zqncr1 = 0x0000006b;

    /* Impedance */
    regs->ddrp_impedance[0] = 0x00009c40; /* 40000 ohm */
    regs->ddrp_impedance[1] = 0x00009c40;
    regs->ddrp_odt_impedance[0] = 0x0000c350; /* 50000 ohm */
    regs->ddrp_odt_impedance[1] = 0x0000c350;

    /* RZQ table (from u-boot) */
    static const uint8_t rzq[] = {
        0x00, 0x01, 0x02, 0x03, 0x06, 0x07, 0x04, 0x05, 0x0C, 0x0D, 0x0E, 0x0F, 0x0A, 0x0B, 0x08, 0x09,
        0x18, 0x19, 0x1A, 0x1B, 0x1E, 0x1F, 0x1C, 0x1D, 0x14, 0x15, 0x16, 0x17, 0x12, 0x13, 0x10, 0x11,
    };
    memcpy(regs->ddrp_rzq, rzq, 32);

    /* Memory sizes */
    regs->ddr_chip0_size = memsize;
    regs->ddr_chip1_size = 0;

    /* Remap array (from u-boot mem_remap_print) */
    uint8_t remap[20];
    for (int i = 0; i < 20; i++)
        remap[i] = i;
    int bank_bits = bank8 ? 3 : 2;
    int bit_width = dw32 ? 2 : 1;
    int addr_bits = bit_width + col + row + bank_bits;
    int swap_bits = bank_bits;
    int startA = (bit_width + col > 12) ? (bit_width + col) : 12;
    int startB = addr_bits - swap_bits - startA;
    startA -= 12;
    for (int i = 0; i < swap_bits; i++) {
        if (startA + i < 20 && startB + i < 20) {
            uint8_t tmp = remap[startA + i];
            remap[startA + i] = remap[startB + i];
            remap[startB + i] = tmp;
        }
    }
    memcpy(regs->remap_array, remap, 20);
}
