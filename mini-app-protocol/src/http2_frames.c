#include "http2_frames.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

size_t h2_frame_build(uint8_t *buf, size_t buf_size, uint8_t type,
                      uint8_t flags, uint32_t stream_id,
                      const uint8_t *payload, size_t payload_len)
{
    size_t total = H2_FRAME_HEADER_SIZE + payload_len;
    if (total > buf_size) return 0;

    buf[0] = (uint8_t)(payload_len >> 16);
    buf[1] = (uint8_t)(payload_len >> 8);
    buf[2] = (uint8_t)(payload_len);

    buf[3] = type;
    buf[4] = flags;

    buf[5] = (uint8_t)(stream_id >> 24);
    buf[6] = (uint8_t)(stream_id >> 16);
    buf[7] = (uint8_t)(stream_id >> 8);
    buf[8] = (uint8_t)(stream_id);
    buf[8] &= 0x7F;

    if (payload && payload_len > 0) {
        memcpy(buf + H2_FRAME_HEADER_SIZE, payload, payload_len);
    }

    return total;
}

int h2_frame_parse(const uint8_t *data, size_t len, H2FrameHeader *hdr,
                   const uint8_t **payload, size_t *payload_len)
{
    if (!data || !hdr || len < H2_FRAME_HEADER_SIZE) return -1;

    hdr->length  = ((uint32_t)data[0] << 16) |
                   ((uint32_t)data[1] << 8)  |
                    (uint32_t)data[2];
    hdr->type    = data[3];
    hdr->flags   = data[4];
    hdr->stream_id = ((uint32_t)(data[5] & 0x7F) << 24) |
                     ((uint32_t)data[6] << 16) |
                     ((uint32_t)data[7] << 8)  |
                      (uint32_t)data[8];

    if (len < (size_t)(H2_FRAME_HEADER_SIZE) + hdr->length) return -2;

    *payload     = data + H2_FRAME_HEADER_SIZE;
    *payload_len = hdr->length;

    return 0;
}

void h2_settings_init(H2Settings *s)
{
    if (!s) return;
    s->header_table_size      = H2_DEFAULT_HEADER_TABLE_SIZE;
    s->max_concurrent_streams = H2_DEFAULT_MAX_CONCURRENT_STREAMS;
    s->initial_window_size    = H2_DEFAULT_INITIAL_WINDOW_SIZE;
    s->max_frame_size         = H2_MAX_FRAME_SIZE;
    s->max_header_list_size   = 0xFFFFFFFF;
    s->enable_push            = true;
}

size_t h2_settings_build(const H2Settings *s, uint8_t *buf, size_t buf_size)
{
    if (!s || !buf || buf_size < 36) return 0;

    size_t pos = 0;
    uint16_t params[][2] = {
        {H2_SETTINGS_HEADER_TABLE_SIZE,      s->header_table_size & 0xFFFF},
        {H2_SETTINGS_ENABLE_PUSH,            s->enable_push ? 1 : 0},
        {H2_SETTINGS_MAX_CONCURRENT_STREAMS, s->max_concurrent_streams},
        {H2_SETTINGS_INITIAL_WINDOW_SIZE,    s->initial_window_size},
        {H2_SETTINGS_MAX_FRAME_SIZE,         s->max_frame_size},
        {H2_SETTINGS_MAX_HEADER_LIST_SIZE,   s->max_header_list_size}
    };

    for (int i = 0; i < 6; i++) {
        buf[pos++] = (uint8_t)(params[i][0] >> 8);
        buf[pos++] = (uint8_t)(params[i][0]);
        buf[pos++] = (uint8_t)(params[i][1] >> 24);
        buf[pos++] = (uint8_t)(params[i][1] >> 16);
        buf[pos++] = (uint8_t)(params[i][1] >> 8);
        buf[pos++] = (uint8_t)(params[i][1]);
    }

    return pos;
}

int h2_settings_parse(const uint8_t *payload, size_t len, H2Settings *s)
{
    if (!payload || !s || len % 6 != 0) return -1;

    h2_settings_init(s);

    for (size_t i = 0; i < len; i += 6) {
        uint16_t id    = ((uint16_t)payload[i] << 8) | payload[i + 1];
        uint32_t value = ((uint32_t)payload[i + 2] << 24) |
                         ((uint32_t)payload[i + 3] << 16) |
                         ((uint32_t)payload[i + 4] << 8)  |
                          (uint32_t)payload[i + 5];

        switch (id) {
        case H2_SETTINGS_HEADER_TABLE_SIZE:
            s->header_table_size = value;
            break;
        case H2_SETTINGS_ENABLE_PUSH:
            s->enable_push = (value != 0);
            break;
        case H2_SETTINGS_MAX_CONCURRENT_STREAMS:
            s->max_concurrent_streams = value;
            break;
        case H2_SETTINGS_INITIAL_WINDOW_SIZE:
            s->initial_window_size = value;
            break;
        case H2_SETTINGS_MAX_FRAME_SIZE:
            s->max_frame_size = value;
            break;
        case H2_SETTINGS_MAX_HEADER_LIST_SIZE:
            s->max_header_list_size = value;
            break;
        default:
            break;
        }
    }

    return 0;
}

