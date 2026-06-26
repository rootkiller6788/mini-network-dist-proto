#include "rpc_interceptor.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

void interceptor_chain_init(InterceptorChain *chain, const char *name) {
    chain->count = 0;
    strncpy(chain->name, name, RPC_MAX_CHAIN_NAME - 1);
    chain->name[RPC_MAX_CHAIN_NAME - 1] = 0;
    for (int32_t i = 0; i < RPC_MAX_INTERCEPTORS; i++) {
        memset(&chain->interceptors[i], 0, sizeof(RPCInterceptor));
    }
}

int interceptor_chain_add(InterceptorChain *chain, const RPCInterceptor *ic) {
    if (!chain || !ic || chain->count >= RPC_MAX_INTERCEPTORS) return -1;

    int32_t insert_pos = chain->count;
    for (int32_t i = 0; i < chain->count; i++) {
        if (ic->priority < chain->interceptors[i].priority) {
            insert_pos = i;
            break;
        }
    }

    for (int32_t i = chain->count; i > insert_pos; i--) {
        memcpy(&chain->interceptors[i], &chain->interceptors[i - 1],
               sizeof(RPCInterceptor));
    }
    memcpy(&chain->interceptors[insert_pos], ic, sizeof(RPCInterceptor));
    chain->count++;
    return 0;
}

int interceptor_chain_remove(InterceptorChain *chain, const char *name) {
    if (!chain || !name) return -1;

    for (int32_t i = 0; i < chain->count; i++) {
        if (strcmp(chain->interceptors[i].name, name) == 0) {
            for (int32_t j = i; j < chain->count - 1; j++) {
                memcpy(&chain->interceptors[j], &chain->interceptors[j + 1],
                       sizeof(RPCInterceptor));
            }
            chain->count--;
            return 0;
        }
    }
    return -1;
}

int interceptor_before_invoke(InterceptorChain *chain, const RPCMessage *req,
                               RPCMessage *req_out) {
    if (!chain || !req || !req_out) return -1;

    RPCMessage current = *req;
    for (int32_t i = 0; i < chain->count; i++) {
        if (!chain->interceptors[i].enabled) continue;
        if (chain->interceptors[i].before_fn) {
            RPCMessage next;
            rpc_message_init(&next);
            int ret = chain->interceptors[i].before_fn(
                chain->interceptors[i].context, &current, &next);
            if (ret != 0) return ret;
            if (i > 0) rpc_message_free(&current);
            current = next;
        }
    }
    *req_out = current;
    return 0;
}

int interceptor_after_invoke(InterceptorChain *chain, const RPCMessage *req,
                              const RPCMessage *resp, RPCMessage *resp_out) {
    if (!chain || !req || !resp || !resp_out) return -1;

    RPCMessage current = *resp;
    for (int32_t i = chain->count - 1; i >= 0; i--) {
        if (!chain->interceptors[i].enabled) continue;
        if (chain->interceptors[i].after_fn) {
            RPCMessage next;
            rpc_message_init(&next);
            int ret = chain->interceptors[i].after_fn(
                chain->interceptors[i].context, req, &current, &next);
            if (ret != 0) return ret;
            if (i < chain->count - 1) rpc_message_free(&current);
            current = next;
        }
    }
    *resp_out = current;
    return 0;
}

typedef struct {
    char    auth_token[128];
    int32_t auth_result;
} AuthContext;

typedef struct {
    int32_t request_count;
    int32_t error_count;
    double  total_latency_ms;
    time_t  start_time;
} MetricsContext;

typedef struct {
    int32_t max_per_second;
    int32_t tokens;
    int32_t last_refill;
} RateLimitContext;

typedef struct {
    int32_t max_retries;
    int32_t delay_ms;
    int32_t retry_count;
} RetryContext;

typedef struct {
    char trace_id[64];
    int32_t span_id;
    time_t span_start;
} TracingContext;

