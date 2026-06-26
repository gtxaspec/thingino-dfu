/**
 * Remote client - sends commands to dfu-remote daemon over TCP
 *
 * All firmware data is sent over the wire. The daemon never loads
 * firmware from its own filesystem.
 */

#include "remote.h"
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#endif

#include "tdfu/protocol.h"
#include "tdfu/tdfu.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#ifndef _SSIZE_T_DEFINED
typedef int ssize_t;
#endif
#define MSG_NOSIGNAL 0
#define CLOSE_SOCKET closesocket
static int wsa_initialized = 0;
#else
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#define CLOSE_SOCKET close
#endif

/* Simple CRC32 (avoids zlib dependency on Windows) */
static uint32_t remote_crc32(const uint8_t *data, size_t len) {
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++)
            crc = (crc >> 1) ^ (0xEDB88320 & -(crc & 1));
    }
    return ~crc;
}

static int remote_fd = -1;

static int net_send_all(int fd, const void *buf, size_t len) {
    const uint8_t *p = buf;
    while (len > 0) {
        ssize_t n = send(fd, (const char *)p, (int)len, MSG_NOSIGNAL);
        if (n <= 0)
            return -1;
        p += n;
        len -= n;
    }
    return 0;
}

static int net_recv_all(int fd, void *buf, size_t len) {
    uint8_t *p = buf;
    while (len > 0) {
        ssize_t n = recv(fd, (char *)p, (int)len, 0);
        if (n <= 0)
            return -1;
        p += n;
        len -= n;
    }
    return 0;
}

int remote_connect(const char *host, int port, const char *token) {
#ifdef _WIN32
    if (!wsa_initialized) {
        WSADATA wsa;
        if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
            fprintf(stderr, "WSAStartup failed\n");
            return -1;
        }
        wsa_initialized = 1;
    }
#endif
    struct addrinfo hints = {.ai_family = AF_INET, .ai_socktype = SOCK_STREAM};
    struct addrinfo *result;
    char port_str[16];
    snprintf(port_str, sizeof(port_str), "%d", port);

    if (getaddrinfo(host, port_str, &hints, &result) != 0) {
        fprintf(stderr, "Failed to resolve host: %s\n", host);
        return -1;
    }

    remote_fd = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
    if (remote_fd < 0) {
        freeaddrinfo(result);
        return -1;
    }

    if (connect(remote_fd, result->ai_addr, result->ai_addrlen) < 0) {
        fprintf(stderr, "Failed to connect to %s:%d\n", host, port);
        CLOSE_SOCKET(remote_fd);
        remote_fd = -1;
        freeaddrinfo(result);
        return -1;
    }

    freeaddrinfo(result);

    /* Send auth handshake if token provided */
    if (token) {
        uint8_t token_len = (uint8_t)strlen(token);
        uint8_t auth_hdr[6];
        uint32_t magic = tdfu_htonl(TDFU_PROTO_MAGIC);
        memcpy(auth_hdr, &magic, 4);
        auth_hdr[4] = TDFU_PROTO_VERSION;
        auth_hdr[5] = token_len;
        if (net_send_all(remote_fd, auth_hdr, 6) < 0 || net_send_all(remote_fd, token, token_len) < 0) {
            fprintf(stderr, "Failed to send auth token\n");
            CLOSE_SOCKET(remote_fd);
            remote_fd = -1;
            return -1;
        }
        /* Read auth response */
        tdfu_resp_header_t resp;
        if (net_recv_all(remote_fd, &resp, sizeof(resp)) < 0 || resp.status != RESP_OK) {
            /* Read error payload if present */
            uint32_t err_len = tdfu_ntohl(resp.payload_len);
            if (err_len > 0 && err_len < 256) {
                char err[256] = {0};
                net_recv_all(remote_fd, err, err_len);
                fprintf(stderr, "Auth failed: %s\n", err);
            } else {
                fprintf(stderr, "Auth failed\n");
            }
            CLOSE_SOCKET(remote_fd);
            remote_fd = -1;
            return -1;
        }
        /* Drain auth OK payload */
        uint32_t ok_len = tdfu_ntohl(resp.payload_len);
        if (ok_len > 0 && ok_len < 256) {
            char buf[256];
            net_recv_all(remote_fd, buf, ok_len);
        }
    }

    return 0;
}

