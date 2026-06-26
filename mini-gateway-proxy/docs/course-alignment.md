# 课程对齐 (Course Alignment)

> 将本实现映射到工业级反向代理、API 网关和服务网格的文档和规范

## NGINX 对齐

| mini-gateway-proxy 模块 | NGINX 对应功能 | NGINX 文档参考 |
|------------------------|---------------|---------------|
| `reverse_proxy.h/c` | `ngx_http_proxy_module` | https://nginx.org/en/docs/http/ngx_http_proxy_module.html |
| `load_balancer.h/c` | `ngx_http_upstream_module` (加权轮询, 最少连接, 一致性哈希) | https://nginx.org/en/docs/http/ngx_http_upstream_module.html |
| `circuit_breaker.h/c` | NGINX Plus 健康检查 (主动/被动) | https://docs.nginx.com/nginx/admin-guide/load-balancer/http-health-check/ |
| `api_gateway.h/c` | NGINX `location` 路由 + `map` 模块 | https://nginx.org/en/docs/http/ngx_http_core_module.html#location |
| `rate_limiter.h/c` | `ngx_http_limit_req_module` (漏斗算法) | https://nginx.org/en/docs/http/ngx_http_limit_req_module.html |

### 关键对应关系

**反向代理核心流程**:
```
NGINX:  client → listen → server_name match → location match
        → proxy_pass → upstream → backend response → return to client

mini:   client → proxy_init(port) → proxy_match_rule(path)
        → proxy_connect_backend → proxy_rewrite_headers
        → proxy_pipe_data → proxy_close_connection
```

**负载均衡算法映射**:

| Nginx upstream 指令 | mini-gateway 枚举 | 算法 |
|---------------------|-------------------|------|
| (default) | `LB_ROUND_ROBIN` | 轮询 |
| `weight=N` | `LB_WEIGHTED_ROUND_ROBIN` | 平滑加权轮询 (SWRR) |
| `least_conn` | `LB_LEAST_CONNECTIONS` | 最少连接数 |
| `hash $remote_addr` | `LB_CONSISTENT_HASH` | 一致性哈希 |
| `random` | `LB_RANDOM` | 随机 |

**请求头改写对比**:

| NGINX 指令 | mini-gateway | 功能 |
|-----------|--------------|------|
| `proxy_set_header Host $host` | `proxy_rewrite_headers()` | 设置 Host 头 |
| `proxy_set_header X-Forwarded-For $remote_addr` | `proxy_rewrite_headers()` | 设置 XFF 头 |
| `proxy_set_header X-Real-IP $remote_addr` | 客户端 IP 嵌入连接结构 | 传递真实 IP |

## Envoy Proxy 对齐

| mini-gateway-proxy 模块 | Envoy 对应概念 | Envoy 文档参考 |
|------------------------|---------------|---------------|
| `reverse_proxy.h` | `HTTP connection manager` + `router` filter | https://www.envoyproxy.io/docs/envoy/latest/intro/arch_overview/http/http_connection_management |
| `load_balancer.h` | `load balancing policies` (RoundRobin, LeastRequest, RingHash, Maglev) | https://www.envoyproxy.io/docs/envoy/latest/intro/arch_overview/upstream/load_balancing/overview |
| `circuit_breaker.h` | `circuit breakers` (max_connections, max_pending_requests, max_requests, max_retries) | https://www.envoyproxy.io/docs/envoy/latest/intro/arch_overview/upstream/circuit_breaking |
| `api_gateway.h` | `route configuration` + `HTTP filters` | https://www.envoyproxy.io/docs/envoy/latest/api-v3/config/route/v3/route.proto |
| `rate_limiter.h` | `rate limit filter` (token bucket, descriptor) | https://www.envoyproxy.io/docs/envoy/latest/configuration/http/http_filters/rate_limit_filter |

### Envoy 架构对齐

```
Envoy:   Listener → Filter Chain → HTTP Connection Manager
         → Route Table → Cluster → Load Balancer → Endpoint

mini:    ProxyServer.listen_fd → proxy_handle_request
         → proxy_match_rule (路由匹配) → proxy_connect_backend
         → 负载均衡器选择 → 数据管道传输
```

**Envoy 断路器四个维度** vs **mini-gateway**:

| Envoy 维度 | 含义 | mini-gateway 对应 |
|-----------|------|-------------------|
| `max_connections` | 最大并发连接数 | `failure_threshold` 驱动的失败计数 |
| `max_pending_requests` | 最大等待请求数 | 半开状态的 `half_open_max_requests` |
| `max_requests` | 每个连接的最大请求数 | N/A (简化实现) |
| `max_retries` | 最大重试次数 | N/A (简化实现) |

