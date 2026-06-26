/**
 * tdfu Constants
 *
 * Timing/size magic numbers for the bootrom USB-boot path, extracted from USB
 * packet captures and empirical testing.
 */

#ifndef TDFU_CONSTANTS_H
#define TDFU_CONSTANTS_H

/* Bulk transfer chunking for the bootrom SPL/U-Boot upload. */
#define CHUNK_SIZE_64KB      (64 * 1024) /* per-chunk bulk-OUT size */
#define INTER_CHUNK_DELAY_MS 50          /* settle between bulk chunks */

/* Settle delay after a bootrom vendor control transfer. */
#define CMD_RESPONSE_DELAY_MS 100

#endif /* TDFU_CONSTANTS_H */
