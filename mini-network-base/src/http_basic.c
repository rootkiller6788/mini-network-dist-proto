#include "http_basic.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>

static int str_case_cmp(const char *a, const char *b)
{
    if (!a || !b) return 0;
    while (*a && *b) {
        int ca = tolower((unsigned char)*a);
        int cb = tolower((unsigned char)*b);
        if (ca != cb) return ca - cb;
        a++; b++;
    }
    return tolower((unsigned char)*a) - tolower((unsigned char)*b);
}

const char* http_method_str(HTTPMethod method)
{
    switch (method) {
    case HTTP_M_GET:     return HTTP_METHOD_GET;
    case HTTP_M_POST:    return HTTP_METHOD_POST;
    case HTTP_M_PUT:     return HTTP_METHOD_PUT;
    case HTTP_M_DELETE:  return HTTP_METHOD_DELETE;
    case HTTP_M_HEAD:    return HTTP_METHOD_HEAD;
    case HTTP_M_OPTIONS: return HTTP_METHOD_OPTIONS;
    case HTTP_M_PATCH:   return HTTP_METHOD_PATCH;
    default:             return "UNKNOWN";
    }
}

HTTPMethod http_method_from_str(const char *str)
{
    if (!str) return HTTP_M_GET;
    if (strcmp(str, "GET") == 0)     return HTTP_M_GET;
    if (strcmp(str, "POST") == 0)    return HTTP_M_POST;
    if (strcmp(str, "PUT") == 0)     return HTTP_M_PUT;
    if (strcmp(str, "DELETE") == 0)  return HTTP_M_DELETE;
    if (strcmp(str, "HEAD") == 0)    return HTTP_M_HEAD;
    if (strcmp(str, "OPTIONS") == 0) return HTTP_M_OPTIONS;
    if (strcmp(str, "PATCH") == 0)   return HTTP_M_PATCH;
    return HTTP_M_GET;
}

const char* http_status_str(uint16_t code)
{
    switch (code) {
    case 200: return "OK";
    case 201: return "Created";
    case 204: return "No Content";
    case 301: return "Moved Permanently";
    case 302: return "Found";
    case 304: return "Not Modified";
    case 400: return "Bad Request";
    case 401: return "Unauthorized";
    case 403: return "Forbidden";
    case 404: return "Not Found";
    case 405: return "Method Not Allowed";
    case 408: return "Request Timeout";
    case 500: return "Internal Server Error";
    case 502: return "Bad Gateway";
    case 503: return "Service Unavailable";
    default:  return "Unknown";
    }
}

static int skip_crlf(const uint8_t *data, size_t len, size_t *pos)
{
    while (*pos < len && (data[*pos] == '\r' || data[*pos] == '\n'))
        (*pos)++;
    return 0;
}

static int parse_request_line(const uint8_t *data, size_t len,
                               size_t *pos, HTTPRequest *req)
{
    char method_str[HTTP_MAX_METHOD_LEN] = {0};
    size_t mi = 0;
    while (*pos < len && data[*pos] != ' ' && mi < sizeof(method_str) - 1) {
        method_str[mi++] = (char)data[(*pos)++];
    }
    if (*pos < len && data[*pos] == ' ') (*pos)++;
    req->method = http_method_from_str(method_str);
    size_t ui = 0;
    while (*pos < len && data[*pos] != ' ' && ui < sizeof(req->uri) - 1) {
        req->uri[ui++] = (char)data[(*pos)++];
    }
    if (*pos < len && data[*pos] == ' ') (*pos)++;
    size_t vi = 0;
    while (*pos < len && data[*pos] != '\r' && data[*pos] != '\n'
           && vi < sizeof(req->version) - 1) {
        req->version[vi++] = (char)data[(*pos)++];
    }
    skip_crlf(data, len, pos);
    return 0;
}

static int parse_status_line(const uint8_t *data, size_t len,
                              size_t *pos, HTTPResponse *resp)
{
    size_t vi = 0;
    while (*pos < len && data[*pos] != ' ' && vi < sizeof(resp->version) - 1) {
        resp->version[vi++] = (char)data[(*pos)++];
    }
    if (*pos < len && data[*pos] == ' ') (*pos)++;
    char code_str[8] = {0};
    size_t ci = 0;
    while (*pos < len && isdigit((int)data[*pos]) && ci < sizeof(code_str) - 1) {
        code_str[ci++] = (char)data[(*pos)++];
    }
    resp->status_code = (uint16_t)atoi(code_str);
    if (*pos < len && data[*pos] == ' ') (*pos)++;
    size_t ri = 0;
    while (*pos < len && data[*pos] != '\r' && data[*pos] != '\n'
           && ri < sizeof(resp->reason) - 1) {
        resp->reason[ri++] = (char)data[(*pos)++];
    }
    skip_crlf(data, len, pos);
    return 0;
}