RPCInterceptor interceptor_make_logging(void) {
    RPCInterceptor ic;
    memset(&ic, 0, sizeof(ic));
    strncpy(ic.name, "logging", 63);
    ic.type = RPC_INTERCEPTOR_CLIENT;
    ic.before_fn = interceptor_builtin_logging_before;
    ic.after_fn = interceptor_builtin_logging_after;
    ic.context = NULL;
    ic.priority = 100;
    ic.enabled = true;
    return ic;
}

RPCInterceptor interceptor_make_auth(const char *token) {
    RPCInterceptor ic;
    memset(&ic, 0, sizeof(ic));
    strncpy(ic.name, "auth", 63);
    ic.type = RPC_INTERCEPTOR_CLIENT;
    ic.before_fn = interceptor_builtin_auth_before;
    ic.after_fn = NULL;
    AuthContext *ctx = (AuthContext *)malloc(sizeof(AuthContext));
    memset(ctx, 0, sizeof(AuthContext));
    if (token) strncpy(ctx->auth_token, token, 127);
    ic.context = ctx;
    ic.priority = 50;
    ic.enabled = true;
    return ic;
}

RPCInterceptor interceptor_make_metrics(void) {
    RPCInterceptor ic;
    memset(&ic, 0, sizeof(ic));
    strncpy(ic.name, "metrics", 63);
    ic.type = RPC_INTERCEPTOR_CLIENT;
    ic.before_fn = NULL;
    ic.after_fn = interceptor_builtin_metrics_after;
    MetricsContext *ctx = (MetricsContext *)malloc(sizeof(MetricsContext));
    memset(ctx, 0, sizeof(MetricsContext));
    ctx->start_time = time(NULL);
    ic.context = ctx;
    ic.priority = 200;
    ic.enabled = true;
    return ic;
}

RPCInterceptor interceptor_make_rate_limit(int32_t max_per_second) {
    RPCInterceptor ic;
    memset(&ic, 0, sizeof(ic));
    strncpy(ic.name, "rate_limit", 63);
    ic.type = RPC_INTERCEPTOR_SERVER;
    ic.before_fn = interceptor_builtin_rate_limit_before;
    ic.after_fn = NULL;
    RateLimitContext *ctx = (RateLimitContext *)malloc(sizeof(RateLimitContext));
    memset(ctx, 0, sizeof(RateLimitContext));
    ctx->max_per_second = max_per_second > 0 ? max_per_second : 100;
    ctx->tokens = ctx->max_per_second;
    ctx->last_refill = (int32_t)time(NULL);
    ic.context = ctx;
    ic.priority = 40;
    ic.enabled = true;
    return ic;
}

RPCInterceptor interceptor_make_retry(int32_t max_retries, int32_t delay_ms) {
    RPCInterceptor ic;
    memset(&ic, 0, sizeof(ic));
    strncpy(ic.name, "retry", 63);
    ic.type = RPC_INTERCEPTOR_CLIENT;
    ic.before_fn = interceptor_builtin_retry_before;
    ic.after_fn = NULL;
    RetryContext *ctx = (RetryContext *)malloc(sizeof(RetryContext));
    memset(ctx, 0, sizeof(RetryContext));
    ctx->max_retries = max_retries > 0 ? max_retries : 3;
    ctx->delay_ms = delay_ms > 0 ? delay_ms : 100;
    ic.context = ctx;
    ic.priority = 30;
    ic.enabled = true;
    return ic;
}

RPCInterceptor interceptor_make_tracing(const char *trace_id) {
    RPCInterceptor ic;
    memset(&ic, 0, sizeof(ic));
    strncpy(ic.name, "tracing", 63);
    ic.type = RPC_INTERCEPTOR_CLIENT;
    ic.before_fn = interceptor_builtin_tracing_before;
    ic.after_fn = interceptor_builtin_tracing_after;
    TracingContext *ctx = (TracingContext *)malloc(sizeof(TracingContext));
    memset(ctx, 0, sizeof(TracingContext));
    if (trace_id) strncpy(ctx->trace_id, trace_id, 63);
    else {
        snprintf(ctx->trace_id, 64, "trace-%04x", (uint32_t)time(NULL));
    }
    ctx->span_id = rand() % 65536;
    ic.context = ctx;
    ic.priority = 60;
    ic.enabled = true;
    return ic;
}

