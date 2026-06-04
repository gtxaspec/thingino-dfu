#ifndef PLATFORM_COMPAT_H
#define PLATFORM_COMPAT_H

#include <stdint.h>

#if defined(__EMSCRIPTEN__)
#include <emscripten.h>
#include <strings.h>
static inline void tdfu_sleep_seconds(uint32_t seconds) {
    emscripten_sleep(seconds * 1000);
}
static inline void tdfu_sleep_milliseconds(uint32_t milliseconds) {
    emscripten_sleep(milliseconds);
}
static inline void tdfu_sleep_microseconds(uint32_t microseconds) {
    emscripten_sleep((microseconds + 999) / 1000);
}
static inline int tdfu_strcasecmp(const char *a, const char *b) {
    return strcasecmp(a, b);
}
#elif defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <string.h>
#ifndef TDFU_USECONDS_T_DEFINED
#define TDFU_USECONDS_T_DEFINED
typedef unsigned int useconds_t;
#endif
static inline void tdfu_sleep_seconds(uint32_t seconds) {
    Sleep(seconds * 1000);
}
static inline void tdfu_sleep_milliseconds(uint32_t milliseconds) {
    Sleep(milliseconds);
}
static inline void tdfu_sleep_microseconds(uint32_t microseconds) {
    DWORD duration = (microseconds + 999) / 1000;
    if (duration == 0 && microseconds > 0) {
        duration = 1;
    }
    Sleep(duration);
}
static inline int usleep(useconds_t microseconds) {
    tdfu_sleep_microseconds((uint32_t)microseconds);
    return 0;
}
static inline int tdfu_strcasecmp(const char *a, const char *b) {
    return _stricmp(a, b);
}
#else
#include <unistd.h>
#include <time.h>
#include <strings.h>
static inline void tdfu_sleep_seconds(uint32_t seconds) {
    sleep(seconds);
}
static inline void tdfu_sleep_milliseconds(uint32_t milliseconds) {
    struct timespec ts = {.tv_sec = milliseconds / 1000, .tv_nsec = (milliseconds % 1000) * 1000000L};
    nanosleep(&ts, NULL);
}
static inline void tdfu_sleep_microseconds(uint32_t microseconds) {
    usleep(microseconds);
}
static inline int tdfu_strcasecmp(const char *a, const char *b) {
    return strcasecmp(a, b);
}
#endif

#endif
