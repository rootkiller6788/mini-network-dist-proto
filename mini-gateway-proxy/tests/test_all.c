#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <time.h>
#include <math.h>

#include "http_message.h"
#include "load_balancer.h"
#include "circuit_breaker.h"
#include "api_gateway.h"
#include "rate_limiter.h"
#include "service_registry.h"
#include "middleware.h"
#include "tls_context.h"
#include "connection_pool.h"

static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) do { \
    tests_run++; \
    printf("  TEST %-40s", name); \
} while(0)

#define PASS() do { \
    tests_passed++; \
    printf(" PASS\n"); \
} while(0)

#define FAIL(msg) do { \
    tests_failed++; \
    printf(" FAIL: %s\n", msg); \
} while(0)

#define ASSERT_EQ(a, b, msg) do { \
    if ((a) != (b)) { FAIL(msg); return; } \
} while(0)

#define ASSERT_TRUE(cond, msg) do { \
    if (!(cond)) { FAIL(msg); return; } \
} while(0)

#define ASSERT_NOT_NULL(p, msg) do { \
    if ((p) == NULL) { FAIL(msg); return; } \
} while(0)

/* ========== HTTP Message Tests (L1-L4) ========== */

static void test_hm_request_init(void)
{
    TEST("hm_request_init");
    HttpRequest *req = hm_request_init();
    ASSERT_NOT_NULL(req, "request is NULL");
    ASSERT_EQ(req->version, HM_VER_1_1, "default version");
    ASSERT_TRUE(req->keep_alive, "default keep_alive");
    ASSERT_EQ(req->parse_state, HM_START_LINE, "initial parse_state");
    hm_request_free(req);
    PASS();
}

static void test_hm_method_enum(void)
{
    TEST("hm_method_enum");
    ASSERT_EQ(hm_method_enum("GET"), HM_GET, "GET enum");
    ASSERT_EQ(hm_method_enum("POST"), HM_POST, "POST enum");
    ASSERT_EQ(hm_method_enum("PUT"), HM_PUT, "PUT enum");
    ASSERT_EQ(hm_method_enum("DELETE"), HM_DELETE, "DELETE enum");
    ASSERT_EQ(hm_method_enum("PATCH"), HM_PATCH, "PATCH enum");
    ASSERT_EQ(hm_method_enum("HEAD"), HM_HEAD, "HEAD enum");
    ASSERT_EQ(hm_method_enum("OPTIONS"), HM_OPTIONS, "OPTIONS enum");
    ASSERT_EQ(hm_method_enum("CONNECT"), HM_CONNECT, "CONNECT enum");
    ASSERT_EQ(hm_method_enum("TRACE"), HM_TRACE, "TRACE enum");
    PASS();
}

static void test_hm_parse_request_line(void)
{
    TEST("hm_parse_request_line");
    HttpRequest *req = hm_request_init();
    ASSERT_NOT_NULL(req, "alloc failed");
    int ret = hm_parse_request_line(req, "GET /api/users HTTP/1.1");
    ASSERT_EQ(ret, 0, "parse success");
    ASSERT_TRUE(strcmp(req->method, "GET") == 0, "method parsed");
    ASSERT_TRUE(strcmp(req->uri, "/api/users") == 0, "uri parsed");
    ASSERT_EQ(req->version, HM_VER_1_1, "version parsed");
    hm_request_free(req);
    PASS();
}

static void test_hm_parse_header_line(void)
{
    TEST("hm_parse_header_line");
    HttpRequest *req = hm_request_init();
    int ret = hm_parse_header_line(req, "Content-Type: application/json");
    ASSERT_EQ(ret, 0, "parse success");
    ASSERT_EQ(req->num_headers, 1, "header count");
    ASSERT_TRUE(strcasecmp(req->headers[0].name, "Content-Type") == 0, "name correct");
    ASSERT_TRUE(strcmp(req->headers[0].value, "application/json") == 0, "value correct");
    hm_request_free(req);
    PASS();
}

static void test_hm_set_and_get_header(void)
{
    TEST("hm_set/get_header");
    HttpRequest *req = hm_request_init();
    hm_set_header(req, "X-Custom", "test-value");
    const char *val = hm_get_header(req, "X-Custom");
    ASSERT_NOT_NULL(val, "header not found");
    ASSERT_TRUE(strcmp(val, "test-value") == 0, "header value");
    hm_set_header(req, "X-Custom", "updated");
    val = hm_get_header(req, "X-Custom");
    ASSERT_TRUE(strcmp(val, "updated") == 0, "header updated");
    hm_request_free(req);
    PASS();
}

