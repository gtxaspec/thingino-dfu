/**
 * DDR2 INNO PHY RDD builder
 *
 * Fills DDR2-specific fields in the 124-byte INNO PHY RDD data buffer.
 * Used by T21/T23/T30/T31 family SPLs with DDR2 memory.
 *
 * Source: Derived from vendor reference binaries and verified
 * against ingenic-u-boot-xburst1/tools/ingenic-tools/ddr2_params.c
 */

#include "ddr_binary_builder.h"
#include <string.h>

extern void write_u32_le(uint8_t *buf, uint32_t value);

void ddr_rdd_fill_ddr2(uint8_t *rdd_data, const platform_config_t *platform, const ddr_phy_params_t *params,
                       uint32_t memsize) {
    (void)platform;
    (void)memsize;
    uint8_t rl = params->rl;
    uint8_t wl = params->wl;

    /* [0x1c-0x1f] Geometry */
    rdd_data[0x1c] = wl;
    rdd_data[0x1d] = wl;
    rdd_data[0x1e] = params->row_bits;
    rdd_data[0x1f] = params->col_bits - 7;

    /* [0x20-0x2b] Timing bytes */
    uint8_t tRFC_field = params->tRFC > 2 ? params->tRFC - 2 : 0;
    rdd_data[0x20] = params->tRCD;
    rdd_data[0x21] = params->tRP;
    rdd_data[0x22] = params->tRAS;
    rdd_data[0x23] = params->tCCD;
    rdd_data[0x24] = params->tRC;
    rdd_data[0x25] = params->tRRD;
    rdd_data[0x26] = rl;
    rdd_data[0x27] = 4 << 3; /* ONUM=4 */
    rdd_data[0x28] = tRFC_field;
    rdd_data[0x29] = 0x00;
    rdd_data[0x2a] = params->tXSRD / 2;
    rdd_data[0x2b] = (tRFC_field + 1) / 2;

    /* [0x2c] DDRC_TIMING5: [tRDLAT, tCKE+1, WL, 0xFF] */
    write_u32_le(rdd_data + 0x2c, (rl - 2) | ((params->tCKE + 1) << 8) | (wl << 16) | (0xFF << 24));

    /* [0x30] DDRC_TIMING6: [5, 5, tFAW, tXSRD/4] */
    write_u32_le(rdd_data + 0x30, 5 | (5 << 8) | (params->tFAW << 16) | ((params->tXSRD / 4) << 24));

    /* [0x38] REFCNT config */
    write_u32_le(rdd_data + 0x38, params->tREFI > 0 ? 0x11 : 0);

    /* [0x3c] RL, [0x40] WL */
    write_u32_le(rdd_data + 0x3c, rl);
    write_u32_le(rdd_data + 0x40, wl);

    /* [0x44] DDR2 MR0: [2:0]BL [6:4]CL [11:9]WR */
    uint32_t mr0 = (params->tWR - 1) << 9;
    mr0 |= (params->cl & 0x7) << 4;
    mr0 |= (params->bl == 8 ? 0 : 1) << 3;                    /* BT */
    mr0 |= (params->bl == 4 ? 2 : (params->bl == 8 ? 3 : 0)); /* BL */
    write_u32_le(rdd_data + 0x44, mr0);

    /* [0x48] tXSR */
    write_u32_le(rdd_data + 0x48, params->tXSR > 0 ? params->tXSR : 0x2000);
}
