# mini-gateway-proxy — 网关与代理 (C 语言实现)

> 参考 NGINX, Envoy Proxy, Netflix OSS Hystrix, Kong API Gateway

## 简介

mini-gateway-proxy 是一个用标准 C99 编写的轻量级网关与反向代理库，涵盖了现代微服务架构中的核心网络模式：反向代理、负载均衡、断路器、API 网关和限流。

## 模块概览

| 模块 | 头文件 | 源文件 | 功能 |
|------|--------|--------|------|
| **反向代理** | `include/reverse_proxy.h` | `src/reverse_proxy.c` | HTTP 请求转发、头部改写、客户端-后端数据管道 |
| **负载均衡** | `include/load_balancer.h` | `src/load_balancer.c` | RR/SWRR/Least-Conn/一致性哈希/随机，健康检查 |
| **断路器** | `include/circuit_breaker.h` | `src/circuit_breaker.c` | 三态状态机 (CLOSED/OPEN/HALF_OPEN)，级联故障防护 |
| **API 网关** | `include/api_gateway.h` | `src/api_gateway.c` | 路由匹配、插件链、认证、限流集成 |
| **限流器** | `include/rate_limiter.h` | `src/rate_limiter.c` | 令牌桶/漏桶/固定窗口/滑动窗口日志 |

## 工作流程

```
Client Request
    │
    ▼
┌──────────────────┐
│   Reverse Proxy   │ ← proxy_handle_request()
│  (端口监听/Accept) │
└────────┬─────────┘
         │
         ▼
┌──────────────────┐
│   API Gateway     │ ← gateway_handle_request()
│  (路由匹配/插件链) │
└────────┬─────────┘
         │
    ┌────┴────┐
    ▼         ▼
┌────────┐ ┌──────────┐
│ 认证插件 │ │ 限流插件   │ ← gateway_plugin_run()
└────────┘ └──────────┘
    │
    ▼
┌──────────────────┐
│   Load Balancer   │ ← lb_select_server()
│  (选择后端服务器)   │
└────────┬─────────┘
         │
    ┌────┴────┐
    ▼         ▼
┌────────┐ ┌──────────┐
│Circuit │ │ Rate      │
│Breaker │ │ Limiter   │
└────────┘ └──────────┘
    │
    ▼
┌──────────────────┐
│  Upstream Service │
└──────────────────┘
```

## 快速开始

### 构建

```bash
make
```

### 运行示例

```bash
# 负载均衡算法演示 (5 后端, 100 请求/算法)
make run-load-balance

# 断路器模式演示
make run-circuit-breaker

# API 网关完整演示
make run-api-gateway
```

### 编译单个演示

```bash
make bin/load_balance_demo.exe
make bin/circuit_breaker_demo.exe
make bin/api_gateway_demo.exe
```

## 模块详解

### 1. 反向代理 (Reverse Proxy)

基于 `select` 多路复用的 HTTP 反向代理，支持：
- 客户端连接接受与后端连接建立
- 双向数据管道 (Tunnel)
- HTTP 头改写 (X-Forwarded-For, Host)
- 基于路径的后端路由规则

```c
ProxyServer *ps = proxy_init(8080);
proxy_add_rule(ps, "/api/*", "backend", 3000);
proxy_run(ps);
```

### 2. 负载均衡 (Load Balancer)

5 种算法实现：
- **Round-Robin**: 简单轮询
- **Weighted Round-Robin**: NGINX SWRR 平滑加权轮询
- **Least Connections**: 最少连接数
- **Consistent Hash**: 150 虚拟节点一致性哈希环
- **Random**: 随机选择

```c
LoadBalancer *lb = lb_init(LB_CONSISTENT_HASH);
lb_add_server(lb, "10.0.0.1", 8080, 5);
int idx = lb_select_server(lb, "user-id-123");
```

### 3. 断路器 (Circuit Breaker)

Netflix Hystrix 风格的三态断路器：
```
CLOSED → [failures >= threshold] → OPEN
OPEN   → [timeout expired]       → HALF_OPEN
HALF_OPEN → [probes succeed]     → CLOSED
HALF_OPEN → [probe fails]        → OPEN
```

```c
CBCircuit *cb = cb_init("user-svc", 5, 3, 10000);
int ret = cb_call(cb, make_request, &req);
```

### 4. API 网关 (API Gateway)

支持：
- 基于路径和 HTTP 方法的路由匹配
- 6 种插件: AUTH, RATE_LIMIT, LOGGING, CORS, TRANSFORM, CACHE
- 全局插件链 + 路由级插件链
- 内置断路器集成
- 内置限流器集成

```c
APIGateway *gw = gateway_init();
gateway_register_route(gw, "/users", "GET", "user-svc", 8080, true, 100);
gateway_register_route(gw, "/orders", "POST", "order-svc", 8081, true, 50);
```

### 5. 限流器 (Rate Limiter)

4 种算法：
- **令牌桶**: 允许突发流量
- **漏桶**: 平滑输出流量
- **固定窗口**: 简单计数窗口
- **滑动窗口日志**: 精确滑动窗口

支持全局 + 每用户限流。

```c
RateLimiter *rl = rl_init(RL_TOKEN_BUCKET, 100.0, 200.0, 0);
bool ok = rl_allow(rl);
```

## 项目结构

```
mini-gateway-proxy/
├── include/
│   ├── reverse_proxy.h      # 反向代理头文件
│   ├── load_balancer.h      # 负载均衡头文件
│   ├── circuit_breaker.h    # 断路器头文件
│   ├── api_gateway.h        # API 网关头文件
│   └── rate_limiter.h       # 限流器头文件
├── src/
│   ├── reverse_proxy.c      # 反向代理实现
│   ├── load_balancer.c      # 负载均衡实现
│   ├── circuit_breaker.c    # 断路器实现
│   ├── api_gateway.c        # API 网关实现
│   └── rate_limiter.c       # 限流器实现
├── examples/
│   ├── load_balance_demo.c  # 负载均衡演示
│   ├── circuit_breaker_demo.c # 断路器演示
│   └── api_gateway_demo.c   # API 网关演示
├── demos/
│   ├── mini-load-balancer/
│   │   └── README.md        # 负载均衡算法详解
│   └── mini-circuit-breaker/
│       └── README.md        # 断路器模式详解
├── docs/
│   ├── course-alignment.md  # 课程对齐 (NGINX/Envoy/Hystrix)
│   └── gateway-patterns.md  # 网关模式 (BFF/Sidecar/Ingress)
├── Makefile
└── README.md
```

## 技术规范

- **语言**: C99
- **依赖**: libc + libm (标准库)
- **编译**: GCC, `-Wall -Wextra -O2`
- **平台**: Linux (Unix sockets, `select` 复用)
- **头文件保护**: `#ifndef X_H` / `#define X_H` / `#endif`
- **命名**: `snake_case` 函数, `PascalCase` 类型, `UPPER_SNAKE_CASE` 常量

## 参考资料

- [NGINX Admin Guide](https://docs.nginx.com/nginx/admin-guide/)
- [Envoy Proxy Documentation](https://www.envoyproxy.io/docs/)
- [Netflix Hystrix Wiki](https://github.com/Netflix/Hystrix/wiki)
- [Kong API Gateway](https://docs.konghq.com/)
- [Martin Fowler - Circuit Breaker](https://martinfowler.com/bliki/CircuitBreaker.html)
- [The System Design Primer](https://github.com/donnemartin/system-design-primer)

## License

教育用途，自由使用。
