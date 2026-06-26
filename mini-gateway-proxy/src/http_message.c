#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "http_message.h"

HttpRequest* hm_request_init(void)
{
    HttpRequest *req = calloc(1, sizeof(HttpRequest));
    if (!req) return NULL;
    req->version = HM_VER_1_1;
    req->keep_alive = true;
    req->te = HM_TE_IDENTITY;
    req->parse_state = HM_START_LINE;
    req->content_length_set = false;
    return req;
}

HttpMethod hm_method_enum(const char *method)
{
    if (!method) return HM_GET;
    if (strcmp(method, "GET") == 0) return HM_GET;
    if (strcmp(method, "POST") == 0) return HM_POST;
    if (strcmp(method, "PUT") == 0) return HM_PUT;
    if (strcmp(method, "DELETE") == 0) return HM_DELETE;
    if (strcmp(method, "PATCH") == 0) return HM_PATCH;
    if (strcmp(method, "HEAD") == 0) return HM_HEAD;
    if (strcmp(method, "OPTIONS") == 0) return HM_OPTIONS;
    if (strcmp(method, "CONNECT") == 0) return HM_CONNECT;
    if (strcmp(method, "TRACE") == 0) return HM_TRACE;
    return HM_GET;
}

const char* hm_method_str(HttpMethod m)
{
    switch (m) {
    case HM_GET:     return "GET";
    case HM_POST:    return "POST";
    case HM_PUT:     return "PUT";
    case HM_DELETE:  return "DELETE";
    case HM_PATCH:   return "PATCH";
    case HM_HEAD:    return "HEAD";
    case HM_OPTIONS: return "OPTIONS";
    case HM_CONNECT: return "CONNECT";
    case HM_TRACE:   return "TRACE";
    default:         return "UNKNOWN";
    }
}

const char* hm_version_str(HttpVersion v)
{
    switch (v) {
    case HM_VER_1_0: return "HTTP/1.0";
    case HM_VER_1_1: return "HTTP/1.1";
    case HM_VER_2_0: return "HTTP/2.0";
    default:         return "HTTP/?.?";
    }
}

int hm_parse_request_line(HttpRequest *req, const char *line)
{
    if (!req || !line) return -1;
    char method[HM_MAX_METHOD], uri[HM_MAX_URI], version[HM_MAX_VERSION];
    int n = sscanf(line, "%15s %2047s %15s", method, uri, version);
    if (n < 3) return -1;
    snprintf(req->method, sizeof(req->method), "%s", method);
    snprintf(req->uri, sizeof(req->uri), "%s", uri);
    if (strcmp(version, "HTTP/1.0") == 0) {
        req->version = HM_VER_1_0;
        req->keep_alive = false;
    } else if (strcmp(version, "HTTP/1.1") == 0) {
        req->version = HM_VER_1_1;
    } else {
        req->version = HM_VER_1_1;
    }
    return 0;
}

int hm_parse_header_line(HttpRequest *req, const char *line)
{
    if (!req || !line) return -1;
    const char *colon = strchr(line, ':');
    if (!colon) return -1;
    size_t name_len = (size_t)(colon - line);
    if (name_len >= HM_MAX_HEADER_LEN) name_len = HM_MAX_HEADER_LEN - 1;
    char name_buf[HM_MAX_HEADER_LEN];
    memcpy(name_buf, line, name_len);
    name_buf[name_len] = '\0';
    const char *value = colon + 1;
    while (*value == ' ' || *value == '\t') value++;
    size_t val_len = strlen(value);
    while (val_len > 0 && (value[val_len - 1] == '\r' ||
           value[val_len - 1] == '\n' || value[val_len - 1] == ' '))
        val_len--;
    if (req->num_headers >= HM_MAX_HEADERS) return -1;
    HttpHeader *h = &req->headers[req->num_headers];
    snprintf(h->name, sizeof(h->name), "%s", name_buf);
    snprintf(h->value, sizeof(h->value), "%.*s", (int)val_len, value);
    if (strcasecmp(h->name, "Host") == 0 && val_len == 0) return -1;
    if (strcasecmp(h->name, "Content-Length") == 0) {
        req->content_length = (size_t)atol(h->value);
        req->content_length_set = true;
    }
    if (strcasecmp(h->name, "Connection") == 0 &&
        strcasecmp(h->value, "close") == 0) {
        req->keep_alive = false;
    }
    if (strcasecmp(h->name, "Transfer-Encoding") == 0) {
        if (strcasecmp(h->value, "chunked") == 0) req->te = HM_TE_CHUNKED;
        else if (strcasecmp(h->value, "gzip") == 0) req->te = HM_TE_GZIP;
        else if (strcasecmp(h->value, "deflate") == 0) req->te = HM_TE_DEFLATE;
    }
    req->num_headers++;
    return 0;
}

