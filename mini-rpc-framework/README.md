# mini-rpc-framework — RPC 框架 (C 语言实现)

> 参考 gRPC, Apache Thrift, Dubbo, JSON-RPC

mini-rpc-framework 是一个轻量级的 RPC (Remote Procedure Call) 框架，使用纯 C 语言
(C99) 实现，仅依赖标准库 libc 和 libm。设计目标：轻量、模块化、可嵌入，适合教学、嵌入式
和 IoT 场景下的进程间 / 跨节点远程调用。

## 模块概览 (5 Modules)

### 1. 编码层 (Encoding) — `include/rpc_encoding.h` / `src/rpc_encoding.c`

RPC 消息的序列化与反序列化，支持三种编码格式：

| 编码格式 | 枚举值 | 特点 |
|----------|--------|------|
| JSON | `RPC_CODEC_JSON` | 文本格式，人类可读，兼容 JSON-RPC 2.0 |
| MessagePack | `RPC_CODEC_MSGPACK` | 二进制紧凑格式 (保留接口，未来实现) |
| Binary | `RPC_CODEC_BINARY` | 自定义紧凑二进制协议 |

**二进制协议格式** (Big-Endian):
```
[1B msg_type] [4B msg_len] [4B method_hash] [1B param_count] [params...]
```

支持类型: INT32, INT64, STRING, BOOL, FLOAT, ARRAY。

### 2. 传输层 (Transport) — `include/rpc_transport.h` / `src/rpc_transport.c`

网络传输抽象层，支持 TCP / HTTP / Unix Socket 三种传输方式。

- `rpc_transport_init_server` — 启动 RPC 服务端监听
- `rpc_transport_accept` — 接收客户端连接
- `rpc_transport_connect` — 客户端发起连接
- `rpc_send_message` / `rpc_recv_message` — 阻塞式消息收发
- `rpc_keepalive` — 连接保活与心跳
- 连接池 (Connection Pool): 复用连接，减少握手开销
- 多路复用 (Multiplexing): 单连接多请求并发

### 3. 服务注册 (Registry) — `include/rpc_registry.h` / `src/rpc_registry.c`

服务注册与发现中心，支持：

- `registry_register` / `registry_unregister` — 服务注册/注销
- `registry_discover` — 按服务名查找实例列表
- `registry_health_check` / `registry_heartbeat` — 健康检查与心跳
- `registry_lb_select` — 客户端负载均衡 (Round-Robin / Weighted)

支持三种发现后端: STATIC, DNS, ETCD。

### 4. 桩/代理 (Stub) — `include/rpc_stub.h` / `src/rpc_stub.c`

客户端调用桩 (Client Stub)，封装完整的 RPC 调用流程：

```
stub_call: build message → encode → send → wait response → decode → return
```

- `stub_call` — 同步调用，阻塞等待响应
- `stub_async_call` — 异步调用，通过回调获取结果
- `stub_timeout_handle` — 超时处理与重连机制
- 展示生成桩的设计模式: `int32_t calc_add(CalcStub *s, int32_t a, int32_t b)`

### 5. 拦截器 (Interceptor) — `include/rpc_interceptor.h` / `src/rpc_interceptor.c`

拦截器链 (Interceptor Chain) 实现 AOP (面向切面) 风格的中间件：

```
request → [logging → auth → tracing → business] → response
```

内置拦截器: LOGGING, AUTH, METRICS, RATE_LIMIT, RETRY, TRACING。

- `interceptor_before_invoke` — 按顺序调用所有 before 函数
- `interceptor_after_invoke` — 按逆序调用所有 after 函数

## 构建与运行

### 前置要求

- GCC (支持 C99)
- GNU Make
- 标准 C 库 (libc) 和数学库 (libm)

### 编译

```bash
make all          # 编译静态库 libminirpc.a
make examples     # 编译 3 个示例程序
make demos        # 编译 demo 程序
make clean        # 清理构建产物
```

### 运行示例

```bash
make run_json_demo
make run_registry_demo
make run_interceptor_demo
```

## 项目结构

```
mini-rpc-framework/
├── include/                  # 头文件
│   ├── rpc_encoding.h        # 编码层
│   ├── rpc_transport.h       # 传输层
│   ├── rpc_registry.h        # 服务注册
│   ├── rpc_stub.h            # 客户端桩
│   └── rpc_interceptor.h     # 拦截器链
├── src/                      # 实现源文件
│   ├── rpc_encoding.c
│   ├── rpc_transport.c
│   ├── rpc_registry.c
│   ├── rpc_stub.c
│   └── rpc_interceptor.c
├── examples/                 # 示例程序
│   ├── rpc_json_demo.c       # JSON 编解码演示
│   ├── rpc_registry_demo.c   # 服务注册发现演示
│   └── rpc_interceptor_demo.c # 拦截器链演示
├── demos/                    # 设计文档
│   ├── mini-rpc-core/        # RPC 核心架构
│   └── mini-service-mesh/    # Service Mesh 概念
├── docs/                     # 参考文档
│   ├── course-alignment.md   # 课程对齐
│   └── rpc-framework-internals.md # 框架内部分析
├── Makefile
└── README.md
```

## 设计模式

### RPC 调用流程

```
[Client Stub]                    [Server Skeleton]
     |                                  |
     | 1. stub_call(method, params)     |
     | 2. encode(message)               |
     | 3. transport send                |
     |--------------------------------->|
     |                                  | 4. transport recv
     |                                  | 5. decode(message)
     |                                  | 6. dispatch(method)
     |                                  | 7. invoke(handler)
     |                                  | 8. encode(response)
     | 10. stub returns                 | 9. transport send
     |<---------------------------------|
```

### 拦截器链执行顺序

```
Client Before: logging.before → auth.before → tracing.before → [RPC Call]
Client After:  tracing.after  → auth.after  → logging.after  ← [Response]
```

逆序执行 after 函数确保对称性（如 logging 最外层包裹调用）。

## 参考资料

- [gRPC](https://grpc.io) — Google RPC 框架
- [Apache Thrift](https://thrift.apache.org) — 跨语言 RPC 框架
- [Apache Dubbo](https://dubbo.apache.org) — Java 微服务 RPC 框架
- [JSON-RPC 2.0 Specification](https://www.jsonrpc.org/specification)
