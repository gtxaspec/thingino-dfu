# Cloner Backend Engineering Reference

Internals of thingino-dfu's **cloner** backend - the reverse-engineered Ingenic USB vendor protocol. Covers the USB protocol, flash descriptor formats, DDR binary layout, the compiled-in hardware databases, and platform-specific behavior. The default DFU backend needs none of this; the device's own U-Boot owns DDR init and the flash medium.

## Architecture

### Components

```
[thingino-dfu]  -- USB --  [device]                          (local mode)
[thingino-dfu]  -- TCP --  [dfu-remote]  -- USB --  [device]   (remote mode)
```

- **libtdfu** -- Core library: DDR generation, USB protocol, flash read/write, chip databases
- **cli** (`thingino-dfu`) -- CLI frontend, local or remote mode
- **dfu-remote** -- Daemon on USB host, accepts network commands from CLI

### Build System

CMake with a top-level Makefile wrapper. Key targets:

- `make` -- native x86_64 build
- `make arm64` -- cross-compile via `cmake/toolchain-aarch64-linux.cmake`

Dependencies: `libusb-1.0`, `zlib` (for CRC32).

### Source Layout

```
libtdfu/
  include/tdfu/      # Public headers (tdfu.h, platform_profile.h, etc.)
  src/
    bootstrap.c         # DDR + SPL + U-Boot loading
    operations.c        # High-level bootstrap/read/write orchestration
    platforms/           # Per-platform profile definitions (t31.c, t40.c, a1.c, etc.)
    firmware/
      writer.c          # Flash write loop + erase wait
      handshake.c       # 40-byte VR_WRITE/VR_READ chunk protocol
      flash_descriptor.c # Descriptor generation (GBD, SFC configs)
    ddr/
      ddr_binary_builder.c  # FIDB + RDD builder
      ddr_fidb_xb1.c        # FIDB fields for xburst1 (T10-T31)
      ddr_fidb_xb2.c        # FIDB fields for xburst2 (A1)
      ddr_rdd_xb2.c         # RDD builder for xburst2 (184-byte format)
      ddr_rdd_ddr2.c        # INNO PHY RDD for DDR2
      ddr_rdd_ddr3.c        # INNO PHY RDD for DDR3
cli/
  main.c                # CLI argument parsing and dispatch
dfu-remote/          # Network daemon
```

---

## Hardware Databases

All chip data is compiled in - no config files at runtime. The cloner backend uses these for DDR bring-up and flash detection:

- **DDR chips:** 36 entries (DDR2, DDR3, LPDDR2)
- **SPI NOR flash:** 49 entries (JEDEC-ID matched)
- **NAND flash:** 12 entries
- **Platform configs:** 19 processors

The DFU backend needs none of this - the device's own U-Boot owns DDR init and the flash medium. See [ADDING_HARDWARE.md](ADDING_HARDWARE.md) to extend these databases.

---

## USB Protocol

All communication uses the Ingenic USB boot protocol. The device appears as `a108:c309` (T/A/C series) - or `601a:4770` for the X series - in bootrom mode, and keeps that VID:PID throughout. The bootrom-vs-firmware stage is read over the protocol (`VR_GET_CPU_INFO`), not from a separate USB ID.

### Vendor Request Codes

Bootrom stage (0x00-0x05):

| Code | Name | Direction | Description |
|------|------|-----------|-------------|
| 0x00 | `VR_GET_CPU_INFO` | IN | Read 8-byte CPU magic (e.g., "T31X0000") |
| 0x01 | `VR_SET_DATA_ADDR` | OUT | Set target memory address for next bulk transfer |
| 0x02 | `VR_SET_DATA_LEN` | OUT | Set data length for next operation |
| 0x03 | `VR_FLUSH_CACHE` | OUT | Flush CPU data cache |
| 0x04 | `VR_PROG_STAGE1` | OUT | Execute code at address (SPL entry point) |
| 0x05 | `VR_PROG_STAGE2` | OUT | Execute code at address (U-Boot entry point) |

Firmware stage (0x10-0x26):

