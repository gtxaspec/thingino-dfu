#ifndef PLATFORM_PROFILE_H
#define PLATFORM_PROFILE_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* tdfu_variant_t is defined in tdfu.h */
#include "tdfu/tdfu.h"

/**
 * Erase wait strategy.
 */
typedef enum {
    ERASE_WAIT_POLLING, /* VR_FW_READ_STATUS2 polling */
    ERASE_WAIT_FIXED,   /* Fixed delay (seconds) */
} erase_wait_mode_t;

/**
 * CRC format in the VR_WRITE handshake.
 *
 *  STANDARD: [8-11]=offset [20-23]=CRC32 [24-27]=OPS [28-31]=~CRC (T31)
 *  T20:      [8-11]=offset [20-23]=0     [24-27]=OPS [28-31]=~CRC (T20/T21)
 *  A1:       completely different handshake layout                  (A1)
 *  VENDOR:   uint64_t fields matching vendor host tool (T40/T41)
 *            [0-7]=partition [8-15]=offset(u64) [16-23]=size(u64)
 *            [24-27]=OPS(SPI,RAW)=0x60000 [28-31]=CRC32(raw)
 */
typedef enum {
    CRC_FMT_STANDARD,
    CRC_FMT_T20,
    CRC_FMT_A1,
    CRC_FMT_VENDOR,
} crc_format_t;

/**
 * How the flash descriptor + partition marker are sent before VR_INIT.
 *
 *  RAW_BULK_THEN_SEND: raw bulk marker (ignored), flash_descriptor_send()  (T20)
 *  MARKER_THEN_SEND:   flash_partition_marker_send(), flash_descriptor_send() (T31+)
 */
typedef enum {
    DESC_RAW_BULK_THEN_SEND,
    DESC_MARKER_THEN_SEND,
} descriptor_mode_t;

/**
 * Per-platform profile for the flash write protocol.
 *
 * One of these exists for each platform family.  Retrieved via
 * platform_get_profile(variant).
 */
typedef struct {
    const char *name; /* human-readable, e.g. "T20" */

    /* ---- chunk sizes ---- */
    uint32_t default_chunk_size; /* 64K, 128K, 1M ... */

    /* ---- flash descriptor ---- */
    descriptor_mode_t descriptor_mode;
    const char *descriptor_subdir; /* e.g. "t20", NULL = use t31x default */

    /* ---- erase ---- */
    erase_wait_mode_t erase_wait;
    int erase_delay_seconds; /* only for ERASE_WAIT_FIXED */

    /* ---- VR_WRITE handshake ---- */
    crc_format_t crc_format;
    uint8_t trailer[8];

    /* ---- protocol quirks ---- */
    bool skip_set_data_addr;     /* T20: yes */
    bool per_chunk_status_read;  /* T41: sends VR_FW_READ after each chunk */
    bool poll_spl_after_ddr;     /* T20, T41: poll GET_CPU_INFO after SPL */
    bool reopen_usb_after_spl;   /* T31ZX: USB re-enumerates after SPL */
    bool use_a1_handshake;       /* A1: different handshake layout + VR_FW_READ cmd */
    bool set_data_len_per_chunk; /* T41: SET_DATA_LEN = chunk size, not firmware size */

    /* ---- timing ---- */
    uint32_t ddr_init_wait_ms; /* post-ProgStage1 wait */
} platform_profile_t;

/**
 * Look up the platform profile for a given processor variant.
 * Returns a pointer to a static const profile, never NULL.
 * Unknown variants get the T31 (default) profile.
 */
const platform_profile_t *platform_get_profile(tdfu_variant_t variant);

#endif /* PLATFORM_PROFILE_H */
