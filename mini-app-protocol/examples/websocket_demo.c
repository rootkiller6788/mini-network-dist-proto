#include "websocket.h"

#include <stdio.h>
#include <string.h>

int main(void)
{
    printf("=== WebSocket Demo ===\n\n");

    WSConnection client_conn;
    WSConnection server_conn;

    ws_handshake_client_init(&client_conn, "echo.example.com", "/chat");
    printf("[1] Client Handshake Request\n");
    printf("    Host: %s\n", client_conn.host);
    printf("    URI:  %s\n", client_conn.uri);
    printf("    Key:  %s\n\n", client_conn.key);

    uint8_t client_handshake[1024];
    size_t  hlen = ws_handshake_build_client(&client_conn,
                                             client_handshake,
                                             sizeof(client_handshake));
    printf("    Request (%zu bytes):\n%.*s\n\n", hlen, (int)hlen, client_handshake);

    printf("[2] Server Parses Handshake\n");
    ws_handshake_server_init(&server_conn);
    ws_handshake_parse_client(client_handshake, hlen, &server_conn);
    printf("    Received key: %s\n\n", server_conn.key);

    printf("[3] Server Builds Response\n");
    uint8_t server_response[1024];
    size_t  rlen = ws_handshake_build_server(&server_conn,
                                             server_response,
                                             sizeof(server_response));
    printf("    Response (%zu bytes):\n%.*s\n\n", rlen, (int)rlen, server_response);

    printf("[4] Client Parses Server Response\n");
    ws_handshake_parse_server(server_response, rlen, &client_conn);
    printf("    Handshake done: %s\n",
           client_conn.handshake_done ? "YES" : "NO");
    printf("    Server accept: %s\n\n", client_conn.accept);

    printf("[5] Client Sends Text Message\n");
    const char *chat_text = "Hello from WebSocket!";
    uint8_t txt_frame[1024];
    size_t  txt_len = 0;

    int rc = ws_send_text(chat_text, txt_frame, sizeof(txt_frame), &txt_len);
    printf("    Encode result: %d, frame size: %zu bytes\n", rc, txt_len);

    printf("    Frame bytes: ");
    for (size_t i = 0; i < (txt_len < 32 ? txt_len : 32); i++) {
        printf("%02x ", txt_frame[i]);
    }
    printf("\n\n");

    printf("[6] Server Receives and Decodes Frame\n");
    WSFrame decoded_frame;
    size_t consumed = 0;
    rc = ws_frame_decode(txt_frame, txt_len, &decoded_frame, &consumed);
    printf("    Decode result: %d\n", rc);
    printf("    FIN=%d, Opcode=%d, Mask=%d, PayloadLen=%llu\n",
           decoded_frame.fin, decoded_frame.opcode, decoded_frame.mask,
           (unsigned long long)decoded_frame.payload_len);

    char received_text[1024];
    size_t read_count = 0;
    rc = ws_recv_text(txt_frame, txt_len, received_text,
                      sizeof(received_text), &read_count);
    printf("    Received text: \"%s\"\n", received_text);
    printf("    Bytes consumed: %zu\n\n", read_count);

    printf("[7] Server Echoes Back (PONG simulation)\n");
    uint8_t pong_frame[1024];
    size_t  pong_len = 0;
    rc = ws_send_pong(txt_frame + 2, 4, pong_frame, sizeof(pong_frame), &pong_len);
    printf("    PONG frame: %d bytes (rc=%d)\n\n", (int)pong_len, rc);

    printf("[8] Ping/Pong Exchange\n");
    const char *ping_payload = "keepalive";
    uint8_t ping_frame[1024];
    size_t  ping_len = 0;
    ws_send_ping(ping_payload, ping_frame, sizeof(ping_frame), &ping_len);
    printf("    PING sent (%zu bytes) with payload \"%s\"\n",
           ping_len, ping_payload);

    uint8_t pong_resp[1024];
    size_t  pong_resp_len = 0;
    ws_send_pong((const uint8_t *)ping_payload, strlen(ping_payload),
                 pong_resp, sizeof(pong_resp), &pong_resp_len);
    printf("    PONG response: %zu bytes\n\n", pong_resp_len);

    printf("[9] Close Frame\n");
    uint8_t close_frame[1024];
    size_t  close_len = 0;
    ws_send_close(WS_CLOSE_NORMAL, "bye", close_frame,
                  sizeof(close_frame), &close_len);
    printf("    CLOSE frame: %zu bytes (code=1000 reason=\"bye\")\n\n", close_len);

    printf("[10] SHA-1 + Base64 Verification\n");
    uint8_t test_hash[20];
    ws_sha1_hash((const uint8_t *)"test", 4, test_hash);
    char test_b64[32];
    ws_base64_encode(test_hash, 20, test_b64, sizeof(test_b64));
    printf("    SHA1(\"test\") = ");
    for (int i = 0; i < 20; i++) printf("%02x", test_hash[i]);
    printf("\n    Base64 = %s\n", test_b64);

    char accept_check[64];
    char combined[256];
    snprintf(combined, sizeof(combined), "%s%s", client_conn.key, WS_GUID);
    ws_sha1_hash((const uint8_t *)combined, strlen(combined), test_hash);
    ws_base64_encode(test_hash, 20, accept_check, sizeof(accept_check));
    printf("    Accept key check: %s (matches: %s)\n",
           accept_check,
           strcmp(accept_check, client_conn.accept) == 0 ? "YES" : "NO");

    printf("\n=== Demo Complete ===\n");
    return 0;
}
