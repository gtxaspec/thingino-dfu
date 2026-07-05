/**
 * Web WASM entry point.
 *
 * Provides the global debug flag required by libtdfu and an
 * empty main() so Emscripten has an entry point.  The real work
 * is driven from JavaScript via the exported C API (core.h).
 */

#include <stdbool.h>
#include <stddef.h>

#include <emscripten.h>

#include "tdfu/core.h" /* tdfu_variant_from_string */
#include "tdfu/dfu.h"  /* tdfu_dfu_variant_dir */
#include "tdfu/tdfu.h" /* g_tdfu_log_hook */

/* Route libtdfu log output straight to JS, live. Emscripten buffers stderr
 * until a '\n', which would stall the '\r'-only progress lines that write/read/
 * verify emit (they overwrite one terminal line) until the whole operation
 * ends - so the progress bar never moved on the local backend. Bypass stderr
 * with a log hook, exactly like the dfu-remote daemon, and hand each message to
 * the app immediately. */
static void web_log_hook(const char *msg, size_t len) {
    EM_ASM(
        {
            var s = UTF8ToString($0, $1);
            /* err() (-> printErr, same router) is the fallback so a message can
             * never be dropped if onLibLog is somehow not installed yet. */
            if (Module.onLibLog)
                Module.onLibLog(s);
            else if (typeof err === "function")
                err(s);
        },
        msg, len);
}

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
    /* Route library logging to JS live (see web_log_hook). Set here at module
     * load; it only fires during operations, by which point JS has installed
     * Module.onLibLog. */
    g_tdfu_log_hook = web_log_hook;
    return 0;
}
