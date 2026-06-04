/**
 * Platform Abstraction Layer
 *
 * Centralizes OS-specific calls behind a common interface.
 * Linux is the primary target. Windows/macOS can be added later
 * by implementing the same functions.
 */

#ifndef PLATFORM_H
#define PLATFORM_H

#include <stdint.h>

/**
 * Sleep for the specified number of milliseconds.
 */
void platform_sleep_ms(uint32_t ms);

#endif /* PLATFORM_H */
