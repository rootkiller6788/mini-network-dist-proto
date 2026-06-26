# mini-rpc-core — RPC 框架核心架构

> 深入理解 RPC 框架的设计原理：从 IDL 到网络传输的完整链路

## 目录

1. [RPC 框架分层架构](#rpc-框架分层架构)
2. [IDL 与代码生成](#idl-与代码生成)
3. [序列化与反序列化](#序列化与反序列化)
4. [传输层设计](#传输层设计)
5. [客户端桩 (Stub)](#客户端桩)
6. [服务端调度 (Dispatch)](#服务端调度)
7. [gRPC 深入剖析](#grpc-深入剖析)
8. [设计权衡与最佳实践](#设计权衡与最佳实践)

---

## RPC 框架分层架构

RPC 框架的核心目标：**让调用远程服务像调用本地函数一样简单**。这需要以下分层协同工作：

```
┌─────────────────────────────────────────────────────────┐
│                    Application Layer                     │
│              (业务代码: Calc.Add(1, 2))                   │
├─────────────────────────────────────────────────────────┤
│                   Stub / Proxy Layer                     │
│         (客户端桩: 序列化参数, 发起网络调用)               │
├─────────────────────────────────────────────────────────┤
│                  Serialization Layer                     │
│           (编码层: JSON / Protobuf / Thrift)              │
├─────────────────────────────────────────────────────────┤
│                   Transport Layer                        │
│        (传输层: TCP / HTTP/2 / gRPC Stream)              │
├─────────────────────────────────────────────────────────┤
│                    Network Layer                         │
│            (网络层: Socket, TLS, HTTP)                    │
└─────────────────────────────────────────────────────────┘
```

### 各层职责

| 层级 | 职责 | 典型实现 |
|------|------|----------|
| Application | 定义业务接口和数据模型 | .proto 文件 (gRPC), .thrift 文件 (Thrift) |
| Stub/Proxy | 封装远程调用为本地调用 | 自动生成代码，处理超时、重试 |
| Serialization | 将数据结构转换为字节流 | JSON, Protocol Buffers, MessagePack |
| Transport | 可靠地将字节流从 A 发送到 B | TCP, HTTP/2, Unix Socket |
| Network | 操作系统级网络 I/O | Socket API, epoll/kqueue/IOCP |

---

## IDL 与代码生成

**IDL (Interface Definition Language)** 是 RPC 框架的基石。它允许开发者用一种
语言无关的方式描述服务接口，然后通过编译器生成特定语言的客户端桩和服务端骨架。

### 典型的 IDL 示例 (类 Protocol Buffers)

```protobuf
syntax = "proto3";

package calculator;

service Calculator {
    rpc Add (AddRequest) returns (AddResponse);
    rpc Subtract (SubtractRequest) returns (SubtractResponse);
}

message AddRequest {
    int32 a = 1;
    int32 b = 2;
}

message AddResponse {
    int32 result = 1;
}
```

### 代码生成流程

```
.proto 文件
    │
    ▼
[protoc 编译器]
    │
    ├──► calculator.pb.h/c       (消息序列化代码)
    └──► calculator.grpc.pb.h/c  (stub/skeleton 代码)
```

生成的 Stub 代码伪结构:

```c
// 自动生成的 calc_stub.h
typedef struct {
    RPCTransport *transport;
} CalculatorStub;

int32_t calculator_add(CalculatorStub *stub, int32_t a, int32_t b) {
    // 1. 构建请求消息
    RPCMessage req;
    rpc_message_init(&req);
    req.param_count = 2;
    req.params[0].value.v_int32 = a;
    req.params[1].value.v_int32 = b;

    // 2. 序列化 + 发送 + 接收 + 反序列化
    return stub_call(stub, "Calculator.add", &req);
}
```

---

## 序列化与反序列化

序列化格式的选择直接影响 RPC 的性能和可读性。

### JSON (文本格式)

- **优点**: 人类可读，调试方便，语言互操作性好
- **缺点**: 体积大，解析慢，不支持二进制数据
- **适用场景**: 内部工具、调试、低吞吐量场景

```
请求: {"method":"Calculator.add","params":[1,2],"id":1}
响应: {"id":1,"result":3}
```

### Protocol Buffers (二进制格式)

- **优点**: 体积小 (通常 3-10x 小于 JSON)，解析快
- **缺点**: 需要 schema，不可读
- **编码原理**: Varint + Tag-Length-Value (TLV)
- **适用场景**: 高性能微服务通信

```
[field_number | wire_type] [value]
5个字节表示: hello (字符串)
0x2A 0x05 h e l l o
```

### 自定义二进制 (mini-rpc BINARY)

本框架实现的紧凑二进制协议 (Big-Endian):

```
+----------+----------+----------+----------+----------+----------+
| 1B type  |  4B len  | 4B hash  | 1B count | param 1  | param 2  |
+----------+----------+----------+----------+----------+----------+

type:   1=request, 2=response, 3=error
len:    total message length (excluding type+len)
hash:   FNV-1a hash of method name
count:  number of parameters
```

### 性能对比 (参考数据)

| 格式 | 编码速度 | 解码速度 | 消息大小 | 可读性 |
|------|---------|---------|---------|--------|
| JSON | 慢 | 慢 | 大 | 好 |
| Protobuf | 快 | 快 | 小 | 差 |
| MessagePack | 中 | 中 | 中 | 差 |
| Binary (compact) | 快 | 快 | 最小 | 差 |

---

## 传输层设计

### TCP 传输

- **可靠性**: 基于 TCP 的可靠、有序字节流
- **帧协议**: 需要在应用层定义消息边界
  - 长度前缀 (Length-Prefixed): 4字节长度 + 消息体
  - 分隔符 (Delimiter): 如 `\r\n` (HTTP style)

```
Frame format (Length-Prefixed):
+------------+----------------------+
| 4B length  |     N bytes body     |
+------------+----------------------+
```

### HTTP/2 传输 (gRPC 方式)

- **多路复用**: 单连接承载多个并发流
- **头部压缩**: HPACK 算法
- **流控制**: 连接级 + 流级流控
- **服务端推送**: Server Push

### 连接池

为了避免频繁建立/断开 TCP 连接的开销：

```
[Client] ──► [Pool: conn1, conn2, conn3] ──► [Server:8080]
                 │  ▲        │  ▲
                 │  │ borrow │  │ return
                 ▼  │        ▼  │
              [RPC1]       [RPC2]
```

- **Keep-Alive**: 定期心跳保活
- **连接淘汰**: LRU / TTL 淘汰空闲连接
- **熔断**: 连接失败达到阈值后自动移除

---

## 客户端桩 (Stub)

### 同步调用流程

```
stub_call(method, params, timeout)
    │
    ├─ 1. 从 Registry 发现目标服务实例 (LB 选择一个)
    ├─ 2. 从连接池获取/创建连接
    ├─ 3. 构建 RPCMessage (填充 method_name, params, id)
    ├─ 4. encode → RPCBuffer
    ├─ 5. transport.send(RPCBuffer)  ← 阻塞?
    ├─ 6. transport.recv(RPCBuffer)  ← 超时等待
    ├─ 7. decode → RPCMessage
    ├─ 8. 释放连接回池
    └─ 9. 返回结果
```

### 异步调用

```
stub_async_call(method, params, callback, user_data)
    │
    ├─ 发送请求 (非阻塞)
    ├─ 注册回调 + 超时定时器
    ├─ 返回 call_id
    │
    ▼ (事件循环)
    ├─ 接收响应 → 触发 callback(resp, 0, user_data)
    └─ 超时     → 触发 callback(NULL, -ETIMEDOUT, user_data)
```

### 超时与重试

```
重试策略:
  1. Exponential Backoff: delay = base * 2^retry
  2. Jitter: delay = random(0, delay)  // 避免惊群效应
  3. Max Retries: 通常 3-5 次

幂等性考虑:
  - GET/QUERY 操作可以安全重试
  - MUTATION 操作需要去重机制 (idempotency key)
```

---

## 服务端调度 (Dispatch)

服务端接收到请求后的处理流程：

```
recv_message(conn)
    │
    ├─ decode(RPCBuffer) → RPCMessage
    │
    ├─ lookup(service_name) → ServiceDescriptor
    │
    ├─ invoke_interceptor_chain(req)
    │     ├─ auth.before()
    │     ├─ rate_limit.before()
    │     └─ logging.before()
    │
    ├─ dispatch(method_name)
    │     └─ handler_fn(req, &resp)  ← 实际业务逻辑
    │
    ├─ invoke_interceptor_chain(resp)
    │     ├─ logging.after()
    │     ├─ rate_limit.after()
    │     └─ auth.after()
    │
    ├─ encode(resp) → RPCBuffer
    │
    └─ send_message(conn, buffer)
```

### 线程模型

| 模型 | 描述 | 优点 | 缺点 |
|------|------|------|------|
| Thread-per-request | 每个请求一个线程 | 简单 | 线程开销大，C10K 问题 |
| Thread Pool | 固定线程数 + 任务队列 | 可控资源 | 长请求阻塞线程 |
| Async/Event Loop | 单线程事件循环 | 高吞吐 | 编程复杂 |
| Reactor Pattern | I/O 多路复用 | gRPC 默认 | 需要异步状态机 |

---

## gRPC 深入剖析

### gRPC 技术栈

```
┌────────────────────────────────────┐
│         gRPC Stub / Server          │
├────────────────────────────────────┤
│   gRPC Core (C, C++, Java, Go)     │
├────────────────────────────────────┤
│   HTTP/2 传输 (HPACK + 流控)        │
├────────────────────────────────────┤
│   Protocol Buffers 序列化            │
├────────────────────────────────────┤
│   TLS / mTLS 安全层                 │
├────────────────────────────────────┤
│   TCP 传输层                        │
└────────────────────────────────────┘
```

### gRPC 通信模式

1. **Unary RPC** (一元调用): 客户端发送一个请求，服务端返回一个响应
   ```
   Client ──req──► Server ──resp──► Client
   ```

2. **Server Streaming RPC**: 客户端发送一个请求，服务端返回流式响应
   ```
   Client ──req──► Server ──r1──► ──r2──► ──r3──► (EOF) ──► Client
   ```

3. **Client Streaming RPC**: 客户端发送流式请求，服务端返回一个响应
   ```
   Client ──r1──► ──r2──► ──r3──► (EOF) ──► Server ──resp──► Client
   ```

4. **Bidirectional Streaming RPC**: 双向流式
   ```
   Client ──r1──► ──r2──► ◄──r3── ◄──r4── ──r5──► Server
   ```

### gRPC 拦截器 (Interceptor)

gRPC 的拦截器分为 ClientInterceptor 和 ServerInterceptor:

```
// Client Interceptor
ClientCall<Req, Resp> intercept(
    MethodDescriptor<Req, Resp> method,
    CallOptions options,
    Channel next) {
    // before: 添加 metadata, 设置 deadline
    ClientCall call = next.newCall(method, options);
    return new ForwardingClientCall() {
        onMessage() { /* after: 日志 */ }
    };
}
```

### gRPC 服务发现与负载均衡

gRPC 提供了可插拔的名称解析和负载均衡架构：

```
DNS Resolver → Service Config → Load Balancer → Subchannel
                                            ├── Address 1
                                            ├── Address 2
                                            └── Address 3

LB 策略:
  - pick_first: 选择第一个可用地址
  - round_robin: 轮询所有可用地址
  - grpclb: 外部负载均衡器
  - ring_hash: 一致性哈希 (xDS)
```

---

## 设计权衡与最佳实践

### 同步 vs 异步

| 维度 | 同步 | 异步 |
|------|------|------|
| 编程复杂度 | 低 (直观) | 高 (回调/状态机) |
| 资源利用率 | 低 (线程阻塞) | 高 (非阻塞 I/O) |
| 吞吐量 | 低 | 高 |
| 调试难度 | 低 | 高 |
| 适用场景 | 低并发, 内部调用 | 高并发, 微服务 |

### 协议选择指南

```
需要浏览器直接调用?
  ├─ 是 → JSON-RPC over HTTP (RESTful 兼容)
  └─ 否 → 是否需要流式传输?
            ├─ 是 → gRPC (HTTP/2 streaming)
            └─ 否 → 是否需要极致性能?
                      ├─ 是 → Thrift / 自定义二进制
                      └─ 否 → JSON-RPC (简单可调试)
```

### 可靠性保障

1. **超时控制 (Deadline)**: 每个 RPC 必须有超时时间
2. **重试 (Retry)**: 仅在幂等操作上重试，使用退避策略
3. **熔断 (Circuit Breaker)**: 连续失败 N 次后快速失败
4. **服务发现**: 自动剔除不健康实例
5. **限流 (Rate Limiting)**: 保护服务不被过载

### 可观测性

```
[Trace ID] ──► [Span: Client.Send] ──► [Span: Server.Recv]
                                              │
                    [Metrics: rpc_count, rpc_latency, rpc_errors]
                                              │
                    [Logging: method=Add, status=OK, latency=5ms]
```

## 参考资料

- [gRPC Design and Architecture](https://grpc.io/docs/)
- [Protocol Buffers Encoding](https://protobuf.dev/programming-guides/encoding/)
- [Apache Thrift: The Missing Guide](https://diwakergupta.github.io/thrift-missing-guide/)
- [HTTP/2 Specification (RFC 7540)](https://httpwg.org/specs/rfc7540.html)
- [Designing Data-Intensive Applications - Chapter 4](https://dataintensive.net/)
