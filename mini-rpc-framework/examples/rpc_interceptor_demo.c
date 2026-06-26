#include "rpc_interceptor.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

int main(void) {
    printf("=== RPC Interceptor Demo ===\n\n");

    printf("[1] Creating interceptor chain: logging → auth → metrics → tracing\n\n");

    InterceptorChain chain;
    interceptor_chain_init(&chain, "DemoChain");

    RPCInterceptor logging_ic = interceptor_make_logging();
    RPCInterceptor auth_ic = interceptor_make_auth("bearer-secret-token-12345");
    RPCInterceptor metrics_ic = interceptor_make_metrics();
    RPCInterceptor tracing_ic = interceptor_make_tracing("trace-demo-001");
    RPCInterceptor rate_limit_ic = interceptor_make_rate_limit(5);
    RPCInterceptor retry_ic = interceptor_make_retry(3, 100);

    interceptor_chain_add(&chain, &logging_ic);
    interceptor_chain_add(&chain, &auth_ic);
    interceptor_chain_add(&chain, &metrics_ic);
    interceptor_chain_add(&chain, &tracing_ic);
    interceptor_chain_add(&chain, &rate_limit_ic);
    interceptor_chain_add(&chain, &retry_ic);

    printf("    Chain has %d interceptors:\n", chain.count);
    for (int32_t i = 0; i < chain.count; i++) {
        printf("      [%d] %s (priority=%d)\n",
               i, chain.interceptors[i].name,
               chain.interceptors[i].priority);
    }

    printf("\n[2] Simulating 3 RPC calls through interceptor chain...\n\n");

    for (int call = 0; call < 3; call++) {
        printf("--- Call %d ---\n", call + 1);

        RPCMessage req;
        rpc_message_init(&req);
        req.id = call + 1;
        strncpy(req.method_name, "UserService.getUser", RPC_MAX_METHOD_NAME - 1);
        req.param_count = 1;
        req.params[0].type = RPC_TYPE_INT32;
        req.params[0].value.v_int32 = 100 + call;

        RPCMessage req_processed;
        rpc_message_init(&req_processed);

        int ret = interceptor_before_invoke(&chain, &req, &req_processed);
        if (ret != 0) {
            printf("    [CHAIN] Before invoke rejected (ret=%d)\n", ret);
            continue;
        }

        printf("    [CHAIN] All before interceptors passed\n");
        printf("    [BUSINESS LOGIC] Processing %s(id=%d)...\n",
               req_processed.method_name, req_processed.id);

        RPCMessage resp;
        rpc_message_init(&resp);
        resp.id = req_processed.id;
        resp.is_request = false;
        resp.param_count = 1;
        resp.params[0].type = RPC_TYPE_STRING;
        resp.params[0].value.v_string = strdup("user_data_success");

        RPCMessage resp_processed;
        rpc_message_init(&resp_processed);

        ret = interceptor_after_invoke(&chain, &req_processed, &resp, &resp_processed);
        if (ret != 0) {
            printf("    [CHAIN] After invoke rejected (ret=%d)\n", ret);
        } else {
            printf("    [CHAIN] All after interceptors passed\n");
        }

        printf("    Final response: id=%d result=\"%s\"\n\n",
               resp_processed.id,
               resp_processed.params[0].type == RPC_TYPE_STRING
                   ? resp_processed.params[0].value.v_string : "unknown");
    }

    printf("[3] Testing interceptor chain remove...\n");
    printf("    Removing 'auth' from chain\n");
    interceptor_chain_remove(&chain, "auth");
    printf("    Chain now has %d interceptors:\n", chain.count);
    for (int32_t i = 0; i < chain.count; i++) {
        printf("      [%d] %s\n", i, chain.interceptors[i].name);
    }

    printf("\n[4] Testing without auth interceptor...\n");

    RPCMessage req2;
    rpc_message_init(&req2);
    req2.id = 100;
    strncpy(req2.method_name, "AuthService.login", RPC_MAX_METHOD_NAME - 1);

    RPCMessage req2_out;
    rpc_message_init(&req2_out);

    int ret2 = interceptor_before_invoke(&chain, &req2, &req2_out);
    printf("    Before invoke result: %d\n", ret2);

    RPCMessage resp2;
    rpc_message_init(&resp2);
    resp2.id = 100;
    resp2.is_request = false;
    resp2.params[0].type = RPC_TYPE_BOOL;
    resp2.params[0].value.v_bool = true;

    RPCMessage resp2_out;
    rpc_message_init(&resp2_out);

    int ret3 = interceptor_after_invoke(&chain, &req2_out, &resp2, &resp2_out);
    printf("    After invoke result: %d\n", ret3);

    printf("\n[5] Testing disabled interceptors...\n");
    chain.interceptors[0].enabled = false;
    printf("    Disabled '%s'\n", chain.interceptors[0].name);

    RPCMessage req3;
    rpc_message_init(&req3);
    req3.id = 200;
    strncpy(req3.method_name, "Calculator.add", RPC_MAX_METHOD_NAME - 1);
    req3.param_count = 2;
    req3.params[0].type = RPC_TYPE_INT32;
    req3.params[0].value.v_int32 = 3;
    req3.params[1].type = RPC_TYPE_INT32;
    req3.params[1].value.v_int32 = 7;

    RPCMessage req3_out;
    rpc_message_init(&req3_out);

    interceptor_before_invoke(&chain, &req3, &req3_out);
    RPCMessage resp3;
    rpc_message_init(&resp3);
    resp3.id = 200;
    RPCMessage resp3_out;
    rpc_message_init(&resp3_out);
    interceptor_after_invoke(&chain, &req3_out, &resp3, &resp3_out);

    printf("\n=== Demo Complete ===\n");
    return 0;
}
