/**
 * cloner-remote - daemon that provides USB cloner access over TCP
 *
 * Started manually on the machine with physical USB access.
 * Listens on TCP port, accepts one client at a time, dispatches
 * commands to libcloner.
 */

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#endif

#include "thingino.h"
#include "cloner/protocol.h"
#include "platform.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>

#ifdef _WIN32
#ifndef _SSIZE_T_DEFINED
typedef int ssize_t;
#endif
typedef int socklen_t;
#define MSG_NOSIGNAL 0
#define CLOSE_SOCKET closesocket
#define SHUT_RDWR    SD_BOTH
#else
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
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

#ifndef _WIN32
#include <zlib.h>
#endif

static volatile int g_running = 1;
static volatile int g_cancel = 0;
bool g_debug_enabled = false;

/* Daemon state */
static const char *g_state = "idle";
static const char *g_auth_token = NULL; /* NULL = no auth required */

static int g_server_fd = -1;
static int g_log_client_fd = -1; /* client fd for log forwarding */

static void signal_handler(int sig) {
    (void)sig;
    g_running = 0;
    /* Close server socket to unblock accept() */
    if (g_server_fd >= 0) {
        shutdown(g_server_fd, SHUT_RDWR);
        CLOSE_SOCKET(g_server_fd);
        g_server_fd = -1;
    }
}

/* ------------------------------------------------------------------ */
/* Network helpers                                                     */
/* ------------------------------------------------------------------ */

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

static int send_response(int fd, uint8_t status, const void *payload, uint32_t len) {
    cloner_resp_header_t hdr = {
        .magic = cloner_htonl(CLONER_PROTO_MAGIC),
        .version = CLONER_PROTO_VERSION,
        .status = status,
        .payload_len = cloner_htonl(len),
    };
    if (net_send_all(fd, &hdr, sizeof(hdr)) < 0)
        return -1;
    if (len > 0 && payload) {
        if (net_send_all(fd, payload, len) < 0)
            return -1;
    }
    return 0;
}

static int send_error(int fd, const char *msg) {
    return send_response(fd, RESP_ERROR, msg, strlen(msg));
}

static int send_ok(int fd, const void *payload, uint32_t len) {
    return send_response(fd, RESP_OK, payload, len);
}

/* Forward library log output to the connected client as RESP_LOG */
static void daemon_log_hook(const char *msg, size_t len) {
    /* Always print locally on daemon stderr */
    fwrite(msg, 1, len, stderr);
    /* Forward to client if connected */
    if (g_log_client_fd >= 0) {
        send_response(g_log_client_fd, RESP_LOG, msg, (uint32_t)len);
    }
}

/* ------------------------------------------------------------------ */
/* Command handlers                                                    */
/* ------------------------------------------------------------------ */

static int handle_discover(int client_fd) {
    usb_manager_t manager = {0};
    if (usb_manager_init(&manager) != THINGINO_SUCCESS) {
        return send_error(client_fd, "USB init failed");
    }

    device_info_t *devices = NULL;
    int count = 0;
    usb_manager_find_devices(&manager, &devices, &count);

    /* Build response: array of device entries */
    size_t resp_len = count * sizeof(cloner_device_entry_t);
    cloner_device_entry_t *entries = calloc(count, sizeof(cloner_device_entry_t));
    if (!entries && count > 0) {
        free(devices);
        usb_manager_cleanup(&manager);
        return send_error(client_fd, "out of memory");
    }

    for (int i = 0; i < count; i++) {
        entries[i].bus = devices[i].bus;
        entries[i].address = devices[i].address;
        entries[i].vendor = cloner_htons(devices[i].vendor);
        entries[i].product = cloner_htons(devices[i].product);
        entries[i].stage = devices[i].stage;
        entries[i].variant = devices[i].variant;

        /* For bootrom devices, run SoC auto-detect to get true variant */
        if (devices[i].stage == 0) {
            usb_device_t *dev = NULL;
            if (usb_manager_open_device(&manager, &devices[i], &dev) == THINGINO_SUCCESS) {
                processor_variant_t detected = VARIANT_T31X;
                if (protocol_detect_soc(dev, &detected) == THINGINO_SUCCESS) {
                    entries[i].variant = (uint8_t)detected;
                    DEBUG_PRINT("Discover: device %d auto-detected as %s\n", i, processor_variant_to_string(detected));
                }
                usb_device_close(dev);
            }
        }
    }

    int rc = send_ok(client_fd, entries, resp_len);

    free(entries);
    free(devices);
    usb_manager_cleanup(&manager);
    return rc;
}