| Code | Name | Direction | Description |
|------|------|-----------|-------------|
| 0x10 | `VR_FW_READ` | IN | Read 4-byte ACK/status from burner |
| 0x11 | `VR_FW_HANDSHAKE` | OUT | Initialize burner (triggers flash probe + chip erase) |
| 0x12 | `VR_WRITE` | OUT | Send 40-byte write handshake for a chunk |
| 0x13 | `VR_READ` | OUT | Send 40-byte read handshake for a chunk |
| 0x14 | `VR_FW_WRITE2` | OUT | Send 40-byte control + bulk data (descriptors, markers) |
| 0x16 | `VR_FW_READ_STATUS1` / `VR_REBOOT` | IN | Read status / reboot device |
| 0x19 | `VR_FW_READ_STATUS2` | IN | Read 4-byte erase/write status |
| 0x25 | `VR_FW_READ_STATUS3` | IN | Read status (extended) |
| 0x26 | `VR_FW_READ_STATUS4` | IN | Read status (extended) |

USB request types: `0x40` = host-to-device vendor, `0xC0` = device-to-host vendor.

### Bootstrap Sequence

1. `VR_GET_CPU_INFO` -- identify SoC variant from 8-byte magic
2. `VR_SET_DATA_ADDR(ginfo_addr)` + `VR_SET_DATA_LEN(ddr_size)` + bulk OUT -- load DDR binary
3. `VR_SET_DATA_ADDR(spl_addr)` + `VR_SET_DATA_LEN(spl_size)` + bulk OUT -- load SPL
4. `VR_SET_DATA_LEN(d2i_len)` + `VR_PROG_STAGE1(spl_addr)` -- execute SPL (initializes DDR)
5. Wait for DDR init (platform-dependent: 1100ms for T10/T20/T21, 2000ms for T31+)
6. Optional: poll `VR_GET_CPU_INFO` (T10/T20/T21/T30 only)
7. Optional: reopen USB handle (T31ZX only -- USB re-enumerates after SPL)
8. `VR_SET_DATA_ADDR(uboot_addr)` + `VR_SET_DATA_LEN(uboot_size)` + bulk OUT -- load U-Boot
9. `VR_FLUSH_CACHE` + `VR_PROG_STAGE2(uboot_addr)` -- execute U-Boot (enters firmware stage)

### Write Flow

After bootstrap, the device is in firmware stage running the vendor U-Boot burner.

1. **Partition marker**: `VR_FW_WRITE2` control (40 bytes, size=172) + bulk OUT (172 bytes "ILOP" marker)
   - T10/T20/T21/T30: raw bulk OUT marker instead (DESC_RAW_BULK_THEN_SEND)
2. **Flash descriptor**: `VR_FW_WRITE2` control (40 bytes, size=descriptor_size) + bulk OUT (descriptor)
   - The descriptor tells the burner which flash chip to use and triggers JEDEC auto-detection
3. **VR_FW_HANDSHAKE (0x11)**: initialize burner -- triggers `cloner_init()` on device, which probes SFC flash
   - If erase flag is set in descriptor, chip erase begins here
4. **Erase wait**: platform-dependent
   - T31: poll `VR_FW_READ_STATUS2` until stable
   - T10/T20/T21/T30: fixed delay (50s)
   - T40: poll `VR_FW_READ` for ACK (adaptive -- falls back to silent if no response)
   - T41: same as T40 but falls back to 60s silent wait
   - A1: 60s silent wait (no USB transfers during erase)
5. `VR_SET_DATA_LEN` -- set total firmware size (or per-chunk size for A1)
6. **Chunk loop**:
   - `VR_WRITE (0x12)` with 40-byte handshake struct
   - Bulk OUT: chunk data
   - Drain bulk IN (device logs)
   - `VR_FW_READ (0x10)` ACK poll (with retry on CRC failure)
7. `VR_FLUSH_CACHE` -- finalize

### Read Flow

Same as write through step 3, except:
- Flash descriptor has `SPI_NO_ERASE` flag set (SFC+0x14 = 0x00)
- No erase wait
- Chunk loop uses `VR_READ (0x13)` instead of `VR_WRITE`
- Bulk IN to receive data instead of bulk OUT
- `VR_FW_READ_STATUS2 (0x19)` status check before each bulk IN
- `VR_FW_READ (0x10)` ACK after each bulk IN

---

## Flash Descriptor Formats

