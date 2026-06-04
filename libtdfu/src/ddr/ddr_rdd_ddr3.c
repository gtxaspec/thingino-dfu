/**
 * DDR3 INNO PHY RDD builder
 *
 * Fills DDR3-specific fields in the 124-byte INNO PHY RDD data buffer.
 * Used by T31A SPLs with DDR3 memory.
 *
 * DDR3 differences from DDR2:
 * - CWL/CL derived from frequency (not chip spec directly)
 * - MR0 encoding: BL=0(8), CL-4, WR mapping from JEDEC
 * - MR1/MR2/MR3 populated with bank address prefixes
 * - Timing adjustments: tRAS+=2, tRC+=1, tRFC=(cycles+1)*3/2
 * - Geometry: row+1, col-7+1 in INNO format
 *
 * Source: Vendor T31A USB capture + ingenic-u-boot-xburst1/tools/ingenic-tools/ddr3_params.c
 */

#include "ddr_binary_builder.h"
#include <string.h>

extern void write_u32_le(uint8_t *buf, uint32_t value);

void ddr_rdd_fill_ddr3(uint8_t *rdd_data, const platform_config_t *platform, const ddr_phy_params_t *params,
                       uint32_t memsize) {
    (void)memsize;
    /* DDR3 CWL/CL depend on frequency.
     * At 400MHz: CL=7, CWL=6 (vendor T31A capture verified). */
    uint32_t ps_per_tck = 1000000000 / (platform->ddr_freq / 1000);
    int cwl;
    if (ps_per_tck >= 2500)
        cwl = 6; /* 400MHz */
    else if (ps_per_tck >= 1875)
        cwl = 6; /* 533MHz */
    else if (ps_per_tck >= 1500)
        cwl = 7; /* 667MHz */
    else
        cwl = 8; /* 800MHz+ */

    uint8_t eff_wl = cwl;
    uint8_t eff_rl = cwl + 1;
    uint8_t eff_cl = eff_rl;

    /* [0x1c-0x1f] Geometry: row+1, col-7+1 for DDR3 */
    rdd_data[0x1c] = eff_wl;
    rdd_data[0x1d] = eff_wl;
    rdd_data[0x1e] = params->row_bits + 1;
    rdd_data[0x1f] = params->col_bits - 7 + 1;

    /* Timing adjustments: tRCD >= CL, then tRAS/tRC adjusted */
    uint8_t eff_tRCD = params->tRCD;
    uint8_t eff_tRAS = params->tRAS;
    uint8_t eff_tRC = params->tRC;
    if (eff_tRCD < eff_cl)
        eff_tRCD = eff_cl;
    if (eff_tRAS < eff_tRCD + params->tRP)
        eff_tRAS = eff_tRCD + params->tRP;
    eff_tRC = eff_tRAS + params->tRP;
    eff_tRAS += 2;
    eff_tRC += 1;

    /* tRFC: 1.5x factor for DDR3 */
    uint8_t tRFC_field = (uint8_t)((params->tRFC + 1) * 3 / 2);

    /* [0x20-0x2b] Timing bytes */
    rdd_data[0x20] = eff_tRCD;
    rdd_data[0x21] = params->tRP;
    rdd_data[0x22] = eff_tRAS;
    rdd_data[0x23] = params->tCCD;
    rdd_data[0x24] = eff_tRC;
    rdd_data[0x25] = params->tRRD;
    rdd_data[0x26] = eff_wl;       /* DDR3: WL in timing byte */
    rdd_data[0x27] = (4 << 3) | 1; /* DDR3: ONUM=4|1 */
    rdd_data[0x28] = tRFC_field;
    rdd_data[0x29] = 0x00;
    rdd_data[0x2a] = params->tXSRD / 2;
    rdd_data[0x2b] = tRFC_field > 4 ? (tRFC_field - 4) / 2 : 0;

    /* [0x2c] DDRC_TIMING5: [tRDLAT, tCKE+1, RL, 0xFF] */
    write_u32_le(rdd_data + 0x2c, (eff_rl - 2) | ((params->tCKE + 1) << 8) | (eff_rl << 16) | (0xFF << 24));

    /* [0x30] DDRC_TIMING6: [5, 5, tFAW, 0x80] */
    write_u32_le(rdd_data + 0x30, 5 | (5 << 8) | (params->tFAW << 16) | (0x80 << 24));

    /* [0x38] REFCNT config */
    write_u32_le(rdd_data + 0x38, params->tREFI > 0 ? 0x10 : 0);

    /* [0x3c] RL, [0x40] WL */
    write_u32_le(rdd_data + 0x3c, eff_rl);
    write_u32_le(rdd_data + 0x40, eff_wl);

    /* [0x44] DDR3 MR0: [1:0]BL [6:4]CL-4 [11:9]WR [12]PD */
    int mr0_bl = (params->bl == 8) ? 0 : 2;
    int mr0_cl = eff_cl - 4;
    int mr0_wr = params->tWR;
    if (mr0_wr >= 5 && mr0_wr <= 8)
        mr0_wr -= 4;
    else if (mr0_wr >= 9)
        mr0_wr = (mr0_wr + 1) / 2;
    write_u32_le(rdd_data + 0x44, mr0_bl | (mr0_cl << 4) | (mr0_wr << 9));

    /* [0x48] DDR3 MR1 + bank address 1 */
    write_u32_le(rdd_data + 0x48, 0x00010000 | 0x0007);

    /* [0x4c] DDR3 MR2: [5:3]CWL-5 + bank address 2 */
    write_u32_le(rdd_data + 0x4c, 0x00020000 | (((eff_wl - 5) & 0x7) << 3));

    /* [0x50] DDR3 MR3 + bank address 3 */
    write_u32_le(rdd_data + 0x50, 0x00030000);
}
