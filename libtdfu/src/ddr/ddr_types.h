#ifndef DDR_TYPES_H
#define DDR_TYPES_H

#include <stdint.h>

// DDR Type Constants
typedef enum {
    DDR_TYPE_DDR2 = 0,
    DDR_TYPE_DDR3 = 1,
    DDR_TYPE_LPDDR = 2,
    DDR_TYPE_LPDDR2 = 3,
    DDR_TYPE_LPDDR3 = 4,
} ddr_type_t;

// Input configuration structure
typedef struct {
    ddr_type_t type;
    uint32_t clock_mhz; // Clock frequency in MHz

    // Timing parameters (nanoseconds)
    uint32_t cas_latency; // CAS Latency (CL)
    uint32_t tRAS;        // Row Active Time
    uint32_t tRP;         // Row Precharge
    uint32_t tRCD;        // Row to Column Delay
    uint32_t tRC;         // Row Cycle
    uint32_t tWR;         // Write Recovery
    uint32_t tRRD;        // Row to Row Delay
    uint32_t tWTR;        // Write to Read
    uint32_t tRFC;        // Refresh to Active
    uint32_t tXP;         // Power-Down Exit
    uint32_t tCKE;        // Clock Enable
    uint32_t tRL;         // Read Latency
    uint32_t tWL;         // Write Latency
    uint32_t tREFI;       // Refresh Interval (ns per 8k refreshes)

    // Memory configuration
    uint32_t banks;      // Bank count (4 or 8)
    uint32_t row_bits;   // Row address bits
    uint32_t col_bits;   // Column address bits
    uint32_t data_width; // 8 for x8, 16 for x16, 32 for x32

    // Physical
    uint32_t total_size_bytes;
} ddr_config_t;

// Output binary structure (324 bytes total)
typedef struct {
    char fidb_sig[4];   // "FIDB" at 0x00 (4 bytes)
    uint32_t fidb_size; // Size of FIDB section at 0x04 (4 bytes) = 0xb8 (184 bytes)
    uint8_t ddrc[0xb8]; // 184 bytes (0x08-0xbf) - DDR Controller config (FIDB data)
    uint32_t rdd_sig;   // "RDD\0" at 0xc0-0xc3 (4 bytes, packed as uint32)
    uint32_t rdd_size;  // Size of RDD section at 0xc4 (4 bytes) = 0x7c (124 bytes)
    uint8_t ddrp[0x7c]; // 124 bytes (0xc8-0x143) - DDR PHY config (RDD data)
} ddr_binary_t;

#endif // DDR_TYPES_H