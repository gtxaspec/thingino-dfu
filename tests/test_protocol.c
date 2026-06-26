/**
 * Network Protocol Tests
 *
 * Tests protocol header serialization, byte order, and message format
 * without requiring a real TCP connection.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "tdfu/protocol.h"

static int passed = 0;
static int failed = 0;

#define TEST(cond, msg) do { \
    if (cond) { printf("  [PASS] %s\n", msg); passed++; } \
    else { printf("  [FAIL] %s\n", msg); failed++; } \
} while(0)

static void test_byte_order(void) {
    printf("\nByte order tests:\n");

    /* Test htonl/ntohl round-trip */
    uint32_t val = 0x12345678;
    uint32_t net = tdfu_htonl(val);
    uint32_t host = tdfu_ntohl(net);
    TEST(host == val, "htonl/ntohl round-trip for 0x12345678");

    /* Test htons/ntohs round-trip */
    uint16_t val16 = 0xABCD;
    uint16_t net16 = tdfu_htons(val16);
    uint16_t host16 = tdfu_ntohs(net16);
    TEST(host16 == val16, "htons/ntohs round-trip for 0xABCD");

    /* Test magic constant encoding */
    uint32_t magic_net = tdfu_htonl(TDFU_PROTO_MAGIC);
    uint8_t *bytes = (uint8_t *)&magic_net;
    TEST(bytes[0] == 'T' && bytes[1] == 'D' && bytes[2] == 'F' && bytes[3] == 'U',
        "TDFU_PROTO_MAGIC encodes to 'TDFU' in network order");

    /* Test zero and max values */
    TEST(tdfu_htonl(0) == 0, "htonl(0) == 0");
    TEST(tdfu_ntohl(0) == 0, "ntohl(0) == 0");
    TEST(tdfu_ntohl(tdfu_htonl(0xFFFFFFFF)) == 0xFFFFFFFF,
        "round-trip for 0xFFFFFFFF");
}

static void test_message_header(void) {
    printf("\nMessage header tests:\n");

    /* Test request header structure */
    TEST(sizeof(tdfu_msg_header_t) == 10, "request header is 10 bytes");
    TEST(sizeof(tdfu_resp_header_t) == 10, "response header is 10 bytes");

    /* Build a request header and verify layout */
    tdfu_msg_header_t req = {
        .magic = tdfu_htonl(TDFU_PROTO_MAGIC),
        .version = TDFU_PROTO_VERSION,
        .command = CMD_DISCOVER,
        .payload_len = tdfu_htonl(0),
    };

    uint8_t *raw = (uint8_t *)&req;
    TEST(raw[0] == 'T' && raw[1] == 'D' && raw[2] == 'F' && raw[3] == 'U',
        "request magic at offset 0");
    TEST(raw[4] == TDFU_PROTO_VERSION, "version at offset 4");
    TEST(raw[5] == CMD_DISCOVER, "command at offset 5");
    TEST(raw[6] == 0 && raw[7] == 0 && raw[8] == 0 && raw[9] == 0,
        "payload_len=0 at offset 6-9");
}

static void test_device_entry(void) {
    printf("\nDevice entry tests:\n");

    TEST(sizeof(tdfu_device_entry_t) == 8, "device entry is 8 bytes");

    tdfu_device_entry_t entry = {
        .bus = 3,
        .address = 22,
        .vendor = tdfu_htons(0xA108),
        .product = tdfu_htons(0xC309),
        .stage = 0,
        .variant = 6,
    };

    TEST(entry.bus == 3, "bus field");
    TEST(entry.address == 22, "address field");
    TEST(tdfu_ntohs(entry.vendor) == 0xA108, "vendor after ntohs");
    TEST(tdfu_ntohs(entry.product) == 0xC309, "product after ntohs");
    TEST(entry.stage == 0, "stage=bootrom");
    TEST(entry.variant == 6, "variant=t31zx");
}

static void test_command_values(void) {
    printf("\nCommand value tests:\n");

    TEST(CMD_DISCOVER == 0x01, "CMD_DISCOVER = 0x01");
    TEST(CMD_BOOTSTRAP == 0x02, "CMD_BOOTSTRAP = 0x02");
    TEST(CMD_WRITE == 0x03, "CMD_WRITE = 0x03");
    TEST(CMD_READ == 0x04, "CMD_READ = 0x04");
    TEST(CMD_STATUS == 0x05, "CMD_STATUS = 0x05");
    TEST(CMD_CANCEL == 0x06, "CMD_CANCEL = 0x06");

    TEST(RESP_OK == 0x00, "RESP_OK = 0x00");
    TEST(RESP_ERROR == 0x01, "RESP_ERROR = 0x01");
    TEST(RESP_PROGRESS == 0x02, "RESP_PROGRESS = 0x02");
}

static void test_constants(void) {
    printf("\nProtocol constants:\n");

    TEST(TDFU_PROTO_MAGIC == 0x54444655, "magic = 0x54444655 (TDFU)");
    TEST(TDFU_PROTO_VERSION == 1, "version = 1");
    TEST(TDFU_DEFAULT_PORT == 5050, "default port = 5050");
    TEST(TDFU_MAX_PAYLOAD == 64 * 1024 * 1024, "max payload = 64MB");

    TEST(EXIT_SUCCESS_CODE == 0, "EXIT_SUCCESS_CODE = 0");
    TEST(EXIT_DEVICE_ERROR == 1, "EXIT_DEVICE_ERROR = 1");
    TEST(EXIT_TRANSFER_ERROR == 2, "EXIT_TRANSFER_ERROR = 2");
    TEST(EXIT_FILE_ERROR == 3, "EXIT_FILE_ERROR = 3");
    TEST(EXIT_PROTOCOL_ERROR == 4, "EXIT_PROTOCOL_ERROR = 4");
}

int main(void) {
    printf("=== Network Protocol Tests ===\n");

    test_byte_order();
    test_message_header();
    test_device_entry();
    test_command_values();
    test_constants();

    printf("\n=== Results: %d passed, %d failed ===\n", passed, failed);
    return failed > 0 ? 1 : 0;
}
