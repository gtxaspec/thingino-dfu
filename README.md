# thingino-cloner

USB flashing tool for Ingenic SoC devices. Bootstraps a device over USB (DDR init, SPL, U-Boot), then reads or writes SPI NOR flash. Automatically detects the SoC variant, no manual configuration needed.

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

Output: `web/dist/cloner.js` + `web/dist/cloner.wasm`. Serve `web/` with any HTTP server and open in Chrome/Edge. Copy `firmwares/` to `web/public/firmware/` for the bootstrap binaries.

The web flasher compiles the entire C library to WebAssembly and replaces libusb with a WebUSB shim. Supports detect, bootstrap, read, and write directly from the browser — no install required. Requires HTTPS or localhost.

### Android

Requires Android SDK, NDK 27, and Java 17.

```
cd android
./gradlew assembleRelease
```

APK output: `android/app/build/outputs/apk/release/app-release.apk`

The Android app supports both local USB (via USB OTG) and remote mode (connecting to a `cloner-remote` daemon over TCP).

### Install

```
sudo make install                    # install to /usr/local
make install PREFIX=/opt/thingino   # custom prefix
sudo make uninstall                  # remove
```

Output binaries:
- Linux: `build/cli/thingino-cloner`
- ARM64: `build-aarch64/cli/thingino-cloner`
- Windows: `build-win64/cli/thingino-cloner.exe` + `libusb-1.0.dll`
- Android: `android/app/build/outputs/apk/release/app-release.apk`
- Web: `web/dist/cloner.js` + `web/dist/cloner.wasm` (serve with any HTTP server)

## Usage

```
thingino-cloner -i 0 -b                        # Bootstrap device (auto-detect SoC)
thingino-cloner -i 0 -b -w firmware.bin         # Bootstrap + write firmware
thingino-cloner -i 0 -b -r dump.bin             # Bootstrap + read flash
thingino-cloner -l                               # List connected devices
thingino-cloner --list-cpus                      # Show supported CPU targets
```

The SoC is auto-detected by reading hardware ID registers from the bootrom. Use `--cpu <variant>` to override if needed.

### Options

| Option | Description |
|--------|-------------|
| `-l, --list` | List connected Ingenic USB devices |
| `-i, --index <n>` | Device index (default: 0) |
| `-b, --bootstrap` | Bootstrap device (DDR + SPL + U-Boot) |
| `-w, --write <file>` | Write firmware to flash |
| `-r, --read <file>` | Read firmware from flash |
| `--cpu <variant>` | Override SoC variant (default: auto-detect) |
| `--flash-chip <name>` | Override flash chip (default: auto-detect via JEDEC) |
| `--chunk-size <bytes>` | Write chunk size (default: 128KB) |
| `--erase` | Erase flash before writing |
| `--reboot` | Reboot device after write |
| `--skip-ddr` | Skip DDR configuration |
| `--firmware-dir <dir>` | Firmware directory (default: `./firmwares`) |
| `--host <addr>` | Connect to remote cloner-remote daemon |
| `-v, --verbose` | Verbose output |
| `-d, --debug` | Debug output |

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

All DDR configuration is generated dynamically from a compiled-in chip database (36 chips). No vendor `ddr.bin` files needed.

## SoC Auto-Detection

The tool uploads a tiny MIPS program to the device's bootrom that reads hardware ID registers (SoC ID, EFUSE sub-type) and returns the exact chip variant. This enables correct DDR type selection (DDR2 vs DDR3) without manual configuration.

Detected sub-variants include: T31X, T31N, T31A, T31AL, T31ZX, T32LQ, T40N, T40NN, T40XP, T41NQ, A1, and more.

## Windows Setup

1. If the Ingenic vendor USB driver is installed, remove it first via Device Manager
2. Connect the device in USB boot mode
3. Install the WinUSB driver using [Zadig](https://zadig.akeo.ie/). Select the "Ingenic USB Boot Device" and install WinUSB
4. Run `thingino-cloner.exe -i 0 -b`

**Important:** The Ingenic vendor driver (libusb0.sys) is not compatible and must be removed before installing WinUSB via Zadig.

## Flash Chip Detection

The flash chip is auto-detected via JEDEC ID after bootstrap. The tool includes a database of 49 SPI NOR chips. Use `--flash-chip <name>` to override if auto-detection fails or the chip is not in the database.

## Remote Mode

For headless setups (e.g., an Orange Pi connected to devices via USB):

1. Run `cloner-remote` on the USB host machine
2. Use `--host <addr>` from anywhere on the network

Protocol uses TCP port 5050. Both the daemon and client work on all platforms (Linux, macOS, Windows).

## Hardware Databases

All chip data is compiled in, no config files at runtime:

- **DDR chips:** 36 entries (DDR2, DDR3, LPDDR2)
- **SPI NOR flash:** 49 entries
- **NAND flash:** 12 entries
- **Platform configs:** 19 processors

SPL and U-Boot binaries are loaded from `firmwares/<platform>/spl.bin` and `uboot.bin`.

## Technical Details

See [docs/ENGINEERING.md](docs/ENGINEERING.md) for USB protocol documentation, flash descriptor formats, DDR binary layout, and platform-specific behavior notes.
