#ifndef SSE_H
#define SSE_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#define SSE_MAX_EVENT_TYPE    64
#define SSE_MAX_EVENT_DATA    4096
#define SSE_MAX_EVENT_ID      64
#define SSE_MAX_RETRY_MS      30000
#define SSE_MAX_URI           256
#define SSE_MAX_LAST_EVENT_ID 128

enum SSEState {
    SSE_STATE_CONNECTING,
    SSE_STATE_OPEN,
    SSE_STATE_CLOSED
};

typedef struct {
    char    id[SSE_MAX_EVENT_ID];
    char    event[SSE_MAX_EVENT_TYPE];
    char    data[SSE_MAX_EVENT_DATA];
    size_t  data_len;
    int64_t retry_ms;
} SSEEvent;

typedef struct {
    enum SSEState state;
    char          uri[SSE_MAX_URI];
    char          last_event_id[SSE_MAX_LAST_EVENT_ID];
    int64_t       reconnection_time_ms;
    bool          reconnection_time_set;
    char          event_type_buffer[SSE_MAX_EVENT_TYPE];
    char          data_buffer[SSE_MAX_EVENT_DATA];
    size_t        data_buffer_len;
    bool          has_event_type;
} SSEConnection;

typedef struct {
    SSEConnection *connections;
    size_t         connection_count;
    size_t         max_connections;
} SSEServer;

void    sse_connection_init(SSEConnection *conn, const char *uri);
int     sse_build_handshake(const SSEConnection *conn,
                            uint8_t *out, size_t out_size);
size_t  sse_encode_event(const SSEEvent *event,
                         uint8_t *out, size_t out_size);
int     sse_parse_event(const uint8_t *data, size_t len,
                        SSEEvent *events, size_t *event_count,
                        size_t max_events);
int     sse_parse_line(SSEConnection *conn, const char *line,
                       SSEEvent *event_out);
void    sse_set_retry(SSEConnection *conn, int64_t ms);
void    sse_send_comment(uint8_t *out, size_t out_size,
                         const char *comment, size_t *written);
void    sse_connection_close(SSEConnection *conn);
bool    sse_should_reconnect(const SSEConnection *conn);

void    sse_server_init(SSEServer *server, size_t max_conns);
int     sse_server_add_connection(SSEServer *server, const char *uri);
int     sse_server_broadcast(SSEServer *server, const SSEEvent *event,
                             uint8_t **out_buffers, size_t *out_lens,
                             size_t max_outputs);
void    sse_server_remove_connection(SSEServer *server, size_t index);
void    sse_server_free(SSEServer *server);

#endif
