#include "tdfu/platform_profile.h"

const platform_profile_t platform_t30 = {
    .name = "T30",
    .default_chunk_size = 131072, /* 128KB */
    .descriptor_mode = DESC_RAW_BULK_THEN_SEND,
    .descriptor_subdir = "t30",
    .erase_wait = ERASE_WAIT_FIXED,
    .erase_delay_seconds = 50,
    .crc_format = CRC_FMT_T20,
    .trailer = {0x00, 0x38, 0x00, 0xC8, 0x4D, 0x7F, 0x00, 0x00},
    .skip_set_data_addr = true,
    .per_chunk_status_read = false,
    .poll_spl_after_ddr = true,
    .reopen_usb_after_spl = false,
    .use_a1_handshake = false,
    .set_data_len_per_chunk = false,
    .ddr_init_wait_ms = 1100,
};
