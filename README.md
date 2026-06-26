# Mini Network Distrib Protocol

A collection of **from-scratch, zero-dependency C implementations** of network protocols, distributed systems, and communication frameworks. Each module models real networking and distributed computing behavior — from TCP/UDP sockets to RPC frameworks, consensus protocols, distributed transactions, and API gateways. Modules map to MIT, Stanford, CMU courses, bridging network theory to runnable C code.

## Modules

| Module | Topics | Key References |
|--------|--------|----------------|
| [mini-network-base](mini-network-base/) | TCP/UDP socket programming, IP packet structure, DNS resolver, TLS handshake model, HTTP/1.1 basics | MIT 6.829, Stanford CS144 |
| [mini-app-protocol](mini-app-protocol/) | HTTP/2 frames, gRPC/Protobuf, WebSocket frames, MQTT pub/sub, RESTful design | HTTP/2 RFC 9113, gRPC, MQTT 5.0 |
| [mini-dist-protocol](mini-dist-protocol/) | Raft consensus (leader election, log replication), Paxos, Gossip protocol, SWIM membership | Raft paper, Paxos Made Simple |
| [mini-dist-system-theory](mini-dist-system-theory/) | CAP theorem, FLP impossibility, Lamport clocks, vector clocks, CRDT, Byzantine generals | MIT 6.824, Kleppmann DDIA |
| [mini-dist-transaction](mini-dist-transaction/) | 2PC/3PC, Saga pattern, TCC (Try-Confirm-Cancel), distributed locking, idempotency | CMU 15-721, Percolator, Spanner |
| [mini-gateway-proxy](mini-gateway-proxy/) | Reverse proxy, load balancer (round-robin/least-conn/consistent-hash), API gateway, circuit breaker | NGINX, Envoy, Netflix OSS |
| [mini-rpc-framework](mini-rpc-framework/) | RPC encoding (JSON/MsgPack/binary), transport (TCP/HTTP), service registry, stub generation | gRPC, Thrift, Dubbo |

## Design Philosophy

- **Zero external dependencies** — pure C (C99/C11), only `libc` and `libm`
- **Self-contained modules** — each directory has its own `Makefile`, `include/`, `src/`, `examples/`, `demos/`, `tests/`
- **Protocol simulation in user-space** — educational models of network protocols and distributed algorithms
- **Theory-to-code mapping** — every module includes `docs/` with paper-alignment notes
- **Practical demos** — Raft cluster simulator, HTTP/2 frame builder, RPC framework, load balancer, and more

## Building

Each module is standalone. Navigate to a module directory and run:

```bash
cd mini-dist-protocol
make all    # build everything
make test   # run tests
```

Requires **GCC** and **GNU Make**.

## Project Structure

```
mini-network-dist-proto/
├── mini-network-base/          # Network Fundamentals
├── mini-app-protocol/          # Application Protocols
├── mini-dist-protocol/         # Distributed Protocols (Raft, Paxos)
├── mini-dist-system-theory/    # Distributed System Theory
├── mini-dist-transaction/      # Distributed Transactions
├── mini-gateway-proxy/         # Gateways & Proxies
└── mini-rpc-framework/         # RPC Frameworks
```

## License

MIT
