/**
 * Web WASM entry point.
 *
 * Provides the global debug flag required by libcloner and an
 * empty main() so Emscripten has an entry point.  The real work
 * is driven from JavaScript via the exported C API (core.h).
 */

#include <stdbool.h>

/* libcloner references this; default off for web builds */
bool g_debug_enabled = false;

const char *cloner_get_version(void) {
    return VERSION;
}

int main(void) {
    /* Nothing to do — JS drives the API */
    return 0;
}
