#include "sse.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

void sse_connection_init(SSEConnection *conn, const char *uri)
{
    if (!conn) return;
    memset(conn, 0, sizeof(*conn));
    conn->state = SSE_STATE_CONNECTING;
    if (uri) {
        snprintf(conn->uri, sizeof(conn->uri), "%s", uri);
    }
    conn->reconnection_time_ms = 3000;
    conn->reconnection_time_set = false;
}

int sse_build_handshake(const SSEConnection *conn,
                        uint8_t *out, size_t out_size)
{
    if (!conn || !out) return -1;

    const char *last_id = conn->last_event_id[0] ? conn->last_event_id : "";

    int n;
    if (last_id[0]) {
        n = snprintf((char *)out, out_size,
            "GET %s HTTP/1.1\r\n"
            "Host: localhost\r\n"
            "Accept: text/event-stream\r\n"
            "Cache-Control: no-cache\r\n"
            "Last-Event-ID: %s\r\n"
            "\r\n",
            conn->uri, last_id);
    } else {
        n = snprintf((char *)out, out_size,
            "GET %s HTTP/1.1\r\n"
            "Host: localhost\r\n"
            "Accept: text/event-stream\r\n"
            "Cache-Control: no-cache\r\n"
            "\r\n",
            conn->uri);
    }

    return (n > 0) ? 0 : -1;
}

size_t sse_encode_event(const SSEEvent *event,
                        uint8_t *out, size_t out_size)
{
    if (!event || !out) return 0;

    size_t pos = 0;

    if (event->id[0]) {
        int n = snprintf((char *)(out + pos), out_size - pos,
                         "id: %s\n", event->id);
        if (n < 0 || (size_t)n >= out_size - pos) return 0;
        pos += (size_t)n;
    }

    if (event->event[0]) {
        int n = snprintf((char *)(out + pos), out_size - pos,
                         "event: %s\n", event->event);
        if (n < 0 || (size_t)n >= out_size - pos) return 0;
        pos += (size_t)n;
    }

    if (event->retry_ms > 0) {
        int n = snprintf((char *)(out + pos), out_size - pos,
                         "retry: %lld\n", (long long)event->retry_ms);
        if (n < 0 || (size_t)n >= out_size - pos) return 0;
        pos += (size_t)n;
    }

    if (event->data_len > 0 && event->data[0]) {
        int n = snprintf((char *)(out + pos), out_size - pos,
                         "data: %s\n", event->data);
        if (n < 0 || (size_t)n >= out_size - pos) return 0;
        pos += (size_t)n;
    } else if (event->data_len == 0) {
        if (pos + 1 >= out_size) return 0;
        out[pos++] = '\n';
        return pos;
    }

    if (pos + 1 >= out_size) return 0;
    out[pos++] = '\n';

    return pos;
}

static void sse_trim_newline(char *str)
{
    size_t len = strlen(str);
    while (len > 0 && (str[len - 1] == '\n' || str[len - 1] == '\r')) {
        str[--len] = '\0';
    }
}

int sse_parse_line(SSEConnection *conn, const char *line,
                   SSEEvent *event_out)
{
    if (!conn || !line) return -1;

    if (line[0] == ':' && event_out) {
        return 0;
    }

    if (line[0] == '\0') {
        if (conn->data_buffer_len == 0) return 0;

        if (event_out) {
            memset(event_out, 0, sizeof(*event_out));

            if (conn->has_event_type) {
                snprintf(event_out->event, sizeof(event_out->event),
                         "%s", conn->event_type_buffer);
            } else {
                snprintf(event_out->event, sizeof(event_out->event), "message");
            }

            memcpy(event_out->data, conn->data_buffer, conn->data_buffer_len);
            event_out->data[conn->data_buffer_len] = '\0';
            event_out->data_len = conn->data_buffer_len;

            if (conn->reconnection_time_set) {
                event_out->retry_ms = conn->reconnection_time_ms;
            }

            if (conn->last_event_id[0]) {
                snprintf(event_out->id, sizeof(event_out->id),
                         "%s", conn->last_event_id);
            }
        }

        conn->data_buffer_len = 0;
        conn->has_event_type = false;
        return 1;
    }

    if (strncmp(line, "id:", 3) == 0) {
        const char *val = line + 3;
        while (*val == ' ') val++;
        snprintf(conn->last_event_id, sizeof(conn->last_event_id), "%s", val);
        return 0;
    }

    if (strncmp(line, "event:", 6) == 0 || strncmp(line, "event: ", 7) == 0) {
        const char *val = (line[6] == ' ') ? line + 7 : line + 6;
        while (*val == ' ') val++;
        snprintf(conn->event_type_buffer, sizeof(conn->event_type_buffer),
                 "%s", val);
        conn->has_event_type = true;
        return 0;
    }

    if (strncmp(line, "retry:", 6) == 0) {
        const char *val = line + 6;
        while (*val == ' ') val++;
        conn->reconnection_time_ms = (int64_t)atoll(val);
        conn->reconnection_time_set = true;
        return 0;
    }

    if (strncmp(line, "data:", 5) == 0 || strncmp(line, "data: ", 6) == 0) {
        const char *val = (line[5] == ' ') ? line + 6 : line + 5;
        size_t vallen = strlen(val);

        if (conn->data_buffer_len > 0 &&
            conn->data_buffer_len + 1 < SSE_MAX_EVENT_DATA) {
            conn->data_buffer[conn->data_buffer_len++] = '\n';
        }

        if (conn->data_buffer_len + vallen < SSE_MAX_EVENT_DATA) {
            memcpy(conn->data_buffer + conn->data_buffer_len, val, vallen);
            conn->data_buffer_len += vallen;
        }
        return 0;
    }

    return 0;
}

