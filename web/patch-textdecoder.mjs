// Post-link patch for the Emscripten-generated glue (tdfu.js).
//
// With `-s ALLOW_MEMORY_GROWTH=1` and no pthreads, Emscripten's UTF8ArrayToString
// decodes strings > 16 bytes with `UTF8Decoder.decode(heapOrArray.subarray(idx,endPtr))`.
// Recent Chrome backs a growable WASM memory with a *resizable* ArrayBuffer, and
// TextDecoder.decode() throws on a view over a resizable buffer:
//   "Failed to execute 'decode' on 'TextDecoder': The provided ArrayBuffer value
//    must not be resizable"
// so any C string > 16 bytes (device info, status, diag) crashes.
//
// Emscripten already emits `.slice()` (a copy into a fresh, non-resizable buffer)
// for the shared-memory case; it just doesn't do it for resizable-but-unshared
// memory. `-s TEXTDECODER=0` (avoid TextDecoder entirely) was removed in
// Emscripten 5+, so we rewrite the decode views here instead. Rewriting to
// `.slice()` costs one small copy per decoded string - negligible for the short
// strings we decode, and correct on any buffer (fixed, resizable, or shared).
//
// Matches `.decode(<id>.subarray(<id>,<id>))` (identifiers stay un-minified today
// but the regex tolerates single-letter names) and turns subarray -> slice.
// Idempotent, and fails the build loudly if neither form is found (so a future
// Emscripten output change is caught rather than silently shipping the bug).

import { readFileSync, writeFileSync } from "node:fs";

const file = process.argv[2];
if (!file) {
  console.error("patch-textdecoder: missing target file argument");
  process.exit(2);
}

const src = readFileSync(file, "utf8");
const viewRe = /\.decode\((\w+)\.subarray\((\w+),\s*(\w+)\)\)/g;
const sliceRe = /\.decode\((\w+)\.slice\((\w+),\s*(\w+)\)\)/g;

const toPatch = (src.match(viewRe) || []).length;
if (toPatch > 0) {
  const out = src.replace(viewRe, ".decode($1.slice($2,$3))");
  writeFileSync(file, out);
  console.log(`patch-textdecoder: rewrote ${toPatch} TextDecoder view(s) to .slice() in ${file}`);
  process.exit(0);
}

if ((src.match(sliceRe) || []).length > 0) {
  console.log(`patch-textdecoder: already patched (${file})`);
  process.exit(0);
}

console.error(
  `patch-textdecoder: no TextDecoder .decode(x.subarray(...)) found in ${file}. ` +
  `Emscripten output may have changed - re-check UTF8ArrayToString and update this patch.`
);
process.exit(1);
