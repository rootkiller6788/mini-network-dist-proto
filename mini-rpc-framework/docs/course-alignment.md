# Course Alignment — mini-rpc-framework 课程对齐

> 将本框架模块映射到主流 RPC 框架的官方文档与设计论文

## 1. gRPC Official Documentation 对齐

### 核心概念映射

| gRPC 概念 | mini-rpc 对应 | 文件 |
|-----------|-------------|------|
| Protocol Buffers IDL | `RPCMessage` 结构体 | `include/rpc_encoding.h` |
| gRPC Stub | `RPCStub` | `include/rpc_stub.h` |
| gRPC Channel | `RPCTransport` + `RPCConnection` | `include/rpc_transport.h` |
| gRPC Interceptor | `RPCInterceptor` + `InterceptorChain` | `include/rpc_interceptor.h` |
| gRPC Name Resolution | `ServiceRegistry` + `DiscoveryBackend` | `include/rpc_registry.h` |
| gRPC Load Balancing | `registry_lb_select` / `registry_lb_weighted` | `src/rpc_registry.c` |
| gRPC Deadline | `RPCCall.timeout_ms` | `include/rpc_stub.h` |
| gRPC Metadata | (可扩展: RPCMessage 字段) | — |

### gRPC 参考文档链接

- [gRPC Core Concepts](https://grpc.io/docs/what-is-grpc/core-concepts/)
  → 本框架的 Stub/Service 分离设计直接映射
- [gRPC Service Definition](https://grpc.io/docs/what-is-grpc/core-concepts/#service-definition)
  → `ServiceDescriptor` + `MethodDescriptor`
- [gRPC Interceptors](https://grpc.io/docs/guides/interceptors/)
  → `InterceptorChain` 的 before/after 模式
- [gRPC Load Balancing](https://grpc.io/docs/guides/load-balancing/)
  → `registry_lb_select` 的加权轮询

## 2. Apache Thrift Whitepaper 对齐

### 参考论文

> Mark Slee, Aditya Agarwal, Marc Kwiatkowski.
> "Thrift: Scalable Cross-Language Services Implementation" (2007)

### Thrift 协议栈对应

```
Thrift Stack           │  mini-rpc 对应
───────────────────────┼─────────────────────────────
Transport              │  rpc_transport.h
  ├─ TSocket           │    rpc_transport_connect (TCP)
  ├─ TServerSocket     │    rpc_transport_init_server
  └─ TSSLSocket        │    (未实现，可扩展)
Protocol               │  rpc_encoding.h
  ├─ TBinaryProtocol   │    rpc_encode_binary
  ├─ TCompactProtocol  │    rpc_encode_binary (compact)
  └─ TJSONProtocol     │    rpc_encode_json
Processor              │  ServiceDescriptor.methods
  └─ TProcessor        │    RPCHandlerFn
Server                 │  RPCTransport.server_socket
  ├─ TSimpleServer     │    单线程 accept 循环
  ├─ TThreadedServer   │    (未实现)
  └─ TThreadPoolServer │    (未实现)
```

### Thrift IDL 设计理念

```
Thrift IDL → Compiler → Generated Code

mini-rpc 简化了 IDL 层，直接使用 C 结构体定义服务:
  ServiceDescriptor 取代 .thrift 文件
  RPCHandlerFn 取代生成的 Processor 类
```

## 3. Apache Dubbo 架构对齐

### Dubbo 核心概念

| Dubbo 概念 | mini-rpc 对应 | 实现 |
|-----------|-------------|------|
| Service (服务) | `ServiceDescriptor` | `rpc_registry.h` |
| Provider (提供者) | `registry_register` + `ServiceInstance` | `src/rpc_registry.c` |
| Consumer (消费者) | `RPCStub` | `src/rpc_stub.c` |
| Registry (注册中心) | `ServiceRegistry` | `src/rpc_registry.c` |
| Monitor (监控) | `interceptor_make_metrics` | `src/rpc_interceptor.c` |
| Protocol (协议) | `RPCCodec` (JSON/BINARY) | `rpc_encoding.h` |
| Cluster (集群) | 多 `ServiceInstance` + LB | `registry_lb_select` |
| Filter (过滤器) | `RPCInterceptor` chain | `rpc_interceptor.h` |

### Dubbo 调用链 → mini-rpc 调用链

```
Dubbo: Consumer → Proxy → Cluster → Filter → Invoker → Protocol → Exchanger → Transport

mini-rpc:
  Consumer = Stub (rpc_stub.h)
  Proxy    = stub_call (编码 + 寻址 + 发送)
  Cluster  = ServiceInstance[] + LB (rpc_registry.h)
  Filter   = interceptor_before_invoke / interceptor_after_invoke
  Invoker  = RPCHandlerFn
  Protocol = rpc_encode_json / rpc_encode_binary
  Transport = rpc_transport_send / rpc_transport_recv
```

## 4. JSON-RPC 2.0 Specification 对齐

### 协议兼容性

本框架的 JSON 编解码兼容 JSON-RPC 2.0 规范：

| JSON-RPC 2.0 要求 | mini-rpc 实现 |
|-------------------|-------------|
| `jsonrpc: "2.0"` | (可选，可扩展) |
| `method: string` | `msg->method_name` → `"method":"..."` |
| `params: array` | `msg->params[]` → `"params":[...]` |
| `id: int/string` | `msg->id` → `"id":1` |
| `result` on success | 响应消息包含返回值 |
| `error` on failure | `msg->is_error` + `msg->error_msg` |

### JSON-RPC 消息格式

```json
// 请求
{
    "jsonrpc": "2.0",
    "method": "Calculator.add",
    "params": [10, 20],
    "id": 1
}

// 成功响应
{
    "jsonrpc": "2.0",
    "result": 30,
    "id": 1
}

// 错误响应
{
    "jsonrpc": "2.0",
    "error": {
        "code": -32601,
        "message": "Method not found"
    },
    "id": 1
}
```

## 5. 补充参考

### 经典论文

| 论文 | 主题 | 与 mini-rpc 的关系 |
|------|------|-------------------|
| "Implementing Remote Procedure Calls" (Birrell & Nelson, 1984) | RPC 基础理论 | 传输层设计与调用语义 |
| "Thrift: Scalable Cross-Language Services" (2007) | 跨语言 RPC | 编解码与 IDL 分层 |
| "The Tail at Scale" (Dean & Barroso, 2013) | 延迟优化 | 超时、重试、hedged requests |
| "End-to-End Arguments in System Design" | 系统设计原则 | 网络层可靠性的职责分配 |

### 开源项目参考

- [gRPC GitHub](https://github.com/grpc/grpc) — gRPC Core (C/C++) 实现
- [Apache Thrift GitHub](https://github.com/apache/thrift) — 多语言 RPC 框架
- [Apache Dubbo GitHub](https://github.com/apache/dubbo) — Java RPC 框架
- [brpc GitHub](https://github.com/apache/brpc) — 百度高性能 RPC (C++)
- [tarpc GitHub](https://github.com/google/tarpc) — Google Rust RPC

### 标准化文档

- [JSON-RPC 2.0 Specification](https://www.jsonrpc.org/specification)
- [Protocol Buffers Language Guide](https://protobuf.dev/programming-guides/proto3/)
- [HTTP/2 RFC 7540](https://datatracker.ietf.org/doc/html/rfc7540)
- [gRPC over HTTP/2](https://github.com/grpc/grpc/blob/master/doc/PROTOCOL-HTTP2.md)
