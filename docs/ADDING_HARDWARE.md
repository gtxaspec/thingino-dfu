# Adding New Hardware Support

Guide for adding new flash chips, DDR chips, and platform profiles to thingino-dfu.

## Adding a SPI NOR Flash Chip

**File:** `libcloner/src/flash/spi_nor_db.c`

Add an entry to the `spi_nor_chips[]` array:

```c
{
    .name = "GD25Q128CSIG",
    .jedec_id = 0xc84018,      // Manufacturer + device ID (from datasheet)
    .size = 16 * 1024 * 1024,  // 16MB
    .page_size = 256,           // Page program size
    .erase_size = 32768,        // Sector erase size (32KB)
    .chip_erase_cmd = 0xc7,     // Chip erase opcode
    .quad_enable = 1,           // Supports quad SPI
    .addr_mode = 0,             // 0 = 3-byte addressing, 1 = 4-byte

    .timing = {300, 3, 5, 10, 50},

    // Commands: { opcode, dummy_cycles, addr_bytes, mode }
    .cmd_read   = {0x03, 0, 3, 0},   // Standard read
    .cmd_qread  = {0x6b, 8, 3, 5},   // Quad output read
    .cmd_write  = {0x02, 0, 3, 0},   // Page program
    .cmd_qwrite = {0x32, 0, 3, 5},   // Quad page program
    .cmd_erase  = {0x52, 0, 3, 0},   // Block erase
    .cmd_we     = {0x06, 0, 0, 0},   // Write enable
    .cmd_en4b   = {0xb7, 0, 0, 0},   // Enter 4-byte mode (-1 if N/A)

    // Status registers: { cmd, bit, read_en, read_cmd, write_en, reserved }
    .sr1 = {0x31, 1, 1, 1, 1, 0},
    .sr2 = {0x35, 1, 1, 1, 1, 0},
    .sr3 = {0x05, 0, 1, 0, 1, 0},
},
```

**Where to get the values:** The chip datasheet. Key fields:
- `jedec_id`: Read from JEDEC READ ID command (0x9F) — 3 bytes
- `size`, `page_size`, `erase_size`: From datasheet capacity section
- Commands: From the instruction set table in the datasheet
- Status registers: From the status register description

**Testing:** After adding, rebuild and flash a device with the chip. The tool auto-detects via JEDEC ID. Use `--flash-chip <name>` to force if needed.

## Adding a NAND Flash Chip

**File:** `libcloner/src/flash/nand_db.c`

Add an entry to the `nand_chips[]` array:

```c
{
    .chip_id = 0xADBC,          // 2-byte chip ID
    .ext_id = 0x00565590,       // Extended ID bytes
    .name = "H9DA4GH2GJBMCR-4EM",
    .page_size = 2048,
    .block_size = 131072,       // 128KB
    .oob_size = 128,            // Spare bytes per page

    .row_cycles = 1,
    .col_cycles = 8,
    .planes = 2,
    .blocks_exp = 16,
    .dies = 3,
    .buf_size = 65536,

    .flags = {0, 0, 0, 1, 1, 0, 0},
    .timing = {25, 10, 25, 25, 100, 60, 200, 20, 100, 100, 0, 20, 0},
    .spare_config = 0,

    .ecc_strength = 5,
    .ecc_step = 4096,
    .ecc_page = 4096,
    .ecc_reserved = 0,
    .timing2 = {15, 10, 10, 15},
},
```

## Adding a DDR Chip

**File:** `libcloner/src/ddr/ddr_config_database.c`

Add an entry to the `ddr_chips[]` array. All timing values are in **picoseconds** unless noted as clock cycles (tck).

```c
{
    .name = "M14D1G1664A_DDR2",
    .vendor = "ESMT",
    .ddr_type = 1,     // 0=DDR3, 1=DDR2, 2=LPDDR2

    // Geometry
    .bl = 8,           // Burst length
    .cl = 5,           // CAS latency (cycles)
    .col = 10,         // Column bits
    .col1 = -1,        // -1 = single rank
    .row = 13,         // Row bits
    .row1 = -1,        // -1 = single rank
    .rl = 5,           // Read latency
    .wl = 4,           // Write latency

    // Timing (picoseconds unless noted)
    .tRAS = 45000,     // Row active time
    .tRC = 65000,      // Row cycle time
    .tRCD = 20000,     // RAS to CAS delay
    .tRP = 20000,      // Row precharge time
    .tRFC = 75000,     // Refresh cycle time
    .tRTP = 7500,      // Read to precharge
    .tWR = 15000,      // Write recovery
    .tWTR = 7500,      // Write to read delay
    .tRRD = 10000,     // Row to row delay
    .tFAW = 45000,     // Four bank activate window
    .tCCD = 2,         // Column to column (tck)
    .tCKE = 3,         // CKE min pulse (tck)
    .tCKESR = 3,       // CKE self-refresh min (tck)
    .tMRD = 2,         // Mode register set time (tck)
    .tMOD = -1,        // -1 = N/A (DDR3 only)
    .tREFI = 15600000, // Refresh interval
    .tXP = 2,          // Exit power-down (tck)

    // Extended timing (-1 = N/A, use for DDR3)
    .tCKSRE = 10000000,
    .tXPDLL = -1,
    .tXS = -1,
    .tXSDLL = -1,
    .tXSNR = 85000,
    .tXSRD = 200,
    .tXSR = -1,
    .tXARD = 2,
    .tXARDS = 7,
    .tDQSCK = -1,
    .tDQSCKMAX = -1,
    .tDQSSMAX = -1,
},
```