## Netflix OSS 对齐

| mini-gateway-proxy 模块 | Netflix OSS 对应 | 说明 |
|------------------------|-----------------|------|
| `circuit_breaker.h` | **Hystrix** | 断路器模式原始实现，状态机、熔断、半开探测 |
| `api_gateway.h` | **Zuul** | API 网关，路由、过滤器链、插件 |
| `load_balancer.h` | **Ribbon** | 客户端负载均衡，多种算法 |
| `rate_limiter.h` | **Concurrency Limits** | 限制并发（Netflix 后来的替代方案） |

### Hystrix 断路器对齐详解

| Hystrix 概念 | mini-gateway 实现 | 参数对应 |
|-------------|------------------|---------|
| `circuitBreaker.requestVolumeThreshold` | `failure_threshold` | 触发熔断的最小请求量 |
| `circuitBreaker.errorThresholdPercentage` | 简化: 连续失败计数而非百分比 | 错误率阈值 |
| `circuitBreaker.sleepWindowInMilliseconds` | `timeout_ms` | OPEN 到 HALF_OPEN 的等待时间 |
| `metrics.rollingStats.timeInMilliseconds` | N/A | 滑动窗口统计 (简化实现使用计数器) |
| `execution.isolation.strategy` | N/A (未实现线程隔离) | THREAD vs SEMAPHORE 隔离 |

### Zuul 过滤器链 vs mini-gateway 插件链

| Zuul 过滤器类型 | mini-gateway 插件 | 执行阶段 |
|----------------|-------------------|---------|
| `pre` filters | AUTH, RATE_LIMIT, CORS | 请求路由前 |
| `route` filters | 模仿: 路由匹配 + 上游转发 | 路由阶段 |
| `post` filters | LOGGING, TRANSFORM | 响应处理 |
| `error` filters | Circuit Breaker 错误处理 | 错误处理 |

## Kong API Gateway 对齐

| mini-gateway-proxy 模块 | Kong 插件对应 |
|------------------------|--------------|
| `api_gateway.h` (auth) | `key-auth`, `jwt`, `oauth2`, `basic-auth` |
| `rate_limiter.h` | `rate-limiting`, `rate-limiting-advanced` |
| `api_gateway.h` (logging) | `file-log`, `http-log`, `tcp-log` |
| `api_gateway.h` (cors) | `cors` |
| `api_gateway.h` (transform) | `request-transformer`, `response-transformer` |
| `circuit_breaker.h` | 无原生支持 (第三方插件) |

## 服务网格 (Service Mesh) 对齐

| mini-gateway 组件 | Istio 对应 | Linkerd 对应 |
|------------------|-----------|-------------|
| 路由匹配 | VirtualService | HTTPRoute |
| 负载均衡 | DestinationRule | 自动负载均衡 |
| 断路器 | DestinationRule.trafficPolicy.connectionPool | 自动断路 |
| 限流 | EnvoyFilter (rate limit) | 速率限制 Annotation |
| 健康检查 | DestinationRule.trafficPolicy.outlierDetection | 自动健康检测 |

## 实施差异 (已知简化)

| 方面 | 工业实现 | mini-gateway 简化 |
|------|---------|-------------------|
| 网络层 | epoll/kqueue 事件驱动 | select 复用 (兼容性好) |
| HTTP 解析 | 完整 HTTP/1.1/2/3 解析器 | 简化字符串匹配 |
| SSL/TLS | OpenSSL/BoringSSL | 未实现 (纯 HTTP) |
| 配置方式 | 配置文件 (JSON/YAML)/Admin API | 代码内配置 |
| 性能 | 零拷贝/sendfile/内存池 | 直接 read/write 循环 |
| 滑动窗口 | 精确的时间窗口统计 | 简化计数器 |
| 断路器指标 | 滑动窗口百分比 + 调用量阈值 | 连续失败计数 |
| 插件扩展 | 动态加载 (Lua/WebAssembly) | 编译时固定枚举 |

## 参考资料

- NGINX Admin Guide: https://docs.nginx.com/nginx/admin-guide/
- Envoy Proxy Docs: https://www.envoyproxy.io/docs/
- Netflix Hystrix Wiki: https://github.com/Netflix/Hystrix/wiki
- Spring Cloud Gateway: https://spring.io/projects/spring-cloud-gateway
- Kong Gateway: https://docs.konghq.com/gateway/latest/
- Istio: https://istio.io/latest/docs/
- Linkerd: https://linkerd.io/2.16/reference/
