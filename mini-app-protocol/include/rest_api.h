#ifndef REST_API_H
#define REST_API_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#define REST_MAX_RESOURCES       64
#define REST_MAX_URI_PATTERN     256
#define REST_MAX_METHOD_NAME     8
#define REST_MAX_PATH_SEGMENTS   16
#define REST_MAX_QUERY_PARAMS    16
#define REST_MAX_HEADERS         32
#define REST_MAX_BODY_SIZE       65536
#define REST_MAX_PARAM_NAME      64
#define REST_MAX_PARAM_VALUE     256

enum RESTMethod {
    REST_GET    = 0,
    REST_POST   = 1,
    REST_PUT    = 2,
    REST_DELETE = 3,
    REST_PATCH  = 4,
    REST_OPTIONS = 5,
    REST_HEAD   = 6
};

enum RESTStatusCode {
    REST_200_OK                    = 200,
    REST_201_CREATED               = 201,
    REST_204_NO_CONTENT            = 204,
    REST_301_MOVED_PERMANENTLY     = 301,
    REST_400_BAD_REQUEST           = 400,
    REST_401_UNAUTHORIZED          = 401,
    REST_403_FORBIDDEN             = 403,
    REST_404_NOT_FOUND             = 404,
    REST_405_METHOD_NOT_ALLOWED    = 405,
    REST_409_CONFLICT              = 409,
    REST_500_INTERNAL_SERVER_ERROR = 500,
    REST_503_SERVICE_UNAVAILABLE   = 503
};

typedef struct {
    char key[REST_MAX_PARAM_NAME];
    char value[REST_MAX_PARAM_VALUE];
} RESTParam;

typedef struct {
    char name[REST_MAX_PARAM_NAME];
    char value[REST_MAX_PARAM_VALUE];
} RESTHeader;

typedef struct {
    RESTParam  params[REST_MAX_QUERY_PARAMS];
    size_t     count;
} RESTQueryString;

typedef struct {
    char        uri[REST_MAX_URI_PATTERN];
    enum RESTMethod method;
    RESTHeader  headers[REST_MAX_HEADERS];
    size_t      header_count;
    char       *body;
    size_t      body_len;
    RESTParam   path_params[REST_MAX_PATH_SEGMENTS];
    size_t      path_param_count;
    RESTQueryString query;
} RESTRequest;

typedef struct {
    enum RESTStatusCode status_code;
    RESTHeader          headers[REST_MAX_HEADERS];
    size_t              header_count;
    char               *body;
    size_t              body_len;
} RESTResponse;

typedef void (*RESTHandler)(const RESTRequest *req, RESTResponse *resp);

typedef struct {
    char            uri_pattern[REST_MAX_URI_PATTERN];
    uint8_t         methods; /* bitmask of enum RESTMethod */
    RESTHandler     handler;
} RESTResource;

typedef struct {
    RESTResource resources[REST_MAX_RESOURCES];
    size_t       count;
} RESTRouter;

void    rest_router_init(RESTRouter *router);
int     rest_register_route(RESTRouter *router, const char *uri_pattern,
                            enum RESTMethod method, RESTHandler handler);
int     rest_register_routes(RESTRouter *router, const char *uri_pattern,
                             const uint8_t *methods, size_t method_count,
                             RESTHandler handler);
int     rest_dispatch(RESTRouter *router,
                      enum RESTMethod method, const char *uri,
                      const char *body, size_t body_len,
                      RESTResponse *resp);
int     rest_dispatch_full(RESTRouter *router, const RESTRequest *req,
                           RESTResponse *resp);
int     rest_url_parse(const char *url, char *uri, size_t uri_size,
                       RESTQueryString *query);
void    rest_request_init(RESTRequest *req);
void    rest_response_init(RESTResponse *resp);
void    rest_response_set(RESTResponse *resp, enum RESTStatusCode code,
                          const char *body);
void    rest_response_add_header(RESTResponse *resp, const char *name,
                                 const char *value);
void    rest_response_json(RESTResponse *resp, const char *json);
void    rest_response_text(RESTResponse *resp, const char *text);
int     rest_uri_match(const char *pattern, const char *uri,
                       RESTParam *params, size_t *param_count,
                       size_t max_params);
void    rest_method_name(enum RESTMethod method, char *out, size_t out_size);

#endif
