/* mini-network-base: Unified Test Runner
 *
 * Tests all modules using assert-based verification.
 * Compile: make test
 * Run:     ./bin/test_runner.exe
 */

#include "socket_tcp.h"
#include "udp_dns.h"
#include "ip_packet.h"
#include "tls_handshake.h"
#include "http_basic.h"
#include "tcp_congestion.h"
#include "ip_routing.h"
#include "icmp_proto.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <math.h>

static int tests_passed = 0;
static int tests_failed = 0;
static int tests_total  = 0;

#define TEST(name) do { \
    tests_total++; \
    fprintf(stderr, "  TEST %3d: %-55s ... ", tests_total, name); \
} while(0)

#define PASS() do { \
    fprintf(stderr, "PASS\n"); \
    tests_passed++; \
} while(0)

#define FAIL(msg) do { \
    fprintf(stderr, "FAIL: %s\n", msg); \
    tests_failed++; \
} while(0)

#define CHECK(cond, msg) do { \
    if (!(cond)) { FAIL(msg); return; } \
} while(0)

/* ─────────────────────────────────────────────────────────── */
/*  TCP Socket Tests                                           */
/* ─────────────────────────────────────────────────────────── */

static void test_tcp_create(void) {
    TEST("tcp_socket_create allocates and initializes");
    TCPSocket *s = tcp_socket_create();
    CHECK(s != NULL, "socket is NULL");
    CHECK(s->state == TCP_CLOSED, "initial state not CLOSED");
    CHECK(s->mss == TCP_MAX_SEGMENT_SIZE, "MSS not default");
    CHECK(s->send_window == TCP_INITIAL_WINDOW, "send window not initialized");
    CHECK(s->recv_window == TCP_INITIAL_WINDOW, "recv window not initialized");
    tcp_socket_free(s);
    PASS();
}

static void test_tcp_state_names(void) {
    TEST("tcp_state_name returns correct strings");
    CHECK(strcmp(tcp_state_name(TCP_CLOSED), "CLOSED") == 0, "CLOSED mismatch");
    CHECK(strcmp(tcp_state_name(TCP_LISTEN), "LISTEN") == 0, "LISTEN mismatch");
    CHECK(strcmp(tcp_state_name(TCP_ESTABLISHED), "ESTABLISHED") == 0, "ESTABLISHED mismatch");
    CHECK(strcmp(tcp_state_name(TCP_TIME_WAIT), "TIME_WAIT") == 0, "TIME_WAIT mismatch");
    CHECK(strcmp(tcp_state_name((TCPState)99), "UNKNOWN") == 0, "unknown state");
    PASS();
}

static void test_tcp_connect(void) {
    TEST("tcp_connect transitions CLOSED->ESTABLISHED");
    TCPSocket *s = tcp_socket_create();
    CHECK(s != NULL, "create failed");
    int rc = tcp_connect(s, 0x7F000001, 8080);
    CHECK(rc == 0, "connect failed");
    CHECK(s->state == TCP_ESTABLISHED, "not ESTABLISHED after connect");
    CHECK(s->src_ip == 0x7F000001, "src_ip not localhost");
    CHECK(s->dst_port == 8080, "dst_port mismatch");
    tcp_socket_free(s);
    PASS();
}

static void test_tcp_connect_null(void) {
    TEST("tcp_connect NULL returns -1");
    int rc = tcp_connect(NULL, 0, 0);
    CHECK(rc == -1, "should return -1 for NULL");
    PASS();
}

static void test_tcp_bind_listen(void) {
    TEST("tcp_bind_listen sets LISTEN state");
    TCPSocket *s = tcp_socket_create();
    CHECK(s != NULL, "create failed");
    int rc = tcp_bind_listen(s, 8080);
    CHECK(rc == 0, "bind failed");
    CHECK(s->state == TCP_LISTEN, "not LISTEN");
    CHECK(s->passive == true, "not passive");
    CHECK(s->src_port == 8080, "port mismatch");
    tcp_socket_free(s);
    PASS();
}

static void test_tcp_accept(void) {
    TEST("tcp_accept creates child socket");
    TCPSocket *server = tcp_socket_create();
    tcp_bind_listen(server, 8080);
    TCPSocket *child = tcp_accept(server);
    CHECK(child != NULL, "accept returned NULL");
    CHECK(child->state == TCP_ESTABLISHED, "child not ESTABLISHED");
    CHECK(child->passive == true, "child not passive");
    tcp_socket_free(child);
    tcp_socket_free(server);
    PASS();
}

