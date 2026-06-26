# mini-network-base — 网络基础 (C 语言实现)

> 参考 MIT 6.829, Stanford CS144, UNIX Network Programming

## Module Status: COMPLETE ✅

| Level | Name | Status | Lines |
|-------|------|--------|-------|
| **include/** | 8 头文件 | Complete | 856 |
| **src/** | 8 实现文件 | Complete | 2406 |
| **include/ + src/** | **总计** | **Complete** | **3262** |
| **tests/** | 50 assert-based tests | Complete | 829 |

- **L1-L6**: Complete
- **L7**: Complete (3 demo applications + test suite)
- **L8**: Partial (2 advanced topics implemented)
- **L9**: Partial (documented, not implemented)

## 项目简介

`mini-network-base` 是一个纯 C99 的网络协议教学实现，模拟 TCP/IP 协议栈核心机制。本项目涵盖传输层(TCP/UDP)、网络层(IP/ICMP/Routing)、链路层(IP Fragmentation)、应用层(DNS/HTTP)到安全层(TLS 1.3)及拥塞控制(TCP CC)，通过精简的模拟代码帮助理解计算机网络的核心概念。

## 8 大模块

| 模块 | 文件 | 描述 |
|------|------|------|
| TCP Socket | `include/socket_tcp.h` `src/socket_tcp.c` | TCP 连接管理、三次握手、四次挥手、滑动窗口模拟 |
| TCP Congestion | `include/tcp_congestion.h` `src/tcp_congestion.c` | 慢启动、拥塞避免、快速重传、快速恢复、Jacobson RTO |
| UDP/DNS | `include/udp_dns.h` `src/udp_dns.c` | UDP 套接字、DNS 查询构建与响应解析 |
| IP Packet | `include/ip_packet.h` `src/ip_packet.c` | IPv4 数据包构建、分片重组、一补集校验和 |
| IP Routing | `include/ip_routing.h` `src/ip_routing.c` | CIDR、最长前缀匹配、Dijkstra、Bellman-Ford、路由聚合 |
| ICMP | `include/icmp_proto.h` `src/icmp_proto.c` | Echo/Ping、TTL Exceeded、Dest Unreachable、Traceroute |
| TLS | `include/tls_handshake.h` `src/tls_handshake.c` | TLS 1.3 握手、ECDHE 密钥交换、证书验证 |
| HTTP | `include/http_basic.h` `src/http_basic.c` | HTTP/1.1 请求解析、响应构建、分块编码 |

## 九层知识覆盖 (L1-L9)

### L1: 核心定义 (Complete)
- TCP 11 状态机 / Congestion Control 4 状态机 / RTT Estimator
- IPv4 Header / Fragmentation Buffer / CIDR / ICMP Types & Codes
- UDP Socket / DNS Header & RR / TLS State Machine / HTTP Methods
- Routing Table Entry / Network Topology Graph / ICMP Echo & Error

### L2: 核心概念 (Complete)
- TCP 连接管理 (3-way/4-way handshake) — `socket_tcp.c`
- TCP 拥塞控制 (AIMD) — `tcp_congestion.c`
- IP 分片与重组 — `ip_packet.c`
- ICMP 网络诊断 (Ping/Traceroute) — `icmp_proto.c`
- DNS 域名解析 — `udp_dns.c`
- TLS 前向保密 — `tls_handshake.c`
- CIDR 无类域间路由 — `ip_routing.c`
- Count-to-Infinity 问题 (RIP) — `ip_routing.c`

### L3: 工程结构 (Complete)
- TCP 滑动窗口 + 重传队列模拟 — `socket_tcp.c`
- IP 分片缓冲区管理 — `ip_packet.c`
- 路由表排序/聚合 — `ip_routing.c`
- ICMP 错误消息嵌套 — `icmp_proto.c`
- 网络拓扑图邻接表 — `ip_routing.c`

### L4: 标准/定理 (Complete)
- RFC 6298: Jacobson's RTO Algorithm
  - `SRTT = (1-α)·SRTT + α·RTT_sample`
  - `RTTVAR = (1-β)·RTTVAR + β·|SRTT - RTT_sample|`
  - `RTO = SRTT + max(G, K·RTTVAR)`  (α=1/8, β=1/4, K=4)
- RFC 5681: AIMD — Additive Increase / Multiplicative Decrease
- RFC 1071: One's complement checksum
- RFC 4632: CIDR / Subnet mask derivation
- RFC 1812: Longest Prefix Match (Section 5.2.4.3)
- Split Horizon: Count-to-Infinity prevention
- Kleinrock's Independence Approximation (RTT estimation)
- Chiu & Jain (1989): AIMD optimality proof

### L5: 算法/方法 (Complete)
| 算法 | 实现位置 | 复杂度 | 参考 |
|------|---------|--------|------|
| Jacobson's RTO | `tcp_congestion.c` | O(1) | RFC 6298 / SIGCOMM '88 |
| TCP Slow Start | `tcp_congestion.c` | Per-ACK | RFC 5681 §3.1 |
| TCP Fast Retransmit | `tcp_congestion.c` | Per-dupACK | RFC 5681 §3.2 |
| IP 分片算法 | `ip_packet.c` | O(n) | RFC 791 |
| IP 重组 (qsort) | `ip_packet.c` | O(n log n) | RFC 815 |
| 最长前缀匹配 (LPM) | `ip_routing.c` | O(n) | RFC 1812 |
| 路由聚合 (Supernetting) | `ip_routing.c` | O(n²) | RFC 4632 |
| **Dijkstra SPF** | `ip_routing.c` | O(V²) | Dijkstra 1959 |
| **Bellman-Ford DV** | `ip_routing.c` | O(VE) | Bellman 1958 / Ford 1956 |
| ICMP Echo (Ping) | `icmp_proto.c` | O(1) | RFC 792 |
| DNS 名称解码 | `udp_dns.c` | O(n) | RFC 1035 |
| HTTP 分块解码 | `http_basic.c` | O(n) | RFC 7230 |
| 一补集校验和 | `ip_packet.c` | O(n) | RFC 1071 |

### L6: 经典工程问题 (Complete)
- TCP Echo Server/Client — `examples/tcp_demo.c`
- DNS Resolver — `examples/dns_demo.c`
- TLS 1.3 Handshake Simulator — `examples/tls_demo.c`
- TCP Congestion Control Unit Tests — `tests/test_main.c`
- IP Routing Table with LPM — `src/ip_routing.c`
- ICMP Ping/Traceroute Simulation — `src/icmp_proto.c`

### L7: 应用 (Complete, 3+)
1. TCP + HTTP 请求/响应模拟 (Web Server 原型) — `tcp_demo.c`
2. DNS 查询构建与解析 (A/AAAA/MX) — `dns_demo.c`
3. 完整 TLS 1.3 6步握手 + ECDHE 密钥交换 — `tls_demo.c`
4. 50 项自动化测试覆盖所有模块 — `make test`

### L8: 进阶主题 (Partial, 2 implemented)
1. **Dijkstra SPF** (OSPF 链路状态路由) — `ip_routing.c`
2. **Bellman-Ford DV** + Count-to-Infinity (RIP) — `ip_routing.c`
3. Split Horizon / Poison Reverse (文档) — `ip_routing.h`
4. PRR (Proportional Rate Reduction) (RFC 6937) — 文档

### L9: 工业前沿 (Partial, documented)
- TCP CUBIC (RFC 8312) — 文档
- QUIC Protocol (RFC 9000) — 文档
- BBR Congestion Control — 文档
- BGP Path Vector Protocol — 文档

## 九校课程映射

| 学校 | 课程 | 本模块对应知识点 |
|------|------|-----------------|
| **MIT** | 6.829 Computer Networks | TCP CC, IP routing, Dijkstra |
| **Stanford** | CS144 Networking | TCP state machine, sliding window |
| **Berkeley** | CS 168 Internet Architecture | CIDR, LPM, ICMP |
| **CMU** | 15-441 Computer Networks | Bellman-Ford, split horizon |
| **清华** | 计算机网络 | TCP/IP 协议栈全貌 |
| **ETH** | 263-3501 Parallel Programming | (cross-cutting) |

## 快速开始

### 编译

```bash
make all
```

### 运行测试

```bash
make test
# 预期输出: ALL TESTS PASSED (50/50)
```

### 运行示例

```bash
make run-tcp-demo     # TCP 连接演示
make run-dns-demo     # DNS 查询演示
make run-tls-demo     # TLS 握手演示
```

## 依赖

- GCC (或兼容 C99 的编译器)
- GNU Make
- 无外部库依赖 (仅 libc + libm)

## 核心定理列表

| 定理 | 公式/陈述 | 位置 |
|------|----------|------|
| Jacobson's RTO | RTO = SRTT + max(G, 4·RTTVAR) | `tcp_congestion.c:52` |
| AIMD Convergence | cwnd(t+1) = cwnd(t) + a/cwnd(t); on loss: cwnd /= 2 | `tcp_congestion.c:122,140` |
| One's Complement Checksum | sum = Σ w_i; checksum = ~sum (mod 2^16) | `ip_packet.c:46-60` |
| Subnet Mask | mask(n) = 0xFFFFFFFF << (32-n) | `ip_routing.c:35` |
| Dijkstra Optimality | Greedy choice + optimal substructure | `ip_routing.c:Appendix` |
| Bellman-Ford | d[v] = min(d[v], d[u] + w(u,v)) repeated |V|-1 times | `ip_routing.c:Appendix` |
| Split Horizon | Don't advertise route back to nexthop | `ip_routing.c:232` |

## 参考资料

- RFC 793 — Transmission Control Protocol
- RFC 791 — Internet Protocol
- RFC 792 — Internet Control Message Protocol
- RFC 1034/1035 — Domain Names
- RFC 1071 — Computing the Internet Checksum
- RFC 4632 — CIDR
- RFC 5681 — TCP Congestion Control
- RFC 6298 — Computing TCP's RTO
- RFC 8446 — TLS 1.3
- RFC 7230-7235 — HTTP/1.1
- Jacobson, V. (1988). Congestion Avoidance and Control. SIGCOMM '88.
- Dijkstra, E.W. (1959). A Note on Two Problems in Connexion with Graphs.
- Bellman, R. (1958). On a Routing Problem.
- Chiu & Jain (1989). Analysis of Increase/Decrease Algorithms.
- MIT 6.829: Computer Networks
- Stanford CS144: Introduction to Computer Networking
- W. Richard Stevens, UNIX Network Programming
