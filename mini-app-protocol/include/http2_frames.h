#ifndef HTTP2_FRAMES_H
#define HTTP2_FRAMES_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#define H2_FRAME_HEADER_SIZE        9
#define H2_MAX_FRAME_SIZE           16384
#define H2_PREFACE_LENGTH           24
#define H2_DEFAULT_HEADER_TABLE_SIZE 4096
#define H2_DEFAULT_MAX_CONCURRENT_STREAMS 16
#define H2_DEFAULT_INITIAL_WINDOW_SIZE    65535
#define H2_MAX_STREAM_ID            0x7FFFFFFF
#define H2_MAX_STREAMS              16
#define H2_MAX_HEADER_FIELDS        16
#define H2_MAX_FIELD_VALUE          256
#define H2_MAX_PRIORITY_NODES       32

enum H2FrameType {
    H2_FRAME_DATA          = 0x00,
    H2_FRAME_HEADERS       = 0x01,
    H2_FRAME_PRIORITY      = 0x02,
    H2_FRAME_RST_STREAM    = 0x03,
    H2_FRAME_SETTINGS      = 0x04,
    H2_FRAME_PUSH_PROMISE  = 0x05,
    H2_FRAME_PING          = 0x06,
    H2_FRAME_GOAWAY        = 0x07,
    H2_FRAME_WINDOW_UPDATE = 0x08,
    H2_FRAME_CONTINUATION  = 0x09
};

enum H2FrameFlag {
    H2_FLAG_NONE        = 0x00,
    H2_FLAG_END_STREAM  = 0x01,
    H2_FLAG_END_HEADERS = 0x04,
    H2_FLAG_PADDED      = 0x08,
    H2_FLAG_PRIORITY    = 0x20,
    H2_FLAG_ACK         = 0x01
};

enum H2SettingsParam {
    H2_SETTINGS_HEADER_TABLE_SIZE      = 0x01,
    H2_SETTINGS_ENABLE_PUSH            = 0x02,
    H2_SETTINGS_MAX_CONCURRENT_STREAMS = 0x03,
    H2_SETTINGS_INITIAL_WINDOW_SIZE    = 0x04,
    H2_SETTINGS_MAX_FRAME_SIZE         = 0x05,
    H2_SETTINGS_MAX_HEADER_LIST_SIZE   = 0x06
};

enum H2HPACK {
    H2_HPACK_INDEXED              = 0x80,
    H2_HPACK_LITERAL_INC_INDEXING = 0x40,
    H2_HPACK_LITERAL_NO_INDEXING  = 0x00,
    H2_HPACK_LITERAL_NEVER_INDEX  = 0x10,
    H2_HPACK_DYNAMIC_TABLE_SIZE   = 0x20
};

enum H2Error {
    H2_NO_ERROR            = 0x00,
    H2_PROTOCOL_ERROR      = 0x01,
    H2_INTERNAL_ERROR      = 0x02,
    H2_FLOW_CONTROL_ERROR  = 0x03,
    H2_SETTINGS_TIMEOUT    = 0x04,
    H2_STREAM_CLOSED       = 0x05,
    H2_FRAME_SIZE_ERROR    = 0x06,
    H2_REFUSED_STREAM      = 0x07,
    H2_CANCEL              = 0x08,
    H2_COMPRESSION_ERROR   = 0x09,
    H2_CONNECT_ERROR       = 0x0A,
    H2_ENHANCE_YOUR_CALM   = 0x0B,
    H2_INADEQUATE_SECURITY = 0x0C,
    H2_HTTP_1_1_REQUIRED   = 0x0D
};

enum H2StreamState {
    H2_STREAM_IDLE,
    H2_STREAM_RESERVED_LOCAL,
    H2_STREAM_RESERVED_REMOTE,
    H2_STREAM_OPEN,
    H2_STREAM_HALF_CLOSED_LOCAL,
    H2_STREAM_HALF_CLOSED_REMOTE,
    H2_STREAM_TERMINATED
};