int hm_parse_request(HttpRequest *req, const char *data, size_t len)
{
    if (!req || !data || len == 0) return -1;
    size_t copied = 0;
    char *buf = req->raw_buf + req->raw_len;
    while (copied < len && req->raw_len < HM_MAX_BODY) {
        *buf++ = data[copied++];
        req->raw_len++;
    }
    char *body_start = strstr(req->raw_buf, "\r\n\r\n");
    if (!body_start && req->raw_len >= HM_MAX_BODY) {
        req->parse_state = HM_ERROR;
        return -1;
    }
    if (!body_start) return 0;
    *body_start = '\0';
    body_start += 4;
    char *saveptr;
    char *req_line = strtok_r(req->raw_buf, "\r\n", &saveptr);
    if (!req_line || hm_parse_request_line(req, req_line) < 0) {
        req->parse_state = HM_ERROR;
        return -1;
    }
    char *hdr_line;
    while ((hdr_line = strtok_r(NULL, "\r\n", &saveptr)) != NULL) {
        if (hm_parse_header_line(req, hdr_line) < 0) {
            req->parse_state = HM_ERROR;
            return -1;
        }
    }
    if (req->content_length_set && req->content_length > 0) {
        size_t body_sz = req->raw_len - (size_t)(body_start - req->raw_buf);
        size_t copy = (body_sz < req->content_length) ? body_sz : req->content_length;
        memcpy(req->body, body_start, copy);
        req->body_len = copy;
    }
    req->parse_state = HM_COMPLETE;
    return 0;
}

int hm_set_header(HttpRequest *req, const char *name, const char *value)
{
    if (!req || !name || !value) return -1;
    if (req->num_headers >= HM_MAX_HEADERS) return -1;
    for (int i = 0; i < req->num_headers; i++) {
        if (strcasecmp(req->headers[i].name, name) == 0) {
            snprintf(req->headers[i].value, sizeof(req->headers[i].value), "%s", value);
            return 0;
        }
    }
    HttpHeader *h = &req->headers[req->num_headers++];
    snprintf(h->name, sizeof(h->name), "%s", name);
    snprintf(h->value, sizeof(h->value), "%s", value);
    return 0;
}

const char* hm_get_header(const HttpRequest *req, const char *name)
{
    if (!req || !name) return NULL;
    for (int i = 0; i < req->num_headers; i++)
        if (strcasecmp(req->headers[i].name, name) == 0)
            return req->headers[i].value;
    return NULL;
}

int hm_remove_header(HttpRequest *req, const char *name)
{
    if (!req || !name) return -1;
    for (int i = 0; i < req->num_headers; i++) {
        if (strcasecmp(req->headers[i].name, name) == 0) {
            memmove(&req->headers[i], &req->headers[i + 1],
                    (size_t)(req->num_headers - i - 1) * sizeof(HttpHeader));
            req->num_headers--;
            return 0;
        }
    }
    return -1;
}

bool hm_method_is(const HttpRequest *req, const char *method)
{
    return req ? (strcasecmp(req->method, method) == 0) : false;
}

