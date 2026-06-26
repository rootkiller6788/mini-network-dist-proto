# Knowledge Graph — mini-rpc-framework

## Nine-Level Knowledge Coverage

### L1: Core Definitions (Complete)

| Item | Type | Location |
|------|------|----------|
| RPCCodec enum (JSON, MessagePack, Binary) | enum | `include/rpc_encoding.h` |
| RPCValueType enum (INT32, INT64, STRING, BOOL, FLOAT, ARRAY, NULL) | enum | `include/rpc_encoding.h` |
| RPCValue (tagged union) | struct | `include/rpc_encoding.h` |
| RPCMessage (RPC request/response) | struct | `include/rpc_encoding.h` |
| RPCBuffer (dynamic byte buffer) | struct | `include/rpc_encoding.h` |
| RPCTransportType enum (TCP, HTTP, Unix Socket) | enum | `include/rpc_transport.h` |
| RPCConnection (socket wrapper with keepalive) | struct | `include/rpc_transport.h` |
| RPCTransport (connection manager with pool) | struct | `include/rpc_transport.h` |
| ServiceDescriptor (method list + metadata) | struct | `include/rpc_registry.h` |
| ServiceInstance (host:port + weight + health) | struct | `include/rpc_registry.h` |
| ServiceRegistry (service table + instances) | struct | `include/rpc_registry.h` |
| DiscoveryBackend enum (STATIC, DNS, ETCD) | enum | `include/rpc_registry.h` |
| RPCStub (client proxy) | struct | `include/rpc_stub.h` |
| RPCInterceptor (AOP middleware) | struct | `include/rpc_interceptor.h` |
| InterceptorChain (ordered interceptor list) | struct | `include/rpc_interceptor.h` |
| RPCProtoFrame (wire-format frame) | struct | `include/rpc_protocol.h` |
| RPCProtoVersion enum (1.0, 2.0, ANY) | enum | `include/rpc_protocol.h` |
| RPCCompressAlgo enum (NONE, ZLIB, LZ4, SNAPPY) | enum | `include/rpc_protocol.h` |
| RPCFrameType enum (UNARY, STREAM_DATA, END, KEEPALIVE, HANDSHAKE) | enum | `include/rpc_protocol.h` |
| RPCStream (multiplexed bidirectional stream) | struct | `include/rpc_protocol.h` |
| RPCProtocolSession (protocol state machine) | struct | `include/rpc_protocol.h` |
| RPCServer (server skeleton) | struct | `include/rpc_server.h` |
| RPCWorker (thread pool worker) | struct | `include/rpc_server.h` |
| RPCWorkQueue (lock-free MPMC queue) | struct | `include/rpc_server.h` |
| RPCWorkItem (dispatch item) | struct | `include/rpc_server.h` |

### L2: Core Concepts (Complete)

| Concept | Implementation | Files |
|---------|---------------|-------|
| RPC Serialization | JSON + Binary encoder/decoder | `src/rpc_encoding.c` |
| Network Transport | TCP socket with length-prefix framing | `src/rpc_transport.c` |
| Service Discovery | Registry with static/DNS/ETCD backends | `src/rpc_registry.c` |
| Client Stub | Synchronous + asynchronous call wrapper | `src/rpc_stub.c` |
| Interceptor Chain | AOP middleware with before/after hooks | `src/rpc_interceptor.c` |
| Protocol Versioning | Semantic version negotiation | `src/rpc_protocol.c` |
| Request Dispatch | Method lookup + handler invocation | `src/rpc_server.c` |
| Connection Pool | Reusable connection pool with keepalive | `src/rpc_transport.c` |

### L3: Engineering Structures (Complete)

| Structure | Description | Location |
|-----------|-------------|----------|
| JSON Encoder/Decoder | Hand-written recursive descent parser | `src/rpc_encoding.c` |
| Binary Protocol | Tag-Length-Value encoding (Big Endian) | `src/rpc_encoding.c` |
| Connection Pool | LRU eviction with keepalive | `src/rpc_transport.c` |
| Load Balancer | Weighted random + round-robin | `src/rpc_registry.c` |
| Interceptor Chain | Priority-ordered linked chain | `src/rpc_interceptor.c` |
| Protocol Framing | Magic + header + payload + CRC32 trailer | `src/rpc_protocol.c` |
| Lock-free Work Queue | Circular buffer with head/tail (Lamport) | `src/rpc_server.c` |
| Thread Pool | M workers consuming shared queue | `src/rpc_server.c` |

### L4: Standards/Theorems (Complete)

