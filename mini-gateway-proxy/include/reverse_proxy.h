#ifndef REVERSE_PROXY_H
#define REVERSE_PROXY_H

#include <stdbool.h>
#include <stdint.h>
#include <sys/socket.h>
#include <netinet/in.h>

#define RP_MAX_BACKENDS      16
#define RP_MAX_RULES         32
#define RP_BUFFER_SIZE       8192
#define RP_MAX_HEADERS       64

typedef enum {
    RPS_IDLE,
    RPS_CONNECTING,
    RPS_FORWARDING,
    RPS_CLOSING,
    RPS_ERROR
} ProxyConnState;

typedef struct {
    int    client_fd;
    int    backend_fd;
    ProxyConnState state;
    char   client_buf[RP_BUFFER_SIZE];
    char   backend_buf[RP_BUFFER_SIZE];
    size_t client_len;
    size_t backend_len;
    char   client_addr[INET6_ADDRSTRLEN];
    char   backend_addr[INET6_ADDRSTRLEN];
} ProxyConnection;

typedef struct {
    char path[256];
    char backend_host[256];
    int  backend_port;
    bool strip_path;
} ProxyRule;

typedef struct {
    int  listen_port;
    int  listen_fd;
    char backends[RP_MAX_BACKENDS][256];
    int  backend_ports[RP_MAX_BACKENDS];
    int  num_backends;
    ProxyRule rules[RP_MAX_RULES];
    int  num_rules;
} ProxyServer;

ProxyServer*      proxy_init(int port);
int               proxy_add_backend(ProxyServer *ps, const char *host, int port);
int               proxy_add_rule(ProxyServer *ps, const char *path,
                                 const char *backend_host, int backend_port);
int               proxy_handle_request(ProxyServer *ps);
int               proxy_accept_connection(ProxyServer *ps, ProxyConnection *conn);
int               proxy_connect_backend(ProxyConnection *conn,
                                        const char *host, int port);
int               proxy_pipe_data(ProxyConnection *conn);
void              proxy_close_connection(ProxyConnection *conn);
int               proxy_rewrite_headers(char *buf, size_t *len,
                                        const char *client_ip,
                                        const char *original_host);
const ProxyRule*  proxy_match_rule(const ProxyServer *ps, const char *path);
int               proxy_run(ProxyServer *ps);

#endif
