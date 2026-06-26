# thingino-dfu

> 💡 **Just want to use it?** Visit [https://webflash.thingino.com/](https://webflash.thingino.com/) — no install required.

USB flashing tool for Ingenic SoC devices (T10-T41, A1). It boots a device from its USB bootrom into U-Boot's standard USB DFU mode, then reads or writes the on-board flash. The SoC variant is auto-detected - no manual configuration needed.

## How it works

The device runs mainline (and thingino) U-Boot's `dfu` command and enumerates as a standard [USB DFU 1.1](https://www.usb.org/sites/default/files/DFU_1.1.pdf) gadget. The host does two things:

1. **Bootstrap** (`-b`): USB-boots a DFU-capable SPL + U-Boot onto the device from its bootrom. The device then re-enumerates as a DFU gadget.
2. **Transfer**: lists the alt-settings (partitions named by U-Boot's `dfu_alt_info`) and uploads (reads) or downloads (writes) to them.

It's plain DFU 1.1 (no DfuSe/ST extension), so the **device side owns** the medium (SPI NOR / NAND / MMC / MTD), the DDR bring-up, and the partition layout — the host only moves bytes. That makes it portable (any DFU host works, including `dfu-util`), future-proof, and free of reverse-engineered, per-SoC flash protocols.

## Usage

```
thingino-dfu -b                              # Bootrom -> U-Boot DFU mode (auto-detect SoC)
thingino-dfu -l                              # List DFU alt-settings (partitions)
thingino-dfu --alt rootfs -w rootfs.bin      # Write a partition via DFU
thingino-dfu --alt u-boot -r uboot.bin       # Read a partition via DFU
```

The SoC is auto-detected by reading hardware ID registers from the bootrom. Use `--cpu <variant>` to override if needed.

### Options

| Option | Description |
|--------|-------------|
| `-l, --list` | List connected Ingenic USB devices (or DFU alt-settings, in DFU mode) |
| `-i, --index <n>` | Device index (default: 0) |
| `-b, --bootstrap` | Bootstrap device (bootrom -> U-Boot DFU mode) |
| `-w, --write <file>` | Write firmware to a DFU alt-setting |
| `-r, --read <file>` | Read firmware from a DFU alt-setting |
| `--alt <name/num>` | DFU alt-setting (partition) to target |
| `--cpu <variant>` | Override SoC variant (default: auto-detect) |
| `--spl <file>` / `--uboot <file>` | Use a specific SPL / U-Boot for bootstrap (skips SoC detection) |
| `--firmware-dir <dir>` | Firmware root directory (default: `./firmware`) |
| `--wait` | Wait for the device to appear before acting |
| `--host <addr>` | Connect to a remote `dfu-remote` daemon |
| `--port <n>` | Remote daemon port (default: 5050) |
| `--token <secret>` | Auth token for the remote daemon |
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
| C100 | xburst1 | DDR3 | Tested | Tested | Tested | Yes |

## SoC Auto-Detection

The tool auto-detects the SoC during bootstrap: it uploads a tiny MIPS program to the device's bootrom that reads hardware ID registers (SoC ID, EFUSE sub-type) and returns the exact chip variant, which selects the right DFU-capable U-Boot.

Every sub-variant is resolved automatically - steppings, DDR2/DDR3 parts, and EFUSE sub-types are all detected, so there's no need to enumerate them and you never pick one by hand. `--cpu <variant>` is only an override.

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

Output: `web/dist/`. Serve it with any HTTPS server and open in Chrome/Edge (WebUSB requires a secure context). The web flasher compiles the C library to WebAssembly and replaces libusb with a WebUSB shim, so detect, bootstrap, read, and write all run in the browser - no install required. An Advanced panel lets you supply a custom SPL + U-Boot for the bootstrap, and a Remote mode connects to a `dfu-remote` daemon.

### Android

Requires Android SDK, NDK 27, and Java 17.

```
cd android
./gradlew assembleRelease
```

APK: `android/app/build/outputs/apk/release/app-release.apk`. The app supports local USB (via USB OTG) and remote mode (connecting to a `dfu-remote` daemon over TCP).

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

Protocol uses TCP port 5050. Both the daemon and client work on all platforms (Linux, macOS, Windows, Android).

## Windows Setup

1. If the Ingenic vendor USB driver is installed, remove it first via Device Manager.
2. Connect the device in USB boot mode.
3. Install the WinUSB driver using [Zadig](https://zadig.akeo.ie/): select the "Ingenic USB Boot Device" and install WinUSB.
4. Run `thingino-dfu.exe -i 0 -b`.

**Important:** The Ingenic vendor driver (`libusb0.sys`) is not compatible and must be removed before installing WinUSB via Zadig.

## Firmware

The DFU-capable SPL + U-Boot binaries USB-booted during bootstrap live under `firmware/dfu/<variant>/{tpl,spl,uboot}.bin`. They are built from [gtxaspec/u-boot](https://github.com/gtxaspec/u-boot) (the `isvp_<soc>_usbboot` defconfigs) and refreshed by the `sync-usbboot` workflow.

## Documentation

- [docs/ADDING_HARDWARE.md](docs/ADDING_HARDWARE.md) - adding support for a new SoC variant.
- [docs/dfu-to-sd.md](docs/dfu-to-sd.md) - provisioning an SD card over DFU.
