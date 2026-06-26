#include "rpc_encoding.h"
#include "rpc_transport.h"
#include "rpc_registry.h"
#include "rpc_stub.h"
#include "rpc_interceptor.h"
#include "rpc_protocol.h"
#include "rpc_server.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <math.h>

/*
 * test_main.c - Comprehensive test suite for mini-rpc-framework
 *
 * Tests cover:
 * - L1: Core data structures (init/free/field validation)
 * - L2: Encoding/decoding round-trips
 * - L3: Protocol framing with CRC32
 * - L4: CRC32 error detection (Shannon bound verification)
 * - L5: Version negotiation algorithm, load balancing
 * - L6: Server lifecycle (init → register → stats → shutdown)
 * - L7: Full RPC call simulation via interceptor chain
 * - L8: Streaming protocol, back-pressure, thread pool
 */

static int g_tests_passed = 0;
static int g_tests_failed = 0;

#define TEST(name) \
    printf("  TEST: %-50s ", name)

#define PASS() \
    do { printf("[PASS]\n"); g_tests_passed++; } while(0)

#define FAIL(msg) \
    do { printf("[FAIL] %s\n", msg); g_tests_failed++; } while(0)

#define ASSERT_EQ(a, b, msg) \
    do { if ((a) != (b)) { FAIL(msg); return; } } while(0)

#define ASSERT_TRUE(cond, msg) \
    do { if (!(cond)) { FAIL(msg); return; } } while(0)

#define ASSERT_DOUBLE_EQ(a, b, eps, msg) \
    do { if (fabs((a) - (b)) > (eps)) { FAIL(msg); return; } } while(0)

/* ================================================================
 * L1: Core Definitions - Data structure validation
 * ================================================================ */

static void test_buffer_init_free(void) {
    TEST("Buffer init/free lifecycle");
    RPCBuffer buf;
    rpc_buffer_init(&buf);
    ASSERT_TRUE(buf.data != NULL, "buffer data is NULL after init");
    ASSERT_EQ(buf.len, (size_t)0, "buffer len not zero after init");
    ASSERT_EQ(buf.capacity, (size_t)RPC_BUFFER_INIT_CAPACITY,
              "buffer capacity mismatch");

    rpc_buffer_append(&buf, (const uint8_t *)"hello", 5);
    ASSERT_EQ(buf.len, (size_t)5, "buffer len after append");

    rpc_buffer_free(&buf);
    ASSERT_TRUE(buf.data == NULL, "buffer data not NULL after free");
    ASSERT_EQ(buf.len, (size_t)0, "buffer len not zero after free");
    PASS();
}

static void test_message_init_free(void) {
    TEST("Message init/free lifecycle");
    RPCMessage msg;
    rpc_message_init(&msg);
    ASSERT_EQ(msg.id, 0, "message id not zero after init");
    ASSERT_EQ(msg.param_count, 0, "param_count not zero");
    ASSERT_TRUE(msg.is_request, "is_request not true by default");
    ASSERT_TRUE(!msg.is_error, "is_error true by default");

    msg.param_count = 2;
    msg.params[0].type = RPC_TYPE_STRING;
    msg.params[0].value.v_string = strdup("test");
    msg.params[1].type = RPC_TYPE_INT32;
    msg.params[1].value.v_int32 = 42;

    rpc_message_free(&msg);
    ASSERT_EQ(msg.param_count, 0, "param_count not reset after free");
    ASSERT_TRUE(msg.params[0].type == RPC_TYPE_NULL,
                "param not cleared after free");
    PASS();
}

static void test_value_types(void) {
    TEST("RPCValue type system");
    RPCValue v;
    memset(&v, 0, sizeof(v));

    v.type = RPC_TYPE_INT32;
    v.value.v_int32 = -42;
    ASSERT_EQ(v.value.v_int32, (int32_t)-42, "INT32 value mismatch");

    v.type = RPC_TYPE_BOOL;
    v.value.v_bool = true;
    ASSERT_TRUE(v.value.v_bool, "BOOL value mismatch");

    v.type = RPC_TYPE_FLOAT;
    v.value.v_float = 3.14159;
    ASSERT_DOUBLE_EQ(v.value.v_float, 3.14159, 1e-5, "FLOAT value mismatch");

    v.type = RPC_TYPE_NULL;
    ASSERT_TRUE(v.type == RPC_TYPE_NULL, "NULL type mismatch");
    PASS();
}

/* ================================================================
 * L2: JSON Encoding / Decoding Round-trips
 * ================================================================ */

