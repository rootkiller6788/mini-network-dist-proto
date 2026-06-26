#include "websocket.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static const char ws_base64_table[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

void ws_sha1_hash(const uint8_t *input, size_t len, uint8_t output[20])
{
    uint32_t h[5] = {0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0};
    uint64_t total_bits = (uint64_t)len * 8;

    uint8_t padded[128];
    size_t pad_len = len;
    memcpy(padded, input, len);
    padded[pad_len++] = 0x80;

    while ((pad_len % 64) != 56) {
        if (pad_len >= 120) {
            memset(padded + pad_len, 0, 128 - pad_len);
            pad_len = 128;
            break;
        }
        padded[pad_len++] = 0;
    }

    if (pad_len % 64 != 56) {
        while (pad_len % 64 != 56)
            padded[pad_len++] = 0;
    }

    for (int i = 7; i >= 0; i--) {
        padded[pad_len++] = (uint8_t)(total_bits >> (i * 8));
    }

    for (size_t chunk = 0; chunk < pad_len; chunk += 64) {
        uint32_t w[80];
        for (int i = 0; i < 16; i++) {
            w[i] = ((uint32_t)padded[chunk + i * 4] << 24) |
                   ((uint32_t)padded[chunk + i * 4 + 1] << 16) |
                   ((uint32_t)padded[chunk + i * 4 + 2] << 8) |
                    (uint32_t)padded[chunk + i * 4 + 3];
        }
        for (int i = 16; i < 80; i++) {
            uint32_t val = w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16];
            w[i] = (val << 1) | (val >> 31);
        }

        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];

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

            uint32_t temp = ((a << 5) | (a >> 27)) + f + e + k + w[i];
            e = d;
            d = c;
            c = (b << 30) | (b >> 2);
            b = a;
            a = temp;
        }

        h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e;
    }

    for (int i = 0; i < 5; i++) {
        output[i * 4]     = (uint8_t)(h[i] >> 24);
        output[i * 4 + 1] = (uint8_t)(h[i] >> 16);
        output[i * 4 + 2] = (uint8_t)(h[i] >> 8);
        output[i * 4 + 3] = (uint8_t)(h[i]);
    }
}

void ws_base64_encode(const uint8_t *input, size_t len,
                      char *out, size_t out_size)
{
    size_t pos = 0;
    for (size_t i = 0; i < len; i += 3) {
        uint32_t val = ((uint32_t)input[i]) << 16;
        if (i + 1 < len) val |= ((uint32_t)input[i + 1]) << 8;
        if (i + 2 < len) val |= (uint32_t)input[i + 2];

        for (int j = 3; j >= 0; j--) {
            if (pos >= out_size - 1) break;
            size_t idx = (size_t)((val >> (j * 6)) & 0x3F);

            if (i + (2 - j) >= len) {
                out[pos++] = '=';
            } else {
                out[pos++] = ws_base64_table[idx];
            }
        }
    }
    if (pos < out_size) out[pos] = '\0';
}

void ws_handshake_client_init(WSConnection *conn, const char *host,
                              const char *uri)
{
    if (!conn) return;
    memset(conn, 0, sizeof(*conn));
    snprintf(conn->host, sizeof(conn->host), "%s", host ? host : "localhost");
    snprintf(conn->uri, sizeof(conn->uri), "%s", uri ? uri : "/");

    const char *src = "0123456789ABCDEF0123456789";
    size_t klen = strlen(src);
    char raw_key[17];
    for (int i = 0; i < 16; i++) {
        raw_key[i] = src[i % (int)klen];
    }
    raw_key[16] = '\0';
    snprintf(conn->key, sizeof(conn->key), "%s", raw_key);
}

void ws_handshake_server_init(WSConnection *conn)
{
    if (!conn) return;
    memset(conn, 0, sizeof(*conn));
    conn->handshake_done = false;
}

size_t ws_handshake_build_client(const WSConnection *conn,
                                 uint8_t *out, size_t out_size)
{
    if (!conn || !out) return 0;

    size_t n = (size_t)snprintf((char *)out, out_size,
        "GET %s HTTP/1.1\r\n"
        "Host: %s\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Key: %s\r\n"
        "Sec-WebSocket-Version: 13\r\n"
        "\r\n",
        conn->uri, conn->host, conn->key);

    return n;
}