static void test_tcp_send_recv(void) {
    TEST("tcp_send and tcp_recv transfer data");
    TCPSocket *client = tcp_socket_create();
    tcp_connect(client, 0x7F000001, 8080);
    const uint8_t msg[] = "Hello TCP";
    int sent = tcp_send(client, msg, 9);
    CHECK(sent == 9, "send bytes mismatch");
    CHECK(client->send_buf_len == 9, "send_buf_len mismatch");
    CHECK(memcmp(client->send_buf, msg, 9) == 0, "send_buf content mismatch");

    /* Simulate receiving */
    TCPSocket *server = tcp_socket_create();
    tcp_bind_listen(server, 8080);
    TCPSocket *child = tcp_accept(server);
    tcp_simulate_recv(child, msg, 9);
    CHECK(child->recv_buf_len == 9, "recv_buf_len mismatch");

    uint8_t rbuf[64] = {0};
    int rcvd = tcp_recv(child, rbuf, sizeof(rbuf));
    CHECK(rcvd == 9, "recv bytes mismatch");
    CHECK(memcmp(rbuf, msg, 9) == 0, "recv content mismatch");

    tcp_socket_free(client);
    tcp_socket_free(child);
    tcp_socket_free(server);
    PASS();
}

static void test_tcp_close(void) {
    TEST("tcp_close transitions to CLOSED");
    TCPSocket *s = tcp_socket_create();
    tcp_connect(s, 0x7F000001, 8080);
    CHECK(s->state == TCP_ESTABLISHED, "not ESTABLISHED");
    int rc = tcp_close(s);
    CHECK(rc == 0, "close failed");
    CHECK(s->state == TCP_CLOSED, "not CLOSED after close");
    tcp_socket_free(s);
    PASS();
}

static void test_tcp_close_passive(void) {
    TEST("tcp_close_passive transitions CLOSE_WAIT->CLOSED");
    TCPSocket *s = tcp_socket_create();
    s->state = TCP_CLOSE_WAIT;
    int rc = tcp_close_passive(s);
    CHECK(rc == 0, "passive close failed");
    CHECK(s->state == TCP_CLOSED, "not CLOSED");
    tcp_socket_free(s);
    PASS();
}

/* ─────────────────────────────────────────────────────────── */
/*  IP Packet Tests                                            */
/* ─────────────────────────────────────────────────────────── */

static void test_ip_checksum(void) {
    TEST("ip_checksum computes RFC 1071 one's complement");
    uint8_t data[] = { 0x45, 0x00, 0x00, 0x73, 0x00, 0x00, 0x40, 0x00,
                       0x40, 0x11, 0x00, 0x00, 0xc0, 0xa8, 0x00, 0x01,
                       0xc0, 0xa8, 0x00, 0xc7 };
    uint16_t ck = ip_checksum(data, 20);
    /* Validate checksum: compute with checksum field zeroed */
    uint16_t expected = ip_checksum(data, 20);
    CHECK(ck != 0 || expected != 0, "checksum should be computable");
    (void)expected;
    /* Insert checksum and verify */
    data[10] = (uint8_t)((ck >> 8) & 0xFF);
    data[11] = (uint8_t)(ck & 0xFF);
    uint16_t verify = ip_checksum(data, 20);
    CHECK(verify == 0, "checksum verification failed");
    PASS();
}

static void test_ip_header_init(void) {
    TEST("ip_header_init sets version and TTL defaults");
    IPHeader hdr;
    ip_header_init(&hdr);
    CHECK(ip_version(&hdr) == IP_VERSION_4, "version not 4");
    CHECK(ip_ihl(&hdr) == 5, "IHL not 5");
    CHECK(hdr.ttl == IP_DEFAULT_TTL, "TTL not default");
    PASS();
}

static void test_ip_flag_df(void) {
    TEST("IP Don't Fragment flag detection");
    IPHeader hdr;
    memset(&hdr, 0, sizeof(hdr));
    CHECK(!ip_flag_df(&hdr), "DF should be clear when 0");
    hdr.flags_frag_offset = IP_FLAG_DF;
    CHECK(ip_flag_df(&hdr), "DF should be set");
    PASS();
}

static void test_ip_build_packet(void) {
    TEST("ip_build_packet constructs valid IP packet");
    uint8_t packet[1500];
    size_t pkt_len = sizeof(packet);
    uint8_t payload[] = "test payload";
    int rc = ip_build_packet(packet, &pkt_len, IP_PROTO_UDP,
                              0xC0A80001, 0xC0A80002,
                              payload, strlen((char*)payload));
    CHECK(rc == 0, "build failed");
    CHECK(pkt_len == sizeof(IPHeader) + strlen((char*)payload), "packet length wrong");

    IPHeader parsed;
    size_t hlen;
    rc = ip_parse_header(packet, pkt_len, &parsed, &hlen);
    CHECK(rc == 0, "parse failed");
    CHECK(ip_version(&parsed) == IP_VERSION_4, "parsed version wrong");
    CHECK(parsed.protocol == IP_PROTO_UDP, "protocol wrong");
    CHECK(parsed.src_addr == 0xC0A80001, "src addr wrong");
    CHECK(parsed.dst_addr == 0xC0A80002, "dst addr wrong");
    PASS();
}