static void test_json_roundtrip_int(void) {
    TEST("JSON round-trip (INT32 params)");
    RPCMessage req;
    rpc_message_init(&req);
    req.id = 42;
    strncpy(req.method_name, "Calculator.add", RPC_MAX_METHOD_NAME - 1);
    req.param_count = 2;
    req.params[0].type = RPC_TYPE_INT32;
    req.params[0].value.v_int32 = 100;
    req.params[1].type = RPC_TYPE_INT32;
    req.params[1].value.v_int32 = 200;
    req.is_request = true;

    RPCBuffer buf;
    rpc_buffer_init(&buf);
    int ret = rpc_encode_json(&req, &buf);
    ASSERT_EQ(ret, 0, "JSON encode failed");
    ASSERT_TRUE(buf.len > 0, "encoded buffer is empty");

    RPCMessage decoded;
    rpc_message_init(&decoded);
    ret = rpc_decode_json(&buf, &decoded);
    ASSERT_EQ(ret, 0, "JSON decode failed");
    ASSERT_EQ(decoded.id, (int32_t)42, "decoded id mismatch");
    ASSERT_EQ(decoded.param_count, 2, "decoded param_count mismatch");
    ASSERT_EQ(strcmp(decoded.method_name, "Calculator.add"), 0,
              "decoded method_name mismatch");
    ASSERT_EQ(decoded.params[0].value.v_int32, (int32_t)100,
              "decoded param[0] mismatch");
    ASSERT_EQ(decoded.params[1].value.v_int32, (int32_t)200,
              "decoded param[1] mismatch");

    rpc_message_free(&req);
    rpc_message_free(&decoded);
    rpc_buffer_free(&buf);
    PASS();
}

static void test_json_string_bool(void) {
    TEST("JSON round-trip (STRING+BOOL params)");
    RPCMessage req;
    rpc_message_init(&req);
    req.id = 1;
    strncpy(req.method_name, "User.create", RPC_MAX_METHOD_NAME - 1);
    req.param_count = 2;
    req.params[0].type = RPC_TYPE_STRING;
    req.params[0].value.v_string = strdup("alice");
    req.params[1].type = RPC_TYPE_BOOL;
    req.params[1].value.v_bool = true;

    RPCBuffer buf;
    rpc_buffer_init(&buf);
    ASSERT_EQ(rpc_encode_json(&req, &buf), 0, "JSON encode failed");

    RPCMessage decoded;
    rpc_message_init(&decoded);
    ASSERT_EQ(rpc_decode_json(&buf, &decoded), 0, "JSON decode failed");
    ASSERT_EQ(decoded.param_count, 2, "param_count mismatch");
    ASSERT_EQ(decoded.params[0].type, RPC_TYPE_STRING, "param[0] type mismatch");
    ASSERT_EQ(strcmp(decoded.params[0].value.v_string, "alice"), 0,
              "param[0] value mismatch");
    ASSERT_EQ(decoded.params[1].type, RPC_TYPE_BOOL, "param[1] type mismatch");
    ASSERT_EQ(decoded.params[1].value.v_bool, (int)true, "param[1] value mismatch");

    rpc_message_free(&req);
    rpc_message_free(&decoded);
    rpc_buffer_free(&buf);
    PASS();
}

static void test_json_error_response(void) {
    TEST("JSON error response decode");
    RPCMessage msg;
    rpc_message_init(&msg);

    RPCBuffer buf;
    rpc_buffer_init(&buf);
    const char *err_json = "{\"id\":1,\"error\":\"division by zero\"}";
    rpc_buffer_append(&buf, (const uint8_t *)err_json, strlen(err_json));

    ASSERT_EQ(rpc_decode_json(&buf, &msg), 0, "JSON error decode failed");
    ASSERT_EQ(msg.id, (int32_t)1, "error message id mismatch");
    ASSERT_TRUE(msg.is_error, "is_error not set");
    ASSERT_EQ(strcmp(msg.error_msg, "division by zero"), 0,
              "error message mismatch");

    rpc_message_free(&msg);
    rpc_buffer_free(&buf);
    PASS();
}

/* ================================================================
 * L2: Binary Encoding Round-trips
 * ================================================================ */