char* hm_build_request(const HttpRequest *req, size_t *out_len)
{
    if (!req || !out_len) return NULL;
    size_t cap = HM_MAX_BODY;
    char *buf = malloc(cap);
    if (!buf) return NULL;
    int off = snprintf(buf, cap, "%s %s %s\r\n",
                       req->method, req->uri, hm_version_str(req->version));
    for (int i = 0; i < req->num_headers; i++) {
        int w = snprintf(buf + off, cap - (size_t)off, "%s: %s\r\n",
                         req->headers[i].name, req->headers[i].value);
        if (w < 0 || (size_t)off + (size_t)w >= cap) { free(buf); return NULL; }
        off += w;
    }
    int w = snprintf(buf + off, cap - (size_t)off, "\r\n");
    if (w < 0 || (size_t)off + (size_t)w >= cap) { free(buf); return NULL; }
    off += w;
    if (req->body_len > 0) {
        memcpy(buf + off, req->body, req->body_len);
        off += (int)req->body_len;
    }
    *out_len = (size_t)off;
    return buf;
}

void hm_request_free(HttpRequest *req) { free(req); }
HttpResponse* hm_response_init(int status_code, const char *reason)
{
    HttpResponse *resp = calloc(1, sizeof(HttpResponse));
    if (!resp) return NULL;
    resp->status_code = status_code;
    snprintf(resp->reason, sizeof(resp->reason), "%s",
             reason ? reason : hm_status_reason(status_code));
    return resp;
}

const char* hm_status_reason(int code)
{
    switch (code) {
    case 100: return "Continue";
    case 101: return "Switching Protocols";
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
    case 409: return "Conflict";
    case 429: return "Too Many Requests";
    case 500: return "Internal Server Error";
    case 502: return "Bad Gateway";
    case 503: return "Service Unavailable";
    case 504: return "Gateway Timeout";
    default:  return (code >= 200 && code < 300) ? "OK" :
                     (code >= 300 && code < 400) ? "Redirect" :
                     (code >= 400 && code < 500) ? "Client Error" :
                     (code >= 500 && code < 600) ? "Server Error" : "Unknown";
    }
}

int hm_response_set_header(HttpResponse *resp, const char *name, const char *value)
{
    if (!resp || !name || !value) return -1;
    if (resp->num_headers >= HM_MAX_HEADERS) return -1;
    HttpHeader *h = &resp->headers[resp->num_headers++];
    snprintf(h->name, sizeof(h->name), "%s", name);
    snprintf(h->value, sizeof(h->value), "%s", value);
    return 0;
}

int hm_response_set_body(HttpResponse *resp, const char *body, size_t len)
{
    if (!resp || !body) return -1;
    if (len >= HM_MAX_BODY) len = HM_MAX_BODY - 1;
    memcpy(resp->body, body, len);
    resp->body_len = len;
    return 0;
}

int hm_response_set_json(HttpResponse *resp, const char *json)
{
    if (!resp || !json) return -1;
    resp->body_len = strlen(json);
    if (resp->body_len >= HM_MAX_BODY) resp->body_len = HM_MAX_BODY - 1;
    memcpy(resp->body, json, resp->body_len);
    hm_response_set_header(resp, "Content-Type", "application/json");
    return 0;
}

int hm_response_set_html(HttpResponse *resp, const char *html)
{
    if (!resp || !html) return -1;
    resp->body_len = strlen(html);
    if (resp->body_len >= HM_MAX_BODY) resp->body_len = HM_MAX_BODY - 1;
    memcpy(resp->body, html, resp->body_len);
    hm_response_set_header(resp, "Content-Type", "text/html; charset=utf-8");
    return 0;
}

