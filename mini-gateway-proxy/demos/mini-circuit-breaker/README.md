# 断路器模式 (Circuit Breaker Pattern)

> 参考 Netflix Hystrix、Resilience4j、Envoy Circuit Breaking、AWS Well-Architected 可靠性支柱、Martin Fowler 断路器文章

## 概述

断路器模式是微服务架构中的核心弹性模式，用于防止级联故障（Cascading Failure）。当一个下游服务开始失败时，断路器会"跳闸"（trip），快速拒绝后续请求，避免资源耗尽和故障传播。

## 核心概念

### 为什么需要断路器

在微服务架构中，一个请求可能经过多个服务：
```
Client → API Gateway → Auth Service → User Service → Database
```

如果 Database 变慢（每请求耗时从 10ms 增加到 12 秒），会导致：
1. **线程池耗尽**: API Gateway 的请求线程被阻塞等待 Database 响应
2. **连接池耗尽**: 数据库连接被长时间占用
3. **级联失败**: Auth Service 和 User Service 也因等待而耗尽资源
4. **雪崩效应**: 整个系统不可用，即使只有 Database 出问题

断路器的核心价值：**早失败比晚失败好**（Fail Fast > Fail Slow）。

### 三种状态

```
         ┌──────────────┐
         │              │
    ┌───►│   CLOSED     │◄──── success threshold met
    │    │ (正常)        │────┐
    │    │              │    │
    │    └──────────────┘    │
    │           │            │
    │    failures >=         │
    │    threshold           │
    │           │            │
    │           ▼            │
    │    ┌──────────────┐    │
    │    │              │    │
    │    │    OPEN      │    │
    │    │ (快速失败)    │────┤
    │    │              │    │
    │    └──────────────┘    │
    │           │            │
    │    timeout expired     │
    │           │            │
    │           ▼            │
    │    ┌──────────────┐    │
    │    │              │    │
    └────│  HALF_OPEN   │────┘
         │ (探测)        │  probe fails →
         │              │  back to OPEN
         └──────────────┘
```

#### CLOSED (闭合/正常)
- 所有请求正常通过
- 统计失败计数
- 当 `failure_count >= failure_threshold` 时，跳转到 OPEN

#### OPEN (断开/熔断)
- 所有请求立即快速失败（Fast-Fail），不调用下游服务
- 启动超时计时器
- 当 `elapsed_time >= timeout_ms` 时，跳转到 HALF_OPEN
- 快速失败的响应通常是 503 Service Unavailable 或降级响应

#### HALF_OPEN (半开/探测)
- 允许有限数量的请求通过（探针请求）
- 如果探针成功 → 跳转到 CLOSED（恢复）
- 如果探针失败 → 跳转到 OPEN（重新熔断）

## 配置参数

| 参数 | 说明 | 典型值 |
|------|------|--------|
| `failure_threshold` | CLOSED 状态下连续失败多少次后跳闸 | 5 |
| `success_threshold` | HALF_OPEN 状态下连续成功多少次后恢复 | 3 |
| `timeout_ms` | OPEN 状态下等待多久后尝试 HALF_OPEN | 10000-60000ms |
| `half_open_max_requests` | HALF_OPEN 状态下允许的最大并行请求 | success_threshold |

### 参数调优指南

| 场景 | failure_threshold | timeout_ms | 说明 |
|------|-------------------|------------|------|
| 关键服务 | 2-3 | 5000-10000 | 快速检测故障 |
| 非关键服务 | 5-10 | 30000-60000 | 允许更多重试 |
| 批量处理 | 3-5 | 60000-120000 | 长超时，避免假阳性 |
| 实时 API | 2-3 | 3000-5000 | 快速恢复 |

## 级联故障预防

### 故障传播链

```
Database 变慢
    ↓
User Service 等待 Database (线程阻塞)
    ↓
Auth Service 等待 User Service (线程阻塞)
    ↓
API Gateway 等待 Auth Service (连接池耗尽)
    ↓
客户端请求全部超时 → 整个系统不可用
```

### 断路器切断传播链

```
Database 变慢
    ↓
User Service 断路器跳闸 → 快速失败，不阻塞线程
    ↓
Auth Service 收到熔断错误 → 降级响应或缓存数据
    ↓
API Gateway 正常处理 Auth Service 的降级响应
    ↓
客户端仍有部分功能可用 → 单点故障被隔离
```

### 多断路器策略

对不同的依赖服务使用独立的断路器：