void remote_disconnect(void) {
    if (remote_fd >= 0) {
        CLOSE_SOCKET(remote_fd);
        remote_fd = -1;
    }
}

static int send_command(uint8_t cmd, const void *payload, uint32_t len) {
    tdfu_msg_header_t hdr = {
        .magic = tdfu_htonl(TDFU_PROTO_MAGIC),
        .version = TDFU_PROTO_VERSION,
        .command = cmd,
        .payload_len = tdfu_htonl(len),
    };
    if (net_send_all(remote_fd, &hdr, sizeof(hdr)) < 0)
        return -1;
    if (len > 0 && payload) {
        if (net_send_all(remote_fd, payload, len) < 0)
            return -1;
    }
    return 0;
}

/**
 * Receive response, handling RESP_PROGRESS messages inline.
 * Progress messages are printed to stderr and the function
 * keeps reading until a final RESP_OK or RESP_ERROR arrives.
 */
static int recv_response(uint8_t *status, uint8_t **payload, uint32_t *payload_len) {
    for (;;) {
        tdfu_resp_header_t hdr;
        if (net_recv_all(remote_fd, &hdr, sizeof(hdr)) < 0)
            return -1;
        if (tdfu_ntohl(hdr.magic) != TDFU_PROTO_MAGIC)
            return -1;

        uint32_t plen = tdfu_ntohl(hdr.payload_len);

        if (hdr.status == RESP_PROGRESS || hdr.status == RESP_LOG) {
            /* Intermediate message — display and keep reading */
            if (plen > 0 && plen < 65536) {
                uint8_t *data = malloc(plen + 1);
                if (data && net_recv_all(remote_fd, data, plen) == 0) {
                    if (hdr.status == RESP_LOG) {
                        /* Raw log output — print directly like local mode */
                        data[plen] = '\0';
                        fprintf(stderr, "%s", (char *)data);
                    } else if (plen >= 4) {
                        /* Legacy progress: [1:percent][1:stage][2:msg_len][msg] */
                        uint8_t percent = data[0];
                        uint16_t msg_len = ((uint16_t)data[2] << 8) | data[3];
                        if (4u + msg_len <= plen) {
                            data[4 + msg_len] = '\0';
                            fprintf(stderr, "\r[%3d%%] %s", percent, (char *)(data + 4));
                        }
                    }
                }
                free(data);
            } else if (plen > 0) {
                /* Drain oversized payload to keep stream in sync */
                uint8_t drain[1024];
                uint32_t remaining = plen;
                while (remaining > 0) {
                    uint32_t chunk = remaining < sizeof(drain) ? remaining : sizeof(drain);
                    if (net_recv_all(remote_fd, drain, chunk) < 0)
                        return -1;
                    remaining -= chunk;
                }
            }
            continue; /* keep reading for final response */
        }

        /* Final response (OK or ERROR) */
        *status = hdr.status;
        *payload_len = plen;

        if (*payload_len > 0 && *payload_len < TDFU_MAX_PAYLOAD) {
            *payload = malloc(*payload_len + 1);
            if (!*payload)
                return -1;
            if (net_recv_all(remote_fd, *payload, *payload_len) < 0) {
                free(*payload);
                *payload = NULL;
                return -1;
            }
            (*payload)[*payload_len] = '\0';
        } else {
            *payload = NULL;
        }

        return 0;
    } /* for(;;) */
}