static void test_ip_fragment(void) {
    TEST("ip_fragment splits packet at MTU boundary");
    uint8_t packet[1500];
    size_t pkt_len = sizeof(packet);
    uint8_t payload[1400];
    memset(payload, 0xAB, sizeof(payload));
    ip_build_packet(packet, &pkt_len, IP_PROTO_TCP,
                    0x0A000001, 0x0A000002, payload, sizeof(payload));
    IPFragment frags[IP_MAX_FRAGMENTS];
    size_t frag_count = 0;
    int rc = ip_fragment(packet, pkt_len, 576, frags, &frag_count);
    CHECK(rc == 0, "fragment failed");
    CHECK(frag_count > 1, "should produce multiple fragments");
    CHECK(frag_count <= IP_MAX_FRAGMENTS, "too many fragments");
    /* Verify first fragment has MF flag, last does not */
    CHECK(frags[0].more_fragments == true, "first frag missing MF");
    CHECK(frags[frag_count-1].more_fragments == false, "last frag has MF");
    /* Free fragment data */
    for (size_t i = 0; i < frag_count; i++) free(frags[i].data);
    PASS();
}

/* ─────────────────────────────────────────────────────────── */
/*  UDP/DNS Tests                                              */
/* ─────────────────────────────────────────────────────────── */

static void test_udp_create_bind(void) {
    TEST("udp_socket_create and udp_bind");
    UDPSocket *sock = udp_socket_create();
    CHECK(sock != NULL, "create failed");
    CHECK(!sock->is_bound, "should not be bound initially");
    int rc = udp_bind(sock, 5353);
    CHECK(rc == 0, "bind failed");
    CHECK(sock->is_bound, "not bound after bind");
    CHECK(sock->bound_port == 5353, "port mismatch");
    udp_socket_free(sock);
    PASS();
}

static void test_dns_build_query(void) {
    TEST("dns_build_query creates valid A-record query");
    uint8_t buf[512];
    size_t len = sizeof(buf);
    int rc = dns_build_query("www.example.com", DNS_QTYPE_A, buf, &len);
    CHECK(rc == 0, "build failed");
    CHECK(len > 17, "query too short");
    CHECK(buf[2] == 0x01, "flags byte wrong");
    CHECK(buf[5] == 0x01, "QDCOUNT wrong");
    PASS();
}

static void test_dns_parse_header(void) {
    TEST("dns_parse_header extracts DNS header fields");
    uint8_t buf[512];
    size_t len = sizeof(buf);
    dns_build_query("example.com", DNS_QTYPE_A, buf, &len);
    DNSHeader hdr;
    int rc = dns_parse_header(buf, len, &hdr);
    CHECK(rc == 0, "parse failed");
    CHECK(hdr.qdcount == 1, "qdcount mismatch");
    CHECK((hdr.flags & DNS_QR_MASK) == 0, "QR should be 0 for query");
    PASS();
}

static void test_dns_name_decode(void) {
    TEST("dns_name_decode decodes domain name labels");
    uint8_t encoded[] = { 3, 'w', 'w', 'w', 7, 'e', 'x', 'a', 'm', 'p',
                           'l', 'e', 3, 'c', 'o', 'm', 0 };
    char name[DNS_MAX_NAME];
    size_t next;
    int rc = dns_name_decode(encoded, sizeof(encoded), 0, name, sizeof(name), &next);
    CHECK(rc == 0, "decode failed");
    CHECK(strncmp(name, "www.example.com", DNS_MAX_NAME) == 0, "decoded name mismatch");
    PASS();
}

/* ─────────────────────────────────────────────────────────── */
/*  TLS Handshake Tests                                        */
/* ─────────────────────────────────────────────────────────── */

static void test_tls_context_create(void) {
    TEST("tls_context_create initializes TLS context");
    TLSContext *ctx = tls_context_create(true);
    CHECK(ctx != NULL, "create failed");
    CHECK(ctx->state == TLS_STATE_START, "state not START");
    CHECK(ctx->is_client == true, "is_client wrong");
    CHECK(!ctx->handshake_done, "should not be done");
    tls_context_free(ctx);
    PASS();
}