static void test_hm_remove_header(void)
{
    TEST("hm_remove_header");
    HttpRequest *req = hm_request_init();
    hm_set_header(req, "X-Remove", "me");
    ASSERT_EQ(req->num_headers, 1, "has header");
    ASSERT_EQ(hm_remove_header(req, "X-Remove"), 0, "removed");
    ASSERT_EQ(req->num_headers, 0, "count after remove");
    ASSERT_TRUE(hm_get_header(req, "X-Remove") == NULL, "header gone");
    hm_request_free(req);
    PASS();
}

static void test_hm_parse_request(void)
{
    TEST("hm_parse_request");
    HttpRequest *req = hm_request_init();
    const char *raw = "GET /test HTTP/1.1\r\nHost: example.com\r\n\r\n";
    int ret = hm_parse_request(req, raw, strlen(raw));
    ASSERT_EQ(ret, 0, "parse success");
    ASSERT_TRUE(strcmp(req->method, "GET") == 0, "method");
    ASSERT_TRUE(strcmp(req->uri, "/test") == 0, "uri");
    ASSERT_NOT_NULL(hm_get_header(req, "Host"), "Host header");
    hm_request_free(req);
    PASS();
}

static void test_hm_validate_method(void)
{
    TEST("hm_validate_method");
    ASSERT_TRUE(hm_validate_method("GET"), "GET valid");
    ASSERT_TRUE(hm_validate_method("POST"), "POST valid");
    ASSERT_TRUE(!hm_validate_method("INVALID"), "INVALID rejected");
    ASSERT_TRUE(!hm_validate_method(NULL), "NULL rejected");
    PASS();
}

static void test_hm_validate_uri(void)
{
    TEST("hm_validate_uri");
    ASSERT_TRUE(hm_validate_uri("/api/v1"), "valid uri");
    ASSERT_TRUE(!hm_validate_uri("api/v1"), "no leading slash");
    ASSERT_TRUE(!hm_validate_uri(NULL), "NULL rejected");
    PASS();
}

static void test_hm_normalize_uri(void)
{
    TEST("hm_normalize_uri");
    char buf[256] = "/api//v1//users";
    hm_normalize_uri(buf);
    ASSERT_TRUE(strcmp(buf, "/api/v1/users") == 0, "collapsed slashes");
    char pct[256] = "/api/%2e%2e/test";
    hm_normalize_uri(pct);
    ASSERT_TRUE(strstr(pct, "%") == NULL, "percent decoded");
    PASS();
}

static void test_hm_status_reason(void)
{
    TEST("hm_status_reason");
    ASSERT_TRUE(strcmp(hm_status_reason(200), "OK") == 0, "200 OK");
    ASSERT_TRUE(strcmp(hm_status_reason(404), "Not Found") == 0, "404");
    ASSERT_TRUE(strcmp(hm_status_reason(500), "Internal Server Error") == 0, "500");
    PASS();
}

static void test_hm_response_init(void)
{
    TEST("hm_response_init");
    HttpResponse *resp = hm_response_init(201, NULL);
    ASSERT_NOT_NULL(resp, "alloc failed");
    ASSERT_EQ(resp->status_code, 201, "status code");
    ASSERT_TRUE(strcmp(resp->reason, "Created") == 0, "default reason");
    hm_response_free(resp);
    PASS();
}

static void test_hm_build_response(void)
{
    TEST("hm_build_response");
    HttpResponse *resp = hm_response_init(200, "OK");
    hm_response_set_json(resp, "{\"ok\":true}");
    size_t len;
    char *buf = hm_build_response(resp, &len);
    ASSERT_NOT_NULL(buf, "build failed");
    ASSERT_TRUE(strstr(buf, "200 OK") != NULL, "status line");
    ASSERT_TRUE(strstr(buf, "application/json") != NULL, "content type");
    free(buf);
    hm_response_free(resp);
    PASS();
}

static void test_hm_chunked(void)
{
    TEST("hm_chunked_encode/decode");
    const char *input = "Hello, world!";
    char encoded[256], decoded[256];
    size_t elen, dlen;
    hm_chunked_encode(input, strlen(input), encoded, &elen);
    hm_chunked_decode(encoded, elen, decoded, &dlen);
    ASSERT_TRUE(dlen == strlen(input), "length match");
    ASSERT_TRUE(strncmp(decoded, input, dlen) == 0, "content match");
    PASS();
}

