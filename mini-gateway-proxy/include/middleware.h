#ifndef MIDDLEWARE_H
#define MIDDLEWARE_H

#include <stdbool.h>
#include <stddef.h>
#include "http_message.h"

#define MW_MAX_CHAIN      16
#define MW_CTX_SIZE       4096

typedef struct MiddlewareContext {
    HttpRequest   *request;
    HttpResponse  *response;
    void          *user_data;
    char           store[MW_CTX_SIZE];
    size_t         store_len;
    bool           aborted;
    int            error_code;
    const char    *error_message;
} MiddlewareContext;

typedef int (*MiddlewareFunc)(MiddlewareContext *ctx, void *next_data);

typedef struct MiddlewareNode {
    MiddlewareFunc handler;
    void          *data;
    char           name[64];
} MiddlewareNode;

typedef struct {
    MiddlewareNode nodes[MW_MAX_CHAIN];
    int            count;
} MiddlewareChain;

MiddlewareChain*    mw_chain_init(void);
int                 mw_chain_use(MiddlewareChain *chain, const char *name,
                                 MiddlewareFunc handler, void *data);
int                 mw_chain_run(MiddlewareChain *chain, MiddlewareContext *ctx);
int                 mw_chain_remove(MiddlewareChain *chain, const char *name);
MiddlewareContext*  mw_context_init(HttpRequest *req, HttpResponse *resp);
int                 mw_context_set(MiddlewareContext *ctx, const char *key,
                                   const char *value);
const char*         mw_context_get(MiddlewareContext *ctx, const char *key);
void                mw_context_free(MiddlewareContext *ctx);

int mw_builtin_auth(MiddlewareContext *ctx, void *next);
int mw_builtin_logger(MiddlewareContext *ctx, void *next);
int mw_builtin_cors(MiddlewareContext *ctx, void *next);
int mw_builtin_compressor(MiddlewareContext *ctx, void *next);
int mw_builtin_timeout(MiddlewareContext *ctx, void *next);

#endif