/* Read big-endian u32 from buffer */
static uint32_t read_be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

/**
 * Handle CMD_BOOTSTRAP - uses local firmware_dir for DDR/SPL/U-Boot.
 *
 * Payload: [1:device_idx][1:variant_len][N:variant]
 */
static int handle_bootstrap(int client_fd, const uint8_t *payload, uint32_t len, const char *firmware_dir) {
    if (len < 2)
        return send_error(client_fd, "payload too short");

    const uint8_t *p = payload;
    const uint8_t *end = payload + len;

    uint8_t device_index = *p++;
    uint8_t variant_len = *p++;
    if (p + variant_len > end)
        return send_error(client_fd, "bad variant length");

    char variant_str[64] = {0};
    if (variant_len >= sizeof(variant_str))
        variant_len = sizeof(variant_str) - 1;
    memcpy(variant_str, p, variant_len);

    g_state = "bootstrapping";
    g_cancel = 0;
    printf("Bootstrap request: device=%d, cpu=%s, firmware_dir=%s\n", device_index, variant_str, firmware_dir);

    usb_manager_t manager = {0};
    if (usb_manager_init(&manager) != THINGINO_SUCCESS)
        return send_error(client_fd, "USB init failed");

    g_log_client_fd = client_fd;
    thingino_error_t result = cloner_op_bootstrap(&manager, device_index, variant_str, g_debug_enabled, false, NULL,
                                                  NULL, NULL, firmware_dir);
    g_log_client_fd = -1;

    usb_manager_cleanup(&manager);

    g_state = "idle";
    if (result != THINGINO_SUCCESS) {
        char msg[128];
        snprintf(msg, sizeof(msg), "bootstrap failed: %s", thingino_error_to_string(result));
        return send_error(client_fd, msg);
    }

    return send_ok(client_fd, "OK", 2);
}

/**
 * Handle CMD_WRITE - write firmware to device flash.
 *
 * Payload:
 *   [1:device_idx][1:variant_len][N:variant]
 *   [4:fw_len][fw_data][4:crc32]
 */
static int handle_write(int client_fd, const uint8_t *payload, uint32_t len, const char *firmware_dir) {
    if (len < 2)
        return send_error(client_fd, "payload too short");

    const uint8_t *p = payload;
    const uint8_t *end = payload + len;

    uint8_t device_index = *p++;
    uint8_t variant_len = *p++;
    if (p + variant_len > end)
        return send_error(client_fd, "bad variant length");

    char variant_str[64] = {0};
    if (variant_len >= sizeof(variant_str))
        variant_len = sizeof(variant_str) - 1;
    memcpy(variant_str, p, variant_len);
    p += variant_len;

    /* Parse firmware binary + CRC32 */
    if (p + 4 > end)
        return send_error(client_fd, "missing firmware length");
    uint32_t fw_len = read_be32(p);
    p += 4;
    if (p + fw_len + 4 > end)
        return send_error(client_fd, "firmware data truncated");
    const uint8_t *fw_data = p;
    p += fw_len;
    uint32_t fw_crc_expected = read_be32(p);

    /* Verify CRC32 */
    uint32_t fw_crc = remote_crc32(fw_data, fw_len);
    if (fw_crc != fw_crc_expected)
        return send_error(client_fd, "firmware CRC32 mismatch");

    g_state = "writing";
    g_cancel = 0;
    printf("Write request: device=%d, cpu=%s, firmware=%u bytes (CRC OK)\n", device_index, variant_str, fw_len);

    /* Write firmware to temp file */
    char tmpfile[256];
#ifdef _WIN32
    snprintf(tmpfile, sizeof(tmpfile), "%s\\cloner-fw-tmp.bin", getenv("TEMP") ? getenv("TEMP") : ".");
#else
    snprintf(tmpfile, sizeof(tmpfile), "/tmp/cloner-fw-XXXXXX");
    int tmpfd = mkstemp(tmpfile);
    if (tmpfd < 0)
        return send_error(client_fd, "failed to create temp file");
    close(tmpfd);
#endif
    {
        FILE *tmpf = fopen(tmpfile, "wb");
        if (!tmpf)
            return send_error(client_fd, "failed to create temp file");
        if (fwrite(fw_data, 1, fw_len, tmpf) != fw_len) {
            fclose(tmpf);
            remove(tmpfile);
            return send_error(client_fd, "failed to write temp file");
        }
        fclose(tmpf);
    }

    usb_manager_t manager = {0};
    if (usb_manager_init(&manager) != THINGINO_SUCCESS) {
        remove(tmpfile);
        return send_error(client_fd, "USB init failed");
    }

    g_log_client_fd = client_fd;
    thingino_error_t result =
        cloner_op_write_firmware(&manager, device_index, tmpfile, variant_str, NULL, false, false, false,
                                 g_debug_enabled, false, NULL, NULL, NULL, firmware_dir, 0);
    g_log_client_fd = -1;

    remove(tmpfile);
    usb_manager_cleanup(&manager);

    g_state = "idle";
    if (result != THINGINO_SUCCESS) {
        char msg[128];
        snprintf(msg, sizeof(msg), "write failed: %s", thingino_error_to_string(result));
        return send_error(client_fd, msg);
    }

    return send_ok(client_fd, "OK", 2);
}

