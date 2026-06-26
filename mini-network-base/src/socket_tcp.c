#include "socket_tcp.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

TCPSocket* tcp_socket_create(void)
{
    TCPSocket *sock = (TCPSocket*)calloc(1, sizeof(TCPSocket));
    if (!sock) return NULL;
    sock->state = TCP_CLOSED;
    sock->mss = TCP_MAX_SEGMENT_SIZE;
    sock->send_window = TCP_INITIAL_WINDOW;
    sock->recv_window = TCP_INITIAL_WINDOW;
    sock->seq_num = (uint32_t)(rand() & 0xFFFFFFFF);
    sock->send_buf_len = 0;
    sock->recv_buf_len = 0;
    sock->passive = false;
    return sock;
}

void tcp_socket_free(TCPSocket *sock)
{
    if (sock) free(sock);
}

static int simulate_handshake_send(TCPSocket *sock, uint32_t flags)
{
    (void)flags;
    fprintf(stderr, "  [TCP] SRC=%u:%u -> DST=%u:%u seq=%u ack=%u\n",
            sock->src_ip, sock->src_port,
            sock->dst_ip, sock->dst_port,
            sock->seq_num, sock->ack_num);
    return 0;
}

int tcp_connect(TCPSocket *sock, uint32_t dst_ip, uint16_t dst_port)
{
    if (!sock) return -1;
    if (sock->state != TCP_CLOSED) return -2;
    sock->src_ip = 0x7F000001;
    sock->src_port = (uint16_t)(1024 + (rand() % 64512));
    sock->dst_ip = dst_ip;
    sock->dst_port = dst_port;
    sock->state = TCP_SYN_SENT;
    sock->remote_seq = 0;
    sock->remote_ack = 0;
    simulate_handshake_send(sock, 0);
    sock->ack_num = 1;
    sock->seq_num += 1;
    sock->state = TCP_ESTABLISHED;
    sock->send_window = TCP_INITIAL_WINDOW;
    sock->recv_window = TCP_INITIAL_WINDOW;
    fprintf(stderr, "  [TCP] Connected established. State=%s\n",
            tcp_state_name(sock->state));
    return 0;
}

int tcp_bind_listen(TCPSocket *sock, uint16_t port)
{
    if (!sock) return -1;
    if (sock->state != TCP_CLOSED) return -2;
    sock->src_ip = 0x7F000001;
    sock->src_port = port;
    sock->state = TCP_LISTEN;
    sock->passive = true;
    fprintf(stderr, "  [TCP] Listening on port %u. State=%s\n",
            port, tcp_state_name(sock->state));
    return 0;
}

TCPSocket* tcp_accept(TCPSocket *listen_sock)
{
    if (!listen_sock) return NULL;
    if (listen_sock->state != TCP_LISTEN) return NULL;
    TCPSocket *new_sock = tcp_socket_create();
    if (!new_sock) return NULL;
    new_sock->src_ip = listen_sock->src_ip;
    new_sock->src_port = listen_sock->src_port;
    new_sock->dst_ip = 0x7F000001;
    new_sock->dst_port = (uint16_t)(1024 + (rand() % 64512));
    new_sock->state = TCP_SYN_RECEIVED;
    new_sock->remote_seq = 1;
    new_sock->remote_ack = 0;
    new_sock->ack_num = 1;
    new_sock->state = TCP_ESTABLISHED;
    new_sock->passive = true;
    fprintf(stderr, "  [TCP] Accepted connection. Child state=%s\n",
            tcp_state_name(new_sock->state));
    return new_sock;
}

int tcp_send(TCPSocket *sock, const uint8_t *data, size_t len)
{
    if (!sock || !data) return -1;
    if (sock->state != TCP_ESTABLISHED &&
        sock->state != TCP_CLOSE_WAIT) return -2;
    size_t remaining = len;
    size_t offset = 0;
    size_t segments = 0;
    while (remaining > 0) {
        size_t chunk = remaining > sock->mss ? sock->mss : remaining;
        if (sock->send_buf_len + chunk <= TCP_SEND_BUF_SIZE) {
            memcpy(sock->send_buf + sock->send_buf_len, data + offset, chunk);
            sock->send_buf_len += chunk;
        }
        sock->seq_num += (uint32_t)chunk;
        remaining -= chunk;
        offset += chunk;
        segments++;
    }
    fprintf(stderr, "  [TCP] Sent %zu bytes in %zu segments. Seq=%u\n",
            len, segments, sock->seq_num);
    return (int)len;
}

