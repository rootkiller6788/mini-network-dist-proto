# Course Tree — mini-rpc-framework

## Prerequisites (Knowledge This Module Depends On)

```
Computer Architecture (module-1)
    ├── Endianness (big-endian wire format)
    └── Memory hierarchy (cache-friendly CRC32 table)

Operating Systems (module-2)
    ├── Process/Thread model (thread pool)
    ├── Socket API (TCP transport)
    └── Signal handling (graceful shutdown)

Data Structures (module-3)
    ├── Hash table (FNV-1a dispatch lookup)
    ├── Circular buffer (work queue)
    └── Linked list (interceptor chain)

Networking (module-4)
    ├── TCP/IP protocol stack
    ├── Socket programming (BSD sockets)
    └── Application-layer protocols

Mathematics
    ├── Polynomial arithmetic (CRC32 GF(2))
    ├── Probability (Shannon's error bounds)
    └── Queueing theory (Little's Law)
```

## Dependents (Modules That Depend On This One)

```
mini-rpc-framework (module-5)
    │
    ├──→ backend (module-8)
    │       Uses RPC for microservice communication
    │
    ├──→ security (module-13)
    │       Audits RPC auth + transport security
    │
    └──→ app-industry (module-19)
            RPC as enterprise integration backbone
```

## Internal Dependency Graph

```
rpc_encoding.h (base layer: types + serialization)
    │
    ├──→ rpc_transport.h (network I/O)
    │       │
    │       └──→ rpc_stub.h (client proxy)
    │
    ├──→ rpc_registry.h (service discovery)
    │       │
    │       └──→ rpc_stub.h (uses registry for LB)
    │
    ├──→ rpc_interceptor.h (middleware)
    │
    ├──→ rpc_protocol.h (advanced protocol)
    │       │
    │       └── Uses: CRC32, framing, versioning, streams
    │
    └──→ rpc_server.h (server skeleton)
            │
            Uses: rpc_transport, rpc_registry, rpc_interceptor
```

## Learning Path

1. **Start**: `rpc_encoding.h` — understand message format
2. **Transport**: `rpc_transport.h` — how messages move
3. **Discovery**: `rpc_registry.h` — how to find services
4. **Client**: `rpc_stub.h` — how to call services
5. **Middleware**: `rpc_interceptor.h` — cross-cutting concerns
6. **Advanced**: `rpc_protocol.h` — protocol internals
7. **Full Stack**: `rpc_server.h` — the complete server