/**
 * Handle CMD_READ - read firmware from device flash.
 *
 * Payload: [1:device_idx][1:variant_len][N:variant]
 */
static int handle_read(int client_fd, const uint8_t *payload, uint32_t len) {
    if (len < 2)
        return send_error(client_fd, "payload too short");

    const uint8_t *p = payload;
    const uint8_t *end = payload + len;

    uint8_t device_index = *p++;
    uint8_t variant_len = *p++;
    if (p + variant_len > end)
        return send_error(client_fd, "bad variant length");

    char variant_str[64] = {0};
    if (variant_len >= sizeof(variant_str))
        variant_len = sizeof(variant_str) - 1;
    memcpy(variant_str, p, variant_len);

    const char *force_cpu = variant_len > 0 ? variant_str : NULL;
    printf("Read request: device=%d, cpu=%s\n", device_index, force_cpu ? force_cpu : "(auto)");

    /* Read into a temp file, then send contents back */
    char tmpfile[256];
#ifdef _WIN32
    snprintf(tmpfile, sizeof(tmpfile), "%s\\cloner-read-tmp.bin", getenv("TEMP") ? getenv("TEMP") : ".");
#else
    {
        snprintf(tmpfile, sizeof(tmpfile), "/tmp/cloner-read-XXXXXX");
        int tmpfd = mkstemp(tmpfile);
        if (tmpfd < 0)
            return send_error(client_fd, "failed to create temp file");
        close(tmpfd);
    }
#endif

    usb_manager_t manager = {0};
    if (usb_manager_init(&manager) != THINGINO_SUCCESS) {
        remove(tmpfile);
        return send_error(client_fd, "USB init failed");
    }

    g_state = "reading";
    g_cancel = 0;
    g_log_client_fd = client_fd;
    thingino_error_t result = cloner_op_read_firmware(&manager, device_index, tmpfile, force_cpu, NULL);
    g_log_client_fd = -1;
    usb_manager_cleanup(&manager);

    if (result != THINGINO_SUCCESS) {
        remove(tmpfile);
        g_state = "idle";
        char msg[128];
        snprintf(msg, sizeof(msg), "read failed: %s", thingino_error_to_string(result));
        return send_error(client_fd, msg);
    }

    /* Read the temp file into memory */
    FILE *f = fopen(tmpfile, "rb");
    if (!f) {
        remove(tmpfile);
        g_state = "idle";
        return send_error(client_fd, "failed to open read output");
    }
    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (file_size <= 0) {
        fclose(f);
        remove(tmpfile);
        g_state = "idle";
        return send_error(client_fd, "read returned empty data");
    }

    uint8_t *read_data = malloc(file_size);
    if (!read_data) {
        fclose(f);
        remove(tmpfile);
        g_state = "idle";
        return send_error(client_fd, "out of memory");
    }
    if (fread(read_data, 1, file_size, f) != (size_t)file_size) {
        free(read_data);
        fclose(f);
        remove(tmpfile);
        g_state = "idle";
        return send_error(client_fd, "failed to read firmware data");
    }
    fclose(f);
    remove(tmpfile);

    printf("Read complete: %ld bytes\n", file_size);

    /* Send data + CRC32 back */
    uint32_t data_crc = remote_crc32(read_data, file_size);
    size_t resp_len = file_size + 4;
    uint8_t *resp = malloc(resp_len);
    if (!resp) {
        free(read_data);
        g_state = "idle";
        return send_error(client_fd, "out of memory");
    }
    memcpy(resp, read_data, file_size);
    resp[file_size] = (data_crc >> 24) & 0xFF;
    resp[file_size + 1] = (data_crc >> 16) & 0xFF;
    resp[file_size + 2] = (data_crc >> 8) & 0xFF;
    resp[file_size + 3] = data_crc & 0xFF;

    int rc = send_ok(client_fd, resp, resp_len);
    free(resp);
    free(read_data);
    g_state = "idle";
    return rc;
}