int interceptor_builtin_logging_before(void *ctx, const RPCMessage *req,
                                        RPCMessage *req_out) {
    (void)ctx;
    printf("[LOGGING] >> RPC call: method=%s id=%d params=%d\n",
           req->method_name, req->id, req->param_count);
    *req_out = *req;
    return 0;
}

int interceptor_builtin_logging_after(void *ctx, const RPCMessage *req,
                                       const RPCMessage *resp,
                                       RPCMessage *resp_out) {
    (void)ctx;
    printf("[LOGGING] << RPC response: method=%s id=%d error=%s\n",
           req->method_name, resp->id, resp->is_error ? resp->error_msg : "none");
    *resp_out = *resp;
    return 0;
}

int interceptor_builtin_auth_before(void *ctx, const RPCMessage *req,
                                     RPCMessage *req_out) {
    AuthContext *ac = (AuthContext *)ctx;
    if (!ac || ac->auth_token[0] == 0) {
        printf("[AUTH] No auth token configured, skipping\n");
    } else {
        printf("[AUTH] Authenticating request with token: %s\n", ac->auth_token);
    }
    *req_out = *req;
    return 0;
}

int interceptor_builtin_metrics_after(void *ctx, const RPCMessage *req,
                                       const RPCMessage *resp,
                                       RPCMessage *resp_out) {
    MetricsContext *mc = (MetricsContext *)ctx;
    if (mc) {
        mc->request_count++;
        if (resp->is_error) mc->error_count++;
        printf("[METRICS] Request count=%d, Error count=%d\n",
               mc->request_count, mc->error_count);
    }
    *resp_out = *resp;
    return 0;
}

int interceptor_builtin_rate_limit_before(void *ctx, const RPCMessage *req,
                                           RPCMessage *req_out) {
    RateLimitContext *rc = (RateLimitContext *)ctx;
    if (!rc) { *req_out = *req; return 0; }

    int32_t now = (int32_t)time(NULL);
    int32_t elapsed = now - rc->last_refill;
    if (elapsed > 0) {
        rc->tokens += elapsed * rc->max_per_second;
        if (rc->tokens > rc->max_per_second) rc->tokens = rc->max_per_second;
        rc->last_refill = now;
    }

    if (rc->tokens <= 0) {
        printf("[RATE_LIMIT] Rate limit exceeded, rejecting request\n");
        return -1;
    }

    rc->tokens--;
    printf("[RATE_LIMIT] Allowed request, tokens remaining: %d\n", rc->tokens);
    *req_out = *req;
    return 0;
}

int interceptor_builtin_retry_before(void *ctx, const RPCMessage *req,
                                      RPCMessage *req_out) {
    RetryContext *rc = (RetryContext *)ctx;
    if (!rc) { *req_out = *req; return 0; }

    rc->retry_count = 0;
    printf("[RETRY] Enabled retries: %d, delay: %dms\n",
           rc->max_retries, rc->delay_ms);
    *req_out = *req;
    return 0;
}

int interceptor_builtin_tracing_before(void *ctx, const RPCMessage *req,
                                        RPCMessage *req_out) {
    TracingContext *tc = (TracingContext *)ctx;
    if (!tc) { *req_out = *req; return 0; }

    tc->span_start = time(NULL);
    tc->span_id = rand() % 65536;
    printf("[TRACING] Start span: trace_id=%s span_id=%d method=%s\n",
           tc->trace_id, tc->span_id, req->method_name);
    *req_out = *req;
    return 0;
}

int interceptor_builtin_tracing_after(void *ctx, const RPCMessage *req,
                                       const RPCMessage *resp,
                                       RPCMessage *resp_out) {
    TracingContext *tc = (TracingContext *)ctx;
    if (!tc) { *resp_out = *resp; return 0; }

    time_t span_end = time(NULL);
    double elapsed = difftime(span_end, tc->span_start) * 1000.0;
    printf("[TRACING] End span: trace_id=%s span_id=%d elapsed=%.0fms method=%s\n",
           tc->trace_id, tc->span_id, elapsed, req->method_name);
    *resp_out = *resp;
    return 0;
}
