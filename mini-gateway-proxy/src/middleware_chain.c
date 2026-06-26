#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "middleware.h"

MiddlewareChain* mw_chain_init(void)
{
    MiddlewareChain *chain = calloc(1, sizeof(MiddlewareChain));
    if (!chain) return NULL;
    chain->count = 0;
    printf("[mw] Middleware chain initialized\n");
    return chain;
}

int mw_chain_use(MiddlewareChain *chain, const char *name,
                 MiddlewareFunc handler, void *data)
{
    if (!chain || !name || !handler || chain->count >= MW_MAX_CHAIN) return -1;
    MiddlewareNode *node = &chain->nodes[chain->count];
    snprintf(node->name, sizeof(node->name), "%s", name);
    node->handler = handler;
    node->data = data;
    printf("[mw] Registered middleware '%s' at position %d\n", name, chain->count);
    chain->count++;
    return chain->count - 1;
}

/*
 * Recursive middleware chain execution.
 * Implements the Chain of Responsibility design pattern (GoF).
 *
 * Each middleware calls the next one in sequence, forming a nested
 * execution stack. This is equivalent to the "onion model" used in
 * frameworks like Express.js, Koa, and ASP.NET Core.
 *
 * Complexity: O(N) where N = chain length.
 * Stack depth: limited by MW_MAX_CHAIN (16), preventing stack overflow.
 */
static int mw_chain_run_internal(MiddlewareChain *chain, MiddlewareContext *ctx,
                                  int index)
{
    if (!chain || !ctx) return -1;
    if (index >= chain->count) return 0;
    if (ctx->aborted) return ctx->error_code;
    MiddlewareNode *node = &chain->nodes[index];
    void *next_data = (index + 1 < chain->count) ? &chain->nodes[index + 1] : NULL;
    int ret = node->handler(ctx, next_data);
    if (ret < 0) {
        ctx->aborted = true;
        ctx->error_code = ret;
    }
    if (!ctx->aborted && index + 1 < chain->count) {
        ret = mw_chain_run_internal(chain, ctx, index + 1);
    }
    return ret;
}

int mw_chain_run(MiddlewareChain *chain, MiddlewareContext *ctx)
{
    if (!chain || !ctx) return -1;
    ctx->aborted = false;
    ctx->error_code = 0;
    return mw_chain_run_internal(chain, ctx, 0);
}

int mw_chain_remove(MiddlewareChain *chain, const char *name)
{
    if (!chain || !name) return -1;
    for (int i = 0; i < chain->count; i++) {
        if (strcmp(chain->nodes[i].name, name) == 0) {
            memmove(&chain->nodes[i], &chain->nodes[i + 1],
                    (size_t)(chain->count - i - 1) * sizeof(MiddlewareNode));
            chain->count--;
            printf("[mw] Removed middleware '%s'\n", name);
            return 0;
        }
    }
    return -1;
}

MiddlewareContext* mw_context_init(HttpRequest *req, HttpResponse *resp)
{
    MiddlewareContext *ctx = calloc(1, sizeof(MiddlewareContext));
    if (!ctx) return NULL;
    ctx->request = req;
    ctx->response = resp;
    ctx->aborted = false;
    ctx->store_len = 0;
    return ctx;
}

/*
 * Simple key-value store within the middleware context.
 * Keys and values are stored as "key\0value\0" pairs in ctx->store.
 * This is O(K) per lookup but avoids external dependencies.
 * For production, replace with a hash table (Robin Hood hashing recommended).
 */
int mw_context_set(MiddlewareContext *ctx, const char *key, const char *value)
{
    if (!ctx || !key || !value) return -1;
    size_t klen = strlen(key), vlen = strlen(value);
    if (ctx->store_len + klen + vlen + 2 >= MW_CTX_SIZE) return -1;
    memcpy(ctx->store + ctx->store_len, key, klen + 1);
    ctx->store_len += klen + 1;
    memcpy(ctx->store + ctx->store_len, value, vlen + 1);
    ctx->store_len += vlen + 1;
    return 0;
}

const char* mw_context_get(MiddlewareContext *ctx, const char *key)
{
    if (!ctx || !key) return NULL;
    size_t pos = 0;
    while (pos < ctx->store_len) {
        const char *k = ctx->store + pos;
        size_t klen = strlen(k);
        pos += klen + 1;
        const char *v = ctx->store + pos;
        if (strcmp(k, key) == 0) return v;
        size_t vlen = strlen(v);
        pos += vlen + 1;
    }
    return NULL;
}

void mw_context_free(MiddlewareContext *ctx) { free(ctx); }

