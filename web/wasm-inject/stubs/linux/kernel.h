#ifndef _STUB_LINUX_KERNEL_H
#define _STUB_LINUX_KERNEL_H
#include <stddef.h>
#ifndef container_of
#define container_of(ptr, type, member) ({ \
	const typeof(((type *)0)->member) *__mptr = (ptr); \
	(type *)((char *)__mptr - offsetof(type, member)); })
#endif
#ifndef __always_inline
#define __always_inline inline __attribute__((always_inline))
#endif
#ifndef noinline
#define noinline __attribute__((noinline))
#endif
#ifndef __maybe_unused
#define __maybe_unused __attribute__((unused))
#endif
#ifndef likely
#define likely(x)   __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)
#endif
#ifndef ARRAY_SIZE
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#endif
#endif