int h2_settings_exchange(H2Connection *conn)
{
    if (!conn) return -1;

    uint8_t settings_payload[36];
    size_t  plen = h2_settings_build(&conn->local_settings,
                                     settings_payload, sizeof(settings_payload));

    uint8_t frame[H2_FRAME_HEADER_SIZE + 36];
    h2_frame_build(frame, sizeof(frame), H2_FRAME_SETTINGS,
                   H2_FLAG_NONE, 0, settings_payload, plen);

    conn->remote_settings = conn->local_settings;

    uint8_t ack_frame[H2_FRAME_HEADER_SIZE];
    h2_frame_build(ack_frame, sizeof(ack_frame), H2_FRAME_SETTINGS,
                   H2_FLAG_ACK, 0, NULL, 0);

    return 0;
}

int h2_stream_open(H2Connection *conn, uint32_t *stream_id)
{
    if (!conn || !stream_id) return -1;
    if (conn->stream_count >= H2_MAX_STREAMS) return -2;

    conn->last_stream_id += 2;
    *stream_id = conn->last_stream_id;

    H2Stream *s = &conn->streams[conn->stream_count++];
    memset(s, 0, sizeof(*s));
    s->stream_id      = *stream_id;
    s->state          = H2_STREAM_OPEN;
    s->local_window   = conn->remote_settings.initial_window_size;
    s->remote_window  = conn->local_settings.initial_window_size;

    return 0;
}

int h2_stream_close(H2Connection *conn, uint32_t stream_id)
{
    if (!conn) return -1;

    H2Stream *s = h2_stream_get(conn, stream_id);
    if (!s) return -2;

    s->state = H2_STREAM_TERMINATED;
    free(s->outgoing_data);
    s->outgoing_data = NULL;
    s->outgoing_len  = 0;

    return 0;
}

H2Stream *h2_stream_get(H2Connection *conn, uint32_t stream_id)
{
    if (!conn) return NULL;

    for (size_t i = 0; i < conn->stream_count; i++) {
        if (conn->streams[i].stream_id == stream_id)
            return &conn->streams[i];
    }
    return NULL;
}

static int hpack_encode_int(uint8_t *out, size_t out_size,
                            size_t prefix_bits, uint64_t value)
{
    uint8_t mask = (uint8_t)((1 << prefix_bits) - 1);
    if (value < (uint64_t)mask) {
        if (out_size < 1) return -1;
        out[0] = (uint8_t)value;
        return 1;
    }

    if (out_size < 1) return -1;
    out[0] = mask;
    value -= mask;
    size_t pos = 1;

    while (value >= 0x80) {
        if (pos >= out_size) return -1;
        out[pos++] = (uint8_t)((value & 0x7F) | 0x80);
        value >>= 7;
    }
    if (pos >= out_size) return -1;
    out[pos++] = (uint8_t)(value & 0x7F);

    return (int)pos;
}

static int hpack_encode_string(const char *str, uint8_t *out, size_t out_size)
{
    size_t len = strlen(str);
    if (out_size < len + 2) return -1;

    int n = hpack_encode_int(out, out_size, 7, len);
    if (n < 0) return -1;
    memcpy(out + n, str, len);
    return (int)(n + len);
}

