/**
 * Web WASM entry point.
 *
 * Provides the global debug flag required by libtdfu and an
 * empty main() so Emscripten has an entry point.  The real work
 * is driven from JavaScript via the exported C API (core.h).
 */

#include <stdbool.h>

/* libtdfu references this; enabled so [DEBUG] lines reach the in-page log */
bool g_debug_enabled = true;

const char *tdfu_get_version(void) {
    return VERSION;
}

int main(void) {
    /* Nothing to do — JS drives the API */
    return 0;
}
