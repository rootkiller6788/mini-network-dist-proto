#include "socket_tcp.h"
#include "http_basic.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

int main(void)
{
    printf("=== mini-network-base: TCP Demo ===\n\n");
    printf("Simulating a TCP connection on port 8080...\n\n");

    TCPSocket *server = tcp_socket_create();
    if (!server) {
        fprintf(stderr, "Failed to create server socket\n");
        return 1;
    }
    printf("[Server] Socket created.\n");
    tcp_print_state(server);
    printf("\n");

    if (tcp_bind_listen(server, 8080) != 0) {
        fprintf(stderr, "Failed to bind\n");
        return 1;
    }
    printf("[Server] Bound to port 8080. State=%s\n",
           tcp_state_name(TCP_LISTEN));
    printf("\n");

    TCPSocket *client = tcp_socket_create();
    if (!client) {
        fprintf(stderr, "Failed to create client socket\n");
        return 1;
    }
    printf("[Client] Socket created.\n");
    printf("\n");

    printf("[Client] Initiating connection to 127.0.0.1:8080...\n");
    if (tcp_connect(client, 0x7F000001, 8080) != 0) {
        fprintf(stderr, "Connection failed\n");
        return 1;
    }
    printf("[Client] 3-way handshake simulated.\n");
    tcp_print_state(client);
    printf("\n");

    TCPSocket *child = tcp_accept(server);
    if (!child) {
        fprintf(stderr, "Accept failed\n");
        return 1;
    }
    printf("[Server] Accepted connection.\n");
    tcp_print_state(child);
    printf("\n");

    const char *request =
        "GET /index.html HTTP/1.1\r\n"
        "Host: example.com\r\n"
        "User-Agent: mini-tcp/1.0\r\n"
        "Accept: */*\r\n"
        "\r\n";
    printf("[Client] Sending HTTP request (%zu bytes)...\n", strlen(request));
    if (tcp_send(client, (const uint8_t*)request, strlen(request)) < 0) {
        fprintf(stderr, "Send failed\n");
        return 1;
    }
    tcp_simulate_recv(child, (const uint8_t*)request, strlen(request));
    printf("\n");

    printf("[Server] Parsing received HTTP request...\n");
    HTTPRequest req;
    if (http_parse_request((const uint8_t*)request, strlen(request),
                           &req) == 0) {
        http_print_request(&req);
    }
    printf("\n");

    const char *response_body =
        "<html><body><h1>Hello World!</h1></body></html>";
    HTTPResponse resp;
    http_response_set_defaults(&resp, 200);
    http_response_add_header(&resp, "Content-Type", "text/html");
    memcpy(resp.body, response_body, strlen(response_body));
    resp.body_len = strlen(response_body);
    resp.has_body = true;

    uint8_t resp_buf[4096];
    size_t resp_len = sizeof(resp_buf);
    if (http_build_response(&resp, resp_buf, &resp_len) == 0) {
        printf("[Server] Sending HTTP response (%zu bytes)...\n", resp_len);
        if (tcp_send(child, resp_buf, resp_len) < 0) {
            fprintf(stderr, "Server send failed\n");
            return 1;
        }
        tcp_simulate_recv(client, resp_buf, resp_len);
        printf("\n");
    }

    printf("[Client] Received response:\n");
    http_print_message(resp_buf, resp_len);
    printf("\n");

    printf("[Client] Closing connection (active close)...\n");
    tcp_close(client);
    tcp_print_state(client);
    printf("\n");

    child->state = TCP_CLOSE_WAIT;
    printf("[Server] Closing connection (passive close)...\n");
    tcp_close(child);
    tcp_print_state(child);
    printf("\n");

    printf("=== TCP Demo Complete ===\n");
    printf("State transitions observed: CLOSED->SYN_SENT->ESTABLISHED\n");
    printf("                             CLOSED->LISTEN->ESTABLISHED\n");
    printf("4-way close: ESTABLISHED->FIN_WAIT_1->FIN_WAIT_2->TIME_WAIT->CLOSED\n");
    printf("             ESTABLISHED->CLOSE_WAIT->LAST_ACK->CLOSED\n");

    tcp_socket_free(client);
    tcp_socket_free(child);
    tcp_socket_free(server);

    return 0;
}