int h2_header_encode(H2Connection *conn, const H2HeaderBlock *block,
                     uint8_t *out, size_t out_size, size_t *written)
{
    if (!conn || !block || !out || !written) return -1;

    size_t pos = 0;
    static const char *static_table[][2] = {
        {":authority", ""},
        {":method", "GET"},
        {":method", "POST"},
        {":path", "/"},
        {":path", "/index.html"},
        {":scheme", "http"},
        {":scheme", "https"},
        {":status", "200"},
        {":status", "204"},
        {":status", "206"},
        {":status", "304"},
        {":status", "400"},
        {":status", "404"},
        {":status", "500"},
        {"accept-charset", ""},
        {"accept-encoding", "gzip, deflate"},
        {"accept-language", ""},
        {"accept-ranges", ""},
        {"accept", ""},
        {"access-control-allow-origin", ""},
        {"age", ""},
        {"allow", ""},
        {"authorization", ""},
        {"cache-control", ""},
        {"content-disposition", ""},
        {"content-encoding", ""},
        {"content-language", ""},
        {"content-length", ""},
        {"content-location", ""},
        {"content-range", ""},
        {"content-type", ""},
        {"cookie", ""},
        {"date", ""},
        {"etag", ""},
        {"expect", ""},
        {"expires", ""},
        {"from", ""},
        {"host", ""},
        {"if-match", ""},
        {"if-modified-since", ""},
        {"if-none-match", ""},
        {"if-range", ""},
        {"if-unmodified-since", ""},
        {"last-modified", ""},
        {"link", ""},
        {"location", ""},
        {"max-forwards", ""},
        {"proxy-authenticate", ""},
        {"proxy-authorization", ""},
        {"range", ""},
        {"referer", ""},
        {"refresh", ""},
        {"retry-after", ""},
        {"server", ""},
        {"set-cookie", ""},
        {"strict-transport-security", ""},
        {"transfer-encoding", ""},
        {"user-agent", ""},
        {"vary", ""},
        {"via", ""},
        {"www-authenticate", ""}
    };

    for (size_t i = 0; i < block->count; i++) {
        bool indexed = false;
        for (int j = 0; j < 61; j++) {
            if (strcmp(block->fields[i].name, static_table[j][0]) == 0 &&
                (static_table[j][1][0] == '\0' ||
                 strcmp(block->fields[i].value, static_table[j][1]) == 0)) {
                if (pos >= out_size) return -1;
                out[pos] = 0x80;
                int n = hpack_encode_int(&out[pos], out_size - pos, 7, (uint64_t)(j + 1));
                if (n < 0) return -1;
                pos += (size_t)n;
                indexed = true;
                break;
            }
        }

        if (!indexed) {
            if (pos >= out_size) return -1;
            out[pos] = 0x40;
            size_t start = pos;
            pos++;
            int n = hpack_encode_string(block->fields[i].name, out + pos, out_size - pos);
            if (n < 0) return -1;
            pos += (size_t)n;
            n = hpack_encode_string(block->fields[i].value, out + pos, out_size - pos);
            if (n < 0) return -1;
            pos += (size_t)n;
            out[start] = (uint8_t)(0x40 | (pos - start - 1));

            if (conn->hpack_dynamic_count < 64) {
                conn->hpack_dynamic_table[conn->hpack_dynamic_count] = block->fields[i];
                conn->hpack_dynamic_count++;
            }
        }
    }

    *written = pos;
    return 0;
}

