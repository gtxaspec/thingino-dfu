/**
 * Remote client - sends commands to cloner-remote daemon over TCP
 *
 * All firmware data is sent over the wire. The daemon never loads
 * firmware from its own filesystem.
 */

#include "remote.h"
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#endif

#include "cloner/protocol.h"
#include "thingino.h"
#include "ddr_binary_builder.h"
#include "ddr_config_database.h"

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

/* Map variant name to firmware directory name (mirrors loader.c) */
static const char *variant_to_fw_dir(const char *variant) {
    /* Most variants use their name directly as the firmware subdir.
     * Only override where the dir name differs from the variant. */
    if (strcmp(variant, "t31x") == 0 || strcmp(variant, "t31zx") == 0 || strcmp(variant, "t31") == 0)
        return "t31";
    if (strcmp(variant, "a1") == 0 || strcmp(variant, "a1ne") == 0)
        return "a1_n_ne_x";
    if (strcmp(variant, "a1nt") == 0)
        return "a1_nt_a";
    return variant;
}

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
        uint32_t magic = cloner_htonl(CLONER_PROTO_MAGIC);
        memcpy(auth_hdr, &magic, 4);
        auth_hdr[4] = CLONER_PROTO_VERSION;
        auth_hdr[5] = token_len;
        if (net_send_all(remote_fd, auth_hdr, 6) < 0 || net_send_all(remote_fd, token, token_len) < 0) {
            fprintf(stderr, "Failed to send auth token\n");
            CLOSE_SOCKET(remote_fd);
            remote_fd = -1;
            return -1;
        }
        /* Read auth response */
        cloner_resp_header_t resp;
        if (net_recv_all(remote_fd, &resp, sizeof(resp)) < 0 || resp.status != RESP_OK) {
            /* Read error payload if present */
            uint32_t err_len = cloner_ntohl(resp.payload_len);
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
        uint32_t ok_len = cloner_ntohl(resp.payload_len);
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
    cloner_msg_header_t hdr = {
        .magic = cloner_htonl(CLONER_PROTO_MAGIC),
        .version = CLONER_PROTO_VERSION,
        .command = cmd,
        .payload_len = cloner_htonl(len),
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
        cloner_resp_header_t hdr;
        if (net_recv_all(remote_fd, &hdr, sizeof(hdr)) < 0)
            return -1;
        if (cloner_ntohl(hdr.magic) != CLONER_PROTO_MAGIC)
            return -1;

        uint32_t plen = cloner_ntohl(hdr.payload_len);

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

        if (*payload_len > 0 && *payload_len < CLONER_MAX_PAYLOAD) {
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

    int count = payload_len / sizeof(cloner_device_entry_t);
    cloner_device_entry_t *entries = (cloner_device_entry_t *)payload;

    printf("Found %d device(s) (remote):\n", count);
    printf("Index | Bus | Addr | Vendor  | Product | Stage    | Variant\n");
    printf("------|-----|------|---------|---------|----------|--------\n");

    for (int i = 0; i < count; i++) {
        const char *stage = entries[i].stage == 1 ? "firmware" : "bootrom";
        printf("  %3d | %3d | %4d | 0x%04X  | 0x%04X  | %-8s | %d\n", i, entries[i].bus, entries[i].address,
               cloner_ntohs(entries[i].vendor), cloner_ntohs(entries[i].product), stage, entries[i].variant);
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

    int count = (int)(payload_len / sizeof(cloner_device_entry_t));
    if (device_index >= count) {
        fprintf(stderr, "Device index %d out of range (found %d devices)\n", device_index, count);
        free(payload);
        return NULL;
    }

    cloner_device_entry_t *entries = (cloner_device_entry_t *)payload;
    const char *name = processor_variant_to_string((processor_variant_t)entries[device_index].variant);
    free(payload);
    return name;
}

/**
 * Bootstrap a remote device. Reads firmware files locally and sends
 * them over the wire.
 *
 * Payload format:
 *   [1:device_index][1:variant_len][N:variant_str]
 *   [4:ddr_len][ddr_data][4:ddr_crc32]
 *   [4:spl_len][spl_data][4:spl_crc32]
 *   [4:uboot_len][uboot_data][4:uboot_crc32]
 */
int remote_bootstrap(int device_index, const char *cpu_variant, const char *firmware_dir) {
    const char *fw_dir = firmware_dir ? firmware_dir : "./firmwares";
    const char *fw_subdir = variant_to_fw_dir(cpu_variant);

    /* Generate DDR config dynamically (same as local mode) */
    char path[512];
    uint8_t *ddr = NULL, *spl = NULL, *uboot = NULL;
    size_t ddr_len = 0, spl_len = 0, uboot_len = 0;

    processor_variant_t variant_enum = string_to_processor_variant(cpu_variant);
    platform_config_t platform;
    if (ddr_get_platform_config_by_variant(variant_enum, &platform) != 0) {
        fprintf(stderr, "Unknown platform: %s\n", cpu_variant);
        return -1;
    }
    const ddr_chip_config_t *chip = ddr_chip_config_get_default(cpu_variant);
    if (!chip) {
        fprintf(stderr, "No DDR chip config for: %s\n", cpu_variant);
        return -1;
    }
    ddr_phy_params_t phy_params;
    ddr_chip_to_phy_params(chip, platform.ddr_freq, &phy_params);

    ddr = malloc(1024);
    if (!ddr)
        return -1;
    ddr_len = ddr_build_binary(&platform, &phy_params, ddr);
    if (ddr_len == 0) {
        fprintf(stderr, "DDR generation failed for %s\n", cpu_variant);
        free(ddr);
        return -1;
    }

    snprintf(path, sizeof(path), "%s/%s/spl.bin", fw_dir, fw_subdir);
    if (read_file(path, &spl, &spl_len) < 0) {
        fprintf(stderr, "Failed to read SPL: %s\n", path);
        free(ddr);
        return -1;
    }

    snprintf(path, sizeof(path), "%s/%s/uboot.bin", fw_dir, fw_subdir);
    if (read_file(path, &uboot, &uboot_len) < 0) {
        fprintf(stderr, "Failed to read U-Boot: %s\n", path);
        free(ddr);
        free(spl);
        return -1;
    }

    printf("Sending firmware to remote daemon:\n");
    printf("  DDR:   %zu bytes\n", ddr_len);
    printf("  SPL:   %zu bytes\n", spl_len);
    printf("  U-Boot: %zu bytes\n", uboot_len);

    /* Compute CRC32 for each binary */
    uint32_t ddr_crc = remote_crc32(ddr, ddr_len);
    uint32_t spl_crc = remote_crc32(spl, spl_len);
    uint32_t uboot_crc = remote_crc32(uboot, uboot_len);

    /* Build payload */
    size_t variant_len = strlen(cpu_variant);
    size_t payload_len = 2 + variant_len + 4 + ddr_len + 4 + 4 + spl_len + 4 + 4 + uboot_len + 4;

    uint8_t *payload = malloc(payload_len);
    if (!payload) {
        free(ddr);
        free(spl);
        free(uboot);
        return -1;
    }

    uint8_t *p = payload;
    *p++ = (uint8_t)device_index;
    *p++ = (uint8_t)variant_len;
    memcpy(p, cpu_variant, variant_len);
    p += variant_len;

    /* DDR */
    write_be32(p, ddr_len);
    p += 4;
    memcpy(p, ddr, ddr_len);
    p += ddr_len;
    write_be32(p, ddr_crc);
    p += 4;

    /* SPL */
    write_be32(p, spl_len);
    p += 4;
    memcpy(p, spl, spl_len);
    p += spl_len;
    write_be32(p, spl_crc);
    p += 4;

    /* U-Boot */
    write_be32(p, uboot_len);
    p += 4;
    memcpy(p, uboot, uboot_len);
    p += uboot_len;
    write_be32(p, uboot_crc);
    p += 4;

    free(ddr);
    free(spl);
    free(uboot);

    if (send_command(CMD_BOOTSTRAP, payload, payload_len) < 0) {
        free(payload);
        return -1;
    }
    free(payload);

    /* Wait for response (bootstrap takes several seconds) */
    uint8_t resp_status;
    uint8_t *resp = NULL;
    uint32_t resp_len = 0;
    if (recv_response(&resp_status, &resp, &resp_len) < 0) {
        fprintf(stderr, "Lost connection during bootstrap\n");
        return -1;
    }

    if (resp_status != RESP_OK) {
        fprintf(stderr, "Bootstrap failed: %s\n", resp ? (char *)resp : "unknown");
        free(resp);
        return -1;
    }

    printf("Bootstrap completed successfully (remote)\n");
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
int remote_write_firmware(int device_index, const char *cpu_variant, const char *firmware_file) {
    uint8_t *fw_data = NULL;
    size_t fw_len = 0;
    if (read_file(firmware_file, &fw_data, &fw_len) < 0) {
        fprintf(stderr, "Failed to read firmware file: %s\n", firmware_file);
        return -1;
    }

    printf("Sending firmware to remote daemon:\n");
    printf("  File: %s (%zu bytes)\n", firmware_file, fw_len);

    uint32_t fw_crc = remote_crc32(fw_data, fw_len);

    size_t variant_len = strlen(cpu_variant);
    size_t payload_len = 2 + variant_len + 4 + fw_len + 4;
    uint8_t *payload = malloc(payload_len);
    if (!payload) {
        free(fw_data);
        return -1;
    }

    uint8_t *p = payload;
    *p++ = (uint8_t)device_index;
    *p++ = (uint8_t)variant_len;
    memcpy(p, cpu_variant, variant_len);
    p += variant_len;
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
int remote_read_firmware(int device_index, const char *output_file) {
    /* Default: read 8MB from offset 0 (typical SPI NOR) */
    uint32_t offset = 0;
    uint32_t length = 8 * 1024 * 1024;

    printf("Reading firmware from remote device...\n");
    printf("  Offset: 0x%x, Length: %u bytes\n", offset, length);

    uint8_t payload[9];
    payload[0] = (uint8_t)device_index;
    write_be32(payload + 1, offset);
    write_be32(payload + 5, length);

    if (send_command(CMD_READ, payload, sizeof(payload)) < 0)
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
