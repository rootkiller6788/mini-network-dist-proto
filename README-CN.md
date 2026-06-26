# Mini Network Distrib Protocol（迷你网络分布式协议）

**从零开始、零依赖的 C 语言实现**，涵盖网络协议、分布式系统和通信框架核心概念。每个模块以教学级精度建模真实网络和分布式计算行为 — 从 TCP/UDP Socket 到 RPC 框架、共识协议、分布式事务和 API 网关。模块映射到 MIT、Stanford、CMU 课程，将网络理论桥接到可运行的 C 代码。

## 模块总览

| 模块 | 主题 | 参考标准 |
|--------|--------|----------------|
| [mini-network-base](mini-network-base/) | TCP/UDP Socket 编程、IP 包结构、DNS 解析、TLS 握手模型、HTTP/1.1 基础 | MIT 6.829, Stanford CS144 |
| [mini-app-protocol](mini-app-protocol/) | HTTP/2 帧、gRPC/Protobuf、WebSocket 帧、MQTT 发布订阅、RESTful 设计 | HTTP/2 RFC 9113, gRPC, MQTT 5.0 |
| [mini-dist-protocol](mini-dist-protocol/) | Raft 共识（领导者选举、日志复制）、Paxos、Gossip 协议、SWIM 成员管理 | Raft 论文, Paxos Made Simple |
| [mini-dist-system-theory](mini-dist-system-theory/) | CAP 定理、FLP 不可能性、Lamport 时钟、向量时钟、CRDT、拜占庭将军 | MIT 6.824, Kleppmann DDIA |
| [mini-dist-transaction](mini-dist-transaction/) | 2PC/3PC、Saga 模式、TCC、分布式锁、幂等性 | CMU 15-721, Percolator, Spanner |
| [mini-gateway-proxy](mini-gateway-proxy/) | 反向代理、负载均衡（轮询/最少连接/一致性哈希）、API 网关、熔断器 | NGINX, Envoy, Netflix OSS |
| [mini-rpc-framework](mini-rpc-framework/) | RPC 编码（JSON/MsgPack/二进制）、传输、服务注册、Stub 生成 | gRPC, Thrift, Dubbo |

## 设计理念

- **零外部依赖** — 纯 C（C99/C11），仅使用 `libc` 和 `libm`
- **模块自包含** — 每个目录自带 `Makefile`、`include/`、`src/`、`examples/`、`demos/`、`tests/`
- **用户态协议仿真** — 对网络协议和分布式算法的教学级建模
- **理论到代码的映射** — 每个模块包含 `docs/` 目录，内有论文对齐说明
- **实用演示程序** — Raft 集群仿真器、HTTP/2 帧构建器、RPC 框架、负载均衡器等

## 构建方式

每个模块相互独立。进入模块目录后运行：

```bash
cd mini-dist-protocol
make all    # 构建全部
make test   # 运行测试
```

需要 **GCC** 和 **GNU Make**。

## 项目结构

```
mini-network-dist-proto/
├── mini-network-base/          # 网络基础
├── mini-app-protocol/          # 应用层协议
├── mini-dist-protocol/         # 分布式协议（Raft、Paxos）
├── mini-dist-system-theory/    # 分布式系统理论
├── mini-dist-transaction/      # 分布式事务
├── mini-gateway-proxy/         # 网关与代理
└── mini-rpc-framework/         # RPC 框架
```

## 许可证

MIT
