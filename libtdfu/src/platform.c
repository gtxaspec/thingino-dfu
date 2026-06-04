/**
 * Platform Abstraction Layer - Linux implementation
 */

#include "platform.h"
#include "tdfu/platform_compat.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

void platform_sleep_ms(uint32_t ms) {
#ifdef _WIN32
    Sleep(ms);
#else
    tdfu_sleep_milliseconds(ms);
#endif
}
