#include "tdfu/platform_profile.h"

/* T41: xburst2, uses vendor write protocol like T40 but with
 * different erase behavior — cannot be polled during erase
 * (like A1). Uses 984-byte descriptor with dynamic generation.
 * Vendor U-Boot reports X2580 magic in firmware stage. */
const platform_profile_t platform_t41 = {
    .name = "T41",
    .default_chunk_size = 131072, /* 128KB */
    .descriptor_mode = DESC_MARKER_THEN_SEND,
    .descriptor_subdir = NULL,
    .erase_wait = ERASE_WAIT_POLLING,
    .erase_delay_seconds = 0,
    .crc_format = CRC_FMT_VENDOR,
    .trailer = {0},
    .skip_set_data_addr = true,
    .per_chunk_status_read = false,
    .poll_spl_after_ddr = false,
    .reopen_usb_after_spl = false,
    .use_a1_handshake = false,
    .set_data_len_per_chunk = false,
    .ddr_init_wait_ms = 2000,
};
