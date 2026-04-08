/**
 * Cloner Network Protocol
 *
 * Simple binary request/response protocol for client <-> daemon communication.
 * Single client at a time, no multiplexing.
 */

#ifndef CLONER_PROTOCOL_H
#define CLONER_PROTOCOL_H

#include <stdint.h>
#include <stddef.h>

#define CLONER_PROTO_MAGIC   0x434C4E52 /* "CLNR" */
#define CLONER_PROTO_VERSION 1
#define CLONER_DEFAULT_PORT  5050

/* CLI exit codes */
#define EXIT_SUCCESS_CODE   0
#define EXIT_DEVICE_ERROR   1
#define EXIT_TRANSFER_ERROR 2
#define EXIT_FILE_ERROR     3
#define EXIT_PROTOCOL_ERROR 4
#define CLONER_MAX_PAYLOAD  (64 * 1024 * 1024) /* 64MB max */

/* Commands (client -> daemon) */
enum {
    CMD_DISCOVER = 0x01,  /* list USB devices */
    CMD_BOOTSTRAP = 0x02, /* bootstrap device (sends DDR+SPL+U-Boot) */
    CMD_WRITE = 0x03,     /* write firmware to device */
    CMD_READ = 0x04,      /* read firmware from device */
    CMD_STATUS = 0x05,    /* query device state */
    CMD_CANCEL = 0x06,    /* abort current operation */
};

/* Response status */
enum {
    RESP_OK = 0x00,
    RESP_ERROR = 0x01,
    RESP_PROGRESS = 0x02, /* async progress update during long ops */
    RESP_LOG = 0x03,      /* raw log output from library (same as local stderr) */
};

/* Wire format - all fields big-endian */
typedef struct __attribute__((packed)) {
    uint32_t magic;       /* CLONER_PROTO_MAGIC */
    uint8_t version;      /* CLONER_PROTO_VERSION */
    uint8_t command;      /* CMD_* */
    uint32_t payload_len; /* length of payload following this header */
} cloner_msg_header_t;

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint8_t version;
    uint8_t status; /* RESP_* */
    uint32_t payload_len;
} cloner_resp_header_t;

/* Bootstrap request payload */
typedef struct __attribute__((packed)) {
    uint8_t device_index;
    uint8_t variant_len; /* length of variant string */
    /* followed by: variant string, then firmware_dir string (null-terminated) */
} cloner_bootstrap_req_t;

/* Discover response - per device */
typedef struct __attribute__((packed)) {
    uint8_t bus;
    uint8_t address;
    uint16_t vendor;
    uint16_t product;
    uint8_t stage;   /* 0=bootrom, 1=firmware */
    uint8_t variant; /* processor_variant_t enum value */
} cloner_device_entry_t;

/* Progress update payload */
typedef struct __attribute__((packed)) {
    uint8_t percent;  /* 0-100 */
    uint8_t stage;    /* what's happening */
    uint16_t msg_len; /* length of message string following */
} cloner_progress_t;

/* Helpers for big-endian encoding (protocol is network byte order) */
static inline uint32_t cloner_htonl(uint32_t h) {
    uint8_t b[4];
    b[0] = (h >> 24) & 0xFF;
    b[1] = (h >> 16) & 0xFF;
    b[2] = (h >> 8) & 0xFF;
    b[3] = h & 0xFF;
    uint32_t n;
    __builtin_memcpy(&n, b, 4);
    return n;
}

static inline uint32_t cloner_ntohl(uint32_t n) {
    uint8_t b[4];
    __builtin_memcpy(b, &n, 4);
    return ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) | ((uint32_t)b[2] << 8) | b[3];
}

static inline uint16_t cloner_htons(uint16_t h) {
    return (uint16_t)((h >> 8) | (h << 8));
}

static inline uint16_t cloner_ntohs(uint16_t n) {
    return cloner_htons(n);
}

#endif /* CLONER_PROTOCOL_H */
