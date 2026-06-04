/**
 * DDR Binary Builder
 *
 * Generates DDR configuration binaries in the format used by Ingenic's cloner tool.
 * The format was derived from working binaries, SPL analysis, and
 * u-boot source analysis.
 *
 * Binary Format (size depends on platform):
 * - FIDB section (platform-dependent): struct global_info (frequencies, UART, GPIO)
 * - RDD section (132 bytes): DDR PHY register values
 *
 * References:
 * - references/ddr_compiler_final.py (Python implementation)
 * - references/ddr_extracted.bin (Reference binary from working device)
 * - Vendor DDR config files
 */

#ifndef DDR_BINARY_BUILDER_H
#define DDR_BINARY_BUILDER_H

#include <stdint.h>
#include <stddef.h>

// Binary format constants - FIDB size is platform-dependent
// Use DDR_BINARY_SIZE_MAX from constants.h for buffer allocation

/**
 * Platform configuration for FIDB section (192 bytes: 8-byte header + 184-byte data)
 *
 * FIDB = "Firmware Information Data Block"
 *
 * Layout (file offsets):
 *   0x00-0x03: "FIDB" magic marker
 *   0x04-0x07: Size (184 bytes = 0xB8)
 *   0x08-0x0B: Crystal frequency (Hz) - e.g., 24000000 (24 MHz)
 *   0x0C-0x0F: CPU frequency (Hz) - e.g., 576000000 (576 MHz)
 *   0x10-0x13: DDR frequency (Hz) - e.g., 400000000 (400 MHz)
 *   0x14-0x17: Reserved (0x00000000)
 *   0x18-0x1B: Enable flag (0x00000001)
 *   0x1C-0x1F: UART baud rate - e.g., 115200
 *   0x20-0x23: Flag (0x00000001)
 *   0x28-0x2B: Memory size (bytes) - e.g., 8388608 (8 MB)
 *   0x2C-0x2F: Flag (0x00000001)
 *   0x34-0x37: Flag (0x00000011)
 *   0x38-0x3B: Platform ID (0x19800000) - T31-specific?
 *   0x3C-0xBF: Reserved/padding (zeros)
 *
 * Source: Derived from analysis of reference DDR binaries
 */
typedef struct {
    const char *name;               // Platform name (e.g., "t40", "t40xp", "t41")
    uint32_t crystal_freq;          // Crystal oscillator frequency in Hz (typically 24 MHz)
    uint32_t cpu_freq;              // CPU frequency in Hz (e.g., 576 MHz for T31)
    uint32_t ddr_freq;              // DDR memory frequency in Hz (e.g., 400 MHz)
    uint32_t uart_baud;             // UART baud rate for bootloader console (typically 115200)
    uint32_t mem_size;              // Total DDR memory size in bytes (e.g., 8 MB = 8388608)
    uint32_t uart_idx;              // UART index for SPL (selects from uart_gpio_func[])
    uint32_t ginfo_ddr_params_size; // sizeof(ddr_params) in global_info (0 if not embedded)
    int use_fidb_header;            // 1=xburst1 (FIDB+RDD headers), 0=xburst2 (raw struct)
    int use_inno_phy_rdd;           // 1=INNO PHY byte-packed RDD (T21/T31), 0=struct ddr_registers (T20/xburst2)
    int is_xburst2;                 // 1=xburst2 (A1/T40/T41), 0=xburst1
    uint8_t ddr_bank8;              // 0=4 banks, 1=8 banks (from vendor board config)
    uint8_t ddr_dw32;               // 0=16-bit, 1=32-bit data bus
    uint8_t ddr_cs0;                // chip select 0 enabled
    uint8_t ddr_cs1;                // chip select 1 enabled
} platform_config_t;