static void test_binary_roundtrip(void) {
    TEST("Binary encode/decode round-trip");
    RPCMessage req;
    rpc_message_init(&req);
    req.id = 7;
    strncpy(req.method_name, "test.method", RPC_MAX_METHOD_NAME - 1);
    req.param_count = 3;
    req.params[0].type = RPC_TYPE_INT32;
    req.params[0].value.v_int32 = 123;
    req.params[1].type = RPC_TYPE_STRING;
    req.params[1].value.v_string = strdup("hello");
    req.params[2].type = RPC_TYPE_BOOL;
    req.params[2].value.v_bool = true;
    req.is_request = true;

    RPCBuffer buf;
    rpc_buffer_init(&buf);
    ASSERT_EQ(rpc_encode_binary(&req, &buf), 0, "binary encode failed");
    ASSERT_TRUE(buf.len > 0, "encoded buffer empty");

    RPCMessage decoded;
    rpc_message_init(&decoded);
    ASSERT_EQ(rpc_decode_binary(&buf, &decoded), 0, "binary decode failed");
    ASSERT_EQ(decoded.param_count, 3, "decoded param_count mismatch");
    ASSERT_EQ(decoded.params[0].type, RPC_TYPE_INT32, "param[0] type mismatch");
    ASSERT_EQ(decoded.params[0].value.v_int32, (int32_t)123,
              "param[0] value mismatch");
    ASSERT_EQ(decoded.params[1].type, RPC_TYPE_STRING, "param[1] type mismatch");
    ASSERT_EQ(decoded.params[2].type, RPC_TYPE_BOOL, "param[2] type mismatch");
    ASSERT_EQ(decoded.params[2].value.v_bool, (int)true,
              "param[2] value mismatch");

    rpc_message_free(&req);
    rpc_message_free(&decoded);
    rpc_buffer_free(&buf);
    PASS();
}

/* ================================================================
 * L4: CRC32 Error Detection (Shannon's Theorem)
 * ================================================================ */

static void test_crc32_known_values(void) {
    TEST("CRC32 known test vectors (IEEE 802.3)");
    rpc_crc32_init();

    /* Empty input: CRC32("") = 0x00000000 (after finalize XOR) */
    uint32_t crc_empty = rpc_crc32_compute(NULL, 0);
    /* CRC32 of empty bytes with init=0xFFFFFFFF, final XOR */
    ASSERT_EQ(crc_empty, (uint32_t)0x00000000,
              "CRC32 of empty input mismatch");

    /* "123456789" -> 0xCBF43926 (standard check value) */
    const uint8_t *check_str = (const uint8_t *)"123456789";
    uint32_t crc_check = rpc_crc32_compute(check_str, 9);
    ASSERT_EQ(crc_check, (uint32_t)0xCBF43926,
              "CRC32 standard check value mismatch");

    /* CRC32 is not linear for multi-part: update API test */
    uint32_t crc1 = rpc_crc32_update(0xFFFFFFFF, check_str, 4);
    crc1 = rpc_crc32_update(crc1, check_str + 4, 5);
    crc1 = rpc_crc32_finalize(crc1);
    ASSERT_EQ(crc1, (uint32_t)0xCBF43926,
              "CRC32 incremental update mismatch");
    PASS();
}

static void test_crc32_error_detection(void) {
    TEST("CRC32 error detection capability");
    const uint8_t data[] = "Hello, RPC Protocol Frame!";
    size_t len = strlen((const char *)data);

    uint32_t crc = rpc_crc32_compute(data, len);

    /* Corrupt one byte and verify CRC changes (single-bit error detection) */
    uint8_t corrupted[64];
    memcpy(corrupted, data, len);
    corrupted[5] ^= 0x01;  /* Flip one bit */

    uint32_t crc_corrupt = rpc_crc32_compute(corrupted, len);
    ASSERT_TRUE(crc != crc_corrupt, "CRC32 failed to detect 1-bit error");

    /* Corrupt two bytes (double-bit error) */
    corrupted[10] ^= 0x80;
    uint32_t crc_corrupt2 = rpc_crc32_compute(corrupted, len);
    ASSERT_TRUE(crc != crc_corrupt2, "CRC32 failed to detect 2-bit error");

    /* Shannon bound: P(undetected error) <= 2^(-32) */
    double bound = rpc_proto_error_bound(len * 8);
    ASSERT_DOUBLE_EQ(bound, 2.3283064365386963e-10, 1e-16,
                     "CRC32 error bound mismatch (should be 2^-32)");
    PASS();
}

/* ================================================================
 * L3: Protocol Framing with CRC32
 * ================================================================ */

