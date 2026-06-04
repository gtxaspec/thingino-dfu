/**
 * DDR Configuration Database - Embedded processor and DDR chip configurations
 *
 * This file contains embedded configurations for all supported Ingenic processors
 * and DDR memory chips, eliminating the need to distribute separate config files.
 */

#ifndef DDR_CONFIG_DATABASE_H
#define DDR_CONFIG_DATABASE_H

#include <stdint.h>
#include <stddef.h>

/**
 * Processor platform configuration
 */
typedef struct {
    const char *name;      // Platform name (e.g., "t31x", "t30", "t41")
    uint32_t crystal_freq; // Crystal oscillator frequency (Hz)
    uint32_t cpu_freq;     // CPU frequency (Hz)
    uint32_t ddr_freq;     // DDR frequency (Hz)
    uint32_t uart_baud;    // UART baud rate
    uint32_t mem_size;     // Default memory size (bytes)

    // Memory map (from vendor firmware config.cfg)
    uint32_t d2i_len;    // D2I execution length (0x4000 or 0x7000)
    uint32_t ginfo_addr; // Global info address (0x80001000)
    uint32_t spl_addr;   // SPL load address (0x80001800)
    uint32_t uboot_addr; // U-Boot load address (0x80100000)

    // FIDB/global_info layout parameters
    uint32_t uart_idx;              // UART index (selects from SPL's uart_gpio_func[])
    uint32_t ginfo_ddr_params_size; // sizeof(ddr_params) embedded in global_info
                                    // 0 = ddr_params commented out (T20/T21/T23/T30/T31)
                                    // 196 = A1/T40 ddr_params.h
                                    // 204 = T41 ddrcp_chip/ddr_params.h
    int use_fidb_header;            // 1 = xburst1 SPL uses find_param() with FIDB header
                                    // 0 = xburst2 SPL reads struct global_info directly
    int use_inno_phy_rdd;           // 1 = SPL uses INNO PHY byte-packed RDD (T21/T31 family)
                                    // 0 = SPL uses struct ddr_registers RDD (T20, xburst2)
    int is_xburst2;                 // 1 = xburst2 (A1/T40/T41), different FIDB layout
                                    // 0 = xburst1 (T10/T20/T21/T23/T30/T31)

    // DDR geometry (from vendor board config [ddr] section)
    uint8_t ddr_bank8; // 0 = 4 banks, 1 = 8 banks
    uint8_t ddr_dw32;  // 0 = 16-bit bus, 1 = 32-bit bus
    uint8_t ddr_cs0;   // chip select 0 enabled
    uint8_t ddr_cs1;   // chip select 1 enabled
} processor_config_t;

/**
 * DDR chip timing configuration
 *
 * Timing values are stored in picoseconds (ps) or clock cycles (tck).
 * -1 means not applicable for this DDR type.
 * Values originally in ns are converted: ns * 1000 = ps.
 */
typedef struct {
    const char *name;   // Chip name (e.g., "M14D1G1664A_DDR2")
    const char *vendor; // Vendor name (e.g., "ESMT", "Winbond")
    uint32_t ddr_type;  // 0=DDR3, 1=DDR2, 2=LPDDR2, 3=LPDDR, 4=LPDDR3

    // Geometry
    int8_t bl;   // Burst Length
    int8_t cl;   // CAS Latency (cycles)
    int8_t col;  // Column address bits
    int8_t col1; // Column address bits (rank 1, -1 if N/A)
    int8_t row;  // Row address bits
    int8_t row1; // Row address bits (rank 1, -1 if N/A)
    int8_t rl;   // Read Latency (cycles)
    int8_t wl;   // Write Latency (cycles)

    // Core timing (ps or tck, -1 = N/A)
    int32_t tRAS;   // Row Active Time (ps)
    int32_t tRC;    // Row Cycle Time (ps)
    int32_t tRCD;   // RAS to CAS Delay (ps)
    int32_t tRP;    // Row Precharge Time (ps)
    int32_t tRFC;   // Refresh Cycle Time (ps)
    int32_t tRTP;   // Read to Precharge (ps)
    int32_t tWR;    // Write Recovery Time (ps)
    int32_t tWTR;   // Write to Read Delay (ps)
    int32_t tRRD;   // Row to Row Delay (ps)
    int32_t tFAW;   // Four Bank Activate Window (ps)
    int32_t tCCD;   // Column to Column Delay (tck)
    int32_t tCKE;   // CKE minimum pulse width (tck or ps)
    int32_t tCKESR; // CKE self-refresh min (tck or ps)
    int32_t tMRD;   // Mode Register Set command time (tck)
    int32_t tMOD;   // Mode Register Set to command (tck)
    int32_t tREFI;  // Refresh Interval (ps)
    int32_t tXP;    // Exit power-down to command (tck or ps)

    // Extended timing (ps or tck, -1 = N/A)
    int32_t tCKSRE;    // Clock self-refresh exit (tck)
    int32_t tXPDLL;    // Exit power-down to DLL lock (ps)
    int32_t tXS;       // Exit self-refresh to command (ns->ps)
    int32_t tXSDLL;    // Exit self-refresh to DLL lock (tck)
    int32_t tXSNR;     // Exit self-refresh (no DLL) (ps)
    int32_t tXSRD;     // Exit self-refresh to read (tck or ps)
    int32_t tXSR;      // Exit self-refresh (LPDDR) (ps)
    int32_t tXARD;     // Exit active power-down to read (tck)
    int32_t tXARDS;    // Exit active power-down to read (slow, tck)
    int32_t tDQSCK;    // DQS to CK delay (ps)
    int32_t tDQSCKMAX; // DQS to CK max delay (ps)
    int32_t tDQSSMAX;  // DQS max skew (tck, LPDDR only)
} ddr_chip_config_t;

/**
 * Get processor configuration by name
 *
 * @param name Processor name (e.g., "t31x", "t30", "t41", "a1")
 * @return Pointer to processor config, or NULL if not found
 */
const processor_config_t *processor_config_get(const char *name);

/**
 * Get DDR chip configuration by name
 *
 * @param name DDR chip name (e.g., "M14D1G1664A_DDR2", "W631GU6NG_DDR3")
 * @return Pointer to DDR chip config, or NULL if not found
 */
const ddr_chip_config_t *ddr_chip_config_get(const char *name);

/**
 * Get default DDR chip for a processor
 *
 * @param processor_name Processor name (e.g., "t31x")
 * @return Pointer to default DDR chip config, or NULL if not found
 */
const ddr_chip_config_t *ddr_chip_config_get_default(const char *processor_name);

/**
 * List all available processor configurations
 *
 * @param count Output parameter for number of processors
 * @return Array of processor configs
 */
const processor_config_t *processor_config_list(size_t *count);

/**
 * List all available DDR chip configurations
 *
 * @param count Output parameter for number of DDR chips
 * @return Array of DDR chip configs
 */
const ddr_chip_config_t *ddr_chip_config_list(size_t *count);

/**
 * List DDR chips compatible with a specific DDR type
 *
 * @param ddr_type DDR type (0=DDR3, 1=DDR2, 2=LPDDR2, 4=LPDDR3)
 * @param count Output parameter for number of matching chips
 * @return Array of matching DDR chip configs
 */
const ddr_chip_config_t **ddr_chip_config_list_by_type(uint32_t ddr_type, size_t *count);

#endif // DDR_CONFIG_DATABASE_H