static void test_tls_full_handshake(void) {
    TEST("tls_handshake completes all 6 steps");
    TLSContext *ctx = tls_context_create(true);
    CHECK(ctx != NULL, "create failed");
    int rc = tls_handshake(ctx);
    CHECK(rc == 0, "handshake failed");
    CHECK(ctx->handshake_done == true, "handshake not marked done");
    CHECK(ctx->state == TLS_STATE_HANDSHAKE_DONE, "state not HANDSHAKE_DONE");
    tls_context_free(ctx);
    PASS();
}

static void test_tls_cipher_names(void) {
    TEST("tls_cipher_name returns correct names");
    CHECK(strcmp(tls_cipher_name(TLS_CIPHER_AES_128_GCM_SHA256),
                 "AES_128_GCM_SHA256") == 0, "cipher name mismatch");
    CHECK(strcmp(tls_cipher_name(0xFFFF), "UNKNOWN") == 0, "unknown cipher");
    PASS();
}

static void test_tls_group_names(void) {
    TEST("tls_group_name returns correct names");
    CHECK(strcmp(tls_group_name(TLS_GROUP_X25519), "x25519") == 0, "x25519 mismatch");
    CHECK(strcmp(tls_group_name(TLS_GROUP_SECP256R1), "secp256r1") == 0, "secp256r1 mismatch");
    PASS();
}

/* ─────────────────────────────────────────────────────────── */
/*  HTTP Tests                                                 */
/* ─────────────────────────────────────────────────────────── */

static void test_http_method_str(void) {
    TEST("http_method_str returns correct strings");
    CHECK(strcmp(http_method_str(HTTP_M_GET), "GET") == 0, "GET mismatch");
    CHECK(strcmp(http_method_str(HTTP_M_POST), "POST") == 0, "POST mismatch");
    CHECK(strcmp(http_method_str(HTTP_M_PUT), "PUT") == 0, "PUT mismatch");
    CHECK(strcmp(http_method_str(HTTP_M_DELETE), "DELETE") == 0, "DELETE mismatch");
    PASS();
}

static void test_http_status_str(void) {
    TEST("http_status_str returns correct status strings");
    CHECK(strcmp(http_status_str(200), "OK") == 0, "200 mismatch");
    CHECK(strcmp(http_status_str(404), "Not Found") == 0, "404 mismatch");
    CHECK(strcmp(http_status_str(500), "Internal Server Error") == 0, "500 mismatch");
    PASS();
}

static void test_http_parse_request(void) {
    TEST("http_parse_request parses GET request");
    const char *raw = "GET /index.html HTTP/1.1\r\nHost: example.com\r\n\r\n";
    HTTPRequest req;
    int rc = http_parse_request((const uint8_t*)raw, strlen(raw), &req);
    CHECK(rc == 0, "parse failed");
    CHECK(req.method == HTTP_M_GET, "method not GET");
    CHECK(strcmp(req.uri, "/index.html") == 0, "URI mismatch");
    CHECK(strcmp(req.version, "HTTP/1.1") == 0, "version mismatch");
    CHECK(req.header_count >= 1, "no headers parsed");
    PASS();
}

static void test_http_build_response(void) {
    TEST("http_build_response constructs HTTP response");
    HTTPResponse resp;
    http_response_set_defaults(&resp, 200);
    http_response_add_header(&resp, "Content-Type", "text/plain");
    resp.has_body = true;
    memcpy(resp.body, "Hello", 5);
    resp.body_len = 5;

    uint8_t buf[4096];
    size_t len = sizeof(buf);
    int rc = http_build_response(&resp, buf, &len);
    CHECK(rc == 0, "build failed");
    CHECK(len > 20, "response too short");
    /* Verify status line present */
    CHECK(strstr((const char*)buf, "200 OK") != NULL, "status line missing");
    PASS();
}

static void test_http_chunked_decode(void) {
    TEST("http_chunked_decode decodes chunked transfer encoding");
    const char *chunked = "5\r\nHello\r\n6\r\n World\r\n0\r\n\r\n";
    uint8_t decoded[256];
    size_t dlen = sizeof(decoded);
    int rc = http_chunked_decode((const uint8_t*)chunked, strlen(chunked),
                                  decoded, &dlen);
    CHECK(rc == 0, "decode failed");
    CHECK(dlen == 11, "decoded length wrong");
    CHECK(memcmp(decoded, "Hello World", 11) == 0, "decoded content mismatch");
    PASS();
}

static void test_http_headers(void) {
    TEST("http_get_header finds headers case-insensitively");
    HTTPRequest req;
    memset(&req, 0, sizeof(req));
    http_add_header(&req, "Host", "example.com");
    http_add_header(&req, "User-Agent", "mini-test");
    const char *h = http_get_header(req.headers, req.header_count, "host");
    CHECK(h != NULL, "Host header not found");
    CHECK(strcmp(h, "example.com") == 0, "Host value mismatch");
    h = http_get_header(req.headers, req.header_count, "nonexistent");
    CHECK(h == NULL, "should return NULL for missing header");
    PASS();
}