static void test_protocol_framing(void) {
    TEST("Protocol frame build/parse round-trip");
    rpc_crc32_init();

    RPCProtoFrame frame;
    memset(&frame, 0, sizeof(frame));
    frame.version = (uint32_t)RPC_PROTO_VER_1_0;
    frame.frame_type = RPC_FRAME_UNARY;
    frame.stream_id = 0;
    frame.sequence = 0;
    frame.flags = 0x00;

    RPCBuffer payload;
    rpc_buffer_init(&payload);
    const char *payload_str = "This is an RPC request payload";
    rpc_buffer_append(&payload, (const uint8_t *)payload_str, strlen(payload_str));

    RPCBuffer framed;
    rpc_buffer_init(&framed);

    int ret = rpc_proto_frame_build(&frame, &payload, &framed);
    ASSERT_EQ(ret, 0, "frame build failed");
    ASSERT_TRUE(framed.len > RPC_PROTO_OVERHEAD, "frame too small");

    /* Parse back */
    RPCProtoFrame parsed_frame;
    memset(&parsed_frame, 0, sizeof(parsed_frame));
    RPCBuffer parsed_payload;
    rpc_buffer_init(&parsed_payload);

    ret = rpc_proto_frame_parse(&framed, &parsed_frame, &parsed_payload);
    ASSERT_EQ(ret, 0, "frame parse failed");
    ASSERT_EQ(parsed_frame.magic, (uint32_t)RPC_PROTO_MAGIC, "magic mismatch");
    ASSERT_EQ(parsed_frame.frame_type, (uint8_t)RPC_FRAME_UNARY,
              "frame_type mismatch");
    ASSERT_EQ(parsed_payload.len, payload.len, "payload length mismatch");
    ASSERT_EQ(memcmp(parsed_payload.data, payload.data, payload.len), 0,
              "payload content mismatch");

    /* Test CRC error detection by corrupting the frame */
    framed.data[RPC_PROTO_HEADER_SIZE + 2] ^= 0xFF;  /* Corrupt payload */
    RPCBuffer bad_payload;
    rpc_buffer_init(&bad_payload);
    RPCProtoFrame bad_frame;
    memset(&bad_frame, 0, sizeof(bad_frame));

    ret = rpc_proto_frame_parse(&framed, &bad_frame, &bad_payload);
    ASSERT_EQ(ret, -4, "CRC32 should detect corrupted frame (ret != -4)");

    rpc_buffer_free(&payload);
    rpc_buffer_free(&framed);
    rpc_buffer_free(&parsed_payload);
    rpc_buffer_free(&bad_payload);
    PASS();
}

static void test_protocol_session(void) {
    TEST("Protocol session handshake");
    RPCProtocolSession sess;
    rpc_proto_session_init(&sess);
    ASSERT_TRUE(sess.next_stream_id > 0, "next_stream_id not initialized");
    ASSERT_TRUE(!sess.handshake_complete, "handshake already complete");

    /* Build and parse handshake */
    RPCBuffer hs_out;
    rpc_buffer_init(&hs_out);
    ASSERT_EQ(rpc_proto_build_handshake(&sess, &hs_out), 0,
              "handshake build failed");
    ASSERT_TRUE(sess.handshake_complete, "handshake not marked complete");

    /* Verify session stats */
    uint64_t sent, recv, crc_err;
    rpc_proto_session_stats(&sess, &sent, &recv, &crc_err);
    ASSERT_EQ(sent, (uint64_t)0, "sent count non-zero initially");
    ASSERT_EQ(recv, (uint64_t)0, "recv count non-zero initially");

    rpc_proto_session_free(&sess);
    rpc_buffer_free(&hs_out);
    PASS();
}

/* ================================================================
 * L5: Version Negotiation Algorithm
 * ================================================================ */

static void test_version_negotiation(void) {
    TEST("Protocol version negotiation");
    /* Same versions */
    ASSERT_EQ(rpc_proto_version_compare(RPC_PROTO_VER_1_0, RPC_PROTO_VER_1_0),
              0, "same version should be equal");

    /* v2 > v1 */
    ASSERT_EQ(rpc_proto_version_compare(RPC_PROTO_VER_2_0, RPC_PROTO_VER_1_0),
              1, "v2 should be greater than v1");

    /* v1 < v2 */
    ASSERT_EQ(rpc_proto_version_compare(RPC_PROTO_VER_1_0, RPC_PROTO_VER_2_0),
              -1, "v1 should be less than v2");

    /* ANY matches everything */
    ASSERT_EQ(rpc_proto_version_compare(RPC_PROTO_VER_ANY, RPC_PROTO_VER_1_0),
              0, "ANY should match v1");
    ASSERT_EQ(rpc_proto_version_compare(RPC_PROTO_VER_1_0, RPC_PROTO_VER_ANY),
              0, "v1 should match ANY");

    /* Negotiation: select lower version */
    RPCProtoVersion neg = rpc_proto_version_negotiate(
        RPC_PROTO_VER_1_0, RPC_PROTO_VER_2_0);
    ASSERT_EQ(neg, RPC_PROTO_VER_1_0, "should negotiate to v1 (lower)");

    neg = rpc_proto_version_negotiate(RPC_PROTO_VER_ANY, RPC_PROTO_VER_2_0);
    ASSERT_EQ(neg, RPC_PROTO_VER_2_0, "ANY should accept remote version");

    PASS();
}

