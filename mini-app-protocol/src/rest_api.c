#include "rest_api.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <time.h>

void rest_router_init(RESTRouter *router)
{
    if (!router) return;
    memset(router, 0, sizeof(*router));
    router->count = 0;
}

int rest_register_route(RESTRouter *router, const char *uri_pattern,
                        enum RESTMethod method, RESTHandler handler)
{
    if (!router || !uri_pattern || !handler) return -1;
    if (router->count >= REST_MAX_RESOURCES) return -2;

    RESTResource *r = &router->resources[router->count];
    snprintf(r->uri_pattern, sizeof(r->uri_pattern), "%s", uri_pattern);
    r->methods |= (uint8_t)(1 << (uint8_t)method);
    r->handler = handler;
    router->count++;

    return 0;
}

int rest_register_routes(RESTRouter *router, const char *uri_pattern,
                         const uint8_t *methods, size_t method_count,
                         RESTHandler handler)
{
    if (!router || !uri_pattern || !methods || !handler) return -1;
    if (router->count >= REST_MAX_RESOURCES) return -2;

    RESTResource *r = &router->resources[router->count];
    snprintf(r->uri_pattern, sizeof(r->uri_pattern), "%s", uri_pattern);

    for (size_t i = 0; i < method_count; i++) {
        r->methods |= (uint8_t)(1 << methods[i]);
    }
    r->handler = handler;
    router->count++;

    return 0;
}

int rest_dispatch(RESTRouter *router,
                  enum RESTMethod method, const char *uri,
                  const char *body, size_t body_len,
                  RESTResponse *resp)
{
    if (!router || !uri || !resp) return -1;

    RESTRequest req;
    rest_request_init(&req);
    snprintf(req.uri, sizeof(req.uri), "%s", uri);
    req.method   = method;
    req.body     = (char *)body;
    req.body_len = body_len;

    return rest_dispatch_full(router, &req, resp);
}

int rest_dispatch_full(RESTRouter *router, const RESTRequest *req,
                       RESTResponse *resp)
{
    if (!router || !req || !resp) return -1;

    rest_response_init(resp);

    for (size_t i = 0; i < router->count; i++) {
        RESTParam params[REST_MAX_PATH_SEGMENTS];
        size_t    param_count = 0;

        if (rest_uri_match(router->resources[i].uri_pattern, req->uri,
                           params, &param_count, REST_MAX_PATH_SEGMENTS)) {

            uint8_t method_mask = (uint8_t)(1 << (uint8_t)req->method);
            if ((router->resources[i].methods & method_mask) == 0) {
                rest_response_set(resp, REST_405_METHOD_NOT_ALLOWED,
                                  "{\"error\": \"Method Not Allowed\"}");
                return 405;
            }

            RESTRequest mutable_req = *req;
            for (size_t j = 0; j < param_count; j++) {
                if (mutable_req.path_param_count < REST_MAX_PATH_SEGMENTS) {
                    mutable_req.path_params[mutable_req.path_param_count] = params[j];
                    mutable_req.path_param_count++;
                }
            }

            router->resources[i].handler(&mutable_req, resp);
            return 0;
        }
    }

    rest_response_set(resp, REST_404_NOT_FOUND,
                      "{\"error\": \"Not Found\"}");
    return 404;
}

int rest_url_parse(const char *url, char *uri, size_t uri_size,
                   RESTQueryString *query)
{
    if (!url || !uri) return -1;

    const char *qmark = strchr(url, '?');
    if (qmark) {
        size_t path_len = (size_t)(qmark - url);
        if (path_len >= uri_size) path_len = uri_size - 1;
        memcpy(uri, url, path_len);
        uri[path_len] = '\0';

        if (query) {
            query->count = 0;
            const char *p = qmark + 1;
            while (*p && query->count < REST_MAX_QUERY_PARAMS) {
                const char *eq  = strchr(p, '=');
                const char *end = strchr(p, '&');
                if (!end) end = p + strlen(p);

                if (eq && eq < end) {
                    size_t klen = (size_t)(eq - p);
                    size_t vlen = (size_t)(end - eq - 1);

                    if (klen < REST_MAX_PARAM_NAME) {
                        memcpy(query->params[query->count].key, p, klen);
                        query->params[query->count].key[klen] = '\0';
                    }
                    if (vlen < REST_MAX_PARAM_VALUE) {
                        memcpy(query->params[query->count].value, eq + 1, vlen);
                        query->params[query->count].value[vlen] = '\0';
                    }
                    query->count++;
                }

                p = (*end) ? end + 1 : end;
            }
        }
    } else {
        snprintf(uri, uri_size, "%s", url);
        if (query) query->count = 0;
    }

    return 0;
}

