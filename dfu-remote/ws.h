/**
 * Minimal WebSocket server layer for dfu-remote.
 *
 * Lets a browser (the web flasher served over HTTPS) talk to the daemon over a
 * plain ws:// connection - browsers can't open raw TCP, and Chrome's Local
 * Network Access permission exempts ws:// to a local/LAN host from mixed-content
 * blocking, so no TLS is needed. This only does the transport (RFC 6455
 * handshake + frame codec); the daemon's existing CLNR command protocol rides on
 * top unchanged, treating the WebSocket as a byte stream.
 */

#ifndef DFU_REMOTE_WS_H
#define DFU_REMOTE_WS_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    int fd;
    /* Streaming de-frame state: bytes left in the current inbound data frame,
     * its masking key, and how far through that key we are (a frame can span
     * several ws_recv() calls and vice-versa). */
    uint64_t frame_remaining;
    uint8_t mask[4];
    uint32_t mask_pos;
} ws_conn_t;

/* Complete the HTTP->WebSocket upgrade on fd (the caller has already peeked the
 * leading "GET"). Returns 0 on success, -1 on failure. */
int ws_handshake(int fd);

/* Answer an OPTIONS CORS / Local-Network-Access preflight with permissive
 * headers, then return (caller closes the socket). */
void ws_preflight(int fd);

/* Send len bytes as a single binary WebSocket frame (server->client, unmasked).
 * Returns 0 / -1. */
int ws_send(ws_conn_t *c, const void *buf, size_t len);

/* Read exactly len bytes from the WebSocket byte stream, de-framing and
 * unmasking and answering pings transparently. Returns 0 / -1. */
int ws_recv(ws_conn_t *c, void *buf, size_t len);

#endif /* DFU_REMOTE_WS_H */