/* ================================================================
 * L5: Service Registry and Load Balancing
 * ================================================================ */

static int test_handler_add(const RPCMessage *req, RPCMessage *resp) {
    (void)req;
    resp->param_count = 1;
    resp->params[0].type = RPC_TYPE_INT32;
    resp->params[0].value.v_int32 = 42;
    return 0;
}

static void test_registry_operations(void) {
    TEST("Service registry register/discover/lb");
    ServiceRegistry reg;
    registry_init(&reg);

    /* Register service */
    ServiceDescriptor svc;
    memset(&svc, 0, sizeof(svc));
    strncpy(svc.service_name, "TestService", RPC_MAX_NAME_LEN - 1);
    svc.version = 1;
    svc.method_count = 1;
    strncpy(svc.methods[0].name, "testMethod", RPC_MAX_NAME_LEN - 1);
    svc.methods[0].handler = test_handler_add;
    svc.methods[0].timeout_ms = 1000;

    int idx = registry_register(&reg, &svc);
    ASSERT_TRUE(idx >= 0, "service registration failed");

    /* Add instances (registry_add_instance returns instance index) */
    int inst1 = registry_add_instance(&reg, "TestService",
                                       "10.0.0.1", 8080, 100);
    ASSERT_TRUE(inst1 >= 0, "add instance 1 failed");

    int inst2 = registry_add_instance(&reg, "TestService",
                                       "10.0.0.2", 8080, 200);
    ASSERT_TRUE(inst2 >= 0, "add instance 2 failed");

    int inst3 = registry_add_instance(&reg, "TestService",
                                       "10.0.0.3", 8080, 50);
    ASSERT_TRUE(inst3 >= 0, "add instance 3 failed");
    (void)inst1; (void)inst2; (void)inst3;

    /* Discover */
    ServiceInstance instances[16];
    int found = registry_discover(&reg, "TestService", instances, 16);
    ASSERT_EQ(found, 3, "discover found wrong count");

    /* Load balancing - select should return valid index */
    int lb_idx = registry_lb_select(&reg, "TestService");
    ASSERT_TRUE(lb_idx >= 0 && lb_idx < 3, "lb_select returned invalid index");

    /* Round-robin */
    int32_t cursor = 0;
    for (int i = 0; i < 10; i++) {
        int rr = registry_lb_round_robin(&reg, "TestService", &cursor);
        ASSERT_TRUE(rr >= 0 && rr < 3, "round_robin returned invalid index");
    }

    /* Heartbeat */
    ASSERT_EQ(registry_heartbeat(&reg, "TestService", "10.0.0.1", 8080), 0,
              "heartbeat failed");

    /* Health check */
    int unhealthy = registry_health_check(&reg);
    ASSERT_EQ(unhealthy, 0, "unexpected unhealthy instances");

    /* Remove instance */
    ASSERT_EQ(registry_remove_instance(&reg, "TestService",
                                        "10.0.0.3", 8080), 0,
              "remove instance failed");
    found = registry_discover(&reg, "TestService", instances, 16);
    ASSERT_EQ(found, 2, "instance count after removal wrong");

    /* Unregister */
    ASSERT_EQ(registry_unregister(&reg, "TestService"), 0,
              "unregister failed");
    ASSERT_TRUE(registry_lookup(&reg, "TestService") == NULL,
                "service still found after unregister");

    PASS();
}

/* ================================================================
 * L5: FNV-1a Hash
 * ================================================================ */

static void test_fnv1a_hash(void) {
    TEST("FNV-1a hash consistency");
    int32_t h1 = rpc_fnv1a_hash("hello", 5);
    int32_t h2 = rpc_fnv1a_hash("hello", 5);
    ASSERT_EQ(h1, h2, "same input produced different hashes");

    int32_t h3 = rpc_fnv1a_hash("world", 5);
    ASSERT_TRUE(h1 != h3, "different inputs produced same hash");
    PASS();
}

/* ================================================================
 * L6: Server Lifecycle (no network, unit-test mode)
 * ================================================================ */

static int echo_handler(const RPCMessage *req, RPCMessage *resp) {
    resp->id = req->id;
    resp->is_request = false;
    resp->param_count = 1;
    resp->params[0].type = RPC_TYPE_STRING;
    resp->params[0].value.v_string = strdup("echo_ok");
    return 0;
}

