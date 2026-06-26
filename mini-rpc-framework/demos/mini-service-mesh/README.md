# mini-service-mesh — Service Mesh 概念与实践

> 从 Sidecar Proxy 到控制平面：理解服务网格的核心理念

## 目录

1. [什么是 Service Mesh](#什么是-service-mesh)
2. [Sidecar 代理模式](#sidecar-代理模式)
3. [控制平面 vs 数据平面](#控制平面-vs-数据平面)
4. [mTLS 与安全通信](#mtls-与安全通信)
5. [可观测性：Metrics, Tracing, Logging](#可观测性)
6. [流量管理](#流量管理)
7. [主流 Service Mesh 对比](#主流-service-mesh-对比)
8. [Service Mesh 与 RPC 框架的关系](#service-mesh-与-rpc-框架的关系)

---

## 什么是 Service Mesh

**Service Mesh (服务网格)** 是一个专用的基础设施层，用于处理服务间通信。
它将通信逻辑从业务代码中剥离，下沉到 Sidecar 代理中，使开发者无需在应用代码
中实现重试、超时、监控、TLS 等网络功能。

### 核心问题

在微服务架构中，服务间通信面临以下挑战：

```
问题: 每个服务都需要处理:
  - 服务发现 (我的依赖在哪?)
  - 负载均衡 (选哪个实例?)
  - 重试与超时 (失败了怎么办?)
  - 断路保护 (服务宕机了如何降级?)
  - 加密通信 (如何保证安全?)
  - 可观测性 (如何监控调用链?)
```

### 解决方案：将通信逻辑外移

```
┌──────────────────────────────────────────────────────┐
│   Service A          Service B          Service C     │
│   ┌──────┐           ┌──────┐           ┌──────┐      │
│   │ App  │           │ App  │           │ App  │      │
│   └──┬───┘           └──┬───┘           └──┬───┘      │
│      │ (localhost)       │                  │          │
│   ┌──┴───────────────────┴──────────────────┴───┐      │
│   │              Service Mesh                    │      │
│   │  ┌──────────┐  ┌──────────┐  ┌──────────┐   │      │
│   │  │ Sidecar  │  │ Sidecar  │  │ Sidecar  │   │      │
│   │  │ Envoy    │◄─┤ Envoy    │◄─┤ Envoy    │   │      │
│   │  └──────────┘  └──────────┘  └──────────┘   │      │
│   └──────────────────────────────────────────────┘      │
│                         │                               │
│                 ┌───────┴────────┐                      │
│                 │ Control Plane  │                      │
│                 │   (Istio)      │                      │
│                 └────────────────┘                      │
└──────────────────────────────────────────────────────┘
```

---

## Sidecar 代理模式

### 经典部署模型

每个服务 Pod 包含两个容器：

```
┌─────────────────────┐
│    Kubernetes Pod    │
│  ┌───────────────┐   │
│  │ App Container │   │  ← 业务代码
│  │ (port 8080)   │   │
│  └───────┬───────┘   │
│          │ localhost  │
│  ┌───────┴───────┐   │
│  │ Sidecar Proxy │   │  ← Envoy / Linkerd-proxy
│  │ (port 15001)  │   │
│  └───────────────┘   │
└─────────────────────┘
```

### Sidecar 的职责

| 功能 | 描述 |
|------|------|
| 流量拦截 | 通过 iptables 规则透明接管所有进出流量 |
| 协议解析 | 理解 HTTP/1.1, HTTP/2, gRPC, TCP 等协议 |
| 负载均衡 | 根据策略选择目标实例 |
| 健康检查 | 主动/被动健康检查，异常检测 |
| 安全通信 | 自动 mTLS，证书管理 |
| 遥测收集 | 自动生成 metrics, traces, access logs |

### 无侵入性的关键

应用代码不需要任何修改：

```
// 应用代码: 仍然是普通 HTTP 调用
response, err := http.Get("http://service-b:8080/api/data")

// Sidecar 透明代理:
// 1. 拦截出站流量 (iptables → localhost:15001)
// 2. 查找 service-b 的目标 Pod IP
// 3. 建立 mTLS 连接
// 4. 添加 trace headers
// 5. 转发请求
// 6. 记录 metrics
// 7. 返回响应
```

---

## 控制平面 vs 数据平面

Service Mesh 分为两层：

### 数据平面 (Data Plane)

- **组成**: 所有 Sidecar 代理 (Envoy, Linkerd-proxy)
- **职责**: 处理每一个数据包的转发、路由、健康检查、负载均衡、认证、监控
- **特点**: 高性能、低延迟、无状态 (或软状态)

```
┌──────┐    ┌──────┐    ┌──────┐
│Envoy │◄──►│Envoy │◄──►│Envoy │   ← 数据平面
└──────┘    └──────┘    └──────┘
     ▲          ▲          ▲
     │ (xDS API: LDS, RDS, CDS, EDS)
     │
┌────┴─────────────────┐
│    Istiod (控制平面)   │
└──────────────────────┘
```

### 控制平面 (Control Plane)

- **组成**: Istiod (Istio), Linkerd Controller
- **职责**: 配置管理、服务发现、证书签发、策略分发
- **xDS 协议**: 动态服务发现 API

```
xDS API 类型:
  ├─ LDS (Listener Discovery Service):  监听器配置
  ├─ RDS (Route Discovery Service):     路由规则
  ├─ CDS (Cluster Discovery Service):   上游集群
  ├─ EDS (Endpoint Discovery Service):  集群成员
  └─ SDS (Secret Discovery Service):    证书/密钥
```

### 配置分发流程

```
开发者应用 VirtualService, DestinationRule
           │
           ▼
     [Istiod 控制平面]
           │
    ┌──────┼──────┐
    ▼      ▼      ▼
  Envoy  Envoy  Envoy      ← 通过 xDS 推送配置
    │      │      │
    ▼      ▼      ▼
  流量按照规则路由、分流、重试...
```

---

## mTLS 与安全通信

### 为什么需要 mTLS

在零信任网络中，不能假设内网是安全的。mTLS (Mutual TLS) 提供：

1. **身份认证**: 双方都用证书证明身份
2. **加密**: 传输层加密 (TLS)
3. **完整性**: 防止数据被篡改

### mTLS 握手过程

```
Client (App A + Sidecar)              Server (App B + Sidecar)
     │                                         │
     │─────── ClientHello ────────────────────►│
     │◄────── ServerHello + ServerCert ────────│
     │            + CertRequest                │
     │─────── ClientCert ─────────────────────►│
     │─────── Finished ───────────────────────►│
     │◄────── Finished ────────────────────────│
     │                                         │
     │◄═══════ Encrypted Channel ═════════════►│
```

### Istio 中的 mTLS

```yaml
apiVersion: security.istio.io/v1beta1
kind: PeerAuthentication
metadata:
  name: default
spec:
  mtls:
    mode: STRICT  # 强制所有服务间通信使用 mTLS
```

### SPIFFE 身份标识

Istio 使用 SPIFFE (Secure Production Identity Framework for Everyone) 格式：

```
spiffe://cluster.local/ns/default/sa/my-service

格式: spiffe://trust-domain/ns/namespace/sa/service-account
```

---

## 可观测性 (Observability)

Service Mesh 自动生成三大遥测信号：

### Metrics (指标)

自动收集的 RED (Rate, Error, Duration) 指标：

```
istio_requests_total{reporter="source", response_code="200"}  12345
istio_request_duration_milliseconds{quantile="0.99"}          42
istio_request_bytes_sum{reporter="destination"}               9876543
```

### 分布式追踪 (Distributed Tracing)

Sidecar 自动注入 trace headers，无需应用代码修改：

```
┌─────┐    ┌─────┐    ┌─────┐    ┌─────┐
│ SvcA│    │Envoy│    │Envoy│    │ SvcB│
└──┬──┘    └──┬──┘    └──┬──┘    └──┬──┘
   │ Span A   │          │          │
   │─────────►│ Span A   │          │
   │          │─────────►│ Span B   │
   │          │          │─────────►│
   │          │          │◄─────────│
   │          │◄─────────│          │
   │◄─────────│          │          │

Headers automatically propagated:
  x-request-id
  x-b3-traceid      (Zipkin)
  x-b3-spanid
  x-b3-parentspanid
  traceparent        (W3C Trace Context)
```

### 访问日志 (Access Logging)

```
[2024-01-15T10:30:00.000Z] "GET /api/users HTTP/1.1" 200
  upstream_cluster=user-service
  duration=23ms
  bytes_received=1024
  bytes_sent=512
  response_flags=-
```

---

## 流量管理

### 请求路由 (Request Routing)

```yaml
apiVersion: networking.istio.io/v1beta1
kind: VirtualService
metadata:
  name: reviews-route
spec:
  hosts:
  - reviews
  http:
  - match:
    - headers:
        end-user:
          exact: jason
    route:
    - destination:
        host: reviews
        subset: v2     # Jason 看到 v2 版本
  - route:
    - destination:
        host: reviews
        subset: v1     # 其他用户看到 v1 版本
    weight: 100
```

### 高级流量管理模式

#### 金丝雀发布 (Canary Deployment)

```
Service v1 (90%) ────► 生产流量
Service v2 (10%) ────► 灰度流量

通过 DestinationRule 定义子集，VirtualService 控制权重
```

#### 蓝绿部署 (Blue-Green)

```
Blue  (v1, 100%)  →  Green (v2, 0%)
Blue  (v1, 0%)    →  Green (v2, 100%)

一键切换所有流量到新版本
```

#### 故障注入 (Fault Injection)

```yaml
fault:
  delay:
    percentage:
      value: 10.0
    fixedDelay: 5s       # 10% 请求延迟 5 秒
  abort:
    percentage:
      value: 20.0
    httpStatus: 500       # 20% 请求返回 500
```

#### 熔断 (Circuit Breaking)

```yaml
trafficPolicy:
  connectionPool:
    tcp:
      maxConnections: 100
    http:
      http1MaxPendingRequests: 100
      maxRequestsPerConnection: 10
  outlierDetection:
    consecutiveErrors: 5       # 连续 5 次错误
    interval: 30s              # 检测间隔
    baseEjectionTime: 30s      # 驱逐 30 秒
    maxEjectionPercent: 50     # 最多驱逐 50%
```

---

## 主流 Service Mesh 对比

| 特性 | Istio | Linkerd | Consul Connect | Kuma |
|------|-------|---------|---------------|------|
| 代理 | Envoy (C++) | linkerd2-proxy (Rust) | Envoy / Built-in | Envoy |
| 性能开销 | 中 (~3ms p99) | 低 (~0.5ms p99) | 中 | 中 |
| 资源消耗 | 高 (100MB+) | 低 (10MB) | 中 | 中 |
| 复杂度 | 高 | 低 | 中 | 低 |
| mTLS | 自动 | 自动 | 自动 | 自动 |
| 多集群 | 完善 | 有限 | 完善 | 完善 |
| 社区 | CNCF Graduated | CNCF Graduated | HashiCorp | CNCF Sandbox |
| 适合场景 | 大型企业 | 简单场景 | Consul 用户 | 通用 |

### 选型建议

```
需要完整功能 (授权策略、多集群、混合部署)?
  └─ Istio (功能最全，但运维成本高)

追求简单、低资源?
  └─ Linkerd (开箱即用，运维简单)

已有 Consul 基础设施?
  └─ Consul Connect (无缝集成)

需要多 Mesh 支持 (Kubernetes + VM + 裸金属)?
  └─ Kuma (统一控制面)
```

---

## Service Mesh 与 RPC 框架的关系

### 功能重叠与互补

```
┌──────────────────────────────────────────────────────────┐
│                      关注点分离                            │
├───────────────────────────┬──────────────────────────────┤
│     RPC Framework          │      Service Mesh            │
│   (gRPC, Dubbo, Thrift)   │   (Istio, Linkerd)           │
├───────────────────────────┼──────────────────────────────┤
│ ✓ 序列化 / 反序列化        │ ✓ 流量管理 (路由、灰度)       │
│ ✓ IDL 定义 + 代码生成      │ ✓ mTLS 自动加密               │
│ ✓ Stub / Proxy 生成        │ ✓ 可观测性 (Metrics/Tracing)  │
│ ✓ 异步调用 / Streaming     │ ✓ 熔断 / 限流 / 重试          │
│ ✓ 多语言支持              │ ✓ 故障注入 / 混沌工程          │
│                           │ ✓ 访问控制 / 授权策略          │
├───────────────────────────┼──────────────────────────────┤
│            协同工作: RPC 专注应用协议, Mesh 专注网络传输    │
└───────────────────────────┴──────────────────────────────┘
```

### 最佳实践：gRPC + Istio

```
应用层: gRPC (Proto 定义、代码生成、业务逻辑)
   │
   ▼
代理层: Envoy (mTLS, 路由, 遥测)
   │
   ▼
传输层: TCP / HTTP/2
```

**职责清晰划分：**

- **gRPC 处理**: 方法定义、参数序列化、流式传输、错误码
- **Istio 处理**: TLS、流量路由、熔断、限流、遥测、认证授权

这种组合避免了业务代码中包含基础设施逻辑，实现了真正的关注点分离。

### 演进路径

```
Phase 1: 单体应用
  └─ 进程内函数调用

Phase 2: 微服务 + 库内嵌 RPC
  └─ gRPC/Thrift SDK 嵌入应用
  └─ 问题: SDK 升级需要重新部署所有服务

Phase 3: 微服务 + Service Mesh
  └─ RPC SDK 移入 Sidecar
  └─ 基础设施变更无需修改应用代码
  └─ 问题: 引入额外延迟和运维复杂度

Phase 4: Proxyless gRPC (xDS)
  └─ gRPC 直接集成 xDS 协议
  └─ 消去 Sidecar 代理
  └─ 仍有控制平面统一管理
```

## 参考资料

- [Istio Documentation](https://istio.io/latest/docs/)
- [Linkerd Architecture](https://linkerd.io/2.15/reference/architecture/)
- [The Service Mesh: Past, Present, and Future](https://www.infoq.com/articles/service-mesh-ultimate-guide/)
- [SPIFFE Specification](https://spiffe.io/specs/)
- [Envoy Proxy Documentation](https://www.envoyproxy.io/docs)