static void test_hm_query_string(void)
{
    TEST("hm_parse_query_string");
    char path[256];
    HttpHeader params[8];
    int n = 0;
    hm_parse_query_string("/search?q=test&page=1&sort=asc", path, 256, params, &n, 8);
    ASSERT_EQ(n, 3, "param count");
    ASSERT_TRUE(strcmp(path, "/search") == 0, "path extracted");
    ASSERT_TRUE(strcmp(params[0].name, "q") == 0, "param 0 name");
    ASSERT_TRUE(strcmp(params[0].value, "test") == 0, "param 0 value");
    PASS();
}

static void test_hm_content_type(void)
{
    TEST("hm_parse_content_type");
    char mt[128], cs[64];
    hm_parse_content_type("text/html; charset=utf-8", mt, 128, cs, 64);
    ASSERT_TRUE(strcmp(mt, "text/html") == 0, "media type");
    ASSERT_TRUE(strcmp(cs, "utf-8") == 0, "charset");
    PASS();
}

static void test_hm_negotiate_accept(void)
{
    TEST("hm_negotiate_accept");
    const char *avail[] = {"text/html", "application/json", "text/plain"};
    char best[128];
    int idx = hm_negotiate_accept("application/json, text/html;q=0.9", avail, 3, best, 128);
    ASSERT_EQ(idx, 1, "json selected");
    ASSERT_TRUE(strcmp(best, "application/json") == 0, "best is json");
    PASS();
}

static void test_hm_security_audit(void)
{
    TEST("hm_header_security_audit");
    HttpRequest *req = hm_request_init();
    hm_set_header(req, "Host", "example.com");
    hm_set_header(req, "Accept", "*/*");
    char report[4096];
    int missing = hm_header_security_audit(req, report, 4096);
    ASSERT_TRUE(missing >= 0, "audit ran");
    hm_request_free(req);
    PASS();
}

/* ========== Load Balancer Tests (L2, L5) ========== */

static void test_lb_init(void)
{
    TEST("lb_init");
    LoadBalancer *lb = lb_init(LB_ROUND_ROBIN);
    ASSERT_NOT_NULL(lb, "alloc failed");
    ASSERT_EQ(lb->num_servers, 0, "empty init");
    free(lb);
    PASS();
}

static void test_lb_add_server(void)
{
    TEST("lb_add_server");
    LoadBalancer *lb = lb_init(LB_ROUND_ROBIN);
    int idx = lb_add_server(lb, "10.0.0.1", 8080, 5);
    ASSERT_EQ(idx, 0, "first server index");
    ASSERT_EQ(lb->num_servers, 1, "server count");
    ASSERT_TRUE(strcmp(lb->servers[0].address, "10.0.0.1:8080") == 0, "address");
    free(lb);
    PASS();
}

static void test_lb_round_robin(void)
{
    TEST("lb_round_robin");
    LoadBalancer *lb = lb_init(LB_ROUND_ROBIN);
    lb_add_server(lb, "srv-a", 80, 1);
    lb_add_server(lb, "srv-b", 80, 1);
    lb_add_server(lb, "srv-c", 80, 1);
    int first = lb_select_server(lb, NULL);
    int second = lb_select_server(lb, NULL);
    ASSERT_TRUE(first != second, "different servers");
    free(lb);
    PASS();
}

static void test_lb_consistent_hash(void)
{
    TEST("lb_consistent_hash");
    LoadBalancer *lb = lb_init(LB_CONSISTENT_HASH);
    lb_add_server(lb, "srv-a", 80, 1);
    lb_add_server(lb, "srv-b", 80, 1);
    int idx1 = lb_select_server(lb, "user-alice");
    int idx2 = lb_select_server(lb, "user-alice");
    ASSERT_EQ(idx1, idx2, "same key maps to same server");
    free(lb);
    PASS();
}

/* ========== Circuit Breaker Tests (L2, L3, L5) ========== */

static int cb_test_success(void *arg) { (void)arg; return 0; }
static int cb_test_failure(void *arg) { (void)arg; return -1; }

static void test_cb_init(void)
{
    TEST("cb_init");
    CBCircuit *cb = cb_init("test-cb", 3, 2, 5000);
    ASSERT_NOT_NULL(cb, "alloc failed");
    ASSERT_EQ(cb->state, CB_CLOSED, "initial state CLOSED");
    ASSERT_EQ(cb->failure_threshold, 3, "threshold");
    free(cb);
    PASS();
}