static void test_server_lifecycle(void) {
    TEST("Server init/register/stats/shutdown lifecycle");
    RPCServer srv;
    rpc_server_init(&srv, "127.0.0.1", 19999);

    /* Register a handler */
    int reg_ret = rpc_server_register_service(&srv, "EchoService",
                                               "echo", echo_handler, 1000);
    ASSERT_TRUE(reg_ret >= 0, "service registration failed");
    ASSERT_EQ(srv.dispatch_count, 1, "dispatch_count after registration");

    /* Lookup */
    int lookup = rpc_server_lookup_method(&srv, "echo");
    ASSERT_TRUE(lookup >= 0, "method lookup failed");
    ASSERT_EQ(strcmp(srv.dispatch_table[lookup].method_name, "echo"), 0,
              "lookup returned wrong method");

    /* Stats before processing */
    double throughput, latency, little_l;
    int32_t qdepth;
    rpc_server_stats(&srv, &throughput, &latency, &qdepth, &little_l);
    ASSERT_TRUE(throughput >= 0.0, "invalid throughput");
    ASSERT_EQ(qdepth, 0, "queue should be empty");

    /* Unregister */
    ASSERT_EQ(rpc_server_unregister_service(&srv, "EchoService", "echo"), 0,
              "unregister failed");
    ASSERT_EQ(srv.dispatch_count, 0, "dispatch_count after unregister");

    rpc_server_free(&srv);
    PASS();
}

/* ================================================================
 * L7: Interceptor Chain
 * ================================================================ */

static void test_interceptor_chain_basic(void) {
    TEST("Interceptor chain add/remove/invoke");
    InterceptorChain chain;
    interceptor_chain_init(&chain, "TestChain");

    RPCInterceptor log_ic = interceptor_make_logging();
    ASSERT_EQ(interceptor_chain_add(&chain, &log_ic), 0, "add logging failed");
    ASSERT_EQ(chain.count, 1, "chain count after add");

    RPCInterceptor auth_ic = interceptor_make_auth("test-token");
    ASSERT_EQ(interceptor_chain_add(&chain, &auth_ic), 0, "add auth failed");
    ASSERT_EQ(chain.count, 2, "chain count after second add");

    /* Verify priority ordering: auth(50) < logging(100) */
    /* Insert preserves priority order */
    ASSERT_EQ(strcmp(chain.interceptors[0].name, "auth"), 0,
              "priority ordering incorrect");

    /* Invoke before chain */
    RPCMessage req;
    rpc_message_init(&req);
    req.id = 1;
    strncpy(req.method_name, "Test.method", RPC_MAX_METHOD_NAME - 1);

    RPCMessage req_out;
    rpc_message_init(&req_out);

    int ret = interceptor_before_invoke(&chain, &req, &req_out);
    ASSERT_EQ(ret, 0, "before_invoke failed");

    /* Remove an interceptor */
    ASSERT_EQ(interceptor_chain_remove(&chain, "auth"), 0,
              "remove auth failed");
    ASSERT_EQ(chain.count, 1, "chain count after remove");

    rpc_message_free(&req);
    rpc_message_free(&req_out);
    PASS();
}

static void test_interceptor_rate_limit(void) {
    TEST("Rate limit interceptor functionality");
    RPCInterceptor rl = interceptor_make_rate_limit(2);  /* 2 req/sec */
    ASSERT_TRUE(rl.enabled, "rate limit not enabled");
    ASSERT_TRUE(rl.before_fn != NULL, "rate limit before_fn NULL");

    /* First 2 requests should pass */
    RPCMessage req, req_out;
    rpc_message_init(&req);
    req.id = 1;
    rpc_message_init(&req_out);

    int ret1 = rl.before_fn(rl.context, &req, &req_out);
    ASSERT_EQ(ret1, 0, "first request should pass");

    int ret2 = rl.before_fn(rl.context, &req, &req_out);
    ASSERT_EQ(ret2, 0, "second request should pass");

    /* Third request should be rate-limited */
    int ret3 = rl.before_fn(rl.context, &req, &req_out);
    ASSERT_EQ(ret3, -1, "third request should be rate-limited");

    rpc_message_free(&req);
    rpc_message_free(&req_out);
    PASS();
}

/* ================================================================
 * L5/L8: Streaming Protocol
 * ================================================================ */

