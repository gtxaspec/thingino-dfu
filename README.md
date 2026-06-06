# thingino-dfu

USB flashing tool for Ingenic SoC devices (T10-T41, A1). It boots a device from its USB bootrom into U-Boot, then reads or writes the on-board SPI flash. The SoC variant is auto-detected - no manual configuration needed.

Two backends are supported:

- **DFU (default, preferred)** - drives mainline U-Boot's standard USB DFU mode.
- **Cloner (legacy)** - reimplements Ingenic's vendor USB-boot flash protocol.

> **Why DFU is the preferred method**
>
> DFU is a real USB standard - the [USB Device Firmware Upgrade 1.1](https://www.usb.org/sites/default/files/DFU_1.1.pdf) class - and mainline (and thingino) U-Boot implements it natively through the `dfu` command. The device exposes its flash as named partitions (DFU alt-settings, defined by U-Boot's `dfu_alt_info`), and the host simply moves bytes to or from them. The **device side owns** the medium (SPI NOR / NAND / MMC / MTD), the DDR bring-up, and the partition layout.
>
> That makes DFU portable (any DFU host works, including `dfu-util`), future-proof, and free of reverse-engineered, per-SoC flash protocols. The **cloner** backend is the older reverse-engineered vendor path - the host does DDR init, JEDEC flash detection, and drives the raw read/write protocol itself. It's kept for bring-up and for devices that don't yet run a DFU-capable U-Boot.

## Backends

### DFU (default)

The device runs U-Boot's `dfu` command and enumerates as a standard USB DFU 1.1 gadget. The host does two things:

1. **Bootstrap** (`-b`): USB-boots a DFU-capable SPL + U-Boot onto the device from its bootrom. The device then re-enumerates as a DFU gadget.
2. **Transfer**: lists the alt-settings (partitions named by `dfu_alt_info`) and uploads (reads) or downloads (writes) to them.

Plain DFU 1.1, no DfuSe (ST) extension. Because it's standard DFU, the medium and partition layout are entirely a device-side concern - the host only moves bytes.

### Cloner (legacy)

`--cloner` selects the reverse-engineered Ingenic vendor protocol. The host performs DDR init (generated dynamically from a compiled-in chip database - no vendor `ddr.bin` needed), auto-detects the flash chip via JEDEC ID, and drives the raw read/write protocol itself. Useful for bring-up or where a DFU U-Boot isn't available.

## Usage

```
# DFU (default) - device runs U-Boot's `dfu` command:
thingino-dfu -b                              # Bootrom -> U-Boot DFU mode (auto-detect SoC)
thingino-dfu -l                              # List DFU alt-settings (partitions)
thingino-dfu --alt rootfs -w rootfs.bin      # Write a partition via DFU
thingino-dfu --alt u-boot -r uboot.bin       # Read a partition via DFU

# Cloner (legacy) - vendor USB-boot flash protocol:
thingino-dfu --cloner -i 0 -b -w firmware.bin   # Bootstrap + write whole flash
thingino-dfu --list-cpus                     # Show supported CPU targets
```

The SoC is auto-detected by reading hardware ID registers from the bootrom. Use `--cpu <variant>` to override if needed.

### Options

| Option | Description |
|--------|-------------|
| `-l, --list` | List connected Ingenic USB devices (or DFU alt-settings, in DFU mode) |
| `-i, --index <n>` | Device index (default: 0) |
| `-b, --bootstrap` | Bootstrap device (bootrom -> U-Boot) |
| `-w, --write <file>` | Write firmware to flash |
| `-r, --read <file>` | Read firmware from flash |
| `--alt <name/num>` | DFU alt-setting (partition) to target |
| `--cloner` | Use the legacy cloner backend (default: DFU) |
| `--cpu <variant>` | Override SoC variant (default: auto-detect) |
| `--spl <file>` / `--uboot <file>` | Use a specific SPL / U-Boot for bootstrap (skips SoC detection) |
| `--firmware-dir <dir>` | Firmware root directory (default: `./firmware`) |
| `--wait` | Wait for the device to appear before acting |
| `--host <addr>` | Connect to a remote `dfu-remote` daemon |
| `-v, --verbose` | Verbose output |
| `-d, --debug` | Debug output |

Cloner-only options: `--flash-chip <name>` (override JEDEC auto-detect), `--chunk-size <bytes>`, `--erase`, `--reboot`, `--skip-ddr`.

## Supported Platforms

| SoC | Arch | DDR | Bootstrap | Write | Read | Auto-Detect |
|-----|------|-----|-----------|-------|------|-------------|
| T10 | xburst1 | DDR2 | Tested | Tested | Tested | Yes |
| T20 | xburst1 | DDR2 | Tested | Tested | Tested | Yes |
| T21 | xburst1 | DDR2 | Tested | Tested | Tested | Yes |
| T23 | xburst1 | DDR2 | Tested | Tested | Tested | Yes |
| T30 | xburst1 | DDR2 | Tested | Tested | Tested | Yes |
| T31 | xburst1 | DDR2 | Tested | Tested | Tested | Yes |
| T31A | xburst1 | DDR3 | Tested | Tested | Tested | Yes |
| T32 | xburst1 | DDR2 | Tested | Tested | Tested | Yes |
| T40 | xburst2 | DDR2 | Tested | Tested | Tested | Yes |
| T41 | xburst2 | DDR3 | Tested | Tested | Tested | Yes |
| A1 | xburst2 | DDR3 | Tested | Tested | Tested | Yes |

## SoC Auto-Detection

Both backends auto-detect the SoC during bootstrap: the tool uploads a tiny MIPS program to the device's bootrom that reads hardware ID registers (SoC ID, EFUSE sub-type) and returns the exact chip variant. For DFU this selects the right DFU-capable U-Boot; for cloner it also drives DDR type selection (DDR2 vs DDR3).

Every sub-variant is resolved automatically - steppings, DDR2/DDR3 parts, and EFUSE sub-types are all detected, so there's no need to enumerate them and you never pick one by hand. `--cpu <variant>` is only an override; run `thingino-dfu --list-cpus` for the full set of recognized targets.

## Build

### Linux (native)

Requires `libusb-1.0-dev`.

```
make          # native build
make arm64    # cross-compile for aarch64
```

### Windows (cross-compile from Linux)

Requires `gcc-mingw-w64-x86-64` and `p7zip-full`. Downloads libusb automatically.

```
make win64
```

### macOS

Requires `libusb` from Homebrew (`brew install libusb`).

```
make
```

### Web (WASM + WebUSB)

Requires [Emscripten](https://emscripten.org/).

```
cd web
bash build.sh
```

Output: `web/dist/`. Serve it with any HTTPS server and open in Chrome/Edge (WebUSB requires a secure context). The web flasher compiles the entire C library to WebAssembly and replaces libusb with a WebUSB shim, so detect, bootstrap, read, and write all run in the browser - no install required. DFU is the default backend; an Advanced panel lets you supply a custom SPL + U-Boot for the bootstrap.

### Android

Requires Android SDK, NDK 27, and Java 17.

```
cd android
./gradlew assembleRelease
```

APK: `android/app/build/outputs/apk/release/app-release.apk`. The app supports local USB (via USB OTG) and remote mode (connecting to a `dfu-remote` daemon over TCP); the backend (DFU / cloner) is chosen in Settings.

### Install

```
sudo make install                    # install to /usr/local
make install PREFIX=/opt/thingino    # custom prefix
sudo make uninstall                  # remove
```

Output binaries:
- Linux: `build/cli/thingino-dfu`, `build/dfu-remote/dfu-remote`
- ARM64: `build-aarch64/cli/thingino-dfu`
- Windows: `build-win64/cli/thingino-dfu.exe` + `libusb-1.0.dll`
- Android: `android/app/build/outputs/apk/release/app-release.apk`
- Web: `web/dist/` (serve with any HTTPS server)

## Remote Mode

For headless setups (e.g. an Orange Pi connected to devices via USB):

1. Run `dfu-remote` on the USB host machine.
2. Use `--host <addr>` from anywhere on the network.

Remote defaults to DFU as well (`--cloner` for the legacy backend). Protocol uses TCP port 5050. Both the daemon and client work on all platforms (Linux, macOS, Windows, Android).

## Windows Setup

1. If the Ingenic vendor USB driver is installed, remove it first via Device Manager.
2. Connect the device in USB boot mode.
3. Install the WinUSB driver using [Zadig](https://zadig.akeo.ie/): select the "Ingenic USB Boot Device" and install WinUSB.
4. Run `thingino-dfu.exe -i 0 -b`.

**Important:** The Ingenic vendor driver (`libusb0.sys`) is not compatible and must be removed before installing WinUSB via Zadig.

## Firmware

SPL + U-Boot binaries used for bootstrap live under `firmware/`:

- **DFU:** `firmware/dfu/<family>/{spl,uboot}.bin` - DFU-capable mainline U-Boot (the device runs `dfu` after boot).
- **Cloner:** `firmware/cloner/<platform>/{spl,uboot}.bin` - the vendor burner loader.

The DFU images are built from [gtxaspec/u-boot](https://github.com/gtxaspec/u-boot) (the `isvp_<soc>_usbboot` defconfigs).

## Hardware Databases (cloner backend)

All chip data is compiled in, no config files at runtime - the cloner backend uses these for DDR bring-up and flash detection:

- **DDR chips:** 36 entries (DDR2, DDR3, LPDDR2)
- **SPI NOR flash:** 49 entries (JEDEC-ID matched)
- **NAND flash:** 12 entries
- **Platform configs:** 19 processors

(DFU doesn't need these - the device's own U-Boot owns DDR and the flash medium.)

## Technical Details

See [docs/ENGINEERING.md](docs/ENGINEERING.md) for USB protocol documentation, flash descriptor formats, DDR binary layout, and platform-specific behavior notes.
