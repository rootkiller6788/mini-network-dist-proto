# mini-network-base — 网络基础 (C 语言实现)

> 参考 MIT 6.829, Stanford CS144, UNIX Network Programming

## 项目简介

`mini-network-base` 是一个纯 C99 的网络协议教学实现，模拟 TCP/IP 协议栈核心机制。本项目涵盖传输层(TCP/UDP)、网络层(IP)、应用层(DNS/HTTP)到安全层(TLS 1.3)，通过精简的模拟代码帮助理解计算机网络的核心概念。

## 5 大模块

| 模块 | 文件 | 描述 |
|------|------|------|
| TCP | `include/socket_tcp.h` `src/socket_tcp.c` | TCP 连接管理、三次握手、四次挥手、滑动窗口模拟 |
| UDP/DNS | `include/udp_dns.h` `src/udp_dns.c` | UDP 套接字、DNS 查询构建与响应解析 |
| IP | `include/ip_packet.h` `src/ip_packet.c` | IPv4 数据包构建、分片重组、一补集校验和 |
| TLS | `include/tls_handshake.h` `src/tls_handshake.c` | TLS 1.3 握手、ECDHE 密钥交换、证书验证 |
| HTTP | `include/http_basic.h` `src/http_basic.c` | HTTP/1.1 请求解析、响应构建、分块编码 |

## 快速开始

### 编译

```bash
make all
```

### 运行示例

```bash
# TCP 连接演示
make run-tcp-demo

# DNS 查询演示
make run-dns-demo

# TLS 握手演示
make run-tls-demo
```

### 演示输出示例

```
=== mini-network-base: TCP Demo ===

[Server] Socket created.
  [TCP] Listening on port 8080. State=LISTEN
[Client] Initiating connection to 127.0.0.1:8080...
  [TCP] Connected established. State=ESTABLISHED
[Server] Accepted connection.
[Client] Sending HTTP request (78 bytes)...
[Server] Sending HTTP response (234 bytes)...
  [HTTP Message] 234 bytes: HTTP/1.1 200 OK...
[Client] Closing connection (active close)...
  [TCP] Closed. Final state=CLOSED
=== TCP Demo Complete ===
```

## 目录结构

```
mini-network-base/
├── include/
│   ├── socket_tcp.h        # TCP 套接字接口
│   ├── udp_dns.h           # UDP 和 DNS 接口
│   ├── ip_packet.h         # IP 数据包接口
│   ├── tls_handshake.h     # TLS 1.3 握手接口
│   └── http_basic.h        # HTTP 基础接口
├── src/
│   ├── socket_tcp.c        # TCP 实现
│   ├── udp_dns.c           # UDP/DNS 实现
│   ├── ip_packet.c         # IP 实现
│   ├── tls_handshake.c     # TLS 实现
│   └── http_basic.c        # HTTP 实现
├── examples/
│   ├── tcp_demo.c          # TCP 连接模拟
│   ├── dns_demo.c          # DNS 查询模拟
│   └── tls_demo.c          # TLS 握手模拟
├── demos/
│   ├── mini-tcp-stack/
│   │   └── README.md       # TCP 深入解析
│   └── mini-tls-handshake/
│       └── README.md       # TLS 深入解析
├── docs/
│   ├── course-alignment.md # 课程对齐
│   └── network-protocol-basics.md # 协议基础
├── Makefile
└── README.md
```

## 设计原则

- **纯 C99**: 使用 `stdbool.h`, `stdint.h`, `stddef.h`, 无 POSIX 或平台特定扩展
- **libc + libm 依赖**: 仅依赖标准 C 库和数学库
- **内存模拟**: 所有网络通信在进程内模拟, 无需 root 权限或虚拟网卡
- **教学优先**: 代码优先展示协议机制, 而非追求生产级性能
- **命名规范**:
  - 函数: `snake_case`
  - 类型: `PascalCase`
  - 常量: `UPPER_SNAKE_CASE`

## TCP 状态机

```
CLOSED → (connect) → SYN_SENT → (SYN+ACK) → ESTABLISHED
CLOSED → (listen)  → LISTEN   → (SYN→SYN+ACK) → ESTABLISHED

ESTABLISHED → (close) → FIN_WAIT_1 → FIN_WAIT_2 → TIME_WAIT → CLOSED
ESTABLISHED → (rcv FIN)→ CLOSE_WAIT → LAST_ACK → CLOSED
```

## TLS 1.3 握手流程

```
ClientHello          → 支持的密码套件 + ECDHE 公钥
ServerHello          ← 选定的密码套件 + ECDHE 公钥
EncryptedExtensions  ← 加密的扩展参数
Certificate          ← 服务器证书链
CertificateVerify    ← 证书签名验证
Finished             ↔ 完整性验证 (双向)
```

## 依赖

- GCC (或兼容 C99 的编译器)
- GNU Make
- 无外部库依赖

## 许可

本项目仅用于教育和学习目的。

## 参考资料

- RFC 793 — Transmission Control Protocol
- RFC 791 — Internet Protocol
- RFC 1034/1035 — Domain Names
- RFC 8446 — TLS 1.3
- RFC 7230-7235 — HTTP/1.1
- MIT 6.829: Computer Networks
- Stanford CS144: Introduction to Computer Networking
- W. Richard Stevens, UNIX Network Programming
