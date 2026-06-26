#include "http2_frames.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

int main(void)
{
    printf("=== HTTP/2 Frame Demo ===\n\n");

    H2Connection conn;
    memset(&conn, 0, sizeof(conn));
    h2_settings_init(&conn.local_settings);
    conn.remote_settings = conn.local_settings;
    conn.connection_window = conn.local_settings.initial_window_size;

    printf("[1] Settings Exchange\n");
    printf("    Local: header_table_size=%u, max_streams=%u, window=%u\n",
           conn.local_settings.header_table_size,
           conn.local_settings.max_concurrent_streams,
           conn.local_settings.initial_window_size);

    uint8_t settings_payload[36];
    size_t  plen = h2_settings_build(&conn.local_settings,
                                     settings_payload, sizeof(settings_payload));
    printf("    Settings payload: %zu bytes\n", plen);

    uint8_t settings_frame[H2_FRAME_HEADER_SIZE + 36];
    size_t  flen = h2_frame_build(settings_frame, sizeof(settings_frame),
                                  H2_FRAME_SETTINGS, H2_FLAG_NONE, 0,
                                  settings_payload, plen);
    printf("    Settings frame: %zu bytes\n", flen);

    h2_settings_exchange(&conn);
    printf("    Exchange complete\n\n");

    printf("[2] Open Stream\n");
    uint32_t stream_id = 0;
    h2_stream_open(&conn, &stream_id);
    printf("    Stream ID: %u\n", stream_id);

    H2Stream *stream = h2_stream_get(&conn, stream_id);
    printf("    State: %d, Local Window: %u, Remote Window: %u\n\n",
           stream->state, stream->local_window, stream->remote_window);

    printf("[3] Send HEADERS Frame\n");
    H2HeaderBlock headers = {0};
    snprintf(headers.fields[0].name, sizeof(headers.fields[0].name), ":method");
    snprintf(headers.fields[0].value, sizeof(headers.fields[0].value), "GET");
    snprintf(headers.fields[1].name, sizeof(headers.fields[1].name), ":path");
    snprintf(headers.fields[1].value, sizeof(headers.fields[1].value), "/api/data");
    snprintf(headers.fields[2].name, sizeof(headers.fields[2].name), ":scheme");
    snprintf(headers.fields[2].value, sizeof(headers.fields[2].value), "https");
    snprintf(headers.fields[3].name, sizeof(headers.fields[3].name), ":authority");
    snprintf(headers.fields[3].value, sizeof(headers.fields[3].value), "example.com");
    headers.count = 4;

    uint8_t encoded_headers[4096];
    size_t  hdr_written = 0;
    h2_header_encode(&conn, &headers, encoded_headers,
                     sizeof(encoded_headers), &hdr_written);
    printf("    Encoded header block: %zu bytes\n", hdr_written);

    uint8_t header_frame[H2_FRAME_HEADER_SIZE + 4096];
    size_t hf_len = h2_frame_build(header_frame, sizeof(header_frame),
                                   H2_FRAME_HEADERS,
                                   H2_FLAG_END_HEADERS | H2_FLAG_END_STREAM,
                                   stream_id, encoded_headers, hdr_written);
    printf("    HEADERS frame: %zu bytes\n\n", hf_len);

    printf("[4] Decode Returned Headers\n");
    H2HeaderBlock decoded;
    h2_header_decode(&conn, encoded_headers, hdr_written, &decoded);
    printf("    Decoded %zu header fields:\n", decoded.count);
    for (size_t i = 0; i < decoded.count; i++) {
        printf("      %s: %s\n", decoded.fields[i].name, decoded.fields[i].value);
    }
    printf("\n");

    printf("[5] Send DATA Frame\n");
    const char *body = "{\"message\": \"Hello from HTTP/2!\"}";
    size_t body_len  = strlen(body);

    h2_send_data(&conn, stream_id, (const uint8_t *)body, body_len, true);
    printf("    Sent %zu bytes (end_stream=true)\n", body_len);
    printf("    Remaining local window: %u\n\n", stream->local_window);

    printf("[6] Flow Control\n");
    h2_flow_control_update(&conn, stream_id, 65535);
    printf("    Window update +65535 on stream %u\n", stream_id);
    printf("    New remote window: %u\n\n", stream->remote_window);

    printf("[7] Receiving Response (Simulated)\n");
    uint32_t recv_stream = stream_id + 2;
    h2_stream_open(&conn, &recv_stream);

    H2HeaderBlock resp_headers = {0};
    snprintf(resp_headers.fields[0].name, sizeof(resp_headers.fields[0].name), ":status");
    snprintf(resp_headers.fields[0].value, sizeof(resp_headers.fields[0].value), "200");
    snprintf(resp_headers.fields[1].name, sizeof(resp_headers.fields[1].name), "content-type");
    snprintf(resp_headers.fields[1].value, sizeof(resp_headers.fields[1].value), "application/json");
    resp_headers.count = 2;

    h2_send_headers(&conn, recv_stream, &resp_headers, false);

    const char *resp_body = "{\"status\": \"ok\", \"data\": [1,2,3]}";
    h2_send_data(&conn, recv_stream, (const uint8_t *)resp_body,
                 strlen(resp_body), true);
    printf("    Stream %u: HEADERS (status=200) + DATA (end_stream=true)\n", recv_stream);

    h2_stream_close(&conn, stream_id);
    h2_stream_close(&conn, recv_stream);
    printf("    Streams closed.\n\n");

    printf("[8] Frame Parse Test\n");
    H2FrameHeader parsed_hdr;
    const uint8_t *parsed_payload;
    size_t parsed_len;

    int rc = h2_frame_parse(header_frame, hf_len, &parsed_hdr,
                            &parsed_payload, &parsed_len);
    if (rc == 0) {
        printf("    OK: type=%u flags=0x%02x stream=%u payload=%zu bytes\n",
               parsed_hdr.type, parsed_hdr.flags,
               parsed_hdr.stream_id, parsed_len);
    }

    printf("\n=== Demo Complete ===\n");
    return 0;
}