int ws_handshake_parse_server(const uint8_t *data, size_t len,
                              WSConnection *conn)
{
    if (!data || !conn) return -1;

    const char *d = (const char *)data;
    const char *upgrade = strstr(d, "Upgrade: websocket");
    const char *conn_up = strstr(d, "Connection: Upgrade");
    const char *accept  = strstr(d, "Sec-WebSocket-Accept: ");

    if (!upgrade && !conn_up) return -1;

    if (strstr(d, "101") == NULL) return -2;

    if (accept) {
        accept += 22;
        size_t i = 0;
        while (*accept != '\r' && *accept != '\n' && *accept != '\0' &&
               i < sizeof(conn->accept) - 1) {
            conn->accept[i++] = *accept++;
        }
        conn->accept[i] = '\0';
        conn->handshake_done = true;
    }

    return 0;
}

int ws_handshake_parse_client(const uint8_t *data, size_t len,
                              WSConnection *conn)
{
    if (!data || !conn) return -1;

    const char *d = (const char *)data;
    const char *key = strstr(d, "Sec-WebSocket-Key: ");

    if (!key) return -2;

    key += 19;
    size_t i = 0;
    while (*key != '\r' && *key != '\n' && *key != '\0' &&
           i < sizeof(conn->key) - 1) {
        conn->key[i++] = *key++;
    }
    conn->key[i] = '\0';

    const char *host = strstr(d, "Host: ");
    if (host) {
        host += 6;
        i = 0;
        while (*host != '\r' && *host != '\n' && *host != '\0' &&
               i < sizeof(conn->host) - 1) {
            conn->host[i++] = *host++;
        }
        conn->host[i] = '\0';
    }

    return 0;
}

size_t ws_handshake_build_server(const WSConnection *conn,
                                 uint8_t *out, size_t out_size)
{
    if (!conn || !out) return 0;

    char combined[256];
    snprintf(combined, sizeof(combined), "%s%s", conn->key, WS_GUID);

    uint8_t sha1_hash[20];
    ws_sha1_hash((const uint8_t *)combined, strlen(combined), sha1_hash);

    char encoded[32];
    ws_base64_encode(sha1_hash, 20, encoded, sizeof(encoded));

    size_t n = (size_t)snprintf((char *)out, out_size,
        "HTTP/1.1 101 Switching Protocols\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Accept: %s\r\n"
        "\r\n",
        encoded);

    return n;
}

static size_t ws_encode_length(uint64_t len, bool mask, uint8_t *out)
{
    uint8_t first = mask ? 0x80 : 0x00;

    if (len <= 125) {
        out[0] = (uint8_t)(first | len);
        return 1;
    } else if (len <= 65535) {
        out[0] = (uint8_t)(first | 126);
        out[1] = (uint8_t)(len >> 8);
        out[2] = (uint8_t)(len);
        return 3;
    } else {
        out[0] = (uint8_t)(first | 127);
        for (int i = 7; i >= 0; i--) {
            out[1 + (7 - i)] = (uint8_t)(len >> (i * 8));
        }
        return 9;
    }
}

void ws_apply_mask(uint8_t *data, size_t len, const uint8_t mask[4])
{
    for (size_t i = 0; i < len; i++) {
        data[i] ^= mask[i % 4];
    }
}

int ws_frame_encode(const WSFrame *frame, uint8_t *out, size_t out_size,
                    size_t *written)
{
    if (!frame || !out || !written) return -1;

    size_t pos = 0;
    uint8_t first_byte = (uint8_t)((frame->fin ? 0x80 : 0x00) |
                                   (frame->opcode & 0x0F));

    uint8_t len_buf[9];
    size_t  len_hdr = ws_encode_length(frame->payload_len, frame->mask, len_buf);

    size_t total = 1 + len_hdr + (frame->mask ? 4 : 0) + frame->payload_len;
    if (total > out_size) return -2;

    out[pos++] = first_byte;
    memcpy(out + pos, len_buf, len_hdr);
    pos += len_hdr;

    if (frame->mask) {
        memcpy(out + pos, frame->masking_key, 4);
        pos += 4;
    }

    if (frame->payload && frame->payload_len > 0) {
        memcpy(out + pos, frame->payload, frame->payload_len);
        if (frame->mask) {
            ws_apply_mask(out + pos, frame->payload_len, frame->masking_key);
        }
        pos += frame->payload_len;
    }

    *written = pos;
    return 0;
}

