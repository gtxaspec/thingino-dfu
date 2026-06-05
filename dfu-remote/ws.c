/**
 * WebSocket transport for dfu-remote (see ws.h).
 *
 * Self-contained: a tiny SHA-1 + base64 for the handshake accept key, and an
 * RFC 6455 frame codec. No external crypto/TLS - this is plain ws://.
 */

#include "ws.h"

#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#include <winsock2.h>
#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif
#define strncasecmp _strnicmp
#else
#include <sys/socket.h>
#include <strings.h>
#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif
#endif

/* ------------------------------------------------------------------ */
/* Raw socket I/O (ws.c must NOT use the daemon's net_*_all, which would
 * recurse back into the WebSocket layer).                              */
/* ------------------------------------------------------------------ */

static int raw_send_all(int fd, const void *buf, size_t len) {
    const uint8_t *p = (const uint8_t *)buf;
    while (len > 0) {
        int n = (int)send(fd, (const char *)p, (int)len, MSG_NOSIGNAL);
        if (n <= 0)
            return -1;
        p += n;
        len -= (size_t)n;
    }
    return 0;
}

static int raw_recv_all(int fd, void *buf, size_t len) {
    uint8_t *p = (uint8_t *)buf;
    while (len > 0) {
        int n = (int)recv(fd, (char *)p, (int)len, 0);
        if (n <= 0)
            return -1;
        p += n;
        len -= (size_t)n;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* SHA-1 (just enough for Sec-WebSocket-Accept)                         */
/* ------------------------------------------------------------------ */

typedef struct {
    uint32_t h[5];
    uint64_t len;
    uint8_t buf[64];
    size_t n;
} sha1_t;

static uint32_t sha1_rol(uint32_t v, int b) { return (v << b) | (v >> (32 - b)); }

static void sha1_block(sha1_t *s, const uint8_t *p) {
    uint32_t w[80];
    for (int i = 0; i < 16; i++)
        w[i] = ((uint32_t)p[i * 4] << 24) | ((uint32_t)p[i * 4 + 1] << 16) | ((uint32_t)p[i * 4 + 2] << 8) |
               (uint32_t)p[i * 4 + 3];
    for (int i = 16; i < 80; i++)
        w[i] = sha1_rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    uint32_t a = s->h[0], b = s->h[1], c = s->h[2], d = s->h[3], e = s->h[4];
    for (int i = 0; i < 80; i++) {
        uint32_t f, k;
        if (i < 20) {
            f = (b & c) | ((~b) & d);
            k = 0x5A827999;
        } else if (i < 40) {
            f = b ^ c ^ d;
            k = 0x6ED9EBA1;
        } else if (i < 60) {
            f = (b & c) | (b & d) | (c & d);
            k = 0x8F1BBCDC;
        } else {
            f = b ^ c ^ d;
            k = 0xCA62C1D6;
        }
        uint32_t t = sha1_rol(a, 5) + f + e + k + w[i];
        e = d;
        d = c;
        c = sha1_rol(b, 30);
        b = a;
        a = t;
    }
    s->h[0] += a;
    s->h[1] += b;
    s->h[2] += c;
    s->h[3] += d;
    s->h[4] += e;
}

static void sha1_init(sha1_t *s) {
    s->h[0] = 0x67452301;
    s->h[1] = 0xEFCDAB89;
    s->h[2] = 0x98BADCFE;
    s->h[3] = 0x10325476;
    s->h[4] = 0xC3D2E1F0;
    s->len = 0;
    s->n = 0;
}

static void sha1_update(sha1_t *s, const uint8_t *p, size_t len) {
    s->len += len;
    while (len) {
        size_t k = 64 - s->n;
        if (k > len)
            k = len;
        memcpy(s->buf + s->n, p, k);
        s->n += k;
        p += k;
        len -= k;
        if (s->n == 64) {
            sha1_block(s, s->buf);
            s->n = 0;
        }
    }
}

static void sha1_final(sha1_t *s, uint8_t out[20]) {
    uint64_t bits = s->len * 8;
    uint8_t pad = 0x80;
    sha1_update(s, &pad, 1);
    uint8_t z = 0;
    while (s->n != 56)
        sha1_update(s, &z, 1);
    uint8_t lb[8];
    for (int i = 0; i < 8; i++)
        lb[i] = (uint8_t)(bits >> (56 - i * 8));
    sha1_update(s, lb, 8);
    for (int i = 0; i < 5; i++) {
        out[i * 4] = (uint8_t)(s->h[i] >> 24);
        out[i * 4 + 1] = (uint8_t)(s->h[i] >> 16);
        out[i * 4 + 2] = (uint8_t)(s->h[i] >> 8);
        out[i * 4 + 3] = (uint8_t)s->h[i];
    }
}

static void base64(const uint8_t *in, size_t n, char *out) {
    static const char *t = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t o = 0;
    for (size_t i = 0; i < n; i += 3) {
        uint32_t v = (uint32_t)in[i] << 16;
        if (i + 1 < n)
            v |= (uint32_t)in[i + 1] << 8;
        if (i + 2 < n)
            v |= in[i + 2];
        out[o++] = t[(v >> 18) & 63];
        out[o++] = t[(v >> 12) & 63];
        out[o++] = (i + 1 < n) ? t[(v >> 6) & 63] : '=';
        out[o++] = (i + 2 < n) ? t[v & 63] : '=';
    }
    out[o] = '\0';
}

/* ------------------------------------------------------------------ */
/* Handshake                                                            */
/* ------------------------------------------------------------------ */

#define WS_GUID "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"

int ws_handshake(int fd) {
    /* Read request bytes one at a time until the blank line ends the headers.
     * (Requests are small; simplicity over speed.) */
    char req[8192];
    size_t n = 0;
    while (n < sizeof(req) - 1) {
        char ch;
        if (raw_recv_all(fd, &ch, 1) < 0)
            return -1;
        req[n++] = ch;
        if (n >= 4 && memcmp(req + n - 4, "\r\n\r\n", 4) == 0)
            break;
    }
    req[n] = '\0';

    /* Find Sec-WebSocket-Key (header names are case-insensitive). */
    const char *key = NULL;
    for (char *p = req; *p; p++) {
        if (strncasecmp(p, "Sec-WebSocket-Key:", 18) == 0) {
            key = p + 18;
            break;
        }
    }
    if (!key)
        return -1;
    while (*key == ' ')
        key++;
    char keyval[128];
    size_t kl = 0;
    while (*key && *key != '\r' && *key != '\n' && kl < sizeof(keyval) - 1)
        keyval[kl++] = *key++;
    keyval[kl] = '\0';

    char concat[180];
    snprintf(concat, sizeof(concat), "%s%s", keyval, WS_GUID);
    sha1_t s;
    sha1_init(&s);
    sha1_update(&s, (const uint8_t *)concat, strlen(concat));
    uint8_t dig[20];
    sha1_final(&s, dig);
    char accept[40];
    base64(dig, 20, accept);

    char resp[512];
    int rl = snprintf(resp, sizeof(resp),
                      "HTTP/1.1 101 Switching Protocols\r\n"
                      "Upgrade: websocket\r\n"
                      "Connection: Upgrade\r\n"
                      "Sec-WebSocket-Accept: %s\r\n"
                      /* Allow Chrome's Local Network Access (private-network) check. */
                      "Access-Control-Allow-Private-Network: true\r\n"
                      "\r\n",
                      accept);
    return raw_send_all(fd, resp, (size_t)rl);
}

void ws_preflight(int fd) {
    const char *resp = "HTTP/1.1 200 OK\r\n"
                       "Access-Control-Allow-Origin: *\r\n"
                       "Access-Control-Allow-Methods: GET, OPTIONS\r\n"
                       "Access-Control-Allow-Headers: *\r\n"
                       "Access-Control-Allow-Private-Network: true\r\n"
                       "Content-Length: 0\r\n"
                       "\r\n";
    raw_send_all(fd, resp, strlen(resp));
}

/* ------------------------------------------------------------------ */
/* Frame codec                                                          */
/* ------------------------------------------------------------------ */

int ws_send(ws_conn_t *c, const void *buf, size_t len) {
    uint8_t hdr[10];
    size_t hl;
    hdr[0] = 0x82; /* FIN + binary opcode */
    if (len < 126) {
        hdr[1] = (uint8_t)len;
        hl = 2;
    } else if (len < 65536) {
        hdr[1] = 126;
        hdr[2] = (uint8_t)(len >> 8);
        hdr[3] = (uint8_t)len;
        hl = 4;
    } else {
        hdr[1] = 127;
        for (int i = 0; i < 8; i++)
            hdr[2 + i] = (uint8_t)((uint64_t)len >> (56 - i * 8));
        hl = 10;
    }
    if (raw_send_all(c->fd, hdr, hl) < 0)
        return -1;
    if (len > 0 && raw_send_all(c->fd, buf, len) < 0)
        return -1;
    return 0;
}

/* Read the next inbound frame header into c, transparently answering ping and
 * dropping pong; returns 0 with c->frame_remaining set to a data frame's length,
 * or -1 on close/error. */
static int ws_next_frame(ws_conn_t *c) {
    for (;;) {
        uint8_t h[2];
        if (raw_recv_all(c->fd, h, 2) < 0)
            return -1;
        int opcode = h[0] & 0x0F;
        int masked = h[1] & 0x80;
        uint64_t len = h[1] & 0x7F;
        if (len == 126) {
            uint8_t e[2];
            if (raw_recv_all(c->fd, e, 2) < 0)
                return -1;
            len = ((uint64_t)e[0] << 8) | e[1];
        } else if (len == 127) {
            uint8_t e[8];
            if (raw_recv_all(c->fd, e, 8) < 0)
                return -1;
            len = 0;
            for (int i = 0; i < 8; i++)
                len = (len << 8) | e[i];
        }
        if (masked) {
            if (raw_recv_all(c->fd, c->mask, 4) < 0)
                return -1;
        } else {
            memset(c->mask, 0, 4);
        }
        c->mask_pos = 0;

        if (opcode == 0x8) /* close */
            return -1;
        if (opcode == 0x9) { /* ping -> pong (control payload <= 125) */
            uint8_t pl[125];
            uint64_t pn = len > 125 ? 125 : len;
            if (pn && raw_recv_all(c->fd, pl, (size_t)pn) < 0)
                return -1;
            for (uint64_t i = 0; i < pn; i++)
                pl[i] ^= c->mask[i & 3];
            uint8_t pong[127];
            pong[0] = 0x8A; /* FIN + pong */
            pong[1] = (uint8_t)pn;
            memcpy(pong + 2, pl, (size_t)pn);
            if (raw_send_all(c->fd, pong, (size_t)(2 + pn)) < 0)
                return -1;
            continue;
        }
        if (opcode == 0xA) { /* pong: discard */
            uint8_t tmp[128];
            uint64_t rem = len;
            while (rem) {
                uint64_t k = rem > sizeof(tmp) ? sizeof(tmp) : rem;
                if (raw_recv_all(c->fd, tmp, (size_t)k) < 0)
                    return -1;
                rem -= k;
            }
            continue;
        }
        /* data frame (binary / text / continuation) */
        c->frame_remaining = len;
        return 0;
    }
}

int ws_recv(ws_conn_t *c, void *buf, size_t len) {
    uint8_t *out = (uint8_t *)buf;
    while (len > 0) {
        if (c->frame_remaining == 0) {
            if (ws_next_frame(c) < 0)
                return -1;
            if (c->frame_remaining == 0)
                continue; /* zero-length data frame */
        }
        size_t chunk = len < c->frame_remaining ? len : (size_t)c->frame_remaining;
        if (raw_recv_all(c->fd, out, chunk) < 0)
            return -1;
        for (size_t i = 0; i < chunk; i++)
            out[i] ^= c->mask[(c->mask_pos++) & 3];
        out += chunk;
        len -= chunk;
        c->frame_remaining -= chunk;
    }
    return 0;
}