/**
 * DDR PHY parameters for RDD section (132 bytes: 8-byte header + 124-byte data)
 *
 * RDD = "RAM Device Descriptor"
 *
 * Layout (file offsets from 0xC0):
 *   0xC0-0xC3: Header (0x00 + "RDD")
 *   0xC4-0xC7: Size (124 bytes = 0x7C)
 *   0xC8-0xCB: CRC32 checksum (calculated over bytes 0xCC-0x143)
 *   0xCC-0xCF: DDR type (RDD format encoding):
 *                0 = DDR3
 *                1 = DDR2
 *                2 = LPDDR2 / LPDDR
 *                4 = LPDDR3
 *              NOTE: Different from DDRC CFG register (6=DDR3, 4=DDR2, 5=LPDDR2, 3=LPDDR)
 *              NOTE: Different from DDRP DCR register (3=DDR3, 2=DDR2, 4=LPDDR2, 0=LPDDR)
 *              See references/ddr_compiler_final.py line 351 for RDD encoding
 *   0xD0-0xD7: Reserved (zeros)
 *   0xD8-0xDB: Frequency value (ddr_freq / 100000) - e.g., 4000 for 400 MHz
 *   0xDC-0xDF: Frequency value 2 (0x00002800 = 10240) - purpose unknown, possibly tREFI-related
 *   0xE0-0xE3: Fixed values (0x01, 0x00, 0xC2, 0x00) - purpose unknown
 *   0xE4:      CL (CAS Latency) - e.g., 6 or 7 for DDR2
 *   0xE5:      BL (Burst Length) - typically 8 for DDR2/DDR3
 *   0xE6:      ROW bits (stored directly) - e.g., 13
 *                NOTE: Different from DDRC CFG register which uses (row - 12)
 *   0xE7:      COL bits (encoded as col_bits - 7) - e.g., 3 for 10 columns
 *                NOTE: Different from DDRC CFG register which uses (col - 8)
 *   0xE8:      tRAS (Active to Precharge delay, in cycles)
 *   0xE9:      tRC (Active to Active/Refresh delay, in cycles)
 *   0xEA:      tRCD (RAS to CAS delay, in cycles)
 *   0xEB:      tRP (Precharge command period, in cycles)
 *   0xEC:      tRFC (Refresh cycle time, in cycles)
 *   0xED:      Unknown (0x04) - purpose unknown
 *   0xEE:      tRTP (Read to Precharge, in cycles)
 *   0xEF:      Unknown (0x20 = 32) - purpose unknown
 *   0xF0:      tFAW (Four Activate Window, in cycles)
 *   0xF1:      Unknown (0x00) - purpose unknown
 *   0xF2:      tRRD (Active bank A to Active bank B, in cycles)
 *   0xF3:      tWTR (Write to Read delay, in cycles)
 *   0xF4-0x12F: Reserved/unknown fields
 *   0x130-0x143: DQ mapping table (20 bytes) - maps logical DQ pins to physical pins
 *                Default: {12,13,14,3,4,5,6,7,8,9,10,11,0,1,2,15,16,17,18,19}
 *                This is board/hardware-specific and may need customization
 *
 * NOTE: This RDD format is NOT the same as DDRC/DDRP hardware registers!
 *       The DDRC CFG register uses different encoding:
 *       - ROW: stored as (row_bits - 12)
 *       - COL: stored as (col_bits - 8)
 *       See references/ingenic-u-boot-xburst1/tools/ingenic-tools/ddr_params_creator.c
 *       lines 192-208 for DDRC register generation
 *
 * Timing Calculation:
 *   Most timing values are calculated using: ps2cycle_ceil(time_ps, ps_per_tck)
 *   where ps2cycle_ceil(ps, ps_per_tck) = (ps + ps_per_tck - 1) / ps_per_tck
 *
 *   Exception: tRFC uses ps2cycle_ceil with div_tck=2, then divides result by 2:
 *   tRFC = ((time_ps + 2*ps_per_tck - 1) / ps_per_tck) / 2
 *
 *   This formula is documented in:
 *   references/ingenic-u-boot-xburst1/tools/ingenic-tools/ddr_params_creator.c
 *   lines 21-24 (ps2cycle_ceil function) and line 132 (tRFC calculation)
 *
 * Source:
 *   - Derived from reference DDR binary analysis
 *   - references/ddr_compiler_final.py (Python implementation)
 *   - references/ingenic-u-boot-xburst1/tools/ingenic-tools/ddr_params_creator.c
 *     (Official Ingenic u-boot DDR parameter creator tool)
 */
