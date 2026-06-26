#ifndef RPC_INTERCEPTOR_H
#define RPC_INTERCEPTOR_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "rpc_encoding.h"

#define RPC_MAX_INTERCEPTORS 16
#define RPC_MAX_CHAIN_NAME   64

typedef enum {
    RPC_INTERCEPTOR_CLIENT = 0,
    RPC_INTERCEPTOR_SERVER = 1
} RPCInterceptorType;

typedef int (*RPCBeforeFn)(void *ctx, const RPCMessage *req, RPCMessage *req_out);
typedef int (*RPCAfterFn)(void *ctx, const RPCMessage *req, const RPCMessage *resp,
                          RPCMessage *resp_out);

typedef struct {
    char                 name[64];
    RPCInterceptorType   type;
    RPCBeforeFn          before_fn;
    RPCAfterFn           after_fn;
    void                *context;
    int32_t              priority;
    bool                 enabled;
} RPCInterceptor;

typedef struct {
    RPCInterceptor  interceptors[RPC_MAX_INTERCEPTORS];
    int32_t         count;
    char            name[RPC_MAX_CHAIN_NAME];
} InterceptorChain;

void interceptor_chain_init(InterceptorChain *chain, const char *name);

int  interceptor_chain_add(InterceptorChain *chain, const RPCInterceptor *ic);
int  interceptor_chain_remove(InterceptorChain *chain, const char *name);

int  interceptor_before_invoke(InterceptorChain *chain, const RPCMessage *req,
                               RPCMessage *req_out);
int  interceptor_after_invoke(InterceptorChain *chain, const RPCMessage *req,
                              const RPCMessage *resp, RPCMessage *resp_out);

RPCInterceptor interceptor_make_logging(void);
RPCInterceptor interceptor_make_auth(const char *token);
RPCInterceptor interceptor_make_metrics(void);
RPCInterceptor interceptor_make_rate_limit(int32_t max_per_second);
RPCInterceptor interceptor_make_retry(int32_t max_retries, int32_t delay_ms);
RPCInterceptor interceptor_make_tracing(const char *trace_id);

int interceptor_builtin_logging_before(void *ctx, const RPCMessage *req,
                                       RPCMessage *req_out);
int interceptor_builtin_logging_after(void *ctx, const RPCMessage *req,
                                      const RPCMessage *resp, RPCMessage *resp_out);
int interceptor_builtin_auth_before(void *ctx, const RPCMessage *req,
                                    RPCMessage *req_out);
int interceptor_builtin_metrics_after(void *ctx, const RPCMessage *req,
                                      const RPCMessage *resp, RPCMessage *resp_out);
int interceptor_builtin_rate_limit_before(void *ctx, const RPCMessage *req,
                                          RPCMessage *req_out);
int interceptor_builtin_retry_before(void *ctx, const RPCMessage *req,
                                     RPCMessage *req_out);
int interceptor_builtin_tracing_before(void *ctx, const RPCMessage *req,
                                       RPCMessage *req_out);
int interceptor_builtin_tracing_after(void *ctx, const RPCMessage *req,
                                      const RPCMessage *resp, RPCMessage *resp_out);

#endif
