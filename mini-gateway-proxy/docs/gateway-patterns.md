# 网关模式 (Gateway Patterns)

> 涵盖 API 网关、BFF、服务网格 Sidecar、Ingress Controller、限流策略等核心模式

## 1. API 网关模式 (API Gateway Pattern)

### 概述

API 网关是微服务架构的单一入口点，负责请求路由、协议转换、横切关注点（认证、限流、日志）处理。

### 架构图

```
                     ┌─────────────┐
   Mobile App ──────►│             │─────► User Service
   Web App ─────────►│ API Gateway │─────► Order Service
   Third Party ─────►│             │─────► Product Service
                     │             │─────► Payment Service
                     └─────────────┘──────► Notification Service
```

### 核心职责

| 职责 | 说明 | 本实现 |
|------|------|--------|
| **请求路由** | 根据路径、方法路由到对应上游 | `gateway_match_route()` |
| **认证授权** | 验证 JWT/OAuth/API Key | `gateway_plugin_auth()` |
| **流量控制** | 限流、熔断、重试 | `rate_limiter.h`, `circuit_breaker.h` |
| **协议转换** | HTTP/gRPC/WebSocket 桥接 | 示意图: HTTP 转发 |
| **API 组合** | 将多个后端响应组合为一个 | 未实现 |
| **日志监控** | 集中式日志和指标 | `gateway_plugin_logging()` |
| **CORS** | 跨域资源共享 | `gateway_plugin_cors()` |
| **缓存** | 响应缓存减少后端负载 | `gateway_plugin_cache()` |

### 插件链执行顺序

```
┌──────────────────────────────────────────────┐
│            API Gateway Plugin Chain          │
│                                              │
│  Request ──► AUTH ──► RATE_LIMIT ──► CORS───┤
│                                              │
│              LOGGING ◄── TRANSFORM ◄── CACHE │
│                                              │
│  ──► match_route() ──► upstream forwarding   │
│                                              │
│  Response ◄── upstream response ◄────────────│
└──────────────────────────────────────────────┘
```

### 优点与缺点

**优点**:
- 单一入口，简化客户端
- 统一横切关注点处理
- 可以独立于后端服务扩展
- 隐藏内部服务架构变化

**缺点**:
- 单点瓶颈（需集群部署）
- 可能的单点故障（需高可用）
- 增加网络跳数（增加延迟）
- 成为变更的热点

## 2. BFF 模式 (Backend For Frontend)

### 概述

为每种前端类型（Web、Mobile、IoT）创建专门的 BFF 网关，各自优化其 API 接口。

### 架构图

```
  Mobile App ──► Mobile BFF ──┬── User Service
                              ├── Order Service
  Web App ────► Web BFF ──────┤
                              ├── Product Service
  IoT Device ─► IoT BFF ──────┘
                              ╰── Analytics Service
```

### BFF vs API 网关

| 维度 | API 网关 | BFF |
|------|---------|-----|
| 服务对象 | 所有客户端 | 特定客户端类型 |
| API 面 | 通用 REST/gRPC | 针对特定 UI 优化 |
| 数据聚合 | 基础组合 | 深度裁剪组合 |
| 版本管理 | 集中管理 | 各端独立发布 |
| 规模 | 1 个或极少数 | 每类客户端 1 个 |

## 3. 服务网格 Sidecar 模式 (Service Mesh Sidecar)

### 概述

将网络功能从业务应用中剥离到 Sidecar 代理中，每个服务实例伴随一个 Sidecar。

### 架构图

```
  ┌──────────┐        ┌──────────┐
  │ Service A│        │ Service B│
  │  ┌──────┐│        │  ┌──────┐│
  │  │Sidecar││◄──────►│  │Sidecar││
  │  └──────┘│        │  └──────┘│
  └──────────┘        └──────────┘
       ▲                    ▲
       │    Control Plane   │
       └────────────────────┘
```

### Sidecar 职责

| 功能 | Service Mesh Sidecar | API 网关 |
|------|---------------------|---------|
| 服务发现 | 动态服务发现与注册 | DNS/注册表 |
| 负载均衡 | 客户端负载均衡 | 服务端 LB |
| 断路器 | 实例级断路器 | 服务级断路器 |
| mTLS | 自动 mTLS 加密 | TLS 终端 |
| 流量分割 | 金丝雀发布/蓝绿部署 | URL 路由 |
| 可观测性 | 分布式追踪、指标收集 | 请求日志 |

### 实现对应

| Istio/Linkerd 组件 | mini-gateway 近似对应 |
|-------------------|----------------------|
| Envoy Sidecar | `reverse_proxy.c` (客户端-后端管道) |
| Pilot/Istiod | 编译时路由表 (`APIRoute` 数组) |
| Mixer/Telemetry | `gateway_plugin_logging()` |
| Citadel | `gateway_plugin_auth()` |
| VirtualService | `gateway_match_route()` |

