#include "tdfu/platform_profile.h"

/* T40: xburst2 with vendor-format VR_WRITE handshake.
 * Uses 984-byte descriptor (12-byte RDD 0xC9 prefix + 972-byte GBD).
 * Write protocol verified byte-for-byte against t40_vendor_write.pcap:
 *   uint64_t partition=0, uint64_t offset, uint64_t size,
 *   uint32_t ops=OPS(SPI,RAW)=0x60000, uint32_t crc(raw). */
const platform_profile_t platform_t40 = {
    .name = "T40",
    .default_chunk_size = 131072, /* 128KB (vendor capture verified) */
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