typedef struct {
    uint32_t length         : 24;
    uint8_t  type;
    uint8_t  flags;
    uint32_t stream_id      : 31;
    uint8_t  reserved       : 1;
} H2FrameHeader;

typedef struct {
    uint32_t header_table_size;
    uint32_t max_concurrent_streams;
    uint32_t initial_window_size;
    uint32_t max_frame_size;
    uint32_t max_header_list_size;
    bool     enable_push;
} H2Settings;

typedef struct {
    char    name[128];
    char    value[H2_MAX_FIELD_VALUE];
} H2HeaderField;

typedef struct {
    H2HeaderField fields[H2_MAX_HEADER_FIELDS];
    size_t        count;
} H2HeaderBlock;

typedef struct {
    uint32_t            stream_id;
    enum H2StreamState  state;
    uint32_t            local_window;
    uint32_t            remote_window;
    H2HeaderBlock       headers;
    uint8_t            *outgoing_data;
    size_t              outgoing_len;
    size_t              outgoing_pos;
} H2Stream;

typedef struct {
    uint32_t parent_stream_id;
    uint8_t  weight;
    uint32_t self_stream_id;
    bool     exclusive;
    uint32_t children[H2_MAX_PRIORITY_NODES];
    size_t   child_count;
} H2PriorityNode;

typedef struct {
    H2PriorityNode nodes[H2_MAX_PRIORITY_NODES];
    size_t         node_count;
} H2PriorityTree;

typedef struct {
    H2Stream    streams[H2_MAX_STREAMS];
    size_t      stream_count;
    H2Settings  local_settings;
    H2Settings  remote_settings;
    uint32_t    last_stream_id;
    uint32_t    connection_window;
    int         hpack_dynamic_table_size;
    H2HeaderField hpack_dynamic_table[64];
    size_t      hpack_dynamic_count;
    H2PriorityTree priority_tree;
} H2Connection;

size_t h2_frame_build(uint8_t *buf, size_t buf_size, uint8_t type,
                      uint8_t flags, uint32_t stream_id,
                      const uint8_t *payload, size_t payload_len);
int    h2_frame_parse(const uint8_t *data, size_t len, H2FrameHeader *hdr,
                      const uint8_t **payload, size_t *payload_len);
void   h2_settings_init(H2Settings *s);
size_t h2_settings_build(const H2Settings *s, uint8_t *buf, size_t buf_size);
int    h2_settings_parse(const uint8_t *payload, size_t len, H2Settings *s);
int    h2_settings_exchange(H2Connection *conn);
int    h2_stream_open(H2Connection *conn, uint32_t *stream_id);
int    h2_stream_close(H2Connection *conn, uint32_t stream_id);
H2Stream *h2_stream_get(H2Connection *conn, uint32_t stream_id);
int    h2_header_encode(H2Connection *conn, const H2HeaderBlock *block,
                        uint8_t *out, size_t out_size, size_t *written);
int    h2_header_decode(H2Connection *conn, const uint8_t *data, size_t len,
                        H2HeaderBlock *block);
int    h2_flow_control_update(H2Connection *conn, uint32_t stream_id,
                              int32_t increment);
bool   h2_flow_control_can_send(H2Connection *conn, uint32_t stream_id,
                                size_t amount);
int    h2_send_headers(H2Connection *conn, uint32_t stream_id,
                       const H2HeaderBlock *headers, bool end_stream);
int    h2_send_data(H2Connection *conn, uint32_t stream_id,
                    const uint8_t *data, size_t len, bool end_stream);

void   h2_priority_tree_init(H2PriorityTree *tree);
int    h2_priority_add(H2PriorityTree *tree, uint32_t stream_id,
                       uint32_t parent_id, uint8_t weight, bool exclusive);
H2PriorityNode *h2_priority_find(H2PriorityTree *tree, uint32_t stream_id);
int    h2_priority_remove(H2PriorityTree *tree, uint32_t stream_id);
int    h2_priority_allocate_bandwidth(H2PriorityTree *tree,
                                      uint32_t parent_id,
                                      uint32_t total_bandwidth,
                                      uint32_t *allocations,
                                      size_t max_allocations);

#endif