static int parse_header_lines(const uint8_t *data, size_t len,
                               size_t *pos,
                               HTTPHeader *headers, size_t *header_count,
                               size_t max_headers)
{
    *header_count = 0;
    while (*pos < len) {
        if (*pos + 1 < len && data[*pos] == '\r' && data[*pos + 1] == '\n') {
            *pos += 2;
            break;
        }
        if (*pos < len && data[*pos] == '\n') {
            (*pos)++;
            break;
        }
        if (*header_count >= max_headers) break;
        HTTPHeader *h = &headers[*header_count];
        size_t ki = 0;
        while (*pos < len && data[*pos] != ':' && ki < sizeof(h->key) - 1) {
            h->key[ki++] = (char)data[(*pos)++];
        }
        h->key[ki] = '\0';
        if (*pos < len && data[*pos] == ':') (*pos)++;
        while (*pos < len && data[*pos] == ' ') (*pos)++;
        size_t vi = 0;
        while (*pos < len && data[*pos] != '\r' && data[*pos] != '\n'
               && vi < sizeof(h->value) - 1) {
            h->value[vi++] = (char)data[(*pos)++];
        }
        h->value[vi] = '\0';
        skip_crlf(data, len, pos);
        (*header_count)++;
    }
    return 0;
}

int http_parse_request(const uint8_t *data, size_t len, HTTPRequest *req)
{
    if (!data || !req) return -1;
    if (len == 0) return -2;
    memset(req, 0, sizeof(HTTPRequest));
    size_t pos = 0;
    parse_request_line(data, len, &pos, req);
    parse_header_lines(data, len, &pos, req->headers,
                        &req->header_count, HTTP_MAX_HEADERS);
    const char *cl = http_get_header(req->headers, req->header_count,
                                      "Content-Length");
    if (cl) {
        size_t body_len = (size_t)atoi(cl);
        if (pos + body_len <= len && body_len < HTTP_MAX_BODY) {
            memcpy(req->body, data + pos, body_len);
            req->body_len = body_len;
            req->has_body = true;
        }
    }
    return 0;
}

int http_parse_headers(const uint8_t *data, size_t len,
                       HTTPHeader *headers, size_t *header_count,
                       size_t *body_offset)
{
    if (!data || !headers || !header_count) return -1;
    size_t pos = 0;
    while (pos < len && data[pos] != '\r' && data[pos] != '\n') pos++;
    skip_crlf(data, len, &pos);
    parse_header_lines(data, len, &pos, headers,
                        header_count, HTTP_MAX_HEADERS);
    if (body_offset) *body_offset = pos;
    return 0;
}

const char* http_get_header(const HTTPHeader *headers, size_t count,
                            const char *key)
{
    if (!headers || !key) return NULL;
    for (size_t i = 0; i < count; i++) {
        if (str_case_cmp(headers[i].key, key) == 0)
            return headers[i].value;
    }
    return NULL;
}

int http_add_header(HTTPRequest *req, const char *key, const char *value)
{
    if (!req || !key || !value) return -1;
    if (req->header_count >= HTTP_MAX_HEADERS) return -2;
    HTTPHeader *h = &req->headers[req->header_count];
    strncpy(h->key, key, sizeof(h->key) - 1);
    h->key[sizeof(h->key) - 1] = '\0';
    strncpy(h->value, value, sizeof(h->value) - 1);
    h->value[sizeof(h->value) - 1] = '\0';
    req->header_count++;
    return 0;
}

int http_response_add_header(HTTPResponse *resp, const char *key,
                              const char *value)
{
    if (!resp || !key || !value) return -1;
    if (resp->header_count >= HTTP_MAX_HEADERS) return -2;
    HTTPHeader *h = &resp->headers[resp->header_count];
    strncpy(h->key, key, sizeof(h->key) - 1);
    h->key[sizeof(h->key) - 1] = '\0';
    strncpy(h->value, value, sizeof(h->value) - 1);
    h->value[sizeof(h->value) - 1] = '\0';
    resp->header_count++;
    return 0;
}

