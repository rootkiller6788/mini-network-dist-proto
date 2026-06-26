#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/select.h>
#include <arpa/inet.h>
#include <netdb.h>

#include "reverse_proxy.h"

ProxyServer* proxy_init(int port)
{
    ProxyServer *ps = calloc(1, sizeof(ProxyServer));
    if (!ps) return NULL;

    ps->listen_port = port;
    ps->num_backends = 0;
    ps->num_rules = 0;

    ps->listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (ps->listen_fd < 0) {
        free(ps);
        return NULL;
    }

    int opt = 1;
    setsockopt(ps->listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons((uint16_t)port);

    if (bind(ps->listen_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        close(ps->listen_fd);
        free(ps);
        return NULL;
    }

    if (listen(ps->listen_fd, 128) < 0) {
        close(ps->listen_fd);
        free(ps);
        return NULL;
    }

    printf("[proxy] Listening on port %d\n", port);
    return ps;
}

int proxy_add_backend(ProxyServer *ps, const char *host, int port)
{
    if (!ps || ps->num_backends >= RP_MAX_BACKENDS) return -1;
    snprintf(ps->backends[ps->num_backends], 256, "%s", host);
    ps->backend_ports[ps->num_backends] = port;
    ps->num_backends++;
    return 0;
}

int proxy_add_rule(ProxyServer *ps, const char *path,
                   const char *backend_host, int backend_port)
{
    if (!ps || ps->num_rules >= RP_MAX_RULES) return -1;
    ProxyRule *r = &ps->rules[ps->num_rules];
    snprintf(r->path, sizeof(r->path), "%s", path);
    snprintf(r->backend_host, sizeof(r->backend_host), "%s", backend_host);
    r->backend_port = backend_port;
    r->strip_path = false;
    ps->num_rules++;
    return 0;
}

const ProxyRule* proxy_match_rule(const ProxyServer *ps, const char *path)
{
    if (!ps || !path) return NULL;
    int best_match_len = 0;
    const ProxyRule *best = NULL;

    for (int i = 0; i < ps->num_rules; i++) {
        size_t rule_len = strlen(ps->rules[i].path);
        const char *rule_path = ps->rules[i].path;

        if (strncmp(path, rule_path, rule_len) == 0) {
            if ((int)rule_len > best_match_len) {
                best = &ps->rules[i];
                best_match_len = (int)rule_len;
            }
        }
    }
    return best;
}

int proxy_rewrite_headers(char *buf, size_t *len,
                          const char *client_ip,
                          const char *original_host)
{
    if (!buf || !len) return -1;

    char temp[RP_BUFFER_SIZE];
    size_t temp_len = 0;
    char *line = buf;
    char *end = buf + *len;
    bool host_set = false;
    bool xff_set = false;

    while (line < end) {
        char *nl = memchr(line, '\r', (size_t)(end - line));
        if (!nl) break;
        size_t line_len = (size_t)(nl - line);

        if (line_len == 0) {
            memcpy(temp + temp_len, "\r\n", 2);
            temp_len += 2;
            line = nl + 2;
            continue;
        }

        if (strncmp(line, "Host:", 5) == 0 && original_host) {
            int w = snprintf(temp + temp_len,
                             RP_BUFFER_SIZE - (int)temp_len,
                             "Host: %s\r\n", original_host);
            temp_len += w;
            host_set = true;
        } else if (strncmp(line, "X-Forwarded-For:", 16) == 0 && client_ip) {
            int w = snprintf(temp + temp_len,
                             RP_BUFFER_SIZE - (int)temp_len,
                             "X-Forwarded-For: %s\r\n", client_ip);
            temp_len += w;
            xff_set = true;
        } else {
            memcpy(temp + temp_len, line, line_len);
            temp_len += line_len;
            temp[temp_len++] = '\r';
            temp[temp_len++] = '\n';
        }

        line = nl + 2;
    }

    if (!host_set && original_host) {
        int w = snprintf(temp + temp_len,
                         RP_BUFFER_SIZE - (int)temp_len,
                         "Host: %s\r\n", original_host);
        temp_len += w;
    }
    if (!xff_set && client_ip) {
        int w = snprintf(temp + temp_len,
                         RP_BUFFER_SIZE - (int)temp_len,
                         "X-Forwarded-For: %s\r\n", client_ip);
        temp_len += w;
    }

    memcpy(buf, temp, temp_len);
    *len = temp_len;
    return 0;
}

int proxy_connect_backend(ProxyConnection *conn, const char *host, int port)
{
    if (!conn || !host) return -1;

    conn->backend_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (conn->backend_fd < 0) return -1;

    struct hostent *he = gethostbyname(host);
    if (!he) {
        close(conn->backend_fd);
        conn->backend_fd = -1;
        return -1;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    memcpy(&addr.sin_addr, he->h_addr_list[0], (size_t)he->h_length);
    addr.sin_port = htons((uint16_t)port);

    /* non-blocking connect for timeout support */
    int flags = fcntl(conn->backend_fd, F_GETFL, 0);
    fcntl(conn->backend_fd, F_SETFL, flags | O_NONBLOCK);

    int ret = connect(conn->backend_fd, (struct sockaddr*)&addr, sizeof(addr));
    if (ret < 0 && errno != EINPROGRESS) {
        close(conn->backend_fd);
        conn->backend_fd = -1;
        return -1;
    }

    /* wait for connection with select timeout */
    fd_set wfds;
    FD_ZERO(&wfds);
    FD_SET(conn->backend_fd, &wfds);
    struct timeval tv = {5, 0};

    ret = select(conn->backend_fd + 1, NULL, &wfds, NULL, &tv);
    if (ret <= 0) {
        close(conn->backend_fd);
        conn->backend_fd = -1;
        return -1;
    }

    /* restore blocking */
    fcntl(conn->backend_fd, F_SETFL, flags);

    strncpy(conn->backend_addr, host, INET6_ADDRSTRLEN - 1);
    printf("[proxy] Connected to backend %s:%d (fd=%d)\n",
           host, port, conn->backend_fd);
    return 0;
}

int proxy_accept_connection(ProxyServer *ps, ProxyConnection *conn)
{
    if (!ps || !conn) return -1;

    struct sockaddr_in client_addr;
    socklen_t addr_len = sizeof(client_addr);

    conn->client_fd = accept(ps->listen_fd,
                             (struct sockaddr*)&client_addr, &addr_len);
    if (conn->client_fd < 0) return -1;

    inet_ntop(AF_INET, &client_addr.sin_addr,
              conn->client_addr, sizeof(conn->client_addr));
    conn->state = RPS_IDLE;
    conn->backend_fd = -1;
    conn->client_len = 0;
    conn->backend_len = 0;

    printf("[proxy] Accepted client %s (fd=%d)\n",
           conn->client_addr, conn->client_fd);
    return 0;
}

int proxy_pipe_data(ProxyConnection *conn)
{
    if (!conn) return -1;
    if (conn->state != RPS_FORWARDING) return -1;

    fd_set rfds;
    int max_fd = (conn->client_fd > conn->backend_fd)
                  ? conn->client_fd : conn->backend_fd;
    int both_closed = 0;

    while (conn->client_fd >= 0 || conn->backend_fd >= 0) {
        FD_ZERO(&rfds);
        if (conn->client_fd >= 0) FD_SET(conn->client_fd, &rfds);
        if (conn->backend_fd >= 0) FD_SET(conn->backend_fd, &rfds);

        struct timeval tv = {30, 0};
        int ret = select(max_fd + 1, &rfds, NULL, NULL, &tv);
        if (ret < 0) break;
        if (ret == 0) break;

        if (conn->client_fd >= 0 && FD_ISSET(conn->client_fd, &rfds)) {
            ssize_t n = read(conn->client_fd, conn->client_buf,
                             RP_BUFFER_SIZE);
            if (n <= 0) {
                close(conn->client_fd);
                conn->client_fd = -1;
                both_closed++;
            } else if (conn->backend_fd >= 0) {
                write(conn->backend_fd, conn->client_buf, (size_t)n);
            }
        }

        if (conn->backend_fd >= 0 && FD_ISSET(conn->backend_fd, &rfds)) {
            ssize_t n = read(conn->backend_fd, conn->backend_buf,
                             RP_BUFFER_SIZE);
            if (n <= 0) {
                close(conn->backend_fd);
                conn->backend_fd = -1;
                both_closed++;
            } else if (conn->client_fd >= 0) {
                write(conn->client_fd, conn->backend_buf, (size_t)n);
            }
        }

        if (both_closed >= 2) break;
    }

    return 0;
}

int proxy_handle_request(ProxyServer *ps)
{
    if (!ps) return -1;

    ProxyConnection conn;
    memset(&conn, 0, sizeof(conn));

    if (proxy_accept_connection(ps, &conn) < 0) return -1;

    ssize_t n = read(conn.client_fd, conn.client_buf, RP_BUFFER_SIZE - 1);
    if (n <= 0) {
        close(conn.client_fd);
        return -1;
    }
    conn.client_buf[n] = '\0';
    conn.client_len = (size_t)n;

    /* parse request line for path */
    char method[16] = {0};
    char path[1024] = {0};
    sscanf(conn.client_buf, "%15s %1023s", method, path);

    /* match rule */
    const ProxyRule *rule = proxy_match_rule(ps, path);
    const char *backend_host;
    int backend_port;

    if (rule) {
        backend_host = rule->backend_host;
        backend_port = rule->backend_port;
    } else if (ps->num_backends > 0) {
        backend_host = ps->backends[0];
        backend_port = ps->backend_ports[0];
    } else {
        const char *resp = "HTTP/1.1 502 Bad Gateway\r\n"
                           "Content-Length: 0\r\n\r\n";
        write(conn.client_fd, resp, strlen(resp));
        close(conn.client_fd);
        return -1;
    }

    /* rewrite headers */
    proxy_rewrite_headers(conn.client_buf, &conn.client_len,
                          conn.client_addr, backend_host);

    /* connect to backend */
    if (proxy_connect_backend(&conn, backend_host, backend_port) < 0) {
        const char *resp = "HTTP/1.1 502 Bad Gateway\r\n"
                           "Content-Length: 0\r\n\r\n";
        write(conn.client_fd, resp, strlen(resp));
        close(conn.client_fd);
        return -1;
    }

    /* forward request to backend */
    write(conn.backend_fd, conn.client_buf, conn.client_len);

    /* forward backend response to client */
    conn.state = RPS_FORWARDING;
    proxy_pipe_data(&conn);

    proxy_close_connection(&conn);
    return 0;
}

void proxy_close_connection(ProxyConnection *conn)
{
    if (!conn) return;
    if (conn->client_fd >= 0) {
        shutdown(conn->client_fd, SHUT_RDWR);
        close(conn->client_fd);
        conn->client_fd = -1;
    }
    if (conn->backend_fd >= 0) {
        shutdown(conn->backend_fd, SHUT_RDWR);
        close(conn->backend_fd);
        conn->backend_fd = -1;
    }
    conn->state = RPS_CLOSING;
}

int proxy_run(ProxyServer *ps)
{
    if (!ps) return -1;
    printf("[proxy] Starting accept loop on port %d\n", ps->listen_port);
    while (1) {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(ps->listen_fd, &rfds);
        int ret = select(ps->listen_fd + 1, &rfds, NULL, NULL, NULL);
        if (ret < 0) break;
        if (FD_ISSET(ps->listen_fd, &rfds)) {
            proxy_handle_request(ps);
        }
    }
    return 0;
}
