#include "tdfu/platform_profile.h"

const platform_profile_t platform_a1 = {
    .name = "A1",
    .default_chunk_size = 131072, /* 128KB */
    .descriptor_mode = DESC_MARKER_THEN_SEND,
    .descriptor_subdir = NULL, /* uses a1 descriptor loader */
    .erase_wait = ERASE_WAIT_POLLING,
    .erase_delay_seconds = 0,
    .crc_format = CRC_FMT_A1,
    .trailer = {0x30, 0x24, 0x00, 0xD4, 0x02, 0x75, 0x00, 0x00},
    .skip_set_data_addr = true,
    .per_chunk_status_read = false,
    .poll_spl_after_ddr = false,
    .reopen_usb_after_spl = false,
    .use_a1_handshake = true,
    .set_data_len_per_chunk = true,
    .ddr_init_wait_ms = 2000,
};
