/**
 * FIDB builder for xburst2 platforms (A1, T40, T41)
 *
 * The xburst2 struct global_info has a different layout than xburst1.
 * Field values extracted from vendor A1 USB capture (384-byte DDR binary).
 *
 * Layout (verified from vendor capture + xburst2 U-Boot global_info.h):
 *   0x00: extal, cpufreq, ddrfreq, ddr_div, uart_idx, baud_rate (24 bytes)
 *   0x18: platform-specific config fields (GPIO, SFC config)
 *   Remaining: zeros
 */

#include "ddr_binary_builder.h"
#include <string.h>

extern void write_u32_le(uint8_t *buf, uint32_t value);

void ddr_fidb_fill_xb2(uint8_t *d, const platform_config_t *platform) {
    /* Standard global_info header (shared with xburst1) */
    write_u32_le(d + 0x00, platform->crystal_freq);
    write_u32_le(d + 0x04, platform->cpu_freq);
    write_u32_le(d + 0x08, platform->ddr_freq);
    write_u32_le(d + 0x0c, 0); /* ddr_div: 0 = auto */
    write_u32_le(d + 0x10, platform->uart_idx);
    write_u32_le(d + 0x14, platform->uart_baud);

    /* xburst2 platform-specific fields.
     * Values from vendor A1 capture (verified working with UART). */
    write_u32_le(d + 0x18, 0x00000002);
    write_u32_le(d + 0x20, 0x000000C0);
    write_u32_le(d + 0x24, 0x00000001);
    write_u32_le(d + 0x28, 0x00000002);
    write_u32_le(d + 0x30, 0x03f00000);
}
