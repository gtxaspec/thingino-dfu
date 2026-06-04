#include "tdfu/platform_profile.h"

/* Covers T31, T31X, T31ZX, T31A, T31AL, T30, T23, T21 and any other
   xburst1 variant that follows the standard cloner protocol. */
const platform_profile_t platform_t31 = {
    .name = "T31",
    .default_chunk_size = 131072, /* 128KB */
    .descriptor_mode = DESC_MARKER_THEN_SEND,
    .descriptor_subdir = NULL, /* uses t31x default */
    .erase_wait = ERASE_WAIT_POLLING,
    .erase_delay_seconds = 0,
    .crc_format = CRC_FMT_STANDARD,
    .trailer = {0x20, 0xFB, 0x00, 0x08, 0xA2, 0x77, 0x00, 0x00},
    .skip_set_data_addr = false,
    .per_chunk_status_read = false,
    .poll_spl_after_ddr = false,
    .reopen_usb_after_spl = false,
    .use_a1_handshake = false,
    .set_data_len_per_chunk = false,
    .ddr_init_wait_ms = 2000,
};

/* T31ZX needs USB reopen after SPL. Same profile otherwise. */
const platform_profile_t platform_t31zx = {
    .name = "T31ZX",
    .default_chunk_size = 131072,
    .descriptor_mode = DESC_MARKER_THEN_SEND,
    .descriptor_subdir = NULL,
    .erase_wait = ERASE_WAIT_POLLING,
    .erase_delay_seconds = 0,
    .crc_format = CRC_FMT_STANDARD,
    .trailer = {0x20, 0xFB, 0x00, 0x08, 0xA2, 0x77, 0x00, 0x00},
    .skip_set_data_addr = false,
    .per_chunk_status_read = false,
    .poll_spl_after_ddr = false,
    .reopen_usb_after_spl = true,
    .use_a1_handshake = false,
    .set_data_len_per_chunk = false,
    .ddr_init_wait_ms = 2000,
};