int h2_header_decode(H2Connection *conn, const uint8_t *data, size_t len,
                     H2HeaderBlock *block)
{
    if (!conn || !data || !block) return -1;

    block->count = 0;
    size_t pos = 0;

    static const char *static_table[61][2] = {
        {":authority", ""}, {":method", "GET"}, {":method", "POST"},
        {":path", "/"}, {":path", "/index.html"}, {":scheme", "http"},
        {":scheme", "https"}, {":status", "200"}, {":status", "204"},
        {":status", "206"}, {":status", "304"}, {":status", "400"},
        {":status", "404"}, {":status", "500"}, {"accept-charset", ""},
        {"accept-encoding", "gzip, deflate"}, {"accept-language", ""},
        {"accept-ranges", ""}, {"accept", ""},
        {"access-control-allow-origin", ""}, {"age", ""}, {"allow", ""},
        {"authorization", ""}, {"cache-control", ""},
        {"content-disposition", ""}, {"content-encoding", ""},
        {"content-language", ""}, {"content-length", ""},
        {"content-location", ""}, {"content-range", ""}, {"content-type", ""},
        {"cookie", ""}, {"date", ""}, {"etag", ""}, {"expect", ""},
        {"expires", ""}, {"from", ""}, {"host", ""}, {"if-match", ""},
        {"if-modified-since", ""}, {"if-none-match", ""}, {"if-range", ""},
        {"if-unmodified-since", ""}, {"last-modified", ""}, {"link", ""},
        {"location", ""}, {"max-forwards", ""},
        {"proxy-authenticate", ""}, {"proxy-authorization", ""},
        {"range", ""}, {"referer", ""}, {"refresh", ""}, {"retry-after", ""},
        {"server", ""}, {"set-cookie", ""},
        {"strict-transport-security", ""}, {"transfer-encoding", ""},
        {"user-agent", ""}, {"vary", ""}, {"via", ""}, {"www-authenticate", ""}
    };

    while (pos < len && block->count < H2_MAX_HEADER_FIELDS) {
        uint8_t first = data[pos];
        H2HeaderField *field = &block->fields[block->count];

        if (first & 0x80) {
            uint64_t idx;
            int n = hpack_encode_int(NULL, 0, 7, 0);
            (void)n;

            uint8_t mask = 0x7F;
            idx = first & mask;
            pos++;

            if (idx == 0x7F) {
                uint64_t extra = 0;
                size_t shift = 0;
                while (pos < len && (data[pos] & 0x80)) {
                    extra |= ((uint64_t)(data[pos] & 0x7F) << shift);
                    shift += 7;
                    pos++;
                }
                if (pos < len) {
                    extra |= ((uint64_t)data[pos] << shift);
                    pos++;
                }
                idx += extra;
            }

            uint64_t table_idx = idx - 1;
            if (table_idx < 61) {
                snprintf(field->name, sizeof(field->name), "%s", static_table[table_idx][0]);
                snprintf(field->value, sizeof(field->value), "%s", static_table[table_idx][1]);
            } else if (table_idx - 61 < conn->hpack_dynamic_count) {
                *field = conn->hpack_dynamic_table[table_idx - 61];
            } else {
                return -1;
            }
            block->count++;
        } else if (first & 0x40 || first == 0x00 || (first & 0x10)) {
            bool add_to_table = (first & 0x40) ? true : false;
            pos++;
            if (pos >= len) break;

            uint64_t name_len = data[pos] & 0x7F;
            pos++;
            if (pos + name_len > len) return -1;
            memcpy(field->name, data + pos, (size_t)name_len);
            field->name[name_len] = '\0';
            pos += (size_t)name_len;

            if (pos >= len) break;
            uint64_t value_len = data[pos] & 0x7F;
            pos++;
            if (pos + value_len > len) return -1;
            memcpy(field->value, data + pos, (size_t)value_len);
            field->value[value_len] = '\0';
            pos += (size_t)value_len;

            block->count++;

            if (add_to_table && conn->hpack_dynamic_count < 64) {
                conn->hpack_dynamic_table[conn->hpack_dynamic_count++] = *field;
            }
        } else {
            pos++;
        }
    }

    return 0;
}

int h2_flow_control_update(H2Connection *conn, uint32_t stream_id,
                           int32_t increment)
{
    if (!conn) return -1;

    if (stream_id == 0) {
        conn->connection_window += (uint32_t)increment;
    } else {
        H2Stream *s = h2_stream_get(conn, stream_id);
        if (!s) return -2;
        s->remote_window += (uint32_t)increment;
    }

    return 0;
}

bool h2_flow_control_can_send(H2Connection *conn, uint32_t stream_id,
                              size_t amount)
{
    if (!conn) return false;

    H2Stream *s = h2_stream_get(conn, stream_id);
    if (!s) return false;

    return s->local_window >= amount &&
           conn->remote_settings.initial_window_size >= amount;
}

int h2_send_headers(H2Connection *conn, uint32_t stream_id,
                    const H2HeaderBlock *headers, bool end_stream)
{
    if (!conn || !headers) return -1;

    H2Stream *s = h2_stream_get(conn, stream_id);
    if (!s) return -2;

    uint8_t encoded[4096];
    size_t written = 0;

    if (h2_header_encode(conn, headers, encoded, sizeof(encoded), &written) < 0)
        return -3;

    uint8_t frame[H2_FRAME_HEADER_SIZE + 4096];
    uint8_t flags = H2_FLAG_END_HEADERS;
    if (end_stream) flags |= H2_FLAG_END_STREAM;

    h2_frame_build(frame, sizeof(frame), H2_FRAME_HEADERS,
                   flags, stream_id, encoded, written);

    s->headers = *headers;

    return 0;
}

void h2_priority_tree_init(H2PriorityTree *tree)
{
    if (!tree) return;
    memset(tree, 0, sizeof(*tree));
    tree->nodes[0].self_stream_id = 0;
    tree->nodes[0].weight = 16;
    tree->nodes[0].exclusive = false;
    tree->node_count = 1;
}