static void test_cb_closed_to_open(void)
{
    TEST("cb_closed_to_open");
    CBCircuit *cb = cb_init("test-cb", 2, 2, 5000);
    ASSERT_EQ(cb_call(cb, cb_test_failure, NULL), -1, "first failure");
    ASSERT_EQ(cb->state, CB_CLOSED, "still closed after 1");
    ASSERT_EQ(cb_call(cb, cb_test_failure, NULL), -1, "second failure");
    ASSERT_EQ(cb->state, CB_OPEN, "open after 2 failures");
    free(cb);
    PASS();
}

static void test_cb_success_resets(void)
{
    TEST("cb_success_resets_count");
    CBCircuit *cb = cb_init("test-cb", 5, 2, 5000);
    cb_call(cb, cb_test_failure, NULL);
    cb_call(cb, cb_test_success, NULL);
    ASSERT_EQ(cb->failure_count, 0, "failure_count reset on success");
    free(cb);
    PASS();
}

/* ========== Rate Limiter Tests (L2, L5) ========== */

static void test_rl_token_bucket(void)
{
    TEST("rl_token_bucket");
    RateLimiter *rl = rl_init(RL_TOKEN_BUCKET, 10.0, 20.0, 0);
    ASSERT_NOT_NULL(rl, "alloc failed");
    bool allowed = rl_allow(rl);
    ASSERT_TRUE(allowed, "first request allowed");
    free(rl);
    PASS();
}

static void test_rl_fixed_window(void)
{
    TEST("rl_fixed_window");
    RateLimiter *rl = rl_init(RL_FIXED_WINDOW, 5.0, 5.0, 1000);
    for (int i = 0; i < 5; i++)
        ASSERT_TRUE(rl_allow(rl), "within limit");
    ASSERT_TRUE(!rl_allow(rl), "exceeded limit");
    free(rl);
    PASS();
}

static void test_rl_client_isolation(void)
{
    TEST("rl_client_isolation");
    RateLimiter *rl = rl_init(RL_TOKEN_BUCKET, 100.0, 100.0, 0);
    for (int i = 0; i < 10; i++)
        ASSERT_TRUE(rl_allow(rl), "global allows");
    bool ret = rl_allow_client(rl, "client-a");
    ASSERT_TRUE(ret, "client rate limit works");
    free(rl);
    PASS();
}

/* ========== API Gateway Tests (L3, L6) ========== */

static void test_gw_init(void)
{
    TEST("gateway_init");
    APIGateway *gw = gateway_init();
    ASSERT_NOT_NULL(gw, "alloc failed");
    ASSERT_EQ(gw->num_routes, 0, "no routes");
    free(gw);
    PASS();
}

static void test_gw_route_registration(void)
{
    TEST("gateway_register_route");
    APIGateway *gw = gateway_init();
    int ret = gateway_register_route(gw, "/api/test", "GET", "test-svc", 8080, false, 0);
    ASSERT_EQ(ret, 0, "route registered");
    ASSERT_EQ(gw->num_routes, 1, "route count");
    APIRoute *r = gateway_match_route(gw, "/api/test", "GET");
    ASSERT_NOT_NULL(r, "route matched");
    ASSERT_TRUE(strcmp(r->upstream_host, "test-svc") == 0, "upstream host");
    free(gw);
    PASS();
}

static void test_gw_auth_required(void)
{
    TEST("gateway_auth_required");
    APIGateway *gw = gateway_init();
    gateway_register_route(gw, "/secure", "GET", "secure-svc", 8080, true, 0);
    GatewayRequest req;
    memset(&req, 0, sizeof(req));
    snprintf(req.method, sizeof(req.method), "GET");
    snprintf(req.path, sizeof(req.path), "/secure");
    snprintf(req.client_ip, sizeof(req.client_ip), "10.0.0.1");
    req.auth_token[0] = '\0';
    int ret = gateway_handle_request(gw, &req);
    ASSERT_EQ(ret, -1, "unauthorized blocked");
    ASSERT_TRUE(req.blocked, "request blocked");
    ASSERT_EQ(req.status_code, 401, "401 status");
    free(gw);
    PASS();
}

/* ========== Service Registry Tests (L2, L6) ========== */

