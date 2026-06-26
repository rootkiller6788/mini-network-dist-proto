# Knowledge Graph — mini-gateway-proxy

## L1: Core Definitions (Complete)

| Concept | struct/typedef | Location |
|---------|---------------|----------|
| Reverse Proxy Server | `ProxyServer`, `ProxyConnection`, `ProxyRule` | `include/reverse_proxy.h` |
| Load Balancer | `LoadBalancer`, `LBServer`, `LBVirtualNode` | `include/load_balancer.h` |
| Circuit Breaker | `CBCircuit`, `CBCircuitState` | `include/circuit_breaker.h` |
| API Gateway | `APIGateway`, `APIRoute`, `GatewayRequest` | `include/api_gateway.h` |
| Rate Limiter | `RateLimiter`, `TokenBucket`, `LeakyBucket`, `FixedWindow`, `SlidingWindowLog` | `include/rate_limiter.h` |
| HTTP Message | `HttpRequest`, `HttpResponse`, `HttpHeader`, `HttpMethod`, `HttpParseState` | `include/http_message.h` |
| Service Registry | `ServiceRegistry`, `SRService`, `SREndpoint` | `include/service_registry.h` |
| Middleware Chain | `MiddlewareChain`, `MiddlewareContext`, `MiddlewareFunc` | `include/middleware.h` |
| TLS Context | `TLSContext`, `TLSCertificate`, `TLSCipherSuite`, `TLSVersion` | `include/tls_context.h` |
| Connection Pool | `ConnectionPool`, `CPConnection`, `PoolManager` | `include/connection_pool.h` |

## L2: Core Concepts (Complete)

| Concept | Implementation | Knowledge Source |
|---------|---------------|-----------------|
| Reverse Proxy | HTTP request forwarding, header rewriting | NGINX `proxy_pass` |
| Load Balancing | 5 algorithms: RR, WRR, Least-Conn, Consistent Hash, Random | NGINX upstream |
| Circuit Breaking | 3-state FSM: CLOSED/OPEN/HALF_OPEN | Netflix Hystrix |
| API Gateway | Route matching + plugin chain | Kong/Spring Cloud Gateway |
| Rate Limiting | 4 algorithms: Token Bucket, Leaky Bucket, Fixed Window, Sliding Window Log | NGINX limit_req |
| HTTP Message Parsing | Full request/response model with chunked encoding | RFC 7230-7235 |
| Service Discovery | DNS SRV + heartbeat-based health check | Consul/Eureka |
| Middleware Pattern | Chain of Responsibility with context propagation | Express.js/Koa |
| TLS Termination | Certificate validation, SNI, ALPN, cipher negotiation | RFC 8446 |
| Connection Pooling | LRU/FIFO eviction, idle reaping, Little's Law | gRPC connection pool |

## L3: Engineering Structures (Complete)

| Structure | Data + Operations | Location |
|-----------|------------------|----------|
| Proxy Pipeline | `ProxyConnection` + `proxy_pipe_data()` (bidirectional select-based I/O) | `src/reverse_proxy.c` |
| Consistent Hash Ring | `LBVirtualNode` ring + `lb_build_ring()` with sorted insert O(n*v) | `src/load_balancer.c` |
| Circuit State Machine | `CBCircuitState` enum + `cb_call()` state transitions | `src/circuit_breaker.c` |
| Plugin Chain | `GatewayPlugin` enum + `gateway_plugin_run()` dispatcher | `src/api_gateway.c` |
| Rate Limiter Union | Union of 4 algorithm structs in `RateLimiter` | `src/rate_limiter.c` |
| Middleware Stack | `MiddlewareChain` array + recursive execution | `src/middleware_chain.c` |
| Connection Pool | `ConnectionPool` array + acquire/release lifecycle | `src/connection_pool.c` |

## L4: Standards/Theorems (Complete)