/* Helper: read a file into a malloc'd buffer */
static int read_file(const char *path, uint8_t **data, size_t *len) {
    FILE *f = fopen(path, "rb");
    if (!f)
        return -1;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    if (sz < 0) {
        fclose(f);
        return -1;
    }
    fseek(f, 0, SEEK_SET);
    *data = malloc(sz);
    if (!*data) {
        fclose(f);
        return -1;
    }
    if (fread(*data, 1, sz, f) != (size_t)sz) {
        free(*data);
        *data = NULL;
        fclose(f);
        return -1;
    }
    fclose(f);
    *len = sz;
    return 0;
}

/* Helper: write 4 bytes big-endian */
static void write_be32(uint8_t *p, uint32_t v) {
    p[0] = (v >> 24) & 0xFF;
    p[1] = (v >> 16) & 0xFF;
    p[2] = (v >> 8) & 0xFF;
    p[3] = v & 0xFF;
}

int remote_list_devices(void) {
    if (send_command(CMD_DISCOVER, NULL, 0) < 0)
        return -1;

    uint8_t status;
    uint8_t *payload = NULL;
    uint32_t payload_len = 0;
    if (recv_response(&status, &payload, &payload_len) < 0)
        return -1;

    if (status != RESP_OK) {
        fprintf(stderr, "Error: %s\n", payload ? (char *)payload : "unknown");
        free(payload);
        return -1;
    }

    int count = payload_len / sizeof(tdfu_device_entry_t);
    tdfu_device_entry_t *entries = (tdfu_device_entry_t *)payload;

    printf("Found %d device(s) (remote):\n", count);
    printf("Index | Bus | Addr | Vendor  | Product | Stage    | Variant\n");
    printf("------|-----|------|---------|---------|----------|--------\n");

    for (int i = 0; i < count; i++) {
        /* wire stage: 0=bootrom, 1=firmware, 2=DFU gadget */
        const char *stage = entries[i].stage == 1 ? "firmware" : entries[i].stage == 2 ? "dfu" : "bootrom";
        printf("  %3d | %3d | %4d | 0x%04X  | 0x%04X  | %-8s | %d\n", i, entries[i].bus, entries[i].address,
               tdfu_ntohs(entries[i].vendor), tdfu_ntohs(entries[i].product), stage, entries[i].variant);
    }

    free(payload);
    return 0;
}

const char *remote_detect_variant(int device_index) {
    if (send_command(CMD_DISCOVER, NULL, 0) < 0)
        return NULL;

    uint8_t status;
    uint8_t *payload = NULL;
    uint32_t payload_len = 0;
    if (recv_response(&status, &payload, &payload_len) < 0)
        return NULL;

    if (status != RESP_OK) {
        free(payload);
        return NULL;
    }

    int count = (int)(payload_len / sizeof(tdfu_device_entry_t));
    if (device_index >= count) {
        fprintf(stderr, "Device index %d out of range (found %d devices)\n", device_index, count);
        free(payload);
        return NULL;
    }

    tdfu_device_entry_t *entries = (tdfu_device_entry_t *)payload;
    const char *name = tdfu_variant_to_string((tdfu_variant_t)entries[device_index].variant);
    free(payload);
    return name;
}

/* Return the USB stage of a remote device by index: 0 = bootrom,
 * 1 = firmware/DFU gadget, or -1 on error / out of range. Lets a bare
 * -w decide whether it must bootstrap first. */
int remote_device_stage(int device_index) {
    if (send_command(CMD_DISCOVER, NULL, 0) < 0)
        return -1;

    uint8_t status;
    uint8_t *payload = NULL;
    uint32_t payload_len = 0;
    if (recv_response(&status, &payload, &payload_len) < 0)
        return -1;

    if (status != RESP_OK) {
        free(payload);
        return -1;
    }

    int count = (int)(payload_len / sizeof(tdfu_device_entry_t));
    if (device_index < 0 || device_index >= count) {
        free(payload);
        return -1;
    }

    tdfu_device_entry_t *entries = (tdfu_device_entry_t *)payload;
    int stage = entries[device_index].stage;
    free(payload);
    return stage;
}