static void test_sr_init(void)
{
    TEST("sr_init");
    ServiceRegistry *sr = sr_init("example.com");
    ASSERT_NOT_NULL(sr, "alloc failed");
    ASSERT_EQ(sr->num_services, 0, "empty registry");
    free(sr);
    PASS();
}

static void test_sr_register_and_lookup(void)
{
    TEST("sr_register/lookup");
    ServiceRegistry *sr = sr_init(NULL);
    SRService *svc = sr_register_service(sr, "auth-svc", 30);
    ASSERT_NOT_NULL(svc, "service created");
    SRService *found = sr_lookup_service(sr, "auth-svc");
    ASSERT_NOT_NULL(found, "lookup found");
    ASSERT_TRUE(strcmp(found->service_name, "auth-svc") == 0, "name match");
    free(sr);
    PASS();
}

static void test_sr_endpoint_lifecycle(void)
{
    TEST("sr_endpoint_lifecycle");
    ServiceRegistry *sr = sr_init(NULL);
    SRService *svc = sr_register_service(sr, "api", 30);
    sr_add_endpoint(svc, "10.0.0.1", 8080, 5);
    sr_add_endpoint(svc, "10.0.0.2", 8080, 3);
    ASSERT_EQ(svc->num_endpoints, 2, "endpoints added");
    sr_heartbeat_ping(sr, "api", "10.0.0.1", 8080, true);
    ASSERT_EQ(svc->endpoints[0].failure_count, 0, "failures reset");
    sr_remove_endpoint(svc, "10.0.0.2", 8080);
    ASSERT_EQ(svc->num_endpoints, 1, "endpoint removed");
    free(sr);
    PASS();
}

/* ========== Middleware Tests (L3, L5) ========== */

static void test_mw_chain_init(void)
{
    TEST("mw_chain_init");
    MiddlewareChain *chain = mw_chain_init();
    ASSERT_NOT_NULL(chain, "alloc failed");
    ASSERT_EQ(chain->count, 0, "empty chain");
    free(chain);
    PASS();
}

static int mw_test_handler(MiddlewareContext *ctx, void *next)
{
    (void)next;
    mw_context_set(ctx, "test.passed", "yes");
    return 0;
}

static void test_mw_chain_run(void)
{
    TEST("mw_chain_run");
    MiddlewareChain *chain = mw_chain_init();
    mw_chain_use(chain, "test-mw", mw_test_handler, NULL);
    HttpRequest *req = hm_request_init();
    HttpResponse *resp = hm_response_init(200, "OK");
    MiddlewareContext *ctx = mw_context_init(req, resp);
    int ret = mw_chain_run(chain, ctx);
    ASSERT_EQ(ret, 0, "chain ran");
    const char *val = mw_context_get(ctx, "test.passed");
    ASSERT_NOT_NULL(val, "context has value");
    ASSERT_TRUE(strcmp(val, "yes") == 0, "context value correct");
    mw_context_free(ctx);
    hm_response_free(resp);
    hm_request_free(req);
    free(chain);
    PASS();
}

static void test_mw_builtin_auth(void)
{
    TEST("mw_builtin_auth");
    HttpRequest *req = hm_request_init();
    HttpResponse *resp = hm_response_init(200, "OK");
    MiddlewareContext *ctx = mw_context_init(req, resp);
    int ret = mw_builtin_auth(ctx, NULL);
    ASSERT_EQ(ret, -1, "auth fails without header");
    ASSERT_TRUE(ctx->aborted, "context aborted");
    mw_context_free(ctx);
    hm_response_free(resp);
    hm_request_free(req);
    PASS();
}

/* ========== TLS Context Tests (L4, L8) ========== */

static void test_tls_init(void)
{
    TEST("tls_context_init");
    TLSContext *ctx = tls_context_init(TLS_1_2, TLS_1_3);
    ASSERT_NOT_NULL(ctx, "alloc failed");
    ASSERT_EQ(ctx->min_version, TLS_1_2, "min version");
    ASSERT_EQ(ctx->max_version, TLS_1_3, "max version");
    tls_context_free(ctx);
    PASS();
}