int h2_priority_add(H2PriorityTree *tree, uint32_t stream_id,
                    uint32_t parent_id, uint8_t weight, bool exclusive)
{
    if (!tree || tree->node_count >= H2_MAX_PRIORITY_NODES) return -1;
    if (weight < 1) weight = 1;

    H2PriorityNode *parent = h2_priority_find(tree, parent_id);
    if (!parent) return -2;

    H2PriorityNode *existing = h2_priority_find(tree, stream_id);
    if (existing) {
        if (exclusive && parent->child_count > 0) {
            for (size_t i = 0; i < parent->child_count; i++) {
                H2PriorityNode *child = h2_priority_find(tree, parent->children[i]);
                if (child) child->parent_stream_id = stream_id;
            }
        }
        existing->parent_stream_id = parent_id;
        existing->weight = weight;
        existing->exclusive = exclusive;
        return 0;
    }

    H2PriorityNode *node = &tree->nodes[tree->node_count];
    memset(node, 0, sizeof(*node));
    node->self_stream_id = stream_id;
    node->parent_stream_id = parent_id;
    node->weight = weight;
    node->exclusive = exclusive;

    if (exclusive && parent->child_count > 0) {
        for (size_t i = 0; i < parent->child_count; i++) {
            H2PriorityNode *child = h2_priority_find(tree, parent->children[i]);
            if (child) child->parent_stream_id = stream_id;
            node->children[node->child_count++] = parent->children[i];
        }
        parent->child_count = 0;
    }

    parent->children[parent->child_count++] = stream_id;
    tree->node_count++;

    return 0;
}

H2PriorityNode *h2_priority_find(H2PriorityTree *tree, uint32_t stream_id)
{
    if (!tree) return NULL;
    for (size_t i = 0; i < tree->node_count; i++) {
        if (tree->nodes[i].self_stream_id == stream_id)
            return &tree->nodes[i];
    }
    return NULL;
}

int h2_priority_remove(H2PriorityTree *tree, uint32_t stream_id)
{
    if (!tree) return -1;
    if (stream_id == 0) return -2;

    H2PriorityNode *node = h2_priority_find(tree, stream_id);
    if (!node) return -3;

    H2PriorityNode *parent = h2_priority_find(tree, node->parent_stream_id);
    uint32_t grandparent_id = parent ? parent->parent_stream_id : 0;

    for (size_t i = 0; i < node->child_count; i++) {
        H2PriorityNode *child = h2_priority_find(tree, node->children[i]);
        if (child) {
            child->parent_stream_id = grandparent_id;
            if (parent) {
                parent->children[parent->child_count++] = node->children[i];
            }
        }
    }

    if (parent) {
        for (size_t i = 0; i < parent->child_count; i++) {
            if (parent->children[i] == stream_id) {
                parent->children[i] = parent->children[parent->child_count - 1];
                parent->child_count--;
                break;
            }
        }
    }

    memset(node, 0, sizeof(*node));
    return 0;
}

int h2_priority_allocate_bandwidth(H2PriorityTree *tree,
                                    uint32_t parent_id,
                                    uint32_t total_bandwidth,
                                    uint32_t *allocations,
                                    size_t max_allocations)
{
    if (!tree || !allocations) return -1;

    H2PriorityNode *parent = h2_priority_find(tree, parent_id);
    if (!parent || parent->child_count == 0) return -2;

    uint32_t total_weight = 0;
    for (size_t i = 0; i < parent->child_count; i++) {
        H2PriorityNode *child = h2_priority_find(tree, parent->children[i]);
        if (child) total_weight += (uint32_t)child->weight;
    }

    if (total_weight == 0) return -3;

    size_t alloc_idx = 0;
    for (size_t i = 0; i < parent->child_count && alloc_idx < max_allocations; i++) {
        H2PriorityNode *child = h2_priority_find(tree, parent->children[i]);
        if (!child) continue;

        uint32_t share = (uint32_t)(((uint64_t)total_bandwidth * child->weight) / total_weight);
        if (share < 1 && total_bandwidth > 0) share = 1;
        if (alloc_idx + 1 < max_allocations) {
            allocations[alloc_idx++] = child->self_stream_id;
            allocations[alloc_idx++] = share;
        }
    }

    return (int)(alloc_idx / 2);
}

int h2_send_data(H2Connection *conn, uint32_t stream_id,
                 const uint8_t *data, size_t len, bool end_stream)
{
    if (!conn || !data) return -1;

    H2Stream *s = h2_stream_get(conn, stream_id);
    if (!s) return -2;

    if (!h2_flow_control_can_send(conn, stream_id, len))
        return -3;

    uint8_t flags = H2_FLAG_NONE;
    if (end_stream) flags |= H2_FLAG_END_STREAM;

    uint8_t frame[H2_FRAME_HEADER_SIZE + H2_MAX_FRAME_SIZE];
    h2_frame_build(frame, sizeof(frame), H2_FRAME_DATA,
                   flags, stream_id, data, len);

    s->local_window -= (uint32_t)len;

    if (end_stream) {
        s->state = H2_STREAM_HALF_CLOSED_LOCAL;
    }

    return 0;
}