int ws_frame_decode(const uint8_t *data, size_t len, WSFrame *frame,
                    size_t *consumed)
{
    if (!data || !frame || !consumed) return -1;
    if (len < 2) return -2;

    memset(frame, 0, sizeof(*frame));
    size_t pos = 0;

    frame->fin    = (data[pos] & 0x80) ? true : false;
    frame->opcode = data[pos] & 0x0F;
    pos++;

    frame->mask    = (data[pos] & 0x80) ? true : false;
    uint8_t len7   = data[pos] & 0x7F;
    pos++;

    if (len7 <= 125) {
        frame->payload_len = len7;
    } else if (len7 == 126) {
        if (pos + 2 > len) return -3;
        frame->payload_len = ((uint64_t)data[pos] << 8) | data[pos + 1];
        pos += 2;
    } else {
        if (pos + 8 > len) return -4;
        frame->payload_len = 0;
        for (int i = 0; i < 8; i++) {
            frame->payload_len = (frame->payload_len << 8) | data[pos + i];
        }
        pos += 8;
    }

    if (frame->mask) {
        if (pos + 4 > len) return -5;
        memcpy(frame->masking_key, data + pos, 4);
        pos += 4;
    }

    if (pos + frame->payload_len > len) return -6;

    frame->payload = (uint8_t *)(data + pos);

    if (frame->mask && frame->payload_len > 0) {
        ws_apply_mask(frame->payload, (size_t)frame->payload_len,
                      frame->masking_key);
    }

    *consumed = pos + (size_t)frame->payload_len;
    return 0;
}

int ws_send_text(const char *text, uint8_t *out, size_t out_size,
                 size_t *written)
{
    WSFrame frame;
    memset(&frame, 0, sizeof(frame));
    frame.fin         = true;
    frame.opcode      = WS_OP_TEXT;
    frame.mask        = true;
    frame.payload     = (uint8_t *)text;
    frame.payload_len = strlen(text);

    uint8_t default_mask[4] = {0x12, 0x34, 0x56, 0x78};
    memcpy(frame.masking_key, default_mask, 4);

    return ws_frame_encode(&frame, out, out_size, written);
}

int ws_recv_text(const uint8_t *data, size_t len, char *text,
                 size_t text_size, size_t *read_count)
{
    WSFrame frame;
    size_t consumed = 0;

    int rc = ws_frame_decode(data, len, &frame, &consumed);
    if (rc != 0) return rc;

    if (frame.opcode != WS_OP_TEXT || !frame.fin) return -10;

    size_t copy_len = frame.payload_len < text_size - 1 ?
                      (size_t)frame.payload_len : text_size - 1;
    if (frame.payload && copy_len > 0) {
        memcpy(text, frame.payload, copy_len);
    }
    text[copy_len] = '\0';

    if (read_count) *read_count = consumed;
    return 0;
}

int ws_send_close(uint16_t code, const char *reason,
                  uint8_t *out, size_t out_size, size_t *written)
{
    WSFrame frame;
    memset(&frame, 0, sizeof(frame));
    frame.fin      = true;
    frame.opcode   = WS_OP_CLOSE;
    frame.mask     = true;

    uint8_t close_payload[128];
    close_payload[0] = (uint8_t)(code >> 8);
    close_payload[1] = (uint8_t)(code);

    size_t plen = 2;
    if (reason) {
        size_t rlen = strlen(reason);
        if (rlen > 124) rlen = 124;
        memcpy(close_payload + 2, reason, rlen);
        plen += rlen;
    }

    frame.payload     = close_payload;
    frame.payload_len = plen;

    uint8_t default_mask[4] = {0xAB, 0xCD, 0xEF, 0x01};
    memcpy(frame.masking_key, default_mask, 4);

    return ws_frame_encode(&frame, out, out_size, written);
}

int ws_send_ping(const char *payload, uint8_t *out, size_t out_size,
                 size_t *written)
{
    WSFrame frame;
    memset(&frame, 0, sizeof(frame));
    frame.fin         = true;
    frame.opcode      = WS_OP_PING;
    frame.mask        = true;
    frame.payload     = (uint8_t *)payload;
    frame.payload_len = payload ? strlen(payload) : 0;

    uint8_t default_mask[4] = {0x9A, 0xBC, 0xDE, 0xF0};
    memcpy(frame.masking_key, default_mask, 4);

    return ws_frame_encode(&frame, out, out_size, written);
}

int ws_send_pong(const uint8_t *ping_data, size_t ping_len,
                 uint8_t *out, size_t out_size, size_t *written)
{
    WSFrame frame;
    memset(&frame, 0, sizeof(frame));
    frame.fin         = true;
    frame.opcode      = WS_OP_PONG;
    frame.mask        = true;
    frame.payload     = (uint8_t *)ping_data;
    frame.payload_len = ping_len;

    uint8_t default_mask[4] = {0x11, 0x22, 0x33, 0x44};
    memcpy(frame.masking_key, default_mask, 4);

    return ws_frame_encode(&frame, out, out_size, written);
}