The flash descriptor tells the vendor burner which SPI NOR chip to use and how to access it. It contains GBD headers, policy sections, and one or two SFC config blocks with chip commands, timing, and partition info.

### Size by Platform

| Platform | Size | Format |
|----------|------|--------|
| T10/T20/T21/T23/T30/T31/T31A | 972 bytes | GBD only |
| T32 | 976 bytes | GBD with shifted SFC (+4 for blocksize + freq fields) |
| T40/T41 | 984 bytes | 12-byte RDD prefix (0xC9) + 972-byte GBD |
| A1 | 992 bytes | 12-byte RDD prefix (0x84) + 980-byte GBD (shifted SFC) |

### Internal Layout (972-byte T31 reference)

```
[0x000-0x01F]  GBD header (32 bytes): "\0GBD" magic, flags, "ILOP" marker
[0x020-0x0C7]  Policy section (168 bytes): partition layout placeholder
[0x0C8-0x34F]  SFC Config 1 (648 bytes): full flash config + partition info
[0x350-0x3CB]  SFC Config 2 (124 bytes): abbreviated flash config
```

### SFC Config 1 Layout (relative to SFC start)

| Offset | Field | Notes |
|--------|-------|-------|
| +0x00 | `"\0CFS"` magic | Section marker |
| +0x04 | Section size (u32 LE) | 0x2FC (T31), 0x300 (T32) |
| +0x10 | Blocksize (T32/T40 only) | 0x8000 = 32KB |
| +0x12 | Flag | 0x01 |
| +0x14 | **Erase flag** | 0x01 = erase, 0x00 = no erase (read mode) |
| +0x18 | SFC frequency (T32/A1 only) | 50000000 (50 MHz) |
| +0x20 | `"nor\0"` + type=1 | Flash type |
| +0x28 | Chip name (32 bytes) | e.g., "GD25Q127CSIG" |
| +0x48 | JEDEC ID (u32 LE) | e.g., 0xc84018 |
| +0x4C | 7 commands x 6 bytes | read, qread, write, qwrite, erase, we, en4b |
| +0x76 | 3 status regs x 8 bytes | SR1, SR2, SR3 configs |
| +0x8E | quad_enable, addr_mode | Chip feature flags |
| +0x90 | 5 timing values (u32 LE) | Chip-specific timing |
| +0xA4 | Geometry flags | Sector/page organization |
| +0xAC | Erase size (u32 LE) | Typically 0x10000 (64KB) |
| +0xB0 | Chip erase cmd (u32 LE) | Typically 0xC7 or 0x60 |
| +0xB4 | Partition name (32 bytes) | "full_image" |
| +0xD4 | Partition size (u32 LE) | Chip capacity or 0xFFFFFFFF |

### SFC Config 1 Offset Comparison

The SFC Config 1 block starts at different absolute offsets depending on the descriptor format. Fields within the SFC block are also shifted on some platforms.

| Field | T31 (SFC@0xC8) | T32 (SFC@0xC8) | T40 (SFC@0xD4) | A1 (SFC@0xDC) |
|-------|-----------------|-----------------|-----------------|----------------|
| Erase flag | 0xC8+0x14=0xDC | 0xC8+0x14=0xDC | 0xD4+0x14=0xE8 | 0xDC+0x14=0xF0 |
| SFC freq | N/A | 0xC8+0x18 | N/A | 0xDC+0x18 |
| "nor" | 0xC8+0x20 | 0xC8+0x24 | 0xD4+0x20 | 0xDC+0x1C |
| Chip name | 0xC8+0x28 | 0xC8+0x2C | 0xD4+0x28 | 0xDC+0x24 |
| JEDEC ID | 0xC8+0x48 | 0xC8+0x4C | 0xD4+0x48 | 0xDC+0x44 |