/* ─────────────────────────────────────────────────────────── */
/*  TCP Congestion Control Tests                               */
/* ─────────────────────────────────────────────────────────── */

static void test_tcp_cc_init(void) {
    TEST("tcp_cc_init sets Slow Start defaults");
    TCPCongestionControl cc;
    tcp_cc_init(&cc);
    CHECK(cc.cwnd == TCP_CWND_INIT, "cwnd not initial");
    CHECK(cc.ssthresh == TCP_SSTHRESH_INIT, "ssthresh not initial");
    CHECK(cc.cc_state == TCP_CC_SLOW_START, "not in slow start");
    CHECK(!cc.in_recovery, "should not be in recovery");
    PASS();
}

static void test_tcp_rtt_sample(void) {
    TEST("tcp_rtt_sample computes SRTT and RTO");
    TCPRTTEstimator rtt;
    tcp_rtt_init(&rtt);
    /* First sample */
    tcp_rtt_sample(&rtt, 100.0);
    CHECK(rtt.rtt_measured, "should be measured");
    CHECK(rtt.srtt == 100.0, "first SRTT should equal sample");
    CHECK(rtt.rttvar == 50.0, "first RTTVAR should be R/2");
    CHECK(rtt.rto >= (double)TCP_MIN_RTO_MS, "RTO too low");
    CHECK(rtt.rto <= (double)TCP_MAX_RTO_MS, "RTO too high");
    /* Second sample: SRTT should converge */
    tcp_rtt_sample(&rtt, 200.0);
    CHECK(rtt.srtt > 100.0 && rtt.srtt < 200.0, "SRTT should converge toward 200");
    PASS();
}

static void test_tcp_cc_slow_start(void) {
    TEST("tcp_cc_slow_start grows cwnd exponentially");
    TCPCongestionControl cc;
    tcp_cc_init(&cc);
    cc.ssthresh = 100;
    uint32_t initial = cc.cwnd;
    tcp_cc_slow_start(&cc, initial); /* ACK one segment */
    CHECK(cc.cwnd > initial, "cwnd should increase in slow start");
    PASS();
}

static void test_tcp_cc_on_timeout(void) {
    TEST("tcp_cc_on_timeout resets to slow start");
    TCPCongestionControl cc;
    tcp_cc_init(&cc);
    cc.cwnd = 100;
    cc.cc_state = TCP_CC_CONGESTION_AVOID;
    tcp_cc_on_timeout(&cc);
    CHECK(cc.cwnd == TCP_CWND_INIT, "cwnd should reset to init");
    CHECK(cc.cc_state == TCP_CC_SLOW_START, "should return to slow start");
    PASS();
}

static void test_tcp_cc_dup_ack_fast_retransmit(void) {
    TEST("3 duplicate ACKs trigger Fast Retransmit");
    TCPCongestionControl cc;
    tcp_cc_init(&cc);
    cc.cc_state = TCP_CC_CONGESTION_AVOID;
    /* First ACK sets baseline (not a dup), then 3 identical ACKs trigger */
    tcp_cc_on_dup_ack(&cc, 100); /* baseline */
    tcp_cc_on_dup_ack(&cc, 100); /* dup #1 */
    tcp_cc_on_dup_ack(&cc, 100); /* dup #2 */
    tcp_cc_on_dup_ack(&cc, 100); /* dup #3 -> fast retransmit */
    CHECK(cc.cc_state == TCP_CC_FAST_RETRANSMIT,
          "3 dupacks should trigger fast retransmit");
    PASS();
}

static void test_tcp_rtt_backoff(void) {
    TEST("tcp_rtt_backoff doubles RTO (exponential backoff)");
    TCPRTTEstimator rtt;
    tcp_rtt_init(&rtt);
    double original = rtt.rto;
    tcp_rtt_backoff(&rtt);
    CHECK(rtt.rto == original * 2.0, "RTO should double");
    PASS();
}

/* ─────────────────────────────────────────────────────────── */
/*  IP Routing / CIDR Tests                                    */
/* ─────────────────────────────────────────────────────────── */

static void test_ip_prefix_to_mask(void) {
    TEST("ip_prefix_to_mask generates correct subnet masks");
    CHECK(ip_prefix_to_mask(0) == 0, "/0 should be 0");
    CHECK(ip_prefix_to_mask(8) == 0xFF000000, "/8 mismatch");
    CHECK(ip_prefix_to_mask(16) == 0xFFFF0000, "/16 mismatch");
    CHECK(ip_prefix_to_mask(24) == 0xFFFFFF00, "/24 mismatch");
    CHECK(ip_prefix_to_mask(32) == 0xFFFFFFFF, "/32 mismatch");
    PASS();
}