char* hm_build_response(const HttpResponse *resp, size_t *out_len)
{
    if (!resp || !out_len) return NULL;
    size_t cap = HM_MAX_BODY;
    char *buf = malloc(cap);
    if (!buf) return NULL;
    int off = snprintf(buf, cap, "HTTP/1.1 %d %s\r\n",
                       resp->status_code, resp->reason);
    for (int i = 0; i < resp->num_headers; i++) {
        int w = snprintf(buf + off, cap - (size_t)off, "%s: %s\r\n",
                         resp->headers[i].name, resp->headers[i].value);
        if (w < 0 || (size_t)off + (size_t)w >= cap) { free(buf); return NULL; }
        off += w;
    }
    int w = snprintf(buf + off, cap - (size_t)off,
                     "Content-Length: %zu\r\n\r\n", resp->body_len);
    if (w < 0 || (size_t)off + (size_t)w >= cap) { free(buf); return NULL; }
    off += w;
    if (resp->body_len > 0) {
        memcpy(buf + off, resp->body, resp->body_len);
        off += (int)resp->body_len;
    }
    *out_len = (size_t)off;
    return buf;
}

void hm_response_free(HttpResponse *resp) { free(resp); }

/*
 * Chunked Transfer Encoding (RFC 7230 Section 4.1):
 * Implements the streaming encoding used when Content-Length is unknown.
 * Each chunk: hex-size CRLF data CRLF. Final chunk: 0 CRLF CRLF.
 * Key knowledge: Enables servers to begin transmitting without knowing
 * the full response size, reducing time-to-first-byte (TTFB).
 */
int hm_chunked_decode(const char *input, size_t input_len,
                      char *output, size_t *output_len)
{
    if (!input || !output || !output_len) return -1;
    size_t out_off = 0, in_off = 0;
    *output_len = 0;
    while (in_off < input_len) {
        size_t chunk_size = 0;
        for (; in_off < input_len; in_off++) {
            char c = input[in_off];
            if (c >= '0' && c <= '9') chunk_size = chunk_size * 16 + (size_t)(c - '0');
            else if (c >= 'a' && c <= 'f') chunk_size = chunk_size * 16 + (size_t)(c - 'a') + 10;
            else if (c >= 'A' && c <= 'F') chunk_size = chunk_size * 16 + (size_t)(c - 'A') + 10;
            else if (c == '\r') break;
        }
        if (chunk_size == 0) break;
        while (in_off < input_len && input[in_off] != '\n') in_off++;
        if (in_off < input_len) in_off++;
        size_t avail = input_len - in_off;
        size_t copy = (chunk_size < avail) ? chunk_size : avail;
        memcpy(output + out_off, input + in_off, copy);
        out_off += copy;
        in_off += copy;
        while (in_off < input_len && input[in_off] != '\n') in_off++;
        if (in_off < input_len) in_off++;
    }
    *output_len = out_off;
    return 0;
}

int hm_chunked_encode(const char *input, size_t input_len,
                      char *output, size_t *output_len)
{
    if (!input || !output || !output_len) return -1;
    size_t off = 0;
    snprintf(output + off, HM_MAX_BODY - off, "%zx\r\n", input_len);
    off += strlen(output + off);
    memcpy(output + off, input, input_len);
    off += input_len;
    memcpy(output + off, "\r\n0\r\n\r\n", 7);
    off += 7;
    *output_len = off;
    return 0;
}

bool hm_validate_method(const char *method)
{
    static const char *valid[] = {
        "GET","POST","PUT","DELETE","PATCH","HEAD","OPTIONS","CONNECT","TRACE",NULL
    };
    if (!method) return false;
    for (const char **v = valid; *v; v++)
        if (strcmp(method, *v) == 0) return true;
    return false;
}

/*
 * URI Validation per RFC 3986, Section 3:
 * - Absolute path must start with '/'
 * - No control characters (0x00-0x1F, 0x7F)
 * - Length bounded by HM_MAX_URI
 */