static int handle_status(int client_fd) {
    return send_ok(client_fd, g_state, strlen(g_state));
}

static int handle_cancel(int client_fd) {
    g_cancel = 1;
    return send_ok(client_fd, "OK", 2);
}

/* ------------------------------------------------------------------ */
/* Client connection handler                                           */
/* ------------------------------------------------------------------ */

static void handle_client(int client_fd, const char *firmware_dir) {
    printf("Client connected\n");
    g_cloner_log_hook = daemon_log_hook;

    /* Authentication handshake if token is configured */
    if (g_auth_token) {
        /* Expect: [4:magic][1:version][1:token_len][N:token] */
        uint8_t auth_hdr[6];
        if (net_recv_all(client_fd, auth_hdr, 6) < 0) {
            printf("Auth: failed to read handshake\n");
            return;
        }
        uint32_t raw_magic;
        memcpy(&raw_magic, auth_hdr, 4);
        uint32_t magic = cloner_ntohl(raw_magic);
        if (magic != CLONER_PROTO_MAGIC || auth_hdr[4] != CLONER_PROTO_VERSION) {
            send_error(client_fd, "auth: bad handshake");
            return;
        }
        uint8_t token_len = auth_hdr[5];
        char token[256] = {0};
        if (token_len > 0) {
            if (net_recv_all(client_fd, token, token_len) < 0) {
                printf("Auth: failed to read token\n");
                return;
            }
        }
        /* Constant-time comparison to avoid timing side channel */
        size_t token_slen = strlen(token);
        size_t expected_slen = strlen(g_auth_token);
        volatile uint8_t diff = 0;
        for (size_t ci = 0; ci < expected_slen; ci++)
            diff |= (uint8_t)(ci < token_slen ? token[ci] : 0xFF) ^ (uint8_t)g_auth_token[ci];
        if (diff != 0 || token_slen != expected_slen) {
            send_error(client_fd, "auth: invalid token");
            printf("Auth: rejected (wrong token)\n");
            return;
        }
        /* Send OK to confirm auth */
        send_ok(client_fd, "OK", 2);
        printf("Auth: accepted\n");
    }

    while (g_running) {
        cloner_msg_header_t hdr;
        if (net_recv_all(client_fd, &hdr, sizeof(hdr)) < 0)
            break;

        if (cloner_ntohl(hdr.magic) != CLONER_PROTO_MAGIC) {
            send_error(client_fd, "bad magic");
            break;
        }
        if (hdr.version != CLONER_PROTO_VERSION) {
            send_error(client_fd, "version mismatch");
            break;
        }

        uint32_t payload_len = cloner_ntohl(hdr.payload_len);
        if (payload_len > CLONER_MAX_PAYLOAD) {
            send_error(client_fd, "payload too large");
            break;
        }

        uint8_t *payload = NULL;
        if (payload_len > 0) {
            payload = malloc(payload_len);
            if (!payload) {
                send_error(client_fd, "out of memory");
                break;
            }
            if (net_recv_all(client_fd, payload, payload_len) < 0) {
                free(payload);
                break;
            }
        }

        int rc = 0;
        switch (hdr.command) {
        case CMD_DISCOVER:
            rc = handle_discover(client_fd);
            break;
        case CMD_BOOTSTRAP:
            rc = handle_bootstrap(client_fd, payload, payload_len, firmware_dir);
            break;
        case CMD_WRITE:
            rc = handle_write(client_fd, payload, payload_len, firmware_dir);
            break;
        case CMD_READ:
            rc = handle_read(client_fd, payload, payload_len);
            break;
        case CMD_STATUS:
            rc = handle_status(client_fd);
            break;
        case CMD_CANCEL:
            rc = handle_cancel(client_fd);
            break;
        default:
            rc = send_error(client_fd, "unknown command");
            break;
        }

        free(payload);
        if (rc < 0)
            break;
    }

    g_cloner_log_hook = NULL;
    g_log_client_fd = -1;
    printf("Client disconnected\n");
}

