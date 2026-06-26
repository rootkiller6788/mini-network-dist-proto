# Course Alignment — mini-rpc-framework

## Nine-School Curriculum Mapping

### MIT
- **6.824 Distributed Systems**: RPC semantics, at-least-once delivery, interceptors as middleware
- **6.858 Computer Security**: Auth interceptor (token-based), rate limiting, tracing for audit

### Stanford
- **CS 144 Networking**: TCP transport, connection pool, keepalive, protocol framing
- **CS 245 Database**: Service discovery as distributed consensus lite (CAP theorem trade-offs)

### Berkeley
- **CS 162 Operating Systems**: Thread pool, work queue (producer-consumer), graceful shutdown
- **CS 294 AI Systems**: RPC as model serving backbone (conceptual)

### CMU
- **15-410 Operating Systems**: Lock-free work queue, thread scheduling, Amdahl's Law
- **15-418 Parallel Computing**: Work-stealing queue concept, thread pool parallelism

### UT Austin
- **CS 380D Distributed Systems**: Service discovery, load balancing (weighted, round-robin)
- **CS 395T Systems for ML**: RPC as ML inference dispatch (conceptual)

### ETH Zurich
- **263-3501 Parallel Programming**: Thread pool implementation, work distribution
- **263-0006 Computer Architecture**: Endianness (big-endian wire format), CRC32 hardware acceleration concept

### Cambridge
- **Part II: Concurrent Systems**: Lock-free queue, interceptor chain as concurrency pattern
- **Part II: Compiler Construction**: JSON recursive descent parser as compiler frontend

### Tsinghua (清华)
- **操作系统**: Process/thread model, work queue, graceful shutdown
- **计算机网络**: TCP transport, protocol framing, CRC error detection

### Georgia Tech
- **CS 6210 Advanced OS**: Thread pool, lock-free structures, performance analysis
- **CS 6290 HPCA**: CRC32 instruction-level parallelism, cache-friendly table lookups

## Key Course Topics Covered

| Topic | Course(s) | Implementation |
|-------|-----------|----------------|
| RPC Semantics | MIT 6.824, UT CS 380D | stub_call() with retry/timeout |
| Serialization | Stanford CS 144 | JSON + Binary encoders |
| Service Discovery | MIT 6.824, UT CS 380D | Registry with LB backends |
| Middleware/AOP | MIT 6.858 | Interceptor chain |
| Thread Pool | Berkeley CS 162, CMU 15-410 | Work queue + workers |
| CRC32 | Cambridge Part II, ETH 263-0006 | IEEE 802.3 table-driven |
| Amdahl's Law | CMU 15-418, GT CS 6210 | Server speedup prediction |
| Little's Law | GT CS 6210, Berkeley CS 162 | Queue sizing validation |
| Protocol Design | Stanford CS 144, 清华 计算机网络 | Framing + versioning |
| Graceful Shutdown | Berkeley CS 162 | Drain queue -> close -> free |