void http_response_set_defaults(HTTPResponse *resp, uint16_t code)
{
    if (!resp) return;
    memset(resp, 0, sizeof(HTTPResponse));
    resp->status_code = code;
    strncpy(resp->version, HTTP_1_1, sizeof(resp->version) - 1);
    strncpy(resp->reason, http_status_str(code), sizeof(resp->reason) - 1);
    http_response_add_header(resp, "Server", "mini-http/1.0");
    http_response_add_header(resp, "Connection", "close");
}

int http_build_response(HTTPResponse *resp, uint8_t *buf, size_t *buf_len)
{
    if (!resp || !buf || !buf_len) return -1;
    size_t offset = 0;
    int written = snprintf((char*)buf + offset, *buf_len - offset,
                            "%s %u %s\r\n",
                            resp->version, resp->status_code, resp->reason);
    if (written < 0) return -2;
    offset += (size_t)written;
    char body_len_str[32] = {0};
    if (resp->has_body || resp->body_len > 0) {
        snprintf(body_len_str, sizeof(body_len_str), "%zu", resp->body_len);
    }
    for (size_t i = 0; i < resp->header_count; i++) {
        written = snprintf((char*)buf + offset, *buf_len - offset,
                            "%s: %s\r\n",
                            resp->headers[i].key, resp->headers[i].value);
        if (written < 0) return -2;
        offset += (size_t)written;
    }
    if (resp->has_body || resp->body_len > 0) {
        written = snprintf((char*)buf + offset, *buf_len - offset,
                            "Content-Length: %s\r\n", body_len_str);
        if (written < 0) return -2;
        offset += (size_t)written;
    }
    written = snprintf((char*)buf + offset, *buf_len - offset, "\r\n");
    if (written < 0) return -2;
    offset += (size_t)written;
    if ((resp->has_body || resp->body_len > 0) && resp->body) {
        if (offset + resp->body_len <= *buf_len) {
            memcpy(buf + offset, resp->body, resp->body_len);
            offset += resp->body_len;
        }
    }
    *buf_len = offset;
    return 0;
}

int http_chunked_decode(const uint8_t *data, size_t len,
                        uint8_t *decoded, size_t *decoded_len)
{
    if (!data || !decoded || !decoded_len) return -1;
    size_t pos = 0;
    size_t out = 0;
    while (pos < len) {
        if (pos + 1 < len && data[pos] == '\r' && data[pos + 1] == '\n') {
            pos += 2;
            continue;
        }
        char hex[16] = {0};
        size_t hi = 0;
        while (pos < len && data[pos] != '\r' && hi < sizeof(hex) - 1) {
            hex[hi++] = (char)data[pos++];
        }
        unsigned long chunk_size = strtoul(hex, NULL, 16);
        if (chunk_size == 0) break;
        if (*pos < len) pos++;
        if (*pos < len && data[pos] == '\n') pos++;
        if (pos + chunk_size > len) return -2;
        if (out + chunk_size > *decoded_len) return -3;
        memcpy(decoded + out, data + pos, chunk_size);
        out += chunk_size;
        pos += chunk_size;
        if (pos + 1 < len && data[pos] == '\r' && data[pos + 1] == '\n')
            pos += 2;
    }
    *decoded_len = out;
    return 0;
}

void http_print_message(const uint8_t *data, size_t len)
{
    if (!data) return;
    fprintf(stderr, "  [HTTP Message] %zu bytes:\n", len);
    fprintf(stderr, "%.*s\n", (int)len, data);
}

void http_print_request(const HTTPRequest *req)
{
    if (!req) return;
    fprintf(stderr, "  [HTTP Request] %s %s %s\n",
            http_method_str(req->method), req->uri, req->version);
    for (size_t i = 0; i < req->header_count; i++) {
        fprintf(stderr, "    %s: %s\n",
                req->headers[i].key, req->headers[i].value);
    }
    if (req->has_body) {
        fprintf(stderr, "    [Body: %zu bytes]\n", req->body_len);
    }
}

void http_print_response(const HTTPResponse *resp)
{
    if (!resp) return;
    fprintf(stderr, "  [HTTP Response] %u %s %s\n",
            resp->status_code, resp->reason, resp->version);
    for (size_t i = 0; i < resp->header_count; i++) {
        fprintf(stderr, "    %s: %s\n",
                resp->headers[i].key, resp->headers[i].value);
    }
}