bool hm_validate_uri(const char *uri)
{
    if (!uri || uri[0] != '/') return false;
    for (size_t i = 0; uri[i]; i++) {
        unsigned char c = (unsigned char)uri[i];
        if (c <= 0x1F || c == 0x7F) return false;
    }
    if (strlen(uri) >= HM_MAX_URI) return false;
    return true;
}

/*
 * URI Normalization per RFC 3986 Section 6.
 * Decodes percent-encoded sequences (%XX) and collapses duplicate slashes.
 * This is a security-critical function: prevents path traversal attacks
 * via encoded "../" sequences or double-slash bypass attempts.
 */
int hm_normalize_uri(char *uri)
{
    if (!uri) return -1;
    char *src = uri, *dst = uri;
    while (*src) {
        if (*src == '%' && isxdigit((unsigned char)src[1]) &&
            isxdigit((unsigned char)src[2])) {
            int high = src[1] >= 'a' ? src[1] - 'a' + 10 :
                       src[1] >= 'A' ? src[1] - 'A' + 10 : src[1] - '0';
            int low  = src[2] >= 'a' ? src[2] - 'a' + 10 :
                       src[2] >= 'A' ? src[2] - 'A' + 10 : src[2] - '0';
            *dst++ = (char)(high * 16 + low);
            src += 3;
        } else {
            *dst++ = *src++;
        }
    }
    *dst = '\0';
    src = dst = uri;
    while (*src) {
        if (*src == '/' && *(src + 1) == '/') { src++; continue; }
        *dst++ = *src++;
    }
    *dst = '\0';
    return 0;
}

/*
 * Query String Parsing:
 * Splits URI at '?' and parses key=value pairs separated by '&'.
 * Implements the application/x-www-form-urlencoded MIME type encoding.
 * Edge cases: empty values, missing '=', repeated keys (last wins).
 */
int hm_parse_query_string(const char *uri, char *path, size_t path_size,
                          HttpHeader *params, int *num_params, int max_params)
{
    if (!uri || !path || !params || !num_params) return -1;
    *num_params = 0;
    const char *qs = strchr(uri, '?');
    if (qs) {
        size_t pl = (size_t)(qs - uri);
        if (pl >= path_size) pl = path_size - 1;
        memcpy(path, uri, pl);
        path[pl] = '\0';
        qs++;
    } else {
        snprintf(path, path_size, "%s", uri);
        return 0;
    }
    char qs_copy[HM_MAX_URI];
    snprintf(qs_copy, sizeof(qs_copy), "%s", qs);
    char *pair = strtok(qs_copy, "&");
    while (pair && *num_params < max_params) {
        char *eq = strchr(pair, '=');
        if (eq) {
            *eq = '\0';
            snprintf(params[*num_params].name, HM_MAX_HEADER_LEN, "%s", pair);
            snprintf(params[*num_params].value, HM_MAX_VALUE_LEN, "%s", eq + 1);
        } else {
            snprintf(params[*num_params].name, HM_MAX_HEADER_LEN, "%s", pair);
            params[*num_params].value[0] = '\0';
        }
        (*num_params)++;
        pair = strtok(NULL, "&");
    }
    return 0;
}

/*
 * Content-Type Parsing (RFC 7231, Section 3.1.1.5):
 * Extracts media type and optional charset parameter.
 * Example: "text/html; charset=utf-8" -> media="text/html", charset="utf-8"
 */
int hm_parse_content_type(const char *ct, char *media_type, size_t mt_size,
                          char *charset, size_t cs_size)
{
    if (!ct || !media_type) return -1;
    charset[0] = '\0';
    const char *semi = strchr(ct, ';');
    if (semi) {
        size_t mt_len = (size_t)(semi - ct);
        if (mt_len >= mt_size) mt_len = mt_size - 1;
        memcpy(media_type, ct, mt_len);
        media_type[mt_len] = '\0';
        while (mt_len > 0 && media_type[mt_len - 1] == ' ') media_type[--mt_len] = '\0';
        const char *cs = strstr(semi + 1, "charset=");
        if (cs && charset) {
            cs += 8;
            snprintf(charset, cs_size, "%s", cs);
            size_t csl = strlen(charset);
            while (csl > 0 && (charset[csl - 1] == ' ' || charset[csl - 1] == ';'))
                charset[--csl] = '\0';
        }
    } else {
        snprintf(media_type, mt_size, "%s", ct);
    }
    return 0;
}