int sse_parse_event(const uint8_t *data, size_t len,
                    SSEEvent *events, size_t *event_count,
                    size_t max_events)
{
    if (!data || !events || !event_count) return -1;

    SSEConnection parse_conn;
    memset(&parse_conn, 0, sizeof(parse_conn));

    *event_count = 0;
    char line_buf[SSE_MAX_EVENT_DATA];
    size_t line_pos = 0;

    for (size_t i = 0; i < len && *event_count < max_events; i++) {
        char ch = (char)data[i];

        if (ch == '\n') {
            line_buf[line_pos] = '\0';
            sse_trim_newline(line_buf);

            SSEEvent ev;
            int result = sse_parse_line(&parse_conn, line_buf, &ev);
            if (result == 1) {
                memcpy(&events[*event_count], &ev, sizeof(SSEEvent));
                (*event_count)++;
            }
            line_pos = 0;
        } else if (ch == '\r') {
            continue;
        } else {
            if (line_pos < sizeof(line_buf) - 1) {
                line_buf[line_pos++] = ch;
            }
        }
    }

    return 0;
}

void sse_set_retry(SSEConnection *conn, int64_t ms)
{
    if (!conn) return;
    conn->reconnection_time_ms = ms;
    conn->reconnection_time_set = true;
}

void sse_send_comment(uint8_t *out, size_t out_size,
                      const char *comment, size_t *written)
{
    if (!out || !comment || !written) return;
    int n = snprintf((char *)out, out_size, ": %s\n\n", comment);
    *written = (n > 0) ? (size_t)n : 0;
}

void sse_connection_close(SSEConnection *conn)
{
    if (!conn) return;
    conn->state = SSE_STATE_CLOSED;
}

bool sse_should_reconnect(const SSEConnection *conn)
{
    if (!conn) return false;
    return conn->state == SSE_STATE_CLOSED ||
           conn->state == SSE_STATE_CONNECTING;
}

void sse_server_init(SSEServer *server, size_t max_conns)
{
    if (!server) return;
    memset(server, 0, sizeof(*server));
    server->max_connections = max_conns;
    server->connections = (SSEConnection *)calloc(max_conns, sizeof(SSEConnection));
}

int sse_server_add_connection(SSEServer *server, const char *uri)
{
    if (!server || !server->connections || !uri) return -1;
    if (server->connection_count >= server->max_connections) return -2;

    sse_connection_init(&server->connections[server->connection_count], uri);
    server->connections[server->connection_count].state = SSE_STATE_OPEN;
    server->connection_count++;
    return 0;
}

int sse_server_broadcast(SSEServer *server, const SSEEvent *event,
                         uint8_t **out_buffers, size_t *out_lens,
                         size_t max_outputs)
{
    if (!server || !event || !out_buffers || !out_lens) return -1;

    size_t sent = 0;
    for (size_t i = 0; i < server->connection_count && sent < max_outputs; i++) {
        if (server->connections[i].state != SSE_STATE_OPEN) continue;
        out_lens[sent] = sse_encode_event(event, out_buffers[sent], SSE_MAX_EVENT_DATA);
        sent++;
    }

    return (int)sent;
}

void sse_server_remove_connection(SSEServer *server, size_t index)
{
    if (!server || index >= server->connection_count) return;

    sse_connection_close(&server->connections[index]);

    if (index < server->connection_count - 1) {
        memmove(&server->connections[index],
                &server->connections[index + 1],
                (server->connection_count - index - 1) * sizeof(SSEConnection));
    }
    server->connection_count--;
}

void sse_server_free(SSEServer *server)
{
    if (!server) return;
    free(server->connections);
    server->connections = NULL;
    server->connection_count = 0;
    server->max_connections = 0;
}