**Where to get timing values:** The DDR chip datasheet's "AC Timing Parameters" table. Convert nanoseconds to picoseconds (multiply by 1000).

Then associate the DDR chip with a processor in the `platform_ddr_map[]` array:

```c
{"t31x", "M14D1G1664A_DDR2"},
{"t40",  "M14D1G1664A_DDR2"},
```

## Adding a New Platform (SoC)

Adding a new Ingenic SoC requires changes in several files:

### 1. Add variant enum

**File:** `libcloner/include/cloner/thingino.h`

```c
typedef enum processor_variant {
    // ...existing variants...
    VARIANT_NEW_SOC,
} processor_variant_t;
```

### 2. Create platform profile

**File:** `libcloner/src/platforms/new_soc.c`

```c
#include "cloner/platform_profile.h"

const platform_profile_t platform_new_soc = {
    .name = "NEW_SOC",
    .default_chunk_size = 131072,              // 128KB
    .descriptor_mode = DESC_MARKER_THEN_SEND,  // or DESC_RAW_BULK_THEN_SEND for T10/T20
    .descriptor_subdir = NULL,
    .erase_wait = ERASE_WAIT_POLLING,          // or ERASE_WAIT_FIXED
    .erase_delay_seconds = 0,
    .crc_format = CRC_FMT_STANDARD,            // STANDARD, T20, VENDOR, or A1
    .trailer = {0x20, 0xFB, 0x00, 0x08, 0xA2, 0x77, 0x00, 0x00},
    .skip_set_data_addr = false,
    .per_chunk_status_read = false,
    .poll_spl_after_ddr = false,
    .reopen_usb_after_spl = false,
    .use_a1_handshake = false,
    .set_data_len_per_chunk = false,
    .ddr_init_wait_ms = 2000,
};
```

Key decisions based on SoC architecture:
- **xburst1 (T10-T32):** Usually `CRC_FMT_STANDARD` or `CRC_FMT_T20`, `DESC_MARKER_THEN_SEND`
- **xburst2 (T40/T41/A1):** `CRC_FMT_VENDOR` (T40/T41) or `CRC_FMT_A1`, different trailers

### 3. Register the platform

**File:** `libcloner/src/platforms/platform.c`

```c
extern const platform_profile_t platform_new_soc;

const platform_profile_t *platform_get_profile(processor_variant_t variant) {
    switch (variant) {
    // ...existing cases...
    case VARIANT_NEW_SOC:
        return &platform_new_soc;
    }
}
```

### 4. Add processor config

**File:** `libcloner/src/ddr/ddr_config_database.c`

Add to `processor_configs[]`:

```c
{
    .name = "new_soc",
    .crystal_freq = 24000000,
    .cpu_freq = 800000000,
    .ddr_freq = 400000000,
    .uart_baud = 115200,
    .mem_size = 8 * 1024 * 1024,
    .d2i_len = 0x7000,
    .ginfo_addr = 0x80001000,
    .spl_addr = 0x80001800,
    .uboot_addr = 0x80100000,
    .uart_idx = 1,
    .ginfo_ddr_params_size = 0,
    .use_fidb_header = 1,
    .use_inno_phy_rdd = 1,      // 1 for T21/T23/T30/T31, 0 for T10/T20
    .is_xburst2 = 0,            // 1 for T40/T41/A1
    .ddr_bank8 = 0,
    .ddr_dw32 = 0,
    .ddr_cs0 = 1,
    .ddr_cs1 = 0,
},
```

### 5. Add DDR mapping

**File:** `libcloner/src/ddr/ddr_config_database.c`

In `platform_ddr_map[]`:

```c
{"new_soc", "DDR_CHIP_NAME"},
```

### 6. Map variant name to DDR builder

**File:** `libcloner/src/ddr/ddr_binary_builder.c`

In `ddr_generate_config()`:

```c
case VARIANT_NEW_SOC:
    platform_name = "new_soc";
    break;
```

### 7. Add variant string mapping

**File:** `libcloner/src/utils.c`

In `processor_variant_to_string()` and `string_to_processor_variant()`:

```c
case VARIANT_NEW_SOC:  return "new_soc";
// ...
if (strcasecmp(str, "new_soc") == 0) return VARIANT_NEW_SOC;
```

### 8. Add firmware binaries

Place vendor SPL and U-Boot in:
```
firmware/cloner/new_soc/spl.bin
firmware/cloner/new_soc/uboot.bin
```

These are extracted from the Ingenic vendor cloner tool for the target SoC.

### 9. Add auto-detect support

**File:** `libcloner/src/usb/protocol.c`

In `protocol_detect_soc()`, add the CPU ID to the switch:

```c
case 0xNNNN:  // CPU family ID from SoC ID register
    *variant = VARIANT_NEW_SOC;
    break;
```

And add the chip name mapping for the log output.

### 10. Add to CMakeLists

**File:** `libcloner/CMakeLists.txt`

```cmake
set(LIBCLONER_SOURCES
    # ...existing...
    src/platforms/new_soc.c
)
```

### 11. Build and test

```bash
make
./build/cli/thingino-dfu -i 0 -b --cpu new_soc
```

## Where to Get Platform Parameters

Most values come from the Ingenic vendor cloner tool's configuration files:

- **Platform config:** `configs/<soc>/<soc>_sfc_nor_*.cfg` in the vendor cloner
- **DDR timing:** Chip datasheet AC timing tables
- **USB protocol quirks:** USB packet captures of the vendor tool (Wireshark + USBPcap)
- **Flash descriptor format:** Binary comparison against vendor tool's USB traffic
- **SPL/U-Boot binaries:** Extracted from vendor cloner's `firmwares/` directory
