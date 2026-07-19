/*
 * Client-side Wi-Fi injection for thingino images.
 *
 * Right before flashing, the loaded image bytes are already in hand, so we bake
 * the user's Wi-Fi credentials into the writable overlay and hand the modified
 * bytes back to the flasher - no upload, no rebuild.
 *
 *   NOR  (flat 'data' partition): repack the JFFS2 overlay        (mkfs.jffs2)
 *   NAND ('-(ubi)' partition):    rebuild the UBI with a fresh    (mkfs.ubifs
 *                                 UBIFS overlay volume             + ubinize)
 *
 * The WASM engines live in /wasm/ (like tdfu.wasm), loaded on first use.
 */
const ALIGN = 0x10000;                         // JFFS2 eraseblock (NOR)
const OVERLAY_MAX_LEBS = 8192;                 // generous UBIFS -c; autoresize fills the real chip

// --- lazy WASM engine loaders (URL in a variable so the bundler leaves them alone) ---
const _mods = {};
async function engine(name) {
  if (!_mods[name]) { const url = '/wasm/' + name + '.mjs'; _mods[name] = (await import(/* @vite-ignore */ url)).default; }
  return _mods[name];
}

// --- read the flash layout straight from the image (mtdparts in the U-Boot env) ---
export function parseMtdparts(u8) {
  const needle = 'mtdparts=';
  let start = -1;
  for (let i = 0; i + needle.length < u8.length; i++) {
    let ok = true;
    for (let j = 0; j < needle.length; j++) if (u8[i + j] !== needle.charCodeAt(j)) { ok = false; break; }
    if (ok) { start = i; break; }
  }
  if (start < 0) throw new Error('no mtdparts in image (not a thingino flash image?)');
  let end = start;
  while (end < u8.length && u8[end] !== 0 && u8[end] > 0x20) end++;
  let s = ''; for (let i = start; i < end; i++) s += String.fromCharCode(u8[i]);
  const spec = s.split(':')[1] || '';
  const parts = {}; let off = 0;
  for (const seg of spec.split(',')) {
    let m = seg.match(/^(\d+)k(@(\d+))?\((\w+)\)/);
    if (m) {
      const size = +m[1] * 1024, name = m[4];
      if (m[3] !== undefined) { parts[name] = { offset: +m[3] * 1024, size, alias: true }; continue; }
      parts[name] = { offset: off, size }; off += size; continue;
    }
    m = seg.match(/^-\((\w+)\)/);                        // "rest of chip" (NAND ubi)
    if (m) { parts[m[1]] = { offset: off, size: -1 }; }
  }
  return { parts, raw: s };
}

// What kind of overlay does this image have?
export function overlayInfo(u8) {
  const { parts } = parseMtdparts(u8);
  if (parts.data) return { ok: true, type: 'nor', offset: parts.data.offset, size: parts.data.size };
  if (parts.ubi)  return { ok: true, type: 'nand', ubiStart: parts.ubi.offset };
  return { ok: false, reason: 'No writable overlay partition found in this image.' };
}

function wpaConf(ssid, psk) {   // client-mode block, matches on-device `wlan configure`
  return `ctrl_interface=/run/wpa_supplicant\nupdate_config=1\nap_scan=1\n\nnetwork={\n\tssid="${ssid}"\n\tpsk="${psk}"\n}\n`;
}
function fixedMtime(FS, paths) { const t = 1000000000 * 1000; for (const p of paths) FS.utime(p, t, t); }

// --- NOR: repack the JFFS2 'data' partition -----------------------------------
async function injectNor(u8, info, ssid, psk) {
  const Module = await (await engine('mkfs_jffs2_memfs'))({ print: () => {}, printErr: () => {} });
  const FS = Module.FS;
  FS.mkdirTree('/in/etc');
  FS.writeFile('/in/etc/wpa_supplicant.conf', wpaConf(ssid, psk));
  fixedMtime(FS, ['/in', '/in/etc', '/in/etc/wpa_supplicant.conf']);
  Module.callMain(['--little-endian', '--squash', `--eraseblock=0x${ALIGN.toString(16)}`,
                   `--pad=0x${info.size.toString(16)}`, '-d', '/in', '-o', '/out.jffs2']);
  const overlay = FS.readFile('/out.jffs2');
  if (overlay.length > info.size) throw new Error('overlay overflow');
  const out = new Uint8Array(u8);
  out.set(overlay, info.offset);
  return out;
}

