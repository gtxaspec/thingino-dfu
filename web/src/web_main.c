/**
 * Web WASM entry point.
 *
 * Provides the global debug flag required by libtdfu and an
 * empty main() so Emscripten has an entry point.  The real work
 * is driven from JavaScript via the exported C API (core.h).
 */

#include <stdbool.h>

/* libtdfu references this; off by default. The web app toggles it at runtime
 * via tdfu_web_set_debug() when the page is loaded with ?debug. */
bool g_debug_enabled = false;

void tdfu_web_set_debug(int on) {
    g_debug_enabled = on ? true : false;
}

const char *tdfu_get_version(void) {
    return VERSION;
}

int main(void) {
    /* Nothing to do — JS drives the API */
    return 0;
}