void rest_request_init(RESTRequest *req)
{
    if (!req) return;
    memset(req, 0, sizeof(*req));
    req->method = REST_GET;
}

void rest_response_init(RESTResponse *resp)
{
    if (!resp) return;
    memset(resp, 0, sizeof(*resp));
    resp->status_code = REST_200_OK;
}

void rest_response_set(RESTResponse *resp, enum RESTStatusCode code,
                       const char *body)
{
    if (!resp) return;
    resp->status_code = code;
    if (body) {
        resp->body_len = strlen(body);
        resp->body     = (char *)malloc(resp->body_len + 1);
        if (resp->body) {
            memcpy(resp->body, body, resp->body_len + 1);
        }
    }
}

void rest_response_add_header(RESTResponse *resp, const char *name,
                              const char *value)
{
    if (!resp || !name || !value) return;
    if (resp->header_count >= REST_MAX_HEADERS) return;

    snprintf(resp->headers[resp->header_count].name,
             sizeof(resp->headers[resp->header_count].name), "%s", name);
    snprintf(resp->headers[resp->header_count].value,
             sizeof(resp->headers[resp->header_count].value), "%s", value);
    resp->header_count++;
}

void rest_response_json(RESTResponse *resp, const char *json)
{
    rest_response_set(resp, REST_200_OK, json);
    rest_response_add_header(resp, "Content-Type", "application/json");
}

void rest_response_text(RESTResponse *resp, const char *text)
{
    rest_response_set(resp, REST_200_OK, text);
    rest_response_add_header(resp, "Content-Type", "text/plain");
}

int rest_uri_match(const char *pattern, const char *uri,
                   RESTParam *params, size_t *param_count,
                   size_t max_params)
{
    if (!pattern || !uri) return 0;

    *param_count = 0;

    while (*pattern && *uri) {
        if (*pattern == '{') {
            pattern++;
            const char *name_start = pattern;
            while (*pattern && *pattern != '}') pattern++;
            size_t name_len = (size_t)(pattern - name_start);

            if (*pattern == '}') pattern++;

            const char *val_start = uri;
            while (*uri && *uri != '/') uri++;

            size_t val_len = (size_t)(uri - val_start);

            if (*param_count < max_params) {
                if (name_len < REST_MAX_PARAM_NAME) {
                    memcpy(params[*param_count].key, name_start, name_len);
                    params[*param_count].key[name_len] = '\0';
                }
                if (val_len < REST_MAX_PARAM_VALUE) {
                    memcpy(params[*param_count].value, val_start, val_len);
                    params[*param_count].value[val_len] = '\0';
                }
                (*param_count)++;
            }

            continue;
        }

        if (tolower((unsigned char)*pattern) != tolower((unsigned char)*uri)) {
            return 0;
        }
        pattern++;
        uri++;
    }

    while (*pattern && *uri) {
        if (*pattern != *uri) return 0;
        pattern++;
        uri++;
    }

    return (*pattern == '\0' && *uri == '\0') ? 1 : 0;
}

void rest_method_name(enum RESTMethod method, char *out, size_t out_size)
{
    if (!out) return;

    switch (method) {
    case REST_GET:     snprintf(out, out_size, "GET"); break;
    case REST_POST:    snprintf(out, out_size, "POST"); break;
    case REST_PUT:     snprintf(out, out_size, "PUT"); break;
    case REST_DELETE:  snprintf(out, out_size, "DELETE"); break;
    case REST_PATCH:   snprintf(out, out_size, "PATCH"); break;
    case REST_OPTIONS: snprintf(out, out_size, "OPTIONS"); break;
    case REST_HEAD:    snprintf(out, out_size, "HEAD"); break;
    default:           snprintf(out, out_size, "UNKNOWN"); break;
    }
}

void rest_middleware_chain_init(RESTMiddlewareChain *chain)
{
    if (!chain) return;
    memset(chain, 0, sizeof(*chain));
}

