#include "tdfu/platform_profile.h"

/* T31A uses DDR3 (unlike T31/T31X which use DDR2).
 * Vendor pcap shows CRC_FMT_T20 layout (zeros at [20-23], CRC at [28-31]),
 * NOT the CRC_FMT_STANDARD that T31/T31X use. */
const platform_profile_t platform_t31a = {
    .name = "T31A",
    .default_chunk_size = 131072, /* 128KB - matches vendor */
    .descriptor_mode = DESC_MARKER_THEN_SEND,
    .descriptor_subdir = NULL, /* uses t31x default */
    .erase_wait = ERASE_WAIT_POLLING,
    .erase_delay_seconds = 0,
    .crc_format = CRC_FMT_T20,
    .trailer = {0x60, 0x3E, 0x00, 0x54, 0xE3, 0x7F, 0x00, 0x00},
    .skip_set_data_addr = false,
    .per_chunk_status_read = false,
    .poll_spl_after_ddr = false,
    .reopen_usb_after_spl = false,
    .use_a1_handshake = false,
    .set_data_len_per_chunk = false,
    .ddr_init_wait_ms = 2000,
};