/**
 * Bootstrap a remote device over the wire.
 *
 * The daemon USB-boots firmware/dfu/<soc>/ itself, so by default the client
 * sends only the device index + variant. If the user passed both --spl and
 * --uboot, those blobs are streamed and the daemon uses them instead (skipping
 * SoC detection), matching local --spl/--uboot.
 *
 * Payload: [1:device_index][1:variant_len][N:variant_str]
 *          optionally [4:spl_len][spl][4:uboot_len][uboot]
 */
int remote_bootstrap(int device_index, const char *cpu_variant, const char *firmware_dir, const char *spl_file,
                     const char *uboot_file) {
    (void)firmware_dir;
    size_t vlen = cpu_variant ? strlen(cpu_variant) : 0;
    if (vlen > 63)
        vlen = 63;

    uint8_t *spl = NULL, *uboot = NULL;
    size_t spl_len = 0, uboot_len = 0;
    bool override = spl_file && spl_file[0] && uboot_file && uboot_file[0];
    if (override) {
        if (read_file(spl_file, &spl, &spl_len) < 0) {
            fprintf(stderr, "Failed to read --spl file: %s\n", spl_file);
            return -1;
        }
        if (read_file(uboot_file, &uboot, &uboot_len) < 0) {
            fprintf(stderr, "Failed to read --uboot file: %s\n", uboot_file);
            free(spl);
            return -1;
        }
    }

    size_t plen = 2 + vlen + (override ? 4 + spl_len + 4 + uboot_len : 0);
    uint8_t *payload = malloc(plen);
    if (!payload) {
        free(spl);
        free(uboot);
        return -1;
    }
    uint8_t *q = payload;
    *q++ = (uint8_t)device_index;
    *q++ = (uint8_t)vlen;
    if (vlen) {
        memcpy(q, cpu_variant, vlen);
        q += vlen;
    }
    if (override) {
        write_be32(q, (uint32_t)spl_len);
        q += 4;
        memcpy(q, spl, spl_len);
        q += spl_len;
        write_be32(q, (uint32_t)uboot_len);
        q += 4;
        memcpy(q, uboot, uboot_len);
        q += uboot_len;
        printf("Sending custom SPL (%zu B) + U-Boot (%zu B) to daemon\n", spl_len, uboot_len);
    }
    free(spl);
    free(uboot);

    int sc = send_command(CMD_BOOTSTRAP, payload, (uint32_t)plen);
    free(payload);
    if (sc < 0)
        return -1;
    uint8_t st = 0;
    uint8_t *resp = NULL;
    uint32_t rl = 0;
    if (recv_response(&st, &resp, &rl) < 0) {
        fprintf(stderr, "Lost connection during bootstrap\n");
        return -1;
    }
    if (st != RESP_OK) {
        fprintf(stderr, "Bootstrap failed: %s\n", resp ? (char *)resp : "unknown");
        free(resp);
        return -1;
    }
    printf("Bootstrap completed successfully (remote DFU)\n");
    free(resp);
    return 0;
}

/**
 * Write firmware to remote device.
 *
 * Payload format:
 *   [1:device_idx][1:variant_len][N:variant]
 *   [4:fw_len][fw_data][4:crc32]
 */