int tcp_recv(TCPSocket *sock, uint8_t *buf, size_t buf_len)
{
    if (!sock || !buf) return -1;
    if (sock->state != TCP_ESTABLISHED &&
        sock->state != TCP_CLOSE_WAIT &&
        sock->state != TCP_FIN_WAIT_1 &&
        sock->state != TCP_FIN_WAIT_2) return -2;
    if (sock->recv_buf_len == 0) {
        if (sock->state == TCP_CLOSE_WAIT ||
            sock->state == TCP_FIN_WAIT_1 ||
            sock->state == TCP_FIN_WAIT_2) {
            return 0;
        }
        return 0;
    }
    size_t copy_len = sock->recv_buf_len < buf_len ?
                      sock->recv_buf_len : buf_len;
    memcpy(buf, sock->recv_buf, copy_len);
    if (copy_len < sock->recv_buf_len) {
        memmove(sock->recv_buf, sock->recv_buf + copy_len,
                sock->recv_buf_len - copy_len);
    }
    sock->recv_buf_len -= copy_len;
    sock->ack_num += (uint32_t)copy_len;
    fprintf(stderr, "  [TCP] Received %zu bytes. Ack=%u\n",
            copy_len, sock->ack_num);
    return (int)copy_len;
}

int tcp_close(TCPSocket *sock)
{
    if (!sock) return -1;
    return tcp_close_active(sock);
}

int tcp_close_active(TCPSocket *sock)
{
    if (!sock) return -1;
    if (sock->state == TCP_CLOSED) return 0;
    if (sock->state == TCP_ESTABLISHED) {
        sock->state = TCP_FIN_WAIT_1;
        simulate_handshake_send(sock, 0);
        fprintf(stderr, "  [TCP] Active close: FIN_WAIT_1 -> FIN_WAIT_2 -> TIME_WAIT\n");
        sock->state = TCP_FIN_WAIT_2;
        sock->state = TCP_TIME_WAIT;
        sock->state = TCP_CLOSED;
    } else if (sock->state == TCP_CLOSE_WAIT) {
        return tcp_close_passive(sock);
    }
    fprintf(stderr, "  [TCP] Closed. Final state=%s\n",
            tcp_state_name(sock->state));
    return 0;
}

int tcp_close_passive(TCPSocket *sock)
{
    if (!sock) return -1;
    if (sock->state == TCP_CLOSE_WAIT) {
        sock->state = TCP_LAST_ACK;
        simulate_handshake_send(sock, 0);
        sock->state = TCP_CLOSED;
        fprintf(stderr, "  [TCP] Passive close: CLOSE_WAIT -> LAST_ACK -> CLOSED\n");
    }
    return 0;
}

const char* tcp_state_name(TCPState state)
{
    switch (state) {
    case TCP_CLOSED:       return "CLOSED";
    case TCP_LISTEN:       return "LISTEN";
    case TCP_SYN_SENT:     return "SYN_SENT";
    case TCP_SYN_RECEIVED: return "SYN_RECEIVED";
    case TCP_ESTABLISHED:  return "ESTABLISHED";
    case TCP_FIN_WAIT_1:   return "FIN_WAIT_1";
    case TCP_FIN_WAIT_2:   return "FIN_WAIT_2";
    case TCP_CLOSING:      return "CLOSING";
    case TCP_TIME_WAIT:    return "TIME_WAIT";
    case TCP_CLOSE_WAIT:   return "CLOSE_WAIT";
    case TCP_LAST_ACK:     return "LAST_ACK";
    default:               return "UNKNOWN";
    }
}

void tcp_print_state(TCPSocket *sock)
{
    if (!sock) return;
    fprintf(stderr, "  [TCP Socket] SRC=%u:%u DST=%u:%u State=%s "
            "Seq=%u Ack=%u SendWin=%zu RecvWin=%zu\n",
            sock->src_ip, sock->src_port,
            sock->dst_ip, sock->dst_port,
            tcp_state_name(sock->state),
            sock->seq_num, sock->ack_num,
            sock->send_window, sock->recv_window);
}

void tcp_simulate_recv(TCPSocket *sock, const uint8_t *data, size_t len)
{
    if (!sock || !data) return;
    size_t remaining = len;
    size_t offset = 0;
    while (remaining > 0) {
        size_t chunk = remaining > sock->mss ? sock->mss : remaining;
        if (chunk > (size_t)(TCP_RECV_BUF_SIZE - (int)sock->recv_buf_len))
            chunk = (size_t)(TCP_RECV_BUF_SIZE - (int)sock->recv_buf_len);
        memcpy(sock->recv_buf + sock->recv_buf_len, data + offset, chunk);
        sock->recv_buf_len += chunk;
        sock->remote_seq += (uint32_t)chunk;
        remaining -= chunk;
        offset += chunk;
    }
}
