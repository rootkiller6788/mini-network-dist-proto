# Course Alignment — 课程对齐

> 本模块与 MIT 6.829、Stanford CS144 课程内容的映射

## MIT 6.829 — Computer Networks

MIT 6.829 是一门专注于计算机网络原理和实践的研究生课程，涵盖网络架构、协议设计和性能分析。

### 模块与课程对应

| 本模块 | MIT 6.829 讲座 | 主题 |
|--------|---------------|------|
| `socket_tcp.h/.c` | Lecture 6-8 | Transport: TCP reliability, congestion control |
| `udp_dns.h/.c` | Lecture 5 | Transport: UDP, DNS resolution |
| `ip_packet.h/.c` | Lecture 3-4 | Network: IP addressing, routing, fragmentation |
| `tls_handshake.h/.c` | Lecture 19-20 | Security: TLS, certificates, key exchange |
| `http_basic.h/.c` | Lecture 9 | Applications: HTTP, the web |

### 关键概念覆盖

| 概念 | 6.829 关联 | 本模块实现 |
|------|-----------|----------|
| 三次握手 (3-way Handshake) | Lecture 7 | `tcp_connect()`, `tcp_accept()` |
| 四次挥手 (4-way Close) | Lecture 7 | `tcp_close_active()`, `tcp_close_passive()` |
| 滑动窗口 (Sliding Window) | Lecture 7 | `send_window`, `recv_window` |
| 拥塞控制 (Congestion Control) | Lecture 8 | Reno 算法 (文档化) |
| IP 分片重组 (Fragmentation) | Lecture 4 | `ip_fragment()`, `ip_reassemble()` |
| 校验和 (Checksum) | Lecture 3 | `ip_checksum()` — 16位一补集 |
| DNS 解析 (DNS Resolution) | Lecture 5 | `dns_build_query()`, `dns_resolve()` |
| TLS 握手 (TLS Handshake) | Lecture 19-20 | ECDHE, 证书链, Finished |
| HTTP 协议 (HTTP Protocol) | Lecture 9 | 请求解析, 响应构建, 分块编码 |

### 6.829 教材参考

- Peterson & Davie, *Computer Networks: A Systems Approach*
- Kurose & Ross, *Computer Networking: A Top-Down Approach*
- 课程对 TCP 拥塞控制、公平性和路由协议有深入探讨

---

## Stanford CS144 — Introduction to Computer Networking

Stanford CS144 是本科级别的计算机网络课程，注重实验和动手实践。

### 模块与课程对应

| 本模块 | CS144 讲座 | 主题 |
|--------|----------|------|
| `socket_tcp.h/.c` | Lectures 3-5 | Transport Layer: TCP fundamentals |
| `udp_dns.h/.c` | Lecture 3 | Transport Layer: UDP |
| `ip_packet.h/.c` | Lectures 1-2 | Network Layer: IP, packet switching |
| `tls_handshake.h/.c` | Lecture 18 | Security: TLS |
| `http_basic.h/.c` | Lecture 9 | Applications: HTTP |

### CS144 实验对齐

| CS144 Lab | 主题 | 本模块对应 |
|-----------|------|----------|
| Lab 0: ByteStream | 可靠字节流抽象 | `TCPSocket.send_buf` / `recv_buf` |
| Lab 1-3: StreamReassembler, TCPReceiver, TCPSender | TCP 实现 | `tcp_send()`, `tcp_recv()`, 序列号管理 |
| Lab 4: TCPConnection | 完整连接 | `TCPSocket` 状态机 |
| Lab 5: Network Interface | ARP, 以太网 | IP 构建 (本模块) |
| Lab 6: Router | IP 路由 | IP 头部处理 |

### CS144 核心概念

- **可靠传输**: 校验和、确认/重传、序列号
- **流控制**: 基于窗口的端到端流量控制
- **拥塞控制**: AIMD (加性增乘性减)
- **分组交换**: 存储转发、队列延迟
- **分层模型**: OSI 7层、TCP/IP 4层

---

## UNIX Network Programming (Stevens)

| 概念 | UNP 章节 | 本模块对应 |
|------|---------|----------|
| Socket API | Ch. 3-4 | `tcp_socket_create()`, `udp_socket_create()` |
| TCP Client/Server | Ch. 5 | `tcp_connect()`, `tcp_bind_listen()`, `tcp_accept()` |
| I/O Multiplexing | Ch. 6 | (未实现 — 单连接模型) |
| Socket Options | Ch. 7 | (简化 — 仅基本状态) |
| UDP | Ch. 8 | `udp_sendto()`, `udp_recvfrom()` |
| Name/Address Conversions | Ch. 11 | `dns_resolve()`, `ip_str_to_u32()` |
| Daemon Processes | Ch. 13 | (未实现) |
| Advanced I/O | Ch. 14-15 | (未实现) |

---

## 其他参考来源

| 来源 | 主题 | 本模块覆盖 |
|------|------|----------|
| RFC 793 | TCP Specification | 状态机, 序列号 |
| RFC 768 | UDP Specification | UDP 套接字 |
| RFC 791 | IP Specification | IP 头部, 分片 |
| RFC 1034/1035 | DNS | DNS 查询构建与解析 |
| RFC 8446 | TLS 1.3 | 握手流程, 密码套件 |
| RFC 7230-7235 | HTTP/1.1 | 请求解析, 头部处理 |

---

## 学习路径建议

### 初学者路径

1. `http_basic.c` — 从应用层开始, 理解 HTTP 请求/响应
2. `udp_dns.c` — UDP 和 DNS, 简单的无连接协议
3. `ip_packet.c` — IP 数据包结构, 分片和重组
4. `socket_tcp.c` — TCP 状态机和可靠性
5. `tls_handshake.c` — TLS 安全协议

### 进阶研究路径

1. 阅读 MIT 6.829 Lecture 7-8 论文 (CUBIC, BBR)
2. 实现完整的 Reno/CUBIC 拥塞控制
3. 添加 SACK (Selective ACK) 支持
4. 实现 HTTP/2 头部压缩 (HPACK)
5. 实现 QUIC 协议基础
