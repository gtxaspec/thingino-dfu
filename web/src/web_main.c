/**
 * Web WASM entry point.
 *
 * Provides the global debug flag required by libtdfu and an
 * empty main() so Emscripten has an entry point.  The real work
 * is driven from JavaScript via the exported C API (core.h).
 */

#include <stdbool.h>

#include "tdfu/core.h" /* tdfu_variant_from_string */
#include "tdfu/dfu.h"  /* tdfu_dfu_variant_dir */

/* libtdfu references this; off by default. The web app toggles it at runtime
 * via tdfu_web_set_debug() when the page is loaded with ?debug. */
bool g_debug_enabled = false;

void tdfu_web_set_debug(int on) {
    g_debug_enabled = on ? true : false;
}

const char *tdfu_get_version(void) {
    return VERSION;
}

/* firmware/dfu/<dir> for a detected variant NAME. The JS side fetches the
 * loader into MEMFS at this path, which is exactly where the C bootstrap
 * (tdfu_dfu_variant_dir) looks - so the two can never drift. Returns the C
 * string owned by libtdfu; JS reads it with UTF8ToString. */
const char *tdfu_web_dfu_dir(const char *name) {
    return tdfu_dfu_variant_dir(tdfu_variant_from_string(name));
}

int main(void) {
    /* Nothing to do — JS drives the API */
    return 0;
}
