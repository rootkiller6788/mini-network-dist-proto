#ifndef HTTP_BASIC_H
#define HTTP_BASIC_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#define HTTP_MAX_URI       2048
#define HTTP_MAX_HEADERS   16
#define HTTP_MAX_KEY_LEN   128
#define HTTP_MAX_VALUE_LEN 1024
#define HTTP_MAX_BODY      65536
#define HTTP_VERSION_LEN   16
#define HTTP_MAX_METHOD_LEN 16
#define HTTP_MAX_REASON_LEN 64

#define HTTP_1_0 "HTTP/1.0"
#define HTTP_1_1 "HTTP/1.1"

#define HTTP_METHOD_GET     "GET"
#define HTTP_METHOD_POST    "POST"
#define HTTP_METHOD_PUT     "PUT"
#define HTTP_METHOD_DELETE  "DELETE"
#define HTTP_METHOD_HEAD    "HEAD"
#define HTTP_METHOD_OPTIONS "OPTIONS"
#define HTTP_METHOD_PATCH   "PATCH"
#define HTTP_METHOD_CONNECT "CONNECT"
#define HTTP_METHOD_TRACE   "TRACE"

typedef enum {
    HTTP_M_GET     = 0,
    HTTP_M_POST    = 1,
    HTTP_M_PUT     = 2,
    HTTP_M_DELETE  = 3,
    HTTP_M_HEAD    = 4,
    HTTP_M_OPTIONS = 5,
    HTTP_M_PATCH   = 6
} HTTPMethod;

typedef struct {
    char key[HTTP_MAX_KEY_LEN];
    char value[HTTP_MAX_VALUE_LEN];
} HTTPHeader;

typedef struct {
    HTTPMethod method;
    char       uri[HTTP_MAX_URI];
    char       version[HTTP_VERSION_LEN];
    HTTPHeader headers[HTTP_MAX_HEADERS];
    size_t     header_count;
    uint8_t    body[HTTP_MAX_BODY];
    size_t     body_len;
    bool       has_body;
} HTTPRequest;

typedef struct {
    uint16_t    status_code;
    char        reason[HTTP_MAX_REASON_LEN];
    char        version[HTTP_VERSION_LEN];
    HTTPHeader  headers[HTTP_MAX_HEADERS];
    size_t      header_count;
    uint8_t     body[HTTP_MAX_BODY];
    size_t      body_len;
    bool        has_body;
} HTTPResponse;

int         http_parse_request(const uint8_t *data, size_t len,
                               HTTPRequest *req);
int         http_build_response(HTTPResponse *resp,
                                uint8_t *buf, size_t *buf_len);
int         http_parse_headers(const uint8_t *data, size_t len,
                               HTTPHeader *headers, size_t *header_count,
                               size_t *body_offset);
int         http_chunked_decode(const uint8_t *data, size_t len,
                                uint8_t *decoded, size_t *decoded_len);
void        http_print_message(const uint8_t *data, size_t len);
void        http_print_request(const HTTPRequest *req);
void        http_print_response(const HTTPResponse *resp);

const char* http_method_str(HTTPMethod method);
HTTPMethod  http_method_from_str(const char *str);
const char* http_status_str(uint16_t code);
int         http_add_header(HTTPRequest *req, const char *key, const char *value);
int         http_response_add_header(HTTPResponse *resp, const char *key,
                                     const char *value);
const char* http_get_header(const HTTPHeader *headers, size_t count,
                            const char *key);
void        http_response_set_defaults(HTTPResponse *resp, uint16_t code);

#endif