static void test_tls_cipher_negotiation(void)
{
    TEST("tls_cipher_negotiation");
    TLSContext *ctx = tls_context_init(TLS_1_2, TLS_1_3);
    tls_add_cipher_suite(ctx, TLS_CIPHER_AES256_GCM_SHA384);
    tls_add_cipher_suite(ctx, TLS_CIPHER_AES128_GCM_SHA256);
    TLSCipherSuite client_ciphers[] = {
        TLS_CIPHER_RSA_AES128_SHA, TLS_CIPHER_AES128_GCM_SHA256
    };
    TLSCipherSuite agreed;
    int ret = tls_negotiate_cipher(ctx, client_ciphers, 2, &agreed);
    ASSERT_EQ(ret, 0, "negotiation succeeded");
    ASSERT_EQ(agreed, TLS_CIPHER_AES128_GCM_SHA256, "strongest common");
    tls_context_free(ctx);
    PASS();
}

static void test_tls_version_negotiation(void)
{
    TEST("tls_version_negotiation");
    TLSContext *ctx = tls_context_init(TLS_1_2, TLS_1_3);
    TLSVersion agreed;
    int ret = tls_negotiate_version(ctx, TLS_1_2, &agreed);
    ASSERT_EQ(ret, 0, "negotiation succeeded");
    ASSERT_EQ(agreed, TLS_1_2, "agreed 1.2");
    tls_context_free(ctx);
    PASS();
}

/* ========== Connection Pool Tests (L3, L4, L5) ========== */

static void test_cp_init(void)
{
    TEST("cp_init");
    ConnectionPool cp;
    ConnectionPool *ret = cp_init(&cp, "db.example.com", 5432, 10);
    ASSERT_NOT_NULL(ret, "init failed");
    ASSERT_EQ(cp.max_conn, 10, "max_conn");
    ASSERT_EQ(cp.port, 5432, "port");
    ASSERT_EQ(cp.num_conn, 0, "empty pool");
    PASS();
}

static void test_cp_acquire(void)
{
    TEST("cp_acquire");
    ConnectionPool cp;
    cp_init(&cp, "cache.local", 6379, 5);
    CPConnection *c1 = cp_acquire(&cp);
    ASSERT_NOT_NULL(c1, "first acquire");
    ASSERT_EQ(c1->state, CP_ACTIVE, "active state");
    ASSERT_EQ(cp.active_count, 1, "active count");
    CPConnection *c2 = cp_acquire(&cp);
    ASSERT_NOT_NULL(c2, "second acquire");
    ASSERT_TRUE(c1 != c2, "different connections");
    cp_release(&cp, c2, false);
    cp_release(&cp, c1, false);
    ASSERT_EQ(cp.active_count, 0, "all connections released");
    PASS();
}

/* ========== Main ========== */

int main(void)
{
    printf("\n============================================\n");
    printf("  mini-gateway-proxy Test Suite\n");
    printf("============================================\n\n");

    /* HTTP Message Tests */
    test_hm_request_init();
    test_hm_method_enum();
    test_hm_parse_request_line();
    test_hm_parse_header_line();
    test_hm_set_and_get_header();
    test_hm_remove_header();
    test_hm_parse_request();
    test_hm_validate_method();
    test_hm_validate_uri();
    test_hm_normalize_uri();
    test_hm_status_reason();
    test_hm_response_init();
    test_hm_build_response();
    test_hm_chunked();
    test_hm_query_string();
    test_hm_content_type();
    test_hm_negotiate_accept();
    test_hm_security_audit();

    /* Load Balancer Tests */
    test_lb_init();
    test_lb_add_server();
    test_lb_round_robin();
    test_lb_consistent_hash();

    /* Circuit Breaker Tests */
    test_cb_init();
    test_cb_closed_to_open();
    test_cb_success_resets();

    /* Rate Limiter Tests */
    test_rl_token_bucket();
    test_rl_fixed_window();
    test_rl_client_isolation();

    /* API Gateway Tests */
    test_gw_init();
    test_gw_route_registration();
    test_gw_auth_required();

    /* Service Registry Tests */
    test_sr_init();
    test_sr_register_and_lookup();
    test_sr_endpoint_lifecycle();

    /* Middleware Tests */
    test_mw_chain_init();
    test_mw_chain_run();
    test_mw_builtin_auth();

    /* TLS Context Tests */
    test_tls_init();
    test_tls_cipher_negotiation();
    test_tls_version_negotiation();

    /* Connection Pool Tests */
    test_cp_init();
    test_cp_acquire();

    printf("\n============================================\n");
    printf("  Results: %d run, %d passed, %d failed\n",
           tests_run, tests_passed, tests_failed);
    printf("============================================\n\n");

    return tests_failed > 0 ? 1 : 0;
}