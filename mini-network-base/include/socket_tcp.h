#ifndef SOCKET_TCP_H
#define SOCKET_TCP_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#define TCP_SEND_BUF_SIZE 65536
#define TCP_RECV_BUF_SIZE 65536
#define TCP_MAX_SEGMENT_SIZE 1460
#define TCP_INITIAL_WINDOW 10
#define TCP_MAX_RETRIES 5
#define TCP_RTO_INITIAL_MS 1000

typedef enum {
    TCP_CLOSED       = 0,
    TCP_LISTEN       = 1,
    TCP_SYN_SENT     = 2,
    TCP_SYN_RECEIVED = 3,
    TCP_ESTABLISHED  = 4,
    TCP_FIN_WAIT_1   = 5,
    TCP_FIN_WAIT_2   = 6,
    TCP_CLOSING      = 7,
    TCP_TIME_WAIT    = 8,
    TCP_CLOSE_WAIT   = 9,
    TCP_LAST_ACK     = 10
} TCPState;

typedef struct {
    uint32_t src_ip;
    uint32_t dst_ip;
    uint16_t src_port;
    uint16_t dst_port;
    TCPState state;
    uint32_t seq_num;
    uint32_t ack_num;
    uint32_t remote_seq;
    uint32_t remote_ack;
    uint8_t  send_buf[TCP_SEND_BUF_SIZE];
    uint8_t  recv_buf[TCP_RECV_BUF_SIZE];
    size_t   send_buf_len;
    size_t   recv_buf_len;
    size_t   send_window;
    size_t   recv_window;
    uint16_t mss;
    bool     passive;
} TCPSocket;

TCPSocket* tcp_socket_create(void);
int        tcp_connect(TCPSocket *sock, uint32_t dst_ip, uint16_t dst_port);
int        tcp_bind_listen(TCPSocket *sock, uint16_t port);
TCPSocket* tcp_accept(TCPSocket *listen_sock);
int        tcp_send(TCPSocket *sock, const uint8_t *data, size_t len);
int        tcp_recv(TCPSocket *sock, uint8_t *buf, size_t buf_len);
int        tcp_close(TCPSocket *sock);
int        tcp_close_active(TCPSocket *sock);
int        tcp_close_passive(TCPSocket *sock);
void       tcp_socket_free(TCPSocket *sock);
const char* tcp_state_name(TCPState state);
void       tcp_print_state(TCPSocket *sock);
void       tcp_simulate_recv(TCPSocket *sock, const uint8_t *data, size_t len);

#endif