## 4. Ingress Controller 模式

### 概述

Kubernetes Ingress Controller 将外部流量路由到集群内部的服务。

### 架构图

```
  Internet
     │
     ▼
┌─────────────┐
│ LoadBalancer │  (云提供商 LB)
└──────┬──────┘
       │
       ▼
┌─────────────────────────────────────────┐
│              Ingress Controller          │
│  ┌────────────────────────────────────┐  │
│  │  /api/*     → api-service:8080     │  │
│  │  /web/*     → web-service:3000     │  │
│  │  /admin/*   → admin-service:9090   │  │
│  └────────────────────────────────────┘  │
└──────────────────┬──────────────────────┘
                   │
     ┌─────────────┼─────────────┐
     ▼             ▼             ▼
┌─────────┐  ┌─────────┐  ┌─────────┐
│api-pod  │  │web-pod  │  │admin-pod│
│ 8080    │  │ 3000    │  │ 9090    │
└─────────┘  └─────────┘  └─────────┘
```

### 本实现的路径路由对应

```c
/* mini-gateway 等价于 */
gateway_register_route(gw, "/api/*",   "ANY", "api-service", 8080, true, 200);
gateway_register_route(gw, "/web/*",   "ANY", "web-service", 3000, false, 0);
gateway_register_route(gw, "/admin/*", "ANY", "admin-svc", 9090, true, 50);
```

## 5. 限流策略 (Rate Limiting Strategies)

### 算法对比

| 算法 | 适用场景 | 优点 | 缺点 |
|------|---------|------|------|
| **令牌桶** | API 保护、允许突发 | 灵活、突发容忍 | 实现略复杂 |
| **漏桶** | 流量整形、平滑输出 | 固定输出速率 | 不允许突发 |
| **固定窗口** | 简单限制 | 实现简单 | 边界突发问题 |
| **滑动窗口日志** | 精确控制 | 边界精确 | 内存消耗大 |
| **滑动窗口计数器** | 平衡精确与内存 | 好折中 | 近似而非精确 |

### 多维度限流

```
全局限流:    所有 IP 共享 1000 rps
  └─ 用户限流:    每用户 100 rps
      └─ 端点限流:   /users POST: 50 rps
```

### 限流响应头

```
HTTP/1.1 429 Too Many Requests
X-RateLimit-Limit: 100
X-RateLimit-Remaining: 0
X-RateLimit-Reset: 1713600000
Retry-After: 30
```

### 分布式限流考虑

| 方案 | 方式 | 优点 | 缺点 |
|------|------|------|------|
| 本地限流 | 单实例计数 | 无网络开销、低延迟 | 不精确（多实例） |
| 集中式限流 | Redis + Lua 脚本 | 全局一致性 | 单点/RTT 开销 |
| 协调式限流 | Gossip 协议同步 | 最终一致性 | 复杂度高 |

## 6. 模式对比总结

| 模式 | 位置 | 关注点 | 典型实现 |
|------|------|--------|---------|
| API 网关 | 边界入口 | 北向流量（外部→内部） | Kong, APISIX, Spring Cloud Gateway |
| BFF | 边界入口 | 为前端定制的 API | 自定义 Express/Spring 服务 |
| Sidecar | 每服务副本 | 东向西向流量 | Envoy, Linkerd-proxy |
| Ingress | K8s 集群边界 | 外部→K8s Service | NGINX Ingress, Traefik, Contour |
| 限流器 | 任意层级 | 流量控制 | Guava RateLimiter, Sentinel |

## 7. 本实现与模式的对齐

| mini-gateway-proxy 组件 | 对应模式 | 实现程度 |
|------------------------|---------|---------|
| `reverse_proxy.h/c` | Sidecar 代理核心 | 基础 HTTP 转发管道 |
| `load_balancer.h/c` | 所有模式的基础 | 5 种算法 |
| `circuit_breaker.h/c` | Sidecar 断路器 | 三态状态机 |
| `api_gateway.h/c` | API 网关 + Ingress | 路由 + 插件链 |
| `rate_limiter.h/c` | 限流器 | 4 种算法 |

## 参考资料

- Building Microservices (Sam Newman): BFF 和 API 网关模式
- Microservices Patterns (Chris Richardson): API Gateway Pattern, Chapter 8
- NGINX Microservices Reference Architecture: https://www.nginx.com/microservices-reference-architecture/
- Kong Gateway Patterns: https://konghq.com/learning-center
- Kubernetes Ingress: https://kubernetes.io/docs/concepts/services-networking/ingress/
- Istio Architecture: https://istio.io/latest/docs/ops/deployment/architecture/
- Rate Limiting Strategies: Cloudflare Blog - How we built rate limiting