typedef struct {
    // DDR type (RDD format encoding - different from DDRC/DDRP registers!)
    // 0=DDR3, 1=DDR2, 2=LPDDR2/LPDDR, 4=LPDDR3
    // See references/ddr_compiler_final.py line 351 for mapping
    uint32_t ddr_type;

    // Memory geometry (from config file)
    uint8_t row_bits; // ROW field - number of row address bits (e.g., 13)
    uint8_t col_bits; // COL field - number of column address bits (e.g., 10)
    uint8_t cl;       // CL field - CAS Latency in cycles (e.g., 6 or 7 for DDR2)
    uint8_t bl;       // BL field - Burst Length (typically 8 for DDR2/DDR3)

    uint8_t rl; // RL - Read Latency (cycles)
    uint8_t wl; // WL - Write Latency (cycles)

    // Core timing parameters in clock cycles
    uint8_t tRAS; // Active to Precharge delay
    uint8_t tRC;  // Active to Active/Refresh delay
    uint8_t tRCD; // RAS to CAS delay
    uint8_t tRP;  // Precharge command period
    uint8_t tRFC; // Refresh cycle time (special calculation)
    uint8_t tRTP; // Read to Precharge
    uint8_t tFAW; // Four Activate Window
    uint8_t tRRD; // Active bank A to Active bank B
    uint8_t tWTR; // Write to Read delay

    // Extended timing parameters (RDD offsets 0x2c-0x4f)
    uint8_t tWR;     // Write Recovery time
    uint8_t tCKE;    // CKE min pulse width
    uint8_t tXP;     // Exit power-down to command
    uint8_t tXPDLL;  // Exit power-down to DLL lock (0xff if N/A)
    uint8_t tXS;     // Exit self-refresh to command
    uint8_t tXSDLL;  // Exit self-refresh to DLL lock
    uint8_t tCCD;    // Column to Column delay
    uint8_t tMOD;    // Mode Register Set delay
    uint32_t tCKESR; // CKE self-refresh min (RDD 0x38, 4 bytes)
    uint32_t tXSR;   // Exit self-refresh (LPDDR) (RDD 0x48, 4 bytes)
    uint32_t tREFI;  // Refresh interval (RDD 0x44, 4 bytes)
    uint32_t tXSRD;  // Exit self-refresh to read (tCK)
} ddr_phy_params_t;

/**
 * Build FIDB section (struct global_info for SPL)
 *
 * Size depends on platform: T20/T21/T23/T30/T31 = 192 bytes,
 * A1/T40/T41 = larger (ddr_params embedded in global_info).
 *
 * @param platform Platform configuration structure
 * @param output Output buffer (must be at least DDR_FIDB_HEADER_SIZE + DDR_FIDB_DATA_SIZE_MAX)
 * @return Number of bytes written
 */
size_t ddr_build_fidb(const platform_config_t *platform, uint8_t *output);

/**
 * Build RDD section (132 bytes: 8-byte header + 124-byte data)
 *
 * @param platform Platform configuration (used for DDR frequency)
 * @param params DDR PHY parameters (geometry and timing)
 * @param output Output buffer (must be at least 132 bytes)
 * @return Number of bytes written (always 132)
 */
size_t ddr_build_rdd(const platform_config_t *platform, const ddr_phy_params_t *params, uint8_t *output);

/**
 * Build complete DDR binary (FIDB + RDD)
 *
 * @param platform Platform configuration
 * @param params DDR PHY parameters
 * @param output Output buffer (must be at least DDR_BINARY_SIZE_MAX bytes)
 * @return Number of bytes written
 */
size_t ddr_build_binary(const platform_config_t *platform, const ddr_phy_params_t *params, uint8_t *output);

/**
 * Get default platform configuration for a given Ingenic SoC
 *
 * Returns default values for:
 * - Crystal frequency (24 MHz for all platforms)
 * - CPU frequency (576 MHz for T30/T31/T41)
 * - DDR frequency (400 MHz default)
 * - UART baud rate (115200)
 * - Memory size (8 MB default)
 *
 * These defaults match the values found in reference binaries but may need
 * adjustment for specific hardware configurations.
 *
 * @param platform_name Platform name ("t31", "t30", "t41", or NULL for default)
 * @param config Output platform configuration structure
 * @return 0 on success, -1 on error (NULL pointer)
 */
int ddr_get_platform_config(const char *platform_name, platform_config_t *config);

/**
 * Populate DDR PHY params from chip config and DDR frequency.
 * Converts timing values from ps to clock cycles.
 */
#include "ddr_config_database.h"
void ddr_chip_to_phy_params(const ddr_chip_config_t *chip, uint32_t ddr_freq_hz, ddr_phy_params_t *params);

/**
 * Get default platform configuration for a processor variant
 *
 * This is a convenience wrapper around ddr_get_platform_config() that accepts
 * tdfu_variant_t enum values from the main thingino codebase.
 *
 * @param variant Processor variant enum (TDFU_VARIANT_T31X, TDFU_VARIANT_T31ZX, etc.)
 * @param config Output platform configuration structure
 * @return 0 on success, -1 on error (unsupported variant or NULL pointer)
 */
int ddr_get_platform_config_by_variant(int variant, platform_config_t *config);

#endif // DDR_BINARY_BUILDER_H