// --- NAND: parse the UBI, rebuild it with a fresh UBIFS overlay volume ---------
const EC_MAGIC = 0x55424923, VID_MAGIC = 0x55424921;
export function readUbiVolumes(ubi) {
  const be32 = o => ((ubi[o] << 24) | (ubi[o+1] << 16) | (ubi[o+2] << 8) | ubi[o+3]) >>> 0;
  let first = -1;
  for (let b = 0; b + 4 <= ubi.length; b += 0x800) { if (be32(b) === EC_MAGIC) { first = b; break; } }
  if (first < 0) throw new Error('no UBI in image');
  const dataOff = be32(first + 20);
  let peb = 0;
  for (let b = first + 0x800; b + 4 <= ubi.length; b += 0x800) { if (be32(b) === EC_MAGIC) { peb = b - first; break; } }
  if (!peb) peb = 0x20000;
  const vols = {};
  for (let base = first; base + 64 <= ubi.length; base += peb) {
    if (be32(base) !== EC_MAGIC) continue;
    const vidOff = be32(base + 16), dOff = be32(base + 20), vb = base + vidOff;
    if (be32(vb) !== VID_MAGIC) continue;
    const type = ubi[vb + 5], volId = be32(vb + 8), lnum = be32(vb + 12), dataSize = be32(vb + 20);
    const leb = ubi.subarray(base + dOff, base + dOff + (peb - dOff));
    (vols[volId] = vols[volId] || { type, lebs: {} }).lebs[lnum] = { leb, dataSize };
  }
  const out = {};
  for (const [vid, v] of Object.entries(vols)) {
    if (+vid >= 0x7fffef00) continue;
    const lnums = Object.keys(v.lebs).map(Number).sort((a, b) => a - b);
    const parts = lnums.map(ln => { const { leb, dataSize } = v.lebs[ln]; return v.type === 2 ? leb.subarray(0, dataSize) : leb; });
    const n = parts.reduce((s, a) => s + a.length, 0), image = new Uint8Array(n);
    let p = 0; for (const a of parts) { image.set(a, p); p += a.length; }
    out[vid] = { type: v.type, image };
  }
  return { vols: out, peb, dataOff, lebSize: peb - dataOff };
}

async function injectNand(u8, info, ssid, psk) {
  const ubiStart = info.ubiStart;
  const { vols, peb, lebSize, dataOff } = readUbiVolumes(u8.subarray(ubiStart));
  if (!vols[0] || !vols[1] || !vols[2]) throw new Error('unexpected UBI layout (need uboot-env/kernel/rootfs)');
  const page = dataOff / 2;

  // fresh overlay UBIFS with the creds (generous -c; autoresize fills the chip on-device)
  const mk = await (await engine('mkfs_ubifs_memfs'))({ print: () => {}, printErr: () => {} });
  mk.FS.mkdirTree('/in/root/etc');
  mk.FS.writeFile('/in/root/etc/wpa_supplicant.conf', wpaConf(ssid, psk));
  mk.callMain(['-m', String(page), '-e', String(lebSize), '-c', String(OVERLAY_MAX_LEBS), '-r', '/in', '-o', '/ov.ubifs']);
  const overlay = mk.FS.readFile('/ov.ubifs');

  // reassemble UBI: preserved vols 0/1/2 + fresh overlay vol 3
  const ub = await (await engine('ubinize_memfs'))({ print: () => {}, printErr: () => {} });
  ub.FS.writeFile('/v0', vols[0].image); ub.FS.writeFile('/v1', vols[1].image);
  ub.FS.writeFile('/v2', vols[2].image); ub.FS.writeFile('/ov.ubifs', overlay);
  const ovSize = Math.ceil(overlay.length / lebSize) * lebSize;
  ub.FS.writeFile('/c.cfg',
    `[uboot-env]\nmode=ubi\nvol_id=0\nvol_type=dynamic\nvol_name=uboot-env\nvol_size=256KiB\nimage=/v0\n\n` +
    `[kernel]\nmode=ubi\nvol_id=1\nvol_type=static\nvol_name=kernel\nimage=/v1\n\n` +
    `[rootfs]\nmode=ubi\nvol_id=2\nvol_type=static\nvol_name=rootfs\nimage=/v2\n\n` +
    `[overlay]\nmode=ubi\nvol_id=3\nvol_type=dynamic\nvol_name=overlay\nvol_size=${ovSize}\nvol_flags=autoresize\nimage=/ov.ubifs\n`);
  ub.callMain(['-o', '/out.ubi', '-p', '0x' + peb.toString(16), '-m', '0x' + page.toString(16), '/c.cfg']);
  const newUbi = ub.FS.readFile('/out.ubi');

  const out = new Uint8Array(ubiStart + newUbi.length);   // NAND image may grow (overlay LEBs)
  out.set(u8.subarray(0, ubiStart), 0);
  out.set(newUbi, ubiStart);
  return out;
}

// --- public entry -------------------------------------------------------------
export async function injectWifi(u8, { ssid, psk }) {
  if (!ssid) throw new Error('SSID is required');
  const info = overlayInfo(u8);
  if (!info.ok) throw new Error(info.reason);
  return info.type === 'nand' ? injectNand(u8, info, ssid, psk) : injectNor(u8, info, ssid, psk);
}