T40/T41 note: SFC config within the GBD body uses the standard T31 layout (same field offsets relative to SFC start), but the SFC section itself is shifted by 12 bytes due to the RDD prefix. The T40 overrides SFC+0x10 with 0x8000 (blocksize) and clears SFC+0x12 (T31's flag byte).

### RDD Prefix (xburst2 only)

T40/T41 and A1 descriptors have a 12-byte prefix before the GBD body:

```
[0-3]   "\0RDD" magic
[4-7]   size = 4 (u32 LE)
[8]     marker (0xC9 for T40/T41, 0x84 for A1)
[9]     0x00
[10]    marker repeat
[11]    0x00
```

### Erase Behavior

- **Write**: SFC+0x14 = 0x01 triggers full chip erase during `VR_FW_HANDSHAKE`
- **Read**: SFC+0x14 = 0x00 skips erase (non-destructive)
- The burner probes the flash chip via JEDEC ID from the descriptor and performs the erase internally

---

## VR_WRITE / VR_READ 40-byte Struct

Each chunk transfer uses a 40-byte handshake sent via vendor control transfer. The layout varies by platform.

### Write Handshake Formats

**CRC_FMT_STANDARD (T31/T31X):**

```
[0-7]    partition (u64, always 0)
[8-11]   offset (u32 LE, flash byte offset)
[12-15]  zeros
[16-19]  length (u32 LE, chunk size in bytes)
[20-23]  CRC32 of chunk data (u32 LE)
[24-27]  OPS = 0x00060000 (SPI, RAW)
[28-31]  ~CRC32 (bitwise inverted CRC)
[32-39]  trailer: 20 FB 00 08 A2 77 00 00
```

**CRC_FMT_T20 (T10/T20/T21/T30/T31A):**

```
[0-7]    partition (u64, always 0)
[8-11]   offset (u32 LE, flash byte offset)
[12-15]  zeros
[16-19]  length (u32 LE, chunk size in bytes)
[20-23]  zeros (no CRC in this field)
[24-27]  OPS = 0x00060000
[28-31]  ~CRC32 (inverted CRC of chunk data)
[32-39]  trailer (platform-specific)
```

**CRC_FMT_VENDOR (T40/T41):**

```
[0-7]    partition (u64, always 0)
[8-15]   offset (u64 LE, flash byte offset)
[16-23]  size (u64 LE, chunk size in bytes)
[24-27]  OPS = 0x00060000 (SPI, RAW)
[28-31]  CRC32 raw (no final XOR -- ~standard_crc32)
[32-39]  zeros (device ignores)
```

**CRC_FMT_A1:**

```
[0-7]    zeros
[8-11]   0x00000600 (OPS constant, note: different byte order from OPS field above)
[12-15]  offset (u32 LE, flash byte offset)
[16-19]  length (u32 LE, chunk size in bytes)
[20-23]  ~CRC32 (inverted CRC of chunk data)
[24-31]  zeros
[32-39]  trailer: 30 24 00 D4 02 75 00 00
```

### Read Handshake Formats

**T31/xburst1 (all except A1):**

```
[0-7]    partition (u64, always 0)
[8-11]   offset (u32 LE, flash read offset)
[12-15]  zeros
[16-19]  length (u32 LE, chunk size)
[20-23]  zeros
[24-27]  OPS = 0x00060000
[28-39]  zeros
```

**A1:**

```
[0-7]    zeros
[8-11]   OPS = 0x00060000
[12-15]  offset (u32 LE, flash read offset)
[16-19]  length (u32 LE, chunk size)
[20-39]  zeros
```

Read protocol per chunk:
1. `VR_READ (0x13)` control OUT with 40-byte struct
2. `VR_FW_READ_STATUS2 (0x19)` for 8-byte status
3. Bulk IN for chunk data
4. `VR_FW_READ (0x10)` 4-byte status read (ACK)

---

## DDR Binary Format

The DDR binary is loaded to `ginfo_addr` during bootstrap. It contains platform configuration (FIDB) and DDR PHY parameters (RDD). All values are generated dynamically from the chip database.

### Overall Structure

```
[FIDB section]  Platform config (struct global_info for SPL)
[RDD section]   DDR PHY register values
```

### FIDB Section

**xburst1 (T10-T31):** 8-byte header + data (min 184 bytes)

```
[0-3]    "FIDB" magic
[4-7]    data size (u32 LE)
[8+]     struct global_info data
```

**xburst2 (T40/T41/A1):** raw struct global_info (no header). SPL reads directly from `CONFIG_SPL_GINFO_BASE`.

### struct global_info Fields

| Offset | Field | Description |
|--------|-------|-------------|
| 0x00 | crystal_freq | Crystal oscillator (typically 24 MHz) |
| 0x04 | cpu_freq | CPU frequency (e.g., 576 MHz) |
| 0x08 | ddr_freq | DDR frequency (e.g., 400 MHz) |
| 0x0C | ddr_div | DDR divider (0 = auto) |
| 0x10 | uart_idx | UART index for SPL console |
| 0x14 | uart_baud | UART baud rate (115200) |
| 0x18 | nr_gpio_func / flags | Platform-specific config start |

Fields at 0x18+ vary by platform:

**xburst1 standard (T10/T20/T21/T23/T30/T31):**

| Offset | Value | Description |
|--------|-------|-------------|
| 0x18 | 0x00000001 | nr_gpio_func flag |
| 0x20 | mem_size | DDR memory size in bytes |
| 0x24 | 0x00000001 | flag |
| 0x2C | 0x00000011 | config (xburst1 standard) |
| 0x30 | 0x19800000 | Platform ID |

**T32 (vendor cloner 2.5.49):**

Same as xburst1 but:
- 0x2C = 0x00000000 (not 0x11)
- 0x20 = UART GPIO mask (not mem_size in the xburst1 sense)

**xburst2 T40/T41 (uses xb1-style FIDB fill):**

| Offset | Value | Description |
|--------|-------|-------------|
| 0x18 | 0x00000001 | flag |
| 0x20 | UART GPIO mask | (not mem_size) |
| 0x24 | 0x00000002 | flag |
| 0x2C | 0x00000001 | config (xburst2) |
| 0x30 | 0x1F800000 | SFC GPIO: sfc,pa_4bit |
| 0x38 | 0x00000001 | flag |
| 0x3C | 0xE0700000 | Extra GPIO/config |

**xburst2 A1 (unique layout):**

| Offset | Value | Description |
|--------|-------|-------------|
| 0x18 | 0x00000002 | flag |
| 0x20 | 0x000000C0 | config |
| 0x24 | 0x00000001 | flag |
| 0x28 | 0x00000002 | flag |
| 0x30 | 0x03F00000 | SFC GPIO |

### RDD Section

Three formats depending on architecture:

**INNO PHY (T21/T23/T30/T31/T31A):** 132 bytes (8 header + 124 data)

```
[0-3]    "\0RDD" magic
[4-7]    data size = 124 (0x7C)
[8-131]  DDR PHY params (DDRC_CFG, type, MMAP, timing, geometry, remap)
```

Contains DDRC_CFG register, DDR type, MMAP configuration, CAS latency, burst length, row/col bits, and all timing parameters (tRAS, tRC, tRCD, tRP, tRFC, etc.) converted from picoseconds to clock cycles.

**struct ddr_registers (T10/T20):** 204 bytes (8 header + 196 data)

```
[0-3]    "\0RDD" magic
[4-7]    data size = 196
[8-203]  struct ddr_registers (raw U-Boot register format)
```

The SPL reads this via `g_ddr_param` and writes values directly to DDRC/DDRP hardware registers.

**xburst2 (T40/T41/A1):** 192 bytes (8 header + 184 data)

```
[0-3]    "\0RDD" magic
[4-7]    data size = 184
[8-39]   chip name string (32 bytes, null-padded)
[40]     sub-header marker (0xC9=DDR2, 0x82=DDR3-T41, 0x88=DDR3-A1)
[44]     DDR type (0=DDR3, 4=DDR2)
[48]     DDR frequency
[52]     DDRC_CTRL
[56]     DDRC config 2
[60]     CS config
[68]     MMAP
[76]     DDRC_CFG
[80-95]  DDRC_TIMING1-4 register fields
[96-103] Extended timing (DDRC_TIMING5 + tRFC)
[108-119] Impedance/ODT
[120]    PHY config
[124]    RL
[128]    tCTLUPD
[136-148] MR0-MR3 registers
[164]    chip_size
[172-191] remap array (20 bytes)
```

DDRC_TIMING1-4 field mapping (verified against ingenic-u-boot-xburst2 `ddr3_params.c`):

| Register | Byte offsets | Fields |
|----------|-------------|--------|
| DDRC_TIMING1 | [80-83] | tWL, tWR, tWTR, tWDLAT |
| DDRC_TIMING2 | [84-87] | tRL, tRTP, tRTW, tRDLAT |
| DDRC_TIMING3 | [88-91] | tRP, tCCD, tRCD, tEXTRW |
| DDRC_TIMING4 | [92-95] | tRRD, tRAS, tRC, tFAW |

---

## Platform Profiles

Each SoC variant has a `platform_profile_t` that controls protocol behavior. Retrieved via `platform_get_profile(variant)`.

### Profile Fields

| Field | Description |
|-------|-------------|
| `descriptor_mode` | How marker + descriptor are sent (RAW_BULK vs MARKER_THEN_SEND) |
| `erase_wait` | Erase completion strategy (POLLING vs FIXED delay) |
| `crc_format` | VR_WRITE handshake layout (STANDARD, T20, A1, VENDOR) |
| `trailer[8]` | 8-byte tail of the 40-byte handshake |
| `skip_set_data_addr` | Skip VR_SET_DATA_ADDR before write chunks |
| `per_chunk_status_read` | VR_FW_READ after each write chunk |
| `poll_spl_after_ddr` | Poll GET_CPU_INFO after SPL execution |
| `reopen_usb_after_spl` | Reopen USB handle after SPL (T31ZX) |
| `use_a1_handshake` | A1-specific handshake layout |
| `set_data_len_per_chunk` | SET_DATA_LEN = chunk size, not total firmware size |
| `ddr_init_wait_ms` | Post-ProgStage1 wait (1100ms or 2000ms) |

### Profile Summary

| Platform | CRC Format | Descriptor Mode | Erase Wait | Key Quirks |
|----------|-----------|-----------------|------------|------------|
| T10 | T20 | RAW_BULK | Fixed 50s | skip_set_data_addr, poll_spl |
| T20 | T20 | RAW_BULK | Fixed 50s | skip_set_data_addr, poll_spl |
| T21 | T20 | RAW_BULK | Fixed 50s | skip_set_data_addr, poll_spl |
| T30 | T20 | RAW_BULK | Fixed 50s | skip_set_data_addr, poll_spl |
| T31 | STANDARD | MARKER_THEN_SEND | Polling | Reference platform |
| T31A | T20 | MARKER_THEN_SEND | Polling | DDR3 variant, T20-style CRC |
| T31ZX | STANDARD | MARKER_THEN_SEND | Polling | reopen_usb_after_spl |
| T32 | STANDARD | MARKER_THEN_SEND | Polling | Uses T31 profile; 976-byte descriptor |
| T40 | VENDOR | MARKER_THEN_SEND | Polling | skip_set_data_addr, vendor CRC |
| T41 | VENDOR | MARKER_THEN_SEND | Polling | skip_set_data_addr, vendor CRC |
| A1 | A1 | MARKER_THEN_SEND | Polling | a1_handshake, set_data_len_per_chunk |

---

## Platform-Specific Notes

### T31 (Reference Platform)

- Standard xburst1 protocol, 972-byte descriptor
- CRC_FMT_STANDARD: CRC32 at [20-23], OPS at [24-27], ~CRC at [28-31]
- ERASE_WAIT_POLLING: polls `VR_FW_READ_STATUS2` until stable (3 consecutive identical reads)
- Do NOT poll `VR_FW_READ` during erase -- interferes with the SFC controller and causes erase failure
- 128KB default chunk size
- Covers T31, T31X, T31ZX, T31A, T23, T32 variants (T31ZX has `reopen_usb_after_spl`)

### T32

- Uses T31 platform profile (mapped in `platform.c`)
- 976-byte descriptor (4 bytes larger than T31): SFC Config 1 has blocksize (0x8000) at +0x10 and SFC frequency (50 MHz) at +0x18, shifting all subsequent fields by +4
- FIDB config field at 0x2C = 0x00000000 (T31 uses 0x11)
- Vendor cloner 2.5.49 format (different from the 2.5.43 format used by T31/T40/T41/A1)
- Requires `--flash-chip` override (no auto-detect support confirmed)

### T40

- xburst2 architecture, CRC_FMT_VENDOR with uint64_t fields in handshake
- 984-byte descriptor: 12-byte RDD prefix (marker 0xC9) + 972-byte GBD body
- SFC config within GBD uses T31 layout but overrides +0x10 with 0x8000 (blocksize)
- ACK polling during erase: polls `VR_FW_READ (0x10)` with 5s timeout per poll
  - Device returns -110 while busy, real ACK value (0) when ready
  - Adaptive: falls back to 60s silent wait if first polls fail
- Vendor firmware reports X2500 magic in firmware stage
- Write handshake uses raw CRC (no final XOR): `~standard_crc32(data)`
- 50s ACK timeout per chunk (vs 5s for T31)

### T41

- Same xburst2 protocol as T40 (CRC_FMT_VENDOR, 984-byte descriptor)
- Cannot be polled during erase -- ACK polls timeout, falls back to 60s silent wait
- Vendor U-Boot reports X2580 magic in firmware stage
- DDR3 (unlike T40's DDR2)

### A1

- xburst2 with unique A1-specific handshake layout
- 992-byte descriptor: 12-byte RDD prefix (marker 0x84) + 980-byte GBD body
- GBD body uses shifted SFC layout: SFC config at GBD+0xD0 (vs GBD+0xC8 for T31)
- Policy section uses 0xAC (172 bytes) instead of T31's 0xA4 (164 bytes)
- Debug config string "5,100,60" at GBD+0xC8 (before SFC config)
- Write handshake: OPS (0x600) at [8-11], offset at [12-15] (reversed from T31)
- Read handshake: OPS at [8-11], offset at [12-15] (same as write, unlike T31)
- `set_data_len_per_chunk`: VR_SET_DATA_LEN sends chunk size, not total firmware size
- 60s silent erase wait: NO USB transfers during erase or burner reports ep0-control timeout
- FIDB uses unique xb2 layout (different from T40/T41 which use xb1-style FIDB fill)

### T10/T20/T21/T30

- Oldest supported platforms, DESC_RAW_BULK_THEN_SEND mode
- Raw bulk OUT for partition marker (not VR_FW_WRITE2 control + bulk)
- Fixed 50s erase delay (no status polling)
- CRC_FMT_T20: zeros at [20-23] instead of CRC32
- `poll_spl_after_ddr`: polls GET_CPU_INFO after SPL execution
- `skip_set_data_addr`: no VR_SET_DATA_ADDR before write chunks
- DDR init wait: 1100ms (faster than T31's 2000ms)
- T20: requires drain of bulk IN (EP 0x81) after each write chunk or crashes on next VR_WRITE
- T10/T20 use struct ddr_registers RDD format (196 bytes); T21/T30 use INNO PHY (124 bytes)

### MX25L25645G (32MB Flash)

- 32MB chip uses 3-byte addressing by default
- The vendor burner does not send EN4B command, so reads/writes beyond 16MB boundary fail
- Requires `--flash-chip MX25L25645G` override and may need modified descriptor with 4-byte addressing

---

## Vendor Firmware

The SPL and U-Boot binaries in `firmware/cloner/` are extracted from Ingenic's proprietary cloner tool. These run on the target SoC during the firmware stage and implement the flash read/write protocol.

- **Source:** Ingenic cloner 2.5.43 (T10/T20/T21/T23/T30/T31/T31A/T40/T41/A1)
- **T32 exception:** Uses firmware from cloner 2.5.49 (different struct arguments in U-Boot burner)
- **Important:** The vendor burner running on the device is NOT the open-source `f_jz_cloner.c` from U-Boot source. The protocol is similar but the struct layouts differ. All byte-level details in this document were verified against USB packet captures of the actual vendor tool.

---

## Remote Protocol

The `dfu-remote` daemon accepts TCP connections on port 5050 (configurable). Wire format uses big-endian fields.

### Message Header

```
[0-3]    magic: 0x434C4E52 ("CLNR")
[4]      version: 1
[5]      command: CMD_DISCOVER(1), CMD_BOOTSTRAP(2), CMD_WRITE(3), CMD_READ(4), CMD_STATUS(5), CMD_CANCEL(6)
[6-9]    payload_len (u32 BE)
```

### Response Header

```
[0-3]    magic
[4]      version
[5]      status: RESP_OK(0), RESP_ERROR(1), RESP_PROGRESS(2)
[6-9]    payload_len (u32 BE)
```

Max payload: 64 MB. Single client at a time, no multiplexing. Progress updates are sent as RESP_PROGRESS messages during long operations (bootstrap, write, read).
