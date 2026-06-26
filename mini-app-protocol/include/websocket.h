#ifndef WEBSOCKET_H
#define WEBSOCKET_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#define WS_GUID             "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"
#define WS_MAX_FRAME_SIZE   65536
#define WS_MAX_PAYLOAD_SIZE 65535
#define WS_MAX_HEADER_SIZE  14
#define WS_MAX_URI          256
#define WS_MAX_HOST         128
#define WS_MAX_KEY          64
#define WS_MAX_ACCEPT       32

enum WSOpcode {
    WS_OP_CONTINUATION = 0x0,
    WS_OP_TEXT         = 0x1,
    WS_OP_BINARY       = 0x2,
    WS_OP_CLOSE        = 0x8,
    WS_OP_PING         = 0x9,
    WS_OP_PONG         = 0xA
};

enum WSCloseCode {
    WS_CLOSE_NORMAL         = 1000,
    WS_CLOSE_GOING_AWAY     = 1001,
    WS_CLOSE_PROTOCOL_ERROR = 1002,
    WS_CLOSE_UNSUPPORTED    = 1003,
    WS_CLOSE_NO_STATUS      = 1005,
    WS_CLOSE_ABNORMAL       = 1006,
    WS_CLOSE_INVALID_PAYLOAD = 1007,
    WS_CLOSE_POLICY_VIOLATION = 1008,
    WS_CLOSE_TOO_LARGE      = 1009,
    WS_CLOSE_EXTENSION_NEEDED = 1010,
    WS_CLOSE_INTERNAL_ERROR = 1011
};

typedef struct {
    bool     fin;
    uint8_t  opcode;
    bool     mask;
    uint64_t payload_len;
    uint8_t  masking_key[4];
    uint8_t *payload;
} WSFrame;

typedef struct {
    char host[WS_MAX_HOST];
    char uri[WS_MAX_URI];
    char key[WS_MAX_KEY];
    char accept[WS_MAX_ACCEPT];
    bool  handshake_done;
} WSConnection;

size_t ws_handshake_build_client(const WSConnection *conn,
                                 uint8_t *out, size_t out_size);
int    ws_handshake_parse_server(const uint8_t *data, size_t len,
                                 WSConnection *conn);
int    ws_handshake_parse_client(const uint8_t *data, size_t len,
                                 WSConnection *conn);
size_t ws_handshake_build_server(const WSConnection *conn,
                                 uint8_t *out, size_t out_size);
void   ws_handshake_client_init(WSConnection *conn, const char *host,
                                const char *uri);
void   ws_handshake_server_init(WSConnection *conn);
int    ws_frame_encode(const WSFrame *frame, uint8_t *out, size_t out_size,
                       size_t *written);
int    ws_frame_decode(const uint8_t *data, size_t len, WSFrame *frame,
                       size_t *consumed);
int    ws_send_text(const char *text, uint8_t *out, size_t out_size,
                    size_t *written);
int    ws_recv_text(const uint8_t *data, size_t len, char *text,
                    size_t text_size, size_t *read_count);
int    ws_send_close(uint16_t code, const char *reason,
                     uint8_t *out, size_t out_size, size_t *written);
int    ws_send_ping(const char *payload, uint8_t *out, size_t out_size,
                    size_t *written);
int    ws_send_pong(const uint8_t *ping_data, size_t ping_len,
                    uint8_t *out, size_t out_size, size_t *written);
void   ws_sha1_hash(const uint8_t *input, size_t len, uint8_t output[20]);
void   ws_base64_encode(const uint8_t *input, size_t len,
                        char *out, size_t out_size);
void   ws_apply_mask(uint8_t *data, size_t len, const uint8_t mask[4]);

#endif