```
┌─────────────────────────────────┐
│          API Gateway            │
│  ┌─────────┐  ┌──────────────┐  │
│  │ CB(D)   │  │ CB(cache)    │  │
│  │ 5/10s   │  │ 2/3s         │  │
│  └────┬────┘  └──────┬───────┘  │
│       │              │          │
│  ┌──────────┐  ┌──────────┐     │
│  │ CB(auth) │  │ CB(user) │     │
│  │ 3/5s     │  │ 5/15s    │     │
│  └────┬─────┘  └────┬─────┘     │
└───────┼──────────────┼───────────┘
        │              │
   Auth Service   User Service
```

- 每个服务独立的阈值和超时
- 单独故障不牵连其他服务
- 精细化的降级策略

## 隔舱模式 (Bulkhead Pattern)

断路器的伴生模式，通过隔离资源防止单点故障扩散：

| 隔舱策略 | 实现方式 | 效果 |
|---------|---------|------|
| 线程池隔离 | 每个下游服务独立线程池 | 慢服务不占用其他服务的线程 |
| 信号量隔离 | 限制每个服务的同时调用数 | Hystrix 推荐，轻量级 |
| 连接池隔离 | 每个下游独立连接池 | 避免连接争用 |

### 与断路器模式的关系

```
隔舱 (Bulkhead)     →  限制资源使用，阻止故障扩散
断路器 (Circuit)    →  检测故障并快速拒绝，停止加重已故障的服务
```

两者互补：隔舱提供**隔离**，断路器提供**检测和恢复**。

## 降级策略 (Fallback)

当断路器打开时，不应返回原始错误，而应返回降级响应：

| 降级类型 | 示例 | 适用场景 |
|---------|------|---------|
| 缓存回退 | 返回旧版缓存数据 | 读操作 |
| 默认值 | 返回空列表 [] | 列表查询 |
| 静默失败 | 记录日志，不阻塞主流程 | 非关键功能 |
| 降级功能 | 简化版功能 | 部分能力保留 |

## 时间线演示

```
t=0s    CLOSED: 正常请求
        请求1: 成功
        请求2: 失败 (failure=1/5)
        请求3: 失败 (failure=2/5)
        请求4: 成功 (failure=0, reset)
        ...
        请求N: 失败 (failure=5/5) → OPEN

t=0s    OPEN: 超时倒计时 10s
        请求: Fast-Fail (不调用后端)
        请求: Fast-Fail
        ...

t=10s   HALF_OPEN: 发送探针请求
        探针1: 成功 (1/3)
        探针2: 成功 (2/3)
        探针3: 成功 (3/3) → CLOSED

t=10s   如果探针失败:
        探针1: 失败 → OPEN (重新计时 10s)
```

## 实现注意

### 滑动窗口 vs 固定窗口

本实现使用简单计数器，生产环境建议使用滑动窗口：

```
简单计数器:  连续 5 次失败 → OPEN
滑动窗口:    最近 10s 内 50% 失败率 → OPEN
```

滑动窗口更精确，避免偶尔的失败尖峰触发误熔断。

### 半开状态的并发控制

- 本实现限制半开状态的最大并行请求数 (`half_open_max_requests`)
- 防止大量请求同时涌入刚恢复的服务导致再次熔断
- 典型的 `half_open_max_requests = success_threshold`

### 基于时间的半开触发

- 使用 `CLOCK_MONOTONIC` 而非 `CLOCK_REALTIME`，避免系统时间调整影响
- 超时判断使用单调递增的毫秒计数器

## C 语言实现要点

- 状态机使用 `CBCircuitState` 枚举：CLOSED / OPEN / HALF_OPEN
- `cb_call` 接受函数指针 `CBCallFunc`，在 CLOSED 和 HALF_OPEN 时执行
- 在 OPEN 状态时直接返回 -1，不调用函数指针
- 使用 `struct timespec` + `clock_gettime(CLOCK_MONOTONIC)` 进行精确计时
- 统计信息包含 total_successes, total_failures, total_rejected

## 参考资料

- Netflix Hystrix: https://github.com/Netflix/Hystrix/wiki
- Martin Fowler - Circuit Breaker: https://martinfowler.com/bliki/CircuitBreaker.html
- Resilience4j: https://resilience4j.readme.io/docs/circuitbreaker
- Envoy Circuit Breaking: https://www.envoyproxy.io/docs/envoy/latest/intro/arch_overview/upstream/circuit_breaking
- Release It! (Michael Nygard): 断路器模式的原始来源
- AWS Well-Architected - 可靠性支柱
- Google SRE Book: Chapter 22 - Addressing Cascading Failures
