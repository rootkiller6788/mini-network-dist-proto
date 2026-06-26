#ifndef HTTP_MESSAGE_H
#define HTTP_MESSAGE_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#define HM_MAX_HEADERS     64
#define HM_MAX_HEADER_LEN  256
#define HM_MAX_VALUE_LEN   1024
#define HM_MAX_URI         2048
#define HM_MAX_METHOD      16
#define HM_MAX_VERSION     16
#define HM_MAX_BODY        (64 * 1024)

typedef enum {
    HM_GET,
    HM_POST,
    HM_PUT,
    HM_DELETE,
    HM_PATCH,
    HM_HEAD,
    HM_OPTIONS,
    HM_CONNECT,
    HM_TRACE
} HttpMethod;

typedef enum {
    HM_VER_1_0,
    HM_VER_1_1,
    HM_VER_2_0
} HttpVersion;

typedef struct {
    char name[HM_MAX_HEADER_LEN];
    char value[HM_MAX_VALUE_LEN];
} HttpHeader;

typedef struct {
    int    status_code;
    char   reason[64];
    HttpHeader headers[HM_MAX_HEADERS];
    int    num_headers;
    char   body[HM_MAX_BODY];
    size_t body_len;
} HttpResponse;

typedef enum {
    HM_START_LINE,
    HM_HEADERS,
    HM_BODY,
    HM_COMPLETE,
    HM_ERROR
} HttpParseState;

typedef enum {
    HM_TE_IDENTITY,
    HM_TE_CHUNKED,
    HM_TE_COMPRESS,
    HM_TE_DEFLATE,
    HM_TE_GZIP
} TransferEncoding;

typedef struct {
    char           method[HM_MAX_METHOD];
    char           uri[HM_MAX_URI];
    HttpVersion    version;
    HttpHeader     headers[HM_MAX_HEADERS];
    int            num_headers;
    char           body[HM_MAX_BODY];
    size_t         body_len;
    bool           keep_alive;
    TransferEncoding te;
    HttpParseState parse_state;
    size_t         parse_offset;
    size_t         content_length;
    bool           content_length_set;
    int            chunk_size_remaining;
    char           raw_buf[HM_MAX_BODY];
    size_t         raw_len;
} HttpRequest;

HttpRequest*  hm_request_init(void);
int           hm_parse_request(HttpRequest *req, const char *data, size_t len);
int           hm_parse_request_line(HttpRequest *req, const char *line);
int           hm_parse_header_line(HttpRequest *req, const char *line);
int           hm_set_header(HttpRequest *req, const char *name, const char *value);
const char*   hm_get_header(const HttpRequest *req, const char *name);
int           hm_remove_header(HttpRequest *req, const char *name);
bool          hm_method_is(const HttpRequest *req, const char *method);
HttpMethod    hm_method_enum(const char *method);
const char*   hm_method_str(HttpMethod m);
const char*   hm_version_str(HttpVersion v);
char*         hm_build_request(const HttpRequest *req, size_t *out_len);
void          hm_request_free(HttpRequest *req);

HttpResponse* hm_response_init(int status_code, const char *reason);
int           hm_response_set_header(HttpResponse *resp, const char *name, const char *value);
int           hm_response_set_body(HttpResponse *resp, const char *body, size_t len);
int           hm_response_set_json(HttpResponse *resp, const char *json);
int           hm_response_set_html(HttpResponse *resp, const char *html);
char*         hm_build_response(const HttpResponse *resp, size_t *out_len);
const char*   hm_status_reason(int code);
void          hm_response_free(HttpResponse *resp);

int           hm_chunked_decode(const char *input, size_t input_len,
                                char *output, size_t *output_len);
int           hm_chunked_encode(const char *input, size_t input_len,
                                char *output, size_t *output_len);
bool          hm_validate_method(const char *method);
bool          hm_validate_uri(const char *uri);
int           hm_normalize_uri(char *uri);
int           hm_parse_query_string(const char *uri, char *path, size_t path_size,
                                    HttpHeader *params, int *num_params,
                                    int max_params);
int           hm_parse_content_type(const char *ct, char *media_type, size_t mt_size,
                                    char *charset, size_t cs_size);
int           hm_negotiate_accept(const char *accept_header,
                                  const char **available, int num_avail,
                                  char *best, size_t best_size);
int           hm_header_security_audit(const HttpRequest *req,
                                       char *report, size_t report_size);

#endif