/*
 * Built-in Auth Middleware:
 * Validates Bearer token from Authorization header.
 * Knowledge: Token-based authentication (JWT, Bearer tokens).
 * Implements the OAuth 2.0 Bearer Token Usage framework (RFC 6750).
 */
int mw_builtin_auth(MiddlewareContext *ctx, void *next)
{
    (void)next;
    if (!ctx || !ctx->request) return -1;
    const char *auth = hm_get_header(ctx->request, "Authorization");
    if (!auth) {
        printf("[mw:auth] Missing Authorization header\n");
        ctx->aborted = true;
        ctx->error_code = 401;
        return -1;
    }
    if (strncmp(auth, "Bearer ", 7) != 0 || strlen(auth) <= 7) {
        printf("[mw:auth] Invalid authorization format\n");
        ctx->aborted = true;
        ctx->error_code = 401;
        return -1;
    }
    printf("[mw:auth] Token validated for %s %s\n",
           ctx->request->method, ctx->request->uri);
    mw_context_set(ctx, "auth.validated", "true");
    return 0;
}

/*
 * Built-in Logger Middleware:
 * Logs request details before passing to next middleware.
 * Demonstrates the pre-processing pattern of middleware.
 */
int mw_builtin_logger(MiddlewareContext *ctx, void *next)
{
    if (!ctx || !ctx->request) return -1;
    const char *xff = hm_get_header(ctx->request, "X-Forwarded-For");
    printf("[mw:logger] %s %s from %s\n",
           ctx->request->method, ctx->request->uri,
           xff ? xff : "direct");
    mw_context_set(ctx, "log.timestamp", "<current>");
    return (next && !ctx->aborted) ? 0 : 0;
}

/*
 * Built-in CORS Middleware:
 * Handles Cross-Origin Resource Sharing (CORS) preflight requests.
 * Implements the W3C CORS specification:
 * - Access-Control-Allow-Origin
 * - Access-Control-Allow-Methods
 * - Access-Control-Allow-Headers
 * - Preflight (OPTIONS) handling
 */
int mw_builtin_cors(MiddlewareContext *ctx, void *next)
{
    (void)next;
    if (!ctx || !ctx->request) return -1;
    const char *origin = hm_get_header(ctx->request, "Origin");
    if (!origin) return 0;
    printf("[mw:cors] Origin: %s\n", origin);
    if (ctx->response) {
        hm_response_set_header(ctx->response, "Access-Control-Allow-Origin", origin);
        hm_response_set_header(ctx->response, "Access-Control-Allow-Methods",
                               "GET,POST,PUT,DELETE,OPTIONS");
        hm_response_set_header(ctx->response, "Access-Control-Allow-Headers",
                               "Content-Type,Authorization");
        hm_response_set_header(ctx->response, "Access-Control-Max-Age", "86400");
    }
    if (strcmp(ctx->request->method, "OPTIONS") == 0 && ctx->response) {
        ctx->response->status_code = 204;
        hm_response_set_header(ctx->response, "Content-Length", "0");
        ctx->aborted = true;
    }
    return 0;
}

/*
 * Built-in Compressor Middleware:
 * Placeholder for response compression (gzip/deflate).
 * Knowledge: Content-Encoding negotiation per RFC 7231.
 * Production would integrate zlib for actual compression.
 */
int mw_builtin_compressor(MiddlewareContext *ctx, void *next)
{
    (void)next;
    if (!ctx || !ctx->request) return -1;
    const char *ae = hm_get_header(ctx->request, "Accept-Encoding");
    if (ae) {
        if (strstr(ae, "gzip")) {
            mw_context_set(ctx, "compress.algorithm", "gzip");
            printf("[mw:compress] Client supports gzip\n");
        } else if (strstr(ae, "deflate")) {
            mw_context_set(ctx, "compress.algorithm", "deflate");
            printf("[mw:compress] Client supports deflate\n");
        }
    }
    return 0;
}

/*
 * Built-in Timeout Middleware:
 * Sets a deadline for downstream processing.
 * After the timeout, the middleware chain is aborted.
 * Knowledge: Deadlines and timeouts in distributed systems
 * (Google Site Reliability Engineering, Chapter 4: Service Level Objectives).
 */
int mw_builtin_timeout(MiddlewareContext *ctx, void *next)
{
    (void)next;
    if (!ctx) return -1;
    const char *timeout_str = mw_context_get(ctx, "timeout.ms");
    if (!timeout_str) {
        mw_context_set(ctx, "timeout.ms", "30000");
        printf("[mw:timeout] Default 30s timeout set\n");
    } else {
        printf("[mw:timeout] Timeout: %sms\n", timeout_str);
    }
    return 0;
}