static void test_streaming_protocol(void) {
    TEST("Stream open/send/recv/close lifecycle");
    RPCProtocolSession sess;
    rpc_proto_session_init(&sess);

    /* Open a stream */
    int stream_id = rpc_proto_stream_open(&sess);
    ASSERT_TRUE(stream_id >= 0, "stream open failed");

    /* Send a chunk */
    RPCBuffer chunk;
    rpc_buffer_init(&chunk);
    const char *data = "streaming data chunk #1";
    rpc_buffer_append(&chunk, (const uint8_t *)data, strlen(data));

    RPCBuffer framed;
    rpc_buffer_init(&framed);
    int ret = rpc_proto_stream_send(&sess, (uint32_t)stream_id,
                                     &chunk, &framed);
    ASSERT_EQ(ret, 0, "stream send failed");
    ASSERT_TRUE(framed.len > RPC_PROTO_OVERHEAD, "framed output too small");
    ASSERT_EQ(sess.frames_sent, (uint64_t)1, "frames_sent not incremented");

    /* Receive the framed data back */
    RPCBuffer chunk_out;
    rpc_buffer_init(&chunk_out);
    RPCProtoFrame frame_out;
    memset(&frame_out, 0, sizeof(frame_out));

    ret = rpc_proto_stream_recv(&sess, &framed, &chunk_out, &frame_out);
    ASSERT_EQ(ret, 0, "stream recv failed");
    ASSERT_EQ(sess.frames_received, (uint64_t)1, "frames_received not incremented");
    ASSERT_EQ(chunk_out.len, chunk.len, "received chunk length mismatch");

    /* Check back-pressure (should be under watermark) */
    bool bp = rpc_proto_stream_is_backpressured(&sess, (uint32_t)stream_id);
    ASSERT_TRUE(!bp, "unexpected back-pressure");

    /* Close stream */
    RPCBuffer close_frame;
    rpc_buffer_init(&close_frame);
    ret = rpc_proto_stream_close(&sess, (uint32_t)stream_id, &close_frame);
    ASSERT_EQ(ret, 0, "stream close failed");

    rpc_proto_session_free(&sess);
    rpc_buffer_free(&chunk);
    rpc_buffer_free(&framed);
    rpc_buffer_free(&chunk_out);
    rpc_buffer_free(&close_frame);
    PASS();
}

static void test_keepalive_ping_pong(void) {
    TEST("Keepalive PING/PONG frames");
    RPCBuffer ping_buf, pong_buf;
    rpc_buffer_init(&ping_buf);
    rpc_buffer_init(&pong_buf);

    ASSERT_EQ(rpc_proto_build_ping(0, &ping_buf), 0, "build ping failed");
    ASSERT_EQ(rpc_proto_build_pong(0, &pong_buf), 0, "build pong failed");

    /* Parse and verify frame types */
    RPCProtoFrame ping_frame, pong_frame;
    RPCBuffer ping_payload, pong_payload;
    rpc_buffer_init(&ping_payload);
    rpc_buffer_init(&pong_payload);

    memset(&ping_frame, 0, sizeof(ping_frame));
    memset(&pong_frame, 0, sizeof(pong_frame));

    ASSERT_EQ(rpc_proto_frame_parse(&ping_buf, &ping_frame, &ping_payload), 0,
              "parse ping failed");
    ASSERT_EQ(rpc_proto_frame_parse(&pong_buf, &pong_frame, &pong_payload), 0,
              "parse pong failed");

    ASSERT_TRUE(rpc_proto_is_ping(&ping_frame), "is_ping check failed");
    ASSERT_TRUE(rpc_proto_is_pong(&pong_frame), "is_pong check failed");
    ASSERT_TRUE(!rpc_proto_is_ping(&pong_frame), "pong falsely identified as ping");

    rpc_buffer_free(&ping_buf);
    rpc_buffer_free(&pong_buf);
    rpc_buffer_free(&ping_payload);
    rpc_buffer_free(&pong_payload);
    PASS();
}

/* ================================================================
 * L4: Amdahl's Law demonstration
 * ================================================================ */

static void test_amdahls_law(void) {
    TEST("Amdahl's Law speedup calculation");
    /*
     * Amdahl's Law: speedup(N) = 1 / (S + (1-S)/N)
     * where S = serial fraction, N = number of processors
     *
     * For S=0.1 (10% serial), N=4:
     *   speedup = 1 / (0.1 + 0.9/4) = 1 / (0.1 + 0.225) = 1 / 0.325 = 3.0769
     */
    double S = 0.1;
    double N = 4.0;
    double speedup = 1.0 / (S + (1.0 - S) / N);
    ASSERT_DOUBLE_EQ(speedup, 3.076923076923077, 1e-6,
                     "Amdahl's Law speedup incorrect");

    /* Verify diminishing returns: speedup approaches 1/S = 10x as N -> inf */
    double theoretical_max = 1.0 / S;  /* 10x with S=0.1 */
    double N_large = 1000000.0;
    double speedup_large = 1.0 / (S + (1.0 - S) / N_large);
    /* speedup_large = 1/(0.1 + 0.9/1e6) = 1/0.1000009 = 9.99991 */
    ASSERT_DOUBLE_EQ(speedup_large, 9.99991, 1e-5,
                     "Amdahl's Law limits incorrect");
    (void)theoretical_max;

    PASS();
}

