# TODO / Roadmap

Tracked future work for thingino-dfu.

## Future features

### Web/Android UI for `--erase` (whole-chip erase)
CLI + daemon + loader support landed (`--erase`, loader DFU alt "erase", wipe token
`XBURST-FLASH-WIPE`). Remaining: an "Erase entire flash first" checkbox in the web
flasher and Android UI. Web notes: the remote path is trivial (CMD_WRITE of the
17-byte token with alt="erase"); the local WebUSB path needs an alt-index fallback
because the shim can't read iInterface strings (erase = the alt after "flash", or
teach the shim string-descriptor reads). Erase must stay strictly opt-in - it wipes
the whole chip, and iterating on just the boot region must not trigger it.

### `--verify` — read back and verify written data
After a DFU write, read the flash back (DFU UPLOAD) and compare it against the source.
- Host-side only, no firmware change — DFU UPLOAD already works (it's the `-r` read path).
- Run it in the **same DFU session** right after the write (U-Boot DFU is manifestation-tolerant: it returns to idle, so an UPLOAD works without re-bootstrapping).
- Compare only the **written length**: the `flash raw 0x0 0x<size>` alt is the whole chip, so a full read-back includes `0xFF` erase padding past the image.
- **NOR** verifies byte-exact; **NAND** is best-effort (ECC/OOB + bad-block remap can legitimately differ) — warn, don't hard-fail.
- Roughly doubles the flash-cycle time, so make it **opt-in**, not default.
- Wire through: CLI (`--verify`), the dfu-remote protocol, the web UI, and the Android UI. Reuse the existing CRC32 routine for the compare.

### `--transfer-size N` — tune the DFU block size
Let the user set the DNLOAD/UPLOAD block size, **clamped to the device's advertised `wTransferSize`**.
- Host-side knob; can only match-or-shrink the device max — a compat/debug lever for flaky USB paths, not a way to exceed the device.
- The real speedup is **device-side**: bump the loader's advertised `wTransferSize` / DFU buffer in `gtxaspec/u-boot` (the `isvp_<soc>_usbboot` defconfigs), then `sync-usbboot`. 4096 -> 32K/64K cuts the per-block USB+poll round-trips ~8-16x.
- Also check that `dfu_poll_until_ready` honors the device's `bwPollTimeout` instead of over-sleeping (a free host-side win).
- Wire through CLI/daemon/web/Android.

## Code-quality backlog (from the 2026-06-28 review)

### Tier 1 — confirmed bugs (fix first)
- [ ] **libusb_device ref leak** — `usb_device_close` never unrefs `device->device` (refs at `device.c:176,291`; no `libusb_unref_device` anywhere). Daemon leaks one device-ref per device per discovery poll. Field is never read — likely just drop the ref.
- [ ] **OOB read on hostile wire length** — `p + fw_len + 4 > end` wraps on 32-bit / is UB (`dfu-remote/main.c:481,381,389,471`). Use remaining-length compares: `avail = end-p; if (avail < 4 || len > avail-4) ...`.
- [ ] **Windows temp-file collision** — `stage_temp_blob` uses one fixed name, so the U-Boot stage overwrites the SPL stage (`main.c:301,499`). Use a unique temp name per blob.
- [ ] **Parser desync** — `variant_len` clamped to 63 *before* `p += variant_len` in write/read (`main.c:461-464,562-566`); bootstrap does it right. Validate vs `end`, advance by full `len`; factor into one helper.
- [ ] **NULL-deref on alloc failure** — `list->count` set before the `calloc` null-check (`core.c:67`).
- [ ] Smaller: uninitialized `transferred` read on error path (`bootstrap.c:108` + `device.c:442`); `tolower(char)` UB >=0x80 (`utils.c:139`); progress `%` overflow on arm32 >42 MB (`dfu.c:523`); `dfu_close_device(dev,0)` hardcodes iface 0 vs claimed `info.interface` (`dfu.c:633,644`).

### Tier 2 — daemon security posture (harden for the public tool; low urgency on a trusted LAN)
- [ ] Auth off unless `--token`, binds `INADDR_ANY`, wildcard CORS + `Allow-Private-Network: true` (`main.c:64,889,1003`; `ws.c:225,233`) → default to loopback, require a token unless `--insecure`/`--bind` is explicit, echo back only allow-listed Origins.
- [ ] No `SO_RCVTIMEO` → one idle client wedges the single-client daemon (`main.c:115`).

### Tier 3 — dead code / duplication / C-standards (mechanical, low-risk)
- [ ] Dead code: `flag_addr` switch then `(void)` (`protocol.c:621-643`); `response_length` locals never read (`protocol.c:17,42,67,91,117`); `(void)is_bootrom` that *is* used (`manager.c:75,167`); `retry_delays[4]` unreachable (`device.c:493`); `CMD_CANCEL` no-op (`g_cancel` never read, `main.c:683`); dead `#include "platform.h"` (`main.c:18`) + internal `libtdfu/src` include-paths in cli/daemon CMake.
- [ ] Duplication: `usb_manager_find_devices` vs `_fast` ~95% (`manager.c:42,138`); the 3 variant-copy parse blocks; the triplicated CMake source list.
- [ ] Standards: `static` on cli file-locals (`main.c:46,76,177`); `atoi`->`strtol`+range for `--port` (`main.c:144,962`); `load_file` `malloc(0)` guard (`utils.c:416`); `protocol_read_memory` `(int)len` bound (`protocol.c:155`); fragile `block!=0` vs `uint16_t` wrap discriminator (`dfu.c:498`).

### Tier 4 — modularity (make libtdfu a clean standalone library)
- [ ] Phase 1: split `tdfu.h` into a libusb-free public header + internal `usb_internal.h`; make `usb_device_t`/`usb_manager_t` opaque; define `g_debug_enabled` once in the lib; drop the dead internal-header coupling.
- [ ] Phase 2: move `core.c`'s `#ifdef __EMSCRIPTEN__` web glue into `web/` by promoting the index-based wrappers to the public API.
- [ ] Phase 3: one shared `sources.cmake` (ends the triplication/drift) + real `install(EXPORT tdfu::tdfu)`.
- [ ] Phase 4 (large, optional): internal USB-backend vtable (libusb / WebUSB / Android-fd) so the JS shim becomes a first-class backend and Android stops poking struct internals; then the public structs can go fully opaque.