| Theorem/Standard | Code Verification | File |
|-----------------|-------------------|------|
| **CAP Theorem** | Service Registry: prioritizes Availability over Consistency during partition | `src/service_discovery.c: sr_health_check()` |
| **Amdahl's Law** | Content negotiation: serial parsing cost analysis | `src/http_message.c: hm_negotiate_accept()` |
| **Shannon's Theorem** | Security header audit: entropy of security posture | `src/http_message.c: hm_header_security_audit()` |
| **Little's Law** (L = λW) | Connection pool idle reaping | `src/connection_pool.c: pm_reap_idle()` |
| **RFC 7230** (HTTP/1.1) | Chunked transfer encoding | `src/http_message.c: hm_chunked_decode()` |
| **RFC 5280** (X.509) | Certificate validation | `src/tls_termination.c: tls_validate_certificate()` |
| **RFC 8446** (TLS 1.3) | ClientHello parsing, version/cipher/ALPN negotiation | `src/tls_termination.c: tls_parse_client_hello()` |
| **RFC 2782** (DNS SRV) | Service discovery via DNS SRV simulation | `src/service_discovery.c: sr_resolve_dns_srv()` |
| **OWASP Top 10** (A05:2021) | Security header audit | `src/http_message.c: hm_header_security_audit()` |

## L5: Algorithms/Methods (Complete)

| Algorithm | Implementation | Complexity |
|-----------|---------------|------------|
| Round-Robin | `lb_select_round_robin()` | O(1) per select |
| Smooth Weighted Round-Robin | `lb_select_weighted_rr()` (NGINX SWRR) | O(n) per select |
| Least Connections | `lb_select_least_conn()` | O(n) per select |
| Consistent Hashing | `lb_select_consistent_hash()` (FNV-1a + binary search on ring) | O(log n) |
| Random | `lb_select_random()` | O(n) |
| Token Bucket | `rl_allow()` with `rl_refill()` | O(1) |
| Leaky Bucket | `rl_allow()` with `rl_refill()` | O(1) |
| Fixed Window | `rl_allow()` | O(1) |
| Sliding Window Log | `rl_allow()` with timestamp eviction | O(k) amortized |
| Chunked Decode | `hm_chunked_decode()` | O(n) |
| URI Normalization | `hm_normalize_uri()` | O(n) |
| Content Negotiation | `hm_negotiate_accept()` scoring | O(A*N) |
| Health Check TTL Eviction | `sr_health_check()` | O(services * endpoints) |

## L6: Canonical Problems (Complete)

| Problem | Solution | Example |
|---------|----------|---------|
| **Web Server** | Reverse proxy with select() I/O multiplexing | `examples/api_gateway_demo.c` |
| **KV Store Routing** | Consistent hashing ring for sticky sessions | `examples/load_balance_demo.c` |
| **TCP Stack Resilience** | Circuit breaker state machine for cascading failure prevention | `examples/circuit_breaker_demo.c` |
| **Compiler Pipeline** | Plugin chain (analogous to compiler passes) | `src/middleware_chain.c` |
| **Service Mesh** | Sidecar proxy with service discovery + health check | `src/service_discovery.c` |

## L7: Applications (3 examples — Partial+)

| Application | File | Description |
|-------------|------|-------------|
| API Gateway Demo | `examples/api_gateway_demo.c` | Full gateway with routing, auth, rate limiting, circuit breaker |
| Load Balancer Showcase | `examples/load_balance_demo.c` | 5 algorithms with distribution visualization |
| Circuit Breaker Demo | `examples/circuit_breaker_demo.c` | Fault injection with cascading failure prevention |
| Service Mesh Mini-demo | `demos/mini-load-balancer/` | Load balancing patterns documentation |
| Circuit Pattern Demo | `demos/mini-circuit-breaker/` | Circuit breaker patterns documentation |

## L8: Advanced Topics (2 implementations — Partial+)

| Topic | Implementation | File |
|-------|---------------|------|
| **TLS 1.3 Protocol** | ClientHello parsing, SNI, ALPN, cipher negotiation | `src/tls_termination.c` |
| **Formal Verification** | Lean 4 compatibility stubs for state machine verification | `src/lean_formal.c` |
| **RDMA** | Documented in knowledge graph (L9) | N/A |
| **GPU Kernel** | Documented (not applicable to gateway proxy) | N/A |
| **Homomorphic Encryption** | Documented in gap report (future) | N/A |

## L9: Industry Frontiers (Partial — documented)

| Frontier | Documentation | Status |
|----------|--------------|--------|
| **AI Compiler (MLIR)** | Not applicable to gateway proxy | Documented in gap-report.md |
| **Confidential Computing** | TLS termination + secure enclave integration potential | Documented in docs/gateway-patterns.md |
| **Quantum Error Correction** | Not applicable | Documented |
| **Service Mesh (Istio/Linkerd)** | Architecture alignment in docs/course-alignment.md | Documented |
| **gRPC Gateway** | Protocol bridge concept documented | Documented in docs/gateway-patterns.md |