/* ================================================================
 * L5: Compression interface and work queue
 * ================================================================ */

static void test_compression_interface(void) {
    TEST("Compression pluggable codec system");
    /* Identity compression (no codec registered) */
    const uint8_t src[] = "test compression data for RPC protocol";
    size_t src_len = strlen((const char *)src);

    RPCBuffer out;
    rpc_buffer_init(&out);

    /* Compress with NONE (should be identity) */
    int ret = rpc_proto_compress(RPC_COMPRESS_NONE, src, src_len, &out);
    ASSERT_EQ(ret, 0, "compression failed");
    ASSERT_EQ(out.len, src_len, "identity compression changed length");
    ASSERT_EQ(memcmp(out.data, src, src_len), 0, "identity compression corrupted data");

    /* Decompress (should also be identity for NONE) */
    RPCBuffer decomp;
    rpc_buffer_init(&decomp);
    ret = rpc_proto_decompress(RPC_COMPRESS_NONE, out.data, out.len, &decomp);
    ASSERT_EQ(ret, 0, "decompression failed");
    ASSERT_EQ(decomp.len, src_len, "decompression length mismatch");
    ASSERT_EQ(memcmp(decomp.data, src, src_len), 0, "decompression data mismatch");

    /* Register a custom codec */
    ret = rpc_proto_compress_register(RPC_COMPRESS_ZLIB, NULL, NULL);
    ASSERT_EQ(ret, 0, "codec registration failed");

    rpc_buffer_free(&out);
    rpc_buffer_free(&decomp);
    PASS();
}

static void test_work_queue(void) {
    TEST("Work queue push/pop operations");
    RPCWorkQueue q;
    rpc_work_queue_init(&q);

    /* Push items */
    for (int32_t i = 0; i < 10; i++) {
        RPCWorkItem item;
        memset(&item, 0, sizeof(item));
        item.id = i;
        item.conn_idx = i;
        bool pushed = rpc_work_queue_push(&q, &item);
        ASSERT_TRUE(pushed, "work queue push failed");
    }
    ASSERT_EQ(q.count, 10, "work queue count mismatch");

    /* Pop items (should be FIFO order) */
    for (int32_t i = 0; i < 10; i++) {
        RPCWorkItem item;
        bool popped = rpc_work_queue_pop(&q, &item);
        ASSERT_TRUE(popped, "work queue pop failed");
        ASSERT_EQ(item.id, i, "work queue FIFO order violated");
    }

    /* Queue should be empty */
    RPCWorkItem empty_item;
    ASSERT_TRUE(!rpc_work_queue_pop(&q, &empty_item),
                "pop from empty queue should fail");

    /* Close and verify no more pushes */
    rpc_work_queue_close(&q);
    RPCWorkItem item;
    memset(&item, 0, sizeof(item));
    ASSERT_TRUE(!rpc_work_queue_push(&q, &item),
                "push to closed queue should fail");

    PASS();
}

/* ================================================================
 * Main test runner
 * ================================================================ */

int main(void) {
    printf("=== mini-rpc-framework Test Suite ===\n\n");
    printf("L1: Core Definitions\n");

    test_buffer_init_free();
    test_message_init_free();
    test_value_types();
    test_fnv1a_hash();

    printf("\nL2: JSON Encoding/Decoding\n");
    test_json_roundtrip_int();
    test_json_string_bool();
    test_json_error_response();

    printf("\nL2: Binary Encoding/Decoding\n");
    test_binary_roundtrip();

    printf("\nL4: CRC32 Error Detection\n");
    test_crc32_known_values();
    test_crc32_error_detection();

    printf("\nL3: Protocol Framing\n");
    test_protocol_framing();
    test_protocol_session();

    printf("\nL5: Version Negotiation\n");
    test_version_negotiation();

    printf("\nL5: Service Registry & Load Balancing\n");
    test_registry_operations();

    printf("\nL6: Server Lifecycle\n");
    test_server_lifecycle();

    printf("\nL7: Interceptor Chain\n");
    test_interceptor_chain_basic();
    test_interceptor_rate_limit();

    printf("\nL5/L8: Streaming Protocol\n");
    test_streaming_protocol();
    test_keepalive_ping_pong();

    printf("\nL4: Amdahl's Law\n");
    test_amdahls_law();

    printf("\nL5: Compression & Work Queue\n");
    test_compression_interface();
    test_work_queue();

    printf("\n========================================\n");
    printf("Results: %d passed, %d failed out of %d tests\n",
           g_tests_passed, g_tests_failed,
           g_tests_passed + g_tests_failed);
    printf("========================================\n");

    return g_tests_failed > 0 ? 1 : 0;
}