static void test_ip_network_addr(void) {
    TEST("ip_network_addr extracts network portion");
    uint32_t ip = 0xC0A80164; /* 192.168.1.100 */
    uint32_t net = ip_network_addr(ip, 24);
    CHECK(net == 0xC0A80100, "network address mismatch");
    PASS();
}

static void test_ip_broadcast_addr(void) {
    TEST("ip_broadcast_addr computes broadcast");
    uint32_t net = 0xC0A80100;
    uint32_t bc = ip_broadcast_addr(net, 24);
    CHECK(bc == 0xC0A801FF, "broadcast mismatch");
    PASS();
}

static void test_ip_in_subnet(void) {
    TEST("ip_in_subnet correctly identifies subnet membership");
    CHECK(ip_in_subnet(0xC0A80164, 0xC0A80100, 24), "should be in subnet");
    CHECK(!ip_in_subnet(0xC0A80201, 0xC0A80100, 24), "should NOT be in subnet");
    PASS();
}

static void test_ip_subnet_host_count(void) {
    TEST("ip_subnet_host_count computes usable hosts");
    CHECK(ip_subnet_host_count(24) == 254, "/24 should have 254 hosts");
    CHECK(ip_subnet_host_count(30) == 2, "/30 should have 2 hosts");
    CHECK(ip_subnet_host_count(31) == 0, "/31 should have 0 usable hosts");
    CHECK(ip_subnet_host_count(32) == 0, "/32 should have 0 usable hosts");
    PASS();
}

static void test_ip_cidr_from_str(void) {
    TEST("ip_cidr_from_str parses CIDR notation");
    uint32_t net;
    uint8_t pl;
    int rc = ip_cidr_from_str("192.168.1.0/24", &net, &pl);
    CHECK(rc == 0, "parse failed");
    CHECK(net == 0xC0A80100, "network mismatch");
    CHECK(pl == 24, "prefix length mismatch");
    PASS();
}

static void test_ip_routing_lpm(void) {
    TEST("ip_routing_lookup finds longest prefix match");
    IPRoutingTable rt;
    ip_routing_table_init(&rt);
    ip_routing_add(&rt, 0xC0A80000, 16, 0x0A000001, 0, 1);  /* 192.168.0.0/16 */
    ip_routing_add(&rt, 0xC0A80100, 24, 0x0A000002, 0, 1);  /* 192.168.1.0/24 */
    ip_routing_add(&rt, 0, 0, 0x0A0000FF, 0, 10);            /* default */

    IPRouteEntry result;
    /* 192.168.1.100 should match /24, not /16 */
    int rc = ip_routing_lookup(&rt, 0xC0A80164, &result);
    CHECK(rc == 0, "lookup failed");
    CHECK(result.prefix_len == 24, "should match /24 not /16");
    CHECK(result.nexthop == 0x0A000002, "nexthop mismatch");

    /* 10.0.0.1 should match default */
    rc = ip_routing_lookup(&rt, 0x0A000001, &result);
    CHECK(rc == 0, "default route lookup failed");
    CHECK(result.prefix_len == 0, "should match default");
    PASS();
}

static void test_ip_routing_aggregate(void) {
    TEST("ip_routing_aggregate merges adjacent /24 into /23");
    IPRoutingTable rt;
    ip_routing_table_init(&rt);
    ip_routing_add(&rt, 0xC0A80000, 24, 0x0A000001, 0, 1); /* 192.168.0.0/24 */
    ip_routing_add(&rt, 0xC0A80100, 24, 0x0A000001, 0, 1); /* 192.168.1.0/24 */
    CHECK(rt.count == 2, "should have 2 entries");
    int merged = ip_routing_aggregate(&rt);
    CHECK(merged >= 1, "should merge at least 1 pair");
    CHECK(rt.count == 1, "should have 1 entry after merge");
    PASS();
}

