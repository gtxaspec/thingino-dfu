# Adding a New SoC Variant

thingino-dfu drives mainline U-Boot's DFU mode, so the device's own U-Boot owns
DDR bring-up, the flash medium, and the partition layout. Adding support for a
new Ingenic SoC therefore comes down to **detecting** the chip and pointing it at
the right DFU-capable U-Boot — there are no host-side flash or DDR databases to
maintain.

## 1. Add the variant enum

**File:** `libtdfu/include/tdfu/tdfu.h`

Add a value to `tdfu_variant_t`:

```c
typedef enum tdfu_variant {
    // ...existing variants...
    TDFU_VARIANT_NEW_SOC,
} tdfu_variant_t;
```

## 2. Add the variant <-> string mapping

**File:** `libtdfu/src/utils.c`

In `tdfu_variant_to_string()` and `tdfu_variant_from_string()`:

```c
case TDFU_VARIANT_NEW_SOC: return "new_soc";
// ...
if (strcmp(lower, "new_soc") == 0) return TDFU_VARIANT_NEW_SOC;
```

## 3. Add SoC auto-detection

**File:** `libtdfu/src/usb/protocol.c`

`protocol_detect_soc()` uploads a small MIPS stub that reads the SoC ID and
EFUSE sub-type registers from the bootrom. Map the decoded IDs to the new
variant in its `switch` (and add the chip name used in the log output). The
`subtype1` grade code distinguishes steppings / DDR grades that share a CPU
family ID.

```c
case 0xNNNN:  // CPU family ID from the SoC ID register
    *variant = TDFU_VARIANT_NEW_SOC;
    break;
```

## 4. Map the variant to its DFU loader directory

**File:** `libtdfu/src/dfu/dfu.c`

`dfu_variant_dir()` resolves a variant to `firmware/dfu/<dir>`. Add a case only
if the new SoC's loader directory name differs from its variant string;
otherwise the 1:1 default is used.

## 5. Add the DFU loader binaries

Drop the DFU-capable stage1 + U-Boot under:

```
firmware/dfu/new_soc/tpl.bin   (or spl.bin on the big-SPL SoCs)
firmware/dfu/new_soc/uboot.bin
```

These are built from [gtxaspec/u-boot](https://github.com/gtxaspec/u-boot)'s
`isvp_<soc>_usbboot` defconfig. The `sync-usbboot` GitHub workflow refreshes them
from the rolling `usbboot` release.

## 6. Build and test

```bash
make
./build/cli/thingino-dfu -i 0 -b --cpu new_soc   # bootstrap into U-Boot DFU
./build/cli/thingino-dfu -l                       # list DFU alt-settings
```

The variant table is shared by the CLI, the `dfu-remote` daemon, the web flasher,
and the Android app, so all four pick up the new SoC automatically.
