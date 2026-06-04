/**
 * FIDB builder for xburst1 platforms (T10/T20/T21/T23/T30/T31 family)
 *
 * struct global_info layout:
 *   0x00-0x17: extal, cpufreq, ddrfreq, ddr_div, uart_idx, baud_rate
 *   0x18+: platform-specific (nr_gpio_func, gpio[], ddr_test)
 *
 * Values at 0x18+ verified against vendor reference binaries on hardware.
 */

#include "ddr_binary_builder.h"
#include "ddr_config_database.h"
#include <string.h>

extern void write_u32_le(uint8_t *buf, uint32_t value);

#ifndef DDR_PLATFORM_ID
#define DDR_PLATFORM_ID 0x19800000
#endif

void ddr_fidb_fill_xb1(uint8_t *d, const platform_config_t *platform) {
    /* Standard global_info header */
    write_u32_le(d + 0x00, platform->crystal_freq);
    write_u32_le(d + 0x04, platform->cpu_freq);
    write_u32_le(d + 0x08, platform->ddr_freq);
    write_u32_le(d + 0x0c, 0); /* ddr_div: 0 = auto */
    write_u32_le(d + 0x10, platform->uart_idx);
    write_u32_le(d + 0x14, platform->uart_baud);

    /* Platform-specific fields after baud_rate.
     * +0x18: nr_gpio_func / flag
     * +0x20: mem_size (xb1) or UART GPIO mask (T40/T41 xb2)
     * +0x24: flag
     * +0x2c: config (0x11 for xb1, 0x01 for xb2 T40/T41)
     * +0x30: SFC GPIO pin mask (platform-specific)
     * Vendor verified on T20, T21, T30, T31, T31A, T40. */
    write_u32_le(d + 0x18, 0x00000001);
    write_u32_le(d + 0x20, platform->mem_size); /* mem_size or UART GPIO mask */
    if (platform->is_xburst2) {
        /* T40/T41: verified against vendor pcap for T40NN and T40XP.
         * +0x20=0x01800000, +0x24=1, +0x2c=1, +0x30=sfc_gpio.
         * +0x38 is DDR3 flag: 0 for DDR2, 1 for DDR3. */
        write_u32_le(d + 0x20, 0x01800000);
        write_u32_le(d + 0x24, 0x00000001);
        write_u32_le(d + 0x2c, 0x00000001);
        write_u32_le(d + 0x30, 0x1f800000); /* SFC GPIO: sfc,pa_4bit */
        /* DDR3 flag: determine from DDR chip config */
        {
            const char *pname = platform->name;
            const ddr_chip_config_t *chip = ddr_chip_config_get_default(pname);
            uint32_t is_ddr3 = (chip && (chip->ddr_type == 0 || chip->ddr_type == 3)) ? 1 : 0;
            write_u32_le(d + 0x38, is_ddr3);
        }
    } else if (platform->ddr_freq == 200000000) {
        /* T32/PRJ007: different config field (0x00 not 0x11).
         * Verified from t32_vendor_write.pcap DDR binary. */
        write_u32_le(d + 0x24, 0x00000001);
        write_u32_le(d + 0x2c, 0x00000000);
        write_u32_le(d + 0x30, DDR_PLATFORM_ID);
    } else {
        /* T31/T30/T21/T20/T10: standard xburst1 config */
        write_u32_le(d + 0x24, 0x00000001);
        write_u32_le(d + 0x2c, 0x00000011);
        write_u32_le(d + 0x30, DDR_PLATFORM_ID);
    }
}