static void test_ip_dijkstra(void) {
    TEST("Dijkstra finds shortest paths in topology graph");
    IPTopologyGraph g;
    ip_topology_init(&g);
    ip_topology_add_node(&g, 0x0A000001); /* Node 0 */
    ip_topology_add_node(&g, 0x0A000002); /* Node 1 */
    ip_topology_add_node(&g, 0x0A000003); /* Node 2 */
    ip_topology_add_node(&g, 0x0A000004); /* Node 3 */
    ip_topology_add_edge(&g, 0, 1, 1);
    ip_topology_add_edge(&g, 0, 2, 4);
    ip_topology_add_edge(&g, 1, 2, 2);
    ip_topology_add_edge(&g, 1, 3, 5);
    ip_topology_add_edge(&g, 2, 3, 1);

    uint32_t dist[4], prev[4];
    int rc = ip_topology_dijkstra(&g, 0, dist, prev);
    CHECK(rc == 0, "Dijkstra failed");
    CHECK(dist[0] == 0, "dist[0] should be 0");
    CHECK(dist[1] == 1, "dist[1] should be 1");
    CHECK(dist[2] == 3, "dist[2] should be 3 (0->1->2)");
    CHECK(dist[3] == 4, "dist[3] should be 4 (0->1->2->3)");
    PASS();
}

static void test_ip_bellman_ford(void) {
    TEST("Bellman-Ford finds shortest paths");
    IPTopologyGraph g;
    ip_topology_init(&g);
    ip_topology_add_node(&g, 0x0A000001);
    ip_topology_add_node(&g, 0x0A000002);
    ip_topology_add_node(&g, 0x0A000003);
    ip_topology_add_edge(&g, 0, 1, 2);
    ip_topology_add_edge(&g, 1, 2, 3);
    ip_topology_add_edge(&g, 0, 2, 10);

    uint32_t dist[3], prev[3];
    int rc = ip_topology_bellman_ford(&g, 0, dist, prev);
    CHECK(rc == 0, "Bellman-Ford failed");
    CHECK(dist[2] == 5, "dist[2] should be 5 (0->1->2)");
    PASS();
}

/* ─────────────────────────────────────────────────────────── */
/*  ICMP Tests                                                 */
/* ─────────────────────────────────────────────────────────── */

static void test_icmp_echo_build(void) {
    TEST("icmp_echo_build creates valid Echo Request");
    uint8_t pkt[128];
    size_t len = sizeof(pkt);
    uint8_t payload[] = "pingdata";
    int rc = icmp_echo_build(0x1234, 1, payload, 8, pkt, &len);
    CHECK(rc == 0, "echo build failed");
    CHECK(len == ICMP_HEADER_SIZE + 8, "packet length wrong");
    CHECK(pkt[0] == ICMP_TYPE_ECHO_REQUEST, "type not Echo Request");
    CHECK(pkt[4] == 0x12 && pkt[5] == 0x34, "identifier mismatch");
    CHECK(icmp_checksum_verify(pkt, len), "checksum verification failed");
    PASS();
}

static void test_icmp_echo_reply(void) {
    TEST("icmp_echo_reply generates correct reply");
    uint8_t req[128], rep[128];
    size_t rlen = sizeof(req), plen = sizeof(rep);
    icmp_echo_build(0xABCD, 5, NULL, 0, req, &rlen);
    int rc = icmp_echo_reply(req, rlen, rep, &plen);
    CHECK(rc == 0, "reply build failed");
    CHECK(rep[0] == ICMP_TYPE_ECHO_REPLY, "type not Echo Reply");
    CHECK(rep[4] == 0xAB && rep[5] == 0xCD, "identifier lost");
    CHECK(icmp_checksum_verify(rep, plen), "reply checksum fails");
    PASS();
}

static void test_icmp_echo_parse(void) {
    TEST("icmp_echo_parse extracts Echo fields");
    uint8_t pkt[128];
    size_t len = sizeof(pkt);
    uint8_t data[] = "hello";
    icmp_echo_build(42, 7, data, 5, pkt, &len);
    ICMPEcho echo;
    int rc = icmp_echo_parse(pkt, len, &echo);
    CHECK(rc == 0, "parse failed");
    CHECK(echo.identifier == 42, "id mismatch");
    CHECK(echo.sequence_number == 7, "seq mismatch");
    CHECK(echo.data_len == 5, "data_len mismatch");
    CHECK(memcmp(echo.data, "hello", 5) == 0, "data mismatch");
    PASS();
}

static void test_icmp_time_exceeded(void) {
    TEST("icmp_time_exceeded_build embeds original datagram");
    uint8_t original[64];
    memset(original, 0xAA, sizeof(original));
    uint8_t pkt[256];
    size_t len = sizeof(pkt);
    int rc = icmp_time_exceeded_build(ICMP_CODE_TTL_EXCEEDED,
                                       original, sizeof(original),
                                       pkt, &len);
    CHECK(rc == 0, "time exceeded build failed");
    CHECK(pkt[0] == ICMP_TYPE_TIME_EXCEEDED, "type wrong");
    CHECK(pkt[1] == ICMP_CODE_TTL_EXCEEDED, "code wrong");
    CHECK(len > ICMP_HEADER_SIZE, "no embedded data");
    CHECK(icmp_checksum_verify(pkt, len), "checksum fails");
    PASS();
}