int remote_write_firmware(int device_index, const char *cpu_variant, const char *firmware_file, const char *alt) {
    uint8_t *fw_data = NULL;
    size_t fw_len = 0;
    if (read_file(firmware_file, &fw_data, &fw_len) < 0) {
        fprintf(stderr, "Failed to read firmware file: %s\n", firmware_file);
        return -1;
    }

    printf("Sending firmware to remote daemon:\n");
    printf("  File: %s (%zu bytes)\n", firmware_file, fw_len);

    uint32_t fw_crc = remote_crc32(fw_data, fw_len);

    /* variant is optional (DFU write passes none - the daemon resolves the alt) */
    const char *variant = cpu_variant ? cpu_variant : "";
    size_t variant_len = strlen(variant);
    /* alt selector: empty = daemon default (alt 0 = flash); name/num targets it */
    const char *alt_s = alt ? alt : "";
    size_t alt_len = strlen(alt_s);
    size_t payload_len = 2 + variant_len + 1 + alt_len + 4 + fw_len + 4;
    uint8_t *payload = malloc(payload_len);
    if (!payload) {
        free(fw_data);
        return -1;
    }

    uint8_t *p = payload;
    *p++ = (uint8_t)device_index;
    *p++ = (uint8_t)variant_len;
    memcpy(p, variant, variant_len);
    p += variant_len;
    *p++ = (uint8_t)alt_len;
    memcpy(p, alt_s, alt_len);
    p += alt_len;
    write_be32(p, fw_len);
    p += 4;
    memcpy(p, fw_data, fw_len);
    p += fw_len;
    write_be32(p, fw_crc);
    p += 4;

    free(fw_data);

    if (send_command(CMD_WRITE, payload, payload_len) < 0) {
        free(payload);
        return -1;
    }
    free(payload);

    uint8_t resp_status;
    uint8_t *resp = NULL;
    uint32_t resp_len = 0;
    if (recv_response(&resp_status, &resp, &resp_len) < 0) {
        fprintf(stderr, "Lost connection during write\n");
        return -1;
    }

    if (resp_status != RESP_OK) {
        fprintf(stderr, "Write failed: %s\n", resp ? (char *)resp : "unknown");
        free(resp);
        return -1;
    }

    printf("Firmware write completed successfully (remote)\n");
    free(resp);
    return 0;
}

/**
 * Read firmware from remote device.
 *
 * Sends CMD_READ with [1:device_idx][4:offset][4:length]
 * Response contains [fw_data][4:crc32]
 */
int remote_read_firmware(int device_index, const char *output_file, const char *alt) {
    /* alt selector: empty = daemon default (alt 0 = flash); name/num targets it.
     * The daemon uploads the whole selected alt, so no offset/length here. */
    const char *alt_s = alt ? alt : "";
    size_t alt_len = strlen(alt_s);

    printf("Reading firmware from remote device%s%s...\n",
           alt_s[0] ? " alt " : "", alt_s);

    /* Payload: [idx][variant_len=0][alt_len][alt] */
    uint8_t payload[3 + 64];
    int n = 0;
    payload[n++] = (uint8_t)device_index;
    payload[n++] = 0;
    payload[n++] = (uint8_t)alt_len;
    memcpy(payload + n, alt_s, alt_len);
    n += (int)alt_len;

    if (send_command(CMD_READ, payload, (uint32_t)n) < 0)
        return -1;

    uint8_t resp_status;
    uint8_t *resp = NULL;
    uint32_t resp_len = 0;
    if (recv_response(&resp_status, &resp, &resp_len) < 0) {
        fprintf(stderr, "Lost connection during read\n");
        return -1;
    }

    if (resp_status != RESP_OK) {
        fprintf(stderr, "Read failed: %s\n", resp ? (char *)resp : "unknown");
        free(resp);
        return -1;
    }

    if (resp_len < 4) {
        fprintf(stderr, "Read response too short\n");
        free(resp);
        return -1;
    }

    /* Last 4 bytes are CRC32 */
    uint32_t data_len = resp_len - 4;
    uint32_t expected_crc = ((uint32_t)resp[data_len] << 24) | ((uint32_t)resp[data_len + 1] << 16) |
                            ((uint32_t)resp[data_len + 2] << 8) | resp[data_len + 3];
    uint32_t actual_crc = remote_crc32(resp, data_len);

    if (actual_crc != expected_crc) {
        fprintf(stderr, "Read data CRC32 mismatch\n");
        free(resp);
        return -1;
    }

    /* Save to file */
    FILE *f = fopen(output_file, "wb");
    if (!f) {
        fprintf(stderr, "Failed to open output file: %s\n", output_file);
        free(resp);
        return -1;
    }
    fwrite(resp, 1, data_len, f);
    fclose(f);

    printf("Read complete: %u bytes saved to %s (CRC OK)\n", data_len, output_file);
    free(resp);
    return 0;
}