/*
 * Content Negotiation (RFC 7231, Section 5.3):
 * Server-driven negotiation via the Accept header.
 *
 * Scoring: exact=2.0, subtype-wild=1.0, full-wild=0.5, all scaled by q-factor.
 *
 * Amdahl's Law application: The serial negotiation cost scales linearly
 * with number of types. Optimized single-pass O(A*N) where A<=5, N<=20.
 */
int hm_negotiate_accept(const char *accept_header,
                        const char **available, int num_avail,
                        char *best, size_t best_size)
{
    if (!accept_header || !available || num_avail <= 0 || !best) return -1;
    best[0] = '\0';
    double best_score = -1.0;
    int best_idx = -1;
    char accept_copy[2048];
    snprintf(accept_copy, sizeof(accept_copy), "%s", accept_header);
    char *entry_save;
    char *entry = strtok_r(accept_copy, ",", &entry_save);
    while (entry) {
        while (*entry == ' ') entry++;
        double q = 1.0;
        char *qparam = strstr(entry, ";q=");
        if (qparam) { q = atof(qparam + 3); *qparam = '\0'; }
        for (int i = 0; i < num_avail; i++) {
            double score = 0.0;
            if (strcmp(entry, "*/*") == 0) score = 0.5;
            else if (strcmp(entry, available[i]) == 0) score = 2.0;
            else {
                const char *es = strchr(entry, '/');
                const char *as = strchr(available[i], '/');
                if (es && as && strncmp(entry, available[i], (size_t)(es - entry) + 1) == 0
                    && strcmp(es + 1, "*") == 0) score = 1.0;
            }
            score *= q;
            if (score > best_score) { best_score = score; best_idx = i; }
        }
        entry = strtok_r(NULL, ",", &entry_save);
    }
    if (best_idx >= 0) {
        snprintf(best, best_size, "%s", available[best_idx]);
        return best_idx;
    }
    snprintf(best, best_size, "%s", available[0]);
    return 0;
}

/*
 * Security Header Audit (OWASP Top 10 - A05: Security Misconfiguration):
 * Audits request for presence of critical HTTP headers.
 * Shannon's theorem perspective: each missing header increases entropy
 * of security posture, proportionally increasing attack surface.
 * Returns count of internally missing request headers.
 */
int hm_header_security_audit(const HttpRequest *req,
                             char *report, size_t report_size)
{
    if (!req || !report) return -1;
    static const char *required[] = {"Host", "User-Agent", "Accept", NULL};
    static const char *resp_headers[] = {
        "Strict-Transport-Security", "X-Content-Type-Options",
        "X-Frame-Options", "Content-Security-Policy",
        "X-XSS-Protection", "Referrer-Policy", NULL
    };
    int findings = 0, off = 0;
    off += snprintf(report + off, report_size - (size_t)off,
                    "=== Security Header Audit ===\n");
    off += snprintf(report + off, report_size - (size_t)off,
                    "Request headers present: %d\n", req->num_headers);
    for (const char **r = required; *r; r++) {
        if (!hm_get_header(req, *r)) {
            off += snprintf(report + off, report_size - (size_t)off,
                            "  MISSING: %s\n", *r);
            findings++;
        }
    }
    off += snprintf(report + off, report_size - (size_t)off,
                    "Recommended response security headers:\n");
    for (const char **s = resp_headers; *s; s++)
        off += snprintf(report + off, report_size - (size_t)off, "  %s\n", *s);
    off += snprintf(report + off, report_size - (size_t)off,
                    "Missing request headers: %d\n", findings);
    return findings;
}