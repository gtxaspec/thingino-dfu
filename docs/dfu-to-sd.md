# DFU provisioning: SD card (MSC) and boot flash

The Ingenic USB-boot ("usbboot") U-Boot loaders can expose the on-board
**SD card (MSC0)** as a DFU target in addition to the **boot flash**
(SPI-NOR / SPI-NAND). One loader provisions either, selected at transfer
time. This is how to drive it with `thingino-dfu`.

## What the loader exposes

When built with `CONFIG_DFU_MMC` (enabled on the T41 usbboot loaders) the
loader runs `mmc dev 0; dfu 0` and advertises two DFU devices:

| alt name | target                | DFU string                    |
|----------|-----------------------|-------------------------------|
| `flash`  | boot SPI-NOR/SPI-NAND | `flash raw 0x0 0x<chip-size>` |
| `sdcard` | SD card on MSC0       | `mmc 0=sdcard raw 0x0 0x4000`  |

- `flash` is **alt 0** and the default target — tooling that does not ask
  for an alt keeps writing the boot flash, unchanged.
- `sdcard` is **alt 1**, opt-in via `--alt sdcard`.
- The SD alt is **always advertised**, never probed at boot: some boards
  gate the SD slot behind a GPIO the generic loader does not drive, so the
  card is only touched at transfer time. An absent/unpowered card just
  fails that one transfer; it never blocks `flash`.

The default `sdcard` window is `0x4000` blocks = **8 MiB**. Enlarge it in
`board_late_init()` (`arch/mips/mach-xburst/dfu.c`, U-Boot) to write past
8 MiB.

## Bootstrap the loader

Put the device in USB boot mode and load SPL + U-Boot over the bootrom.
Release any NOR boot-strap *before* the DFU write — it shorts NOR DI and
you get "Device not found":

    thingino-dfu --host <daemon-ip> --cpu t41 \
        --spl  u-boot-spl.bin \
        --uboot u-boot.bin -b

(Omit `--host` for a locally-attached device.) If the daemon reports
"Device not found", just retry — its USB scan is a one-time snapshot.

## Write / read / wipe

Select the target with `--alt` (`flash` is used if omitted):

    # write an image to the SD card
    thingino-dfu --host <ip> --alt sdcard -w sdcard.img

    # read the SD card back
    thingino-dfu --host <ip> --alt sdcard -r readback.img

    # write the boot flash (default target)
    thingino-dfu --host <ip> -w u-boot-full.bin

### Wipe the first 1 MiB of the SD card

    head -c 1048576 /dev/zero > zero1m.bin
    thingino-dfu --host <ip> --alt sdcard -w zero1m.bin
    # verify
    thingino-dfu --host <ip> --alt sdcard -r check.bin
    head -c 1048576 check.bin | tr -d '\0' | wc -c    # 0 == all-zero

### Wipe the boot flash

    # all-0xFF image the size of the chip (e.g. 32 MiB)
    head -c 33554432 /dev/zero | tr '\000' '\377' > ff.bin
    thingino-dfu --host <ip> --alt flash -w ff.bin

## Note: the MSC re-init/retry (T41 and other SDHCI MSCs)

On the T41 (XBurst2, standard SDHCI core) the SD multi-block write is
unreliable straight out of the USB-boot path: the controller mishandles
the card's CRC-status a few blocks into a `CMD25` when the transfer runs
on the **stale boot-time card init** (after the USB gadget has come up),
failing with a data CRC / end-bit error. A bare `mmc write` from the
console is reliable only because `mmc rescan` re-inits the card first.

The fix lives in U-Boot `drivers/dfu/dfu_mmc.c`: on a write failure the
DFU MMC backend forces a full card re-init and retries
(`mmc->has_init = 0; mmc_init()` — `mmc_init()` is a no-op unless
`has_init` is cleared). Recovery is transparent; you may see one
`MMC: write error - re-initialising card and retrying` line, after which
the write succeeds.

It is *not* the OTG DMA bus-master contending with the MSC — gating the
OTG clock across the write changes nothing — it is stale controller state
cleared only by a real re-init.

U-Boot branch `ingenic-t-series`, commits:
- `dfu: mmc: retry a failed write after re-initialising the card`
- `xburst: dfu: add SD-card (MSC) write support to the usbboot loaders`
