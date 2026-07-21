
/* emscripten/musl compat: kernel macros ubifs-utils expects from linux headers */
#ifndef __always_inline
#define __always_inline inline __attribute__((always_inline))
#endif
#ifndef noinline
#define noinline __attribute__((noinline))
#endif
#ifndef __const
#define __const const
#endif
#ifndef __maybe_unused
#define __maybe_unused __attribute__((unused))
#endif
#ifndef __force
#define __force
#endif
#ifndef __user
#define __user
#endif
#ifndef likely
#define likely(x)   __builtin_expect(!!(x), 1)
#endif
#ifndef unlikely
#define unlikely(x) __builtin_expect(!!(x), 0)
#endif
