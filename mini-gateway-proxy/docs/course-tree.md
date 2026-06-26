# Course Tree — mini-gateway-proxy

## Prerequisite Knowledge Dependencies

```
                    ┌─────────────────────┐
                    │   C Programming      │
                    │   (struct, pointer,  │
                    │    malloc, select)   │
                    └──────────┬──────────┘
                               │
          ┌────────────────────┼────────────────────┐
          ▼                    ▼                    ▼
┌─────────────────┐  ┌─────────────────┐  ┌─────────────────┐
│  OS: Sockets    │  │  Data Structures│  │  Distributed    │
│  TCP/IP, I/O    │  │  Hash Ring,     │  │  Systems: CAP,  │
│  multiplexing   │  │  State Machine  │  │  Fallacies      │
└────────┬────────┘  └────────┬────────┘  └────────┬────────┘
         │                    │                    │
         └────────────────────┼────────────────────┘
                              │
         ┌────────────────────┴────────────────────┐
         │                                          │
         ▼                                          ▼
┌─────────────────┐                        ┌─────────────────┐
│  Reverse Proxy  │                        │  Load Balancer  │
│  (HTTP forward) │◄────── uses ──────────│  (server select)│
└────────┬────────┘                        └────────┬────────┘
         │                                          │
         │                    ┌─────────────────────┘
         │                    │
         ▼                    ▼
┌─────────────────────────────────────────────────────────┐
│                    API Gateway                           │
│  ┌──────────┐  ┌──────────┐  ┌──────────┐              │
│  │  Routes  │  │ Plugins  │  │ Auth     │              │
│  └──────────┘  └──────────┘  └──────────┘              │
└────────────┬──────────────────────────────┬─────────────┘
             │                              │
             ▼                              ▼
┌─────────────────────┐          ┌─────────────────────┐
│   Circuit Breaker   │          │   Rate Limiter      │
│   (Hystrix FSM)     │          │   (4 algorithms)    │
└─────────────────────┘          └─────────────────────┘

Advanced Dependencies:
    API Gateway ──► Middleware Chain ──► HTTP Message Parser
    API Gateway ──► Service Registry ──► Connection Pool
    TLS Termination ──► Certificate Validation ──► PKI Concepts
```

## Course Mapping

| University | Course | Module Mapping |
|-----------|--------|---------------|
| MIT | 6.004 Computation Structures | State machines (circuit breaker FSM) |
| MIT | 6.824 Distributed Systems | CAP theorem, consensus, fault tolerance |
| Stanford | CS 144 Networking | HTTP, TCP, TLS, socket programming |
| Berkeley | CS 162 Operating Systems | I/O multiplexing, connection management |
| CMU | 15-410 Operating Systems | Network stack, async I/O |
| CMU | 15-445 Database Systems | Connection pooling, Little's Law |
| UT Austin | CS 380D Distributed Systems | Service discovery, health checks |
| ETH | 263-3501 Parallel Programming | Concurrent connection handling |
| Cambridge | Part II: Concurrent Systems | Lock-free patterns (future) |
| Tsinghua | Computer Networks | Full TCP/IP and HTTP stack |
| Georgia Tech | CS 6210 Advanced OS | Kernel-bypass networking (documented) |