static void test_icmp_dest_unreachable(void) {
    TEST("icmp_dest_unreachable_build creates error message");
    uint8_t original[40];
    memset(original, 0xBB, sizeof(original));
    uint8_t pkt[256];
    size_t len = sizeof(pkt);
    int rc = icmp_dest_unreachable_build(ICMP_CODE_PORT_UNREACHABLE,
                                          original, sizeof(original),
                                          pkt, &len);
    CHECK(rc == 0, "dest unreachable build failed");
    CHECK(pkt[0] == ICMP_TYPE_DEST_UNREACHABLE, "type wrong");
    CHECK(pkt[1] == ICMP_CODE_PORT_UNREACHABLE, "code wrong");
    PASS();
}

static void test_icmp_type_names(void) {
    TEST("icmp_type_name returns correct descriptive names");
    CHECK(strcmp(icmp_type_name(ICMP_TYPE_ECHO_REQUEST), "Echo Request") == 0,
          "Echo Request name mismatch");
    CHECK(strcmp(icmp_type_name(ICMP_TYPE_ECHO_REPLY), "Echo Reply") == 0,
          "Echo Reply name mismatch");
    CHECK(strcmp(icmp_type_name(ICMP_TYPE_TIME_EXCEEDED), "Time Exceeded") == 0,
          "Time Exceeded name mismatch");
    CHECK(strcmp(icmp_type_name(ICMP_TYPE_DEST_UNREACHABLE),
                 "Destination Unreachable") == 0, "Dest Unreachable mismatch");
    PASS();
}

/* ─────────────────────────────────────────────────────────── */
int main(void)
{
    fprintf(stderr, "\n=== mini-network-base: Test Suite ===\n\n");

    /* TCP */
    fprintf(stderr, "-- TCP Socket Tests --\n");
    test_tcp_create();
    test_tcp_state_names();
    test_tcp_connect();
    test_tcp_connect_null();
    test_tcp_bind_listen();
    test_tcp_accept();
    test_tcp_send_recv();
    test_tcp_close();
    test_tcp_close_passive();

    /* IP */
    fprintf(stderr, "\n-- IP Packet Tests --\n");
    test_ip_checksum();
    test_ip_header_init();
    test_ip_flag_df();
    test_ip_build_packet();
    test_ip_fragment();

    /* UDP/DNS */
    fprintf(stderr, "\n-- UDP/DNS Tests --\n");
    test_udp_create_bind();
    test_dns_build_query();
    test_dns_parse_header();
    test_dns_name_decode();

    /* TLS */
    fprintf(stderr, "\n-- TLS Tests --\n");
    test_tls_context_create();
    test_tls_full_handshake();
    test_tls_cipher_names();
    test_tls_group_names();

    /* HTTP */
    fprintf(stderr, "\n-- HTTP Tests --\n");
    test_http_method_str();
    test_http_status_str();
    test_http_parse_request();
    test_http_build_response();
    test_http_chunked_decode();
    test_http_headers();

    /* TCP Congestion Control */
    fprintf(stderr, "\n-- TCP Congestion Control Tests --\n");
    test_tcp_cc_init();
    test_tcp_rtt_sample();
    test_tcp_cc_slow_start();
    test_tcp_cc_on_timeout();
    test_tcp_cc_dup_ack_fast_retransmit();
    test_tcp_rtt_backoff();

    /* IP Routing / CIDR */
    fprintf(stderr, "\n-- IP Routing & CIDR Tests --\n");
    test_ip_prefix_to_mask();
    test_ip_network_addr();
    test_ip_broadcast_addr();
    test_ip_in_subnet();
    test_ip_subnet_host_count();
    test_ip_cidr_from_str();
    test_ip_routing_lpm();
    test_ip_routing_aggregate();
    test_ip_dijkstra();
    test_ip_bellman_ford();

    /* ICMP */
    fprintf(stderr, "\n-- ICMP Tests --\n");
    test_icmp_echo_build();
    test_icmp_echo_reply();
    test_icmp_echo_parse();
    test_icmp_time_exceeded();
    test_icmp_dest_unreachable();
    test_icmp_type_names();

    /* Summary */
    fprintf(stderr, "\n====================================\n");
    fprintf(stderr, "  Total:  %d\n", tests_total);
    fprintf(stderr, "  Passed: %d\n", tests_passed);
    fprintf(stderr, "  Failed: %d\n", tests_failed);
    fprintf(stderr, "====================================\n\n");

    if (tests_failed == 0) {
        fprintf(stderr, "ALL TESTS PASSED\n");
        return 0;
    } else {
        fprintf(stderr, "SOME TESTS FAILED\n");
        return 1;
    }
}