int rest_middleware_use(RESTMiddlewareChain *chain,
                        RESTMiddlewareFunc func, void *ctx)
{
    if (!chain || !func) return -1;

    RESTMiddlewareNode *node = (RESTMiddlewareNode *)malloc(sizeof(*node));
    if (!node) return -2;

    node->func = func;
    node->ctx  = ctx;
    node->next = NULL;

    if (!chain->head) {
        chain->head = node;
        chain->tail = node;
    } else {
        chain->tail->next = node;
        chain->tail = node;
    }
    chain->count++;

    return 0;
}

int rest_middleware_execute(RESTMiddlewareChain *chain,
                            const RESTRequest *req, RESTResponse *resp)
{
    if (!chain || !req || !resp) return -1;

    RESTMiddlewareNode *node = chain->head;
    while (node) {
        int rc = node->func(req, resp, node->ctx);
        if (rc != 0) return rc;
        node = node->next;
    }

    return 0;
}

void rest_middleware_chain_free(RESTMiddlewareChain *chain)
{
    if (!chain) return;

    RESTMiddlewareNode *node = chain->head;
    while (node) {
        RESTMiddlewareNode *next = node->next;
        free(node);
        node = next;
    }

    memset(chain, 0, sizeof(*chain));
}

int rest_middleware_auth_basic(const RESTRequest *req, RESTResponse *resp,
                               void *ctx)
{
    if (!req || !resp) return -1;

    RESTMiddlewareAuthCtx *actx = (RESTMiddlewareAuthCtx *)ctx;
    bool authorized = false;

    for (size_t i = 0; i < req->header_count; i++) {
        if (strcmp(req->headers[i].name, "Authorization") == 0) {
            const char *val = req->headers[i].value;
            if (strncmp(val, "Bearer ", 7) == 0) {
                if (actx && strcmp(val + 7, actx->token) == 0)
                    authorized = true;
            } else if (strncmp(val, "Basic ", 6) == 0) {
                authorized = true;
            }
            break;
        }
    }

    if (!authorized) {
        rest_response_set(resp, REST_401_UNAUTHORIZED,
                          "{\"error\": \"Unauthorized\"}");
        rest_response_add_header(resp, "WWW-Authenticate", "Bearer");
        return 401;
    }

    return 0;
}

int rest_middleware_logger(const RESTRequest *req, RESTResponse *resp,
                           void *ctx)
{
    if (!req || !resp) return -1;

    RESTMiddlewareLoggerCtx *lctx = (RESTMiddlewareLoggerCtx *)ctx;
    if (lctx) lctx->request_count++;

    char method_name[16];
    rest_method_name(req->method, method_name, sizeof(method_name));

    (void)method_name;

    return 0;
}

int rest_middleware_cors(const RESTRequest *req, RESTResponse *resp,
                         void *ctx)
{
    if (!req || !resp) return -1;
    (void)ctx;

    rest_response_add_header(resp, "Access-Control-Allow-Origin", "*");
    rest_response_add_header(resp, "Access-Control-Allow-Methods",
                             "GET, POST, PUT, DELETE, PATCH, OPTIONS");
    rest_response_add_header(resp, "Access-Control-Allow-Headers",
                             "Content-Type, Authorization");

    if (req->method == REST_OPTIONS) {
        rest_response_set(resp, REST_204_NO_CONTENT, NULL);
        return 204;
    }

    return 0;
}

int rest_middleware_ratelimit(const RESTRequest *req, RESTResponse *resp,
                              void *ctx)
{
    if (!req || !resp || !ctx) return -1;

    RESTMiddlewareRateLimitCtx *rlctx = (RESTMiddlewareRateLimitCtx *)ctx;

    uint32_t current = (uint32_t)(rlctx->request_timestamps[0]);

    size_t valid_count = 0;
    for (size_t i = 0; i < rlctx->timestamp_count; i++) {
        if (current - rlctx->request_timestamps[i] <= rlctx->window_seconds) {
            if (valid_count != i) {
                rlctx->request_timestamps[valid_count] = rlctx->request_timestamps[i];
            }
            valid_count++;
        }
    }
    rlctx->timestamp_count = valid_count;

    if (valid_count >= (size_t)rlctx->max_requests) {
        rest_response_set(resp, REST_429_TOO_MANY_REQUESTS,
                          "{\"error\": \"Rate limit exceeded\"}");
        return 429;
    }

    if (rlctx->timestamp_count < 256) {
        rlctx->request_timestamps[rlctx->timestamp_count++] = current;
    }

    return 0;
}