| Theorem/Standard | Description | Verification |
|-----------------|-------------|-------------|
| **Shannon's Theorem (Error Detection)** | P(undetected CRC32 error) <= 2^(-32) | `rpc_proto_error_bound()` + test_crc32_error_detection() |
| **CRC32 (IEEE 802.3)** | Polynomial 0xEDB88320, check value 0xCBF43926 | test_crc32_known_values() |
| **Amdahl's Law** | speedup(N) = 1/(S + (1-S)/N) | test_amdahls_law(), server demo prints prediction |
| **Little's Law** | L = lambda * W (queue length = arrival_rate * wait_time) | rpc_server_stats() computes and validates |
| **FNV-1a Hash** | 32-bit non-cryptographic hash (Fowler-Noll-Vo) | test_fnv1a_hash() |
| **Semantic Versioning** | Major.Minor.Patch negotiation for compatibility | test_version_negotiation() |
| **CAP Theorem** | Documented in service discovery trade-offs | docs/course-alignment.md |

### L5: Algorithms/Methods (Complete)

| Algorithm | Complexity | Implementation |
|-----------|------------|---------------|
| **CRC32 (Sarwate 1988)** | O(n) time, O(1) space | `rpc_crc32_update()` with 256-entry LUT |
| **FNV-1a Hash** | O(n) time | `rpc_fnv1a_hash()` |
| **Weighted Load Balancing** | O(k) per selection | `registry_lb_weighted()` |
| **Round-Robin Scheduling** | O(1) per selection | `registry_lb_round_robin()` |
| **JSON Recursive Descent** | O(n) parsing | `rpc_decode_json()` |
| **Binary TLV Encoding** | O(n) encode/decode | `rpc_encode_binary()` / `rpc_decode_binary()` |
| **Semantic Version Comparison** | O(1) | `rpc_proto_version_compare()` |
| **Work Queue (FIFO)** | O(1) push/pop | `rpc_work_queue_push/pop()` |

### L6: Canonical Problems (Complete)

| Problem | Solution | Example |
|---------|----------|---------|
| **JSON-RPC Client/Server** | Full encode/decode pipeline | `examples/rpc_json_demo.c` |
| **Service Discovery & LB** | Registry with health check + weighted LB | `examples/rpc_registry_demo.c` |
| **Middleware/AOP Pipeline** | Interceptor chain with before/after hooks | `examples/rpc_interceptor_demo.c` |
| **Protocol Design** | Framing, CRC32, version negotiation | `examples/rpc_protocol_demo.c` |
| **Concurrent RPC Server** | Thread pool + work queue + graceful shutdown | `examples/rpc_server_demo.c` |

### L7: Applications (Partial+ — 2 of 2 required)

| Application | Description | Location |
|-------------|-------------|----------|
| **Microservice Backend** | Service registration + LB + interceptors | examples/rpc_server_demo.c, examples/rpc_registry_demo.c |
| **API Gateway Middleware** | Authentication + rate limiting + logging + tracing | examples/rpc_interceptor_demo.c |

### L8: Advanced Topics (Partial+ — 3 topics implemented)

| Topic | Implementation | Status |
|-------|---------------|--------|
| **Streaming Protocol** | Multiplexed bidirectional streams with back-pressure | Complete (`src/rpc_protocol.c`) |
| **Lock-free Work Queue** | Lamport circular buffer for MPMC dispatch | Complete (`src/rpc_server.c`) |
| **Back-pressure Flow Control** | Watermark-based congestion control (TCP-style) | Complete (`rpc_proto_stream_is_backpressured()`) |
| Compression Pipeline | Pluggable codec system (ZLIB/LZ4/Snappy interface) | Complete (`rpc_proto_compress_register()`) |
| Thread Pool Scheduling | Worker threads with fair work distribution | Complete (`src/rpc_server.c`) |

### L9: Industry Frontiers (Partial — documented)

| Frontier | Reference | Notes |
|----------|-----------|-------|
| **gRPC / HTTP/2** | Multiplexed streams, HPACK, protobuf | Architecture referenced; C99 limits prevent full impl |
| **Service Mesh (Istio/Linkerd)** | Sidecar proxy, mTLS, circuit breaker | Documented in `demos/mini-service-mesh/` |
| **AI Compiler (Triton/MLIR)** | Optimized RPC dispatch for GPU workloads | Only conceptual documentation |

---

## Summary

| Level | Status | Count |
|-------|--------|-------|
| L1 Definitions | **Complete** | 25 items |
| L2 Core Concepts | **Complete** | 8 items |
| L3 Engineering Structures | **Complete** | 8 items |
| L4 Standards/Theorems | **Complete** | 7 items |
| L5 Algorithms/Methods | **Complete** | 8 items |
| L6 Canonical Problems | **Complete** | 5 items |
| L7 Applications | **Partial+** | 2 items |
| L8 Advanced Topics | **Partial+** | 5 items |
| L9 Industry Frontiers | **Partial** | 3 documented |