/* ------------------------------------------------------------------ */
/* Main                                                                */
/* ------------------------------------------------------------------ */

static void print_usage(const char *name) {
    printf("cloner-remote - USB cloner daemon\n");
    printf("Usage: %s [options]\n\n", name);
    printf("Options:\n");
    printf("  -p, --port <port>         Listen port (default: %d)\n", CLONER_DEFAULT_PORT);
    printf("  --firmware-dir <dir>      Firmware directory (default: ./firmwares)\n");
    printf("  --token <secret>          Require auth token from clients\n");
    printf("  -d, --debug               Enable debug output\n");
    printf("  -h, --help                Show this help\n");
}

/* Resolve firmware directory relative to this binary's location */
static const char *resolve_firmware_dir(const char *argv0) {
    static char buf[4096];
#ifdef _WIN32
    DWORD len = GetModuleFileNameA(NULL, buf, sizeof(buf));
    if (len > 0 && len < sizeof(buf)) {
        char *sep = strrchr(buf, '\\');
        if (sep) {
            snprintf(sep + 1, sizeof(buf) - (sep + 1 - buf), "firmwares");
            return buf;
        }
    }
#else
    ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (len > 0) {
        buf[len] = '\0';
        char *sep = strrchr(buf, '/');
        if (sep) {
            snprintf(sep + 1, sizeof(buf) - (sep + 1 - buf), "firmwares");
            return buf;
        }
    }
#endif
    (void)argv0;
    return "./firmwares";
}

int main(int argc, char **argv) {
    int port = CLONER_DEFAULT_PORT;
    const char *firmware_dir = resolve_firmware_dir(argv[0]);

    for (int i = 1; i < argc; i++) {
        if ((strcmp(argv[i], "-p") == 0 || strcmp(argv[i], "--port") == 0) && i + 1 < argc) {
            port = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--firmware-dir") == 0 && i + 1 < argc) {
            firmware_dir = argv[++i];
        } else if (strcmp(argv[i], "--token") == 0 && i + 1 < argc) {
            g_auth_token = argv[++i];
        } else if (strcmp(argv[i], "-d") == 0 || strcmp(argv[i], "--debug") == 0) {
            g_debug_enabled = true;
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        }
    }

#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        fprintf(stderr, "WSAStartup failed\n");
        return 1;
    }
#endif

    signal(SIGINT, signal_handler);
#ifndef _WIN32
    signal(SIGTERM, signal_handler);
    signal(SIGPIPE, SIG_IGN);
#endif

    /* Create listening socket */
    int server_fd;
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("socket");
        return 1;
    }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, (const char *)&opt, sizeof(opt));

    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port = htons(port),
        .sin_addr.s_addr = INADDR_ANY,
    };

    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        CLOSE_SOCKET(server_fd);
        return 1;
    }

    if (listen(server_fd, 1) < 0) {
        perror("listen");
        CLOSE_SOCKET(server_fd);
        return 1;
    }

    g_server_fd = server_fd;

    printf("cloner-remote listening on port %d\n", port);
    printf("Firmware directory: %s\n", firmware_dir);

    while (g_running) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_len);
        if (client_fd < 0) {
            break;
        }

        printf("Connection from %s:%d\n", inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));

        handle_client(client_fd, firmware_dir);
        CLOSE_SOCKET(client_fd);
    }

    if (g_server_fd >= 0)
        CLOSE_SOCKET(g_server_fd);
    printf("\ncloner-remote stopped\n");
    return 0;
}
