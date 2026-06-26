# 负载均衡算法 (Load Balancing Algorithms)

> 参考 NGINX upstream 模块、Envoy 负载均衡策略、HAProxy 算法实现、Google Maglev 一致性哈希

## 概述

负载均衡器将传入的网络流量分发到多个后端服务器上，以提高可用性、可靠性和吞吐量。本模块实现了五种核心负载均衡算法，覆盖了从简单到复杂的工业级策略。

## 算法详解

### 1. 轮询 (Round-Robin)

最简单的负载均衡策略，按顺序依次将请求分配给每台服务器。

```
请求序列: 1→A, 2→B, 3→C, 4→D, 5→E, 6→A, 7→B, ...
```

**优点**:
- 实现简单，无状态
- 在服务器性能均等的场景下效果良好
- 无额外内存开销

**缺点**:
- 不考虑服务器权重（需使用加权轮询变体）
- 不考虑服务器当前负载
- 不考虑请求亲和性

**使用场景**: 同构服务器集群，所有服务器配置相同。

### 2. 平滑加权轮询 (Smooth Weighted Round-Robin / SWRR)

NGINX 使用的 SWRR 算法。Nginx 的 SWRR 算法通过 `current_weight` 和 `effective_weight` 来保证平滑的加权分布。

**算法原理**:
```
1. 初始化: current_weight[i] = 0
2. 每次选择:
   - current_weight[i] += effective_weight[i]
   - 选择 current_weight 最大的服务器
   - 被选中的服务器: current_weight[i] -= total_weight
```

**示例 (权重 5:1:1:3:2)**:
```
Round  1: A(5) B(1) C(1) D(3) E(2) → 选 A, A-12→-7
Round  2: A(-2) B(2) C(2) D(6) E(4) → 选 D, D-12→-6
Round  3: A(3) B(3) C(3) D(-3) E(6) → 选 E, E-12→-6
Round  4: A(8) B(4) C(4) D(0) E(-4) → 选 A, A-12→-4
Round  5: A(1) B(5) C(5) D(3) E(-2) → 选 C, C-12→-7
Round  6: A(6) B(6) C(-6) D(6) E(0) → 选 A...
```

**特点**:
- 平滑分布，避免权重大的服务器连续被选中
- 每次选择复杂度 O(N)
- 权重可动态调整

**使用场景**: 异构服务器集群，不同服务器处理能力不同。

### 3. 最少连接数 (Least Connections)

总是将请求转发给当前活跃连接数最少的服务器。

```
算法: min(active_connections[i]) for all healthy servers
```

**特点**:
- 动态负载均衡，适应请求处理时间差异大的场景
- 需要维护每台服务器的连接计数器
- 长连接场景优于轮询

**实现细节**:
- 新请求到达时 `active_connections++`
- 请求完成时 `active_connections--`
- 只从健康服务器中选择

**使用场景**: 请求处理时间差异大的场景（如视频转码 vs 图片压缩），WebSocket 长连接。

### 4. 一致性哈希 (Consistent Hash)

使用一致性哈希环将请求映射到服务器，特别适合缓存系统和有状态服务。

**虚拟节点**:
- 每台物理服务器在哈希环上拥有 150 个虚拟节点
- 虚拟节点均匀分布在整个哈希空间
- 添加/移除服务器时，仅有 1/N 的键需要重新映射

**哈希函数**: FNV-1a (Fowler-Noll-Vo)，具有良好分布性和速度。

**哈希环查找**:
```
1. 计算请求 key 的 hash 值
2. 在环上二分查找第一个 hash >= key_hash 的虚拟节点
3. 如果 key_hash 大于环上最大 hash，回绕到第一个节点
4. 返回该虚拟节点对应的物理服务器索引
```

**服务器变更影响分析**:

| 场景 | 影响比例 | 说明 |
|------|---------|------|
| 添加 1 台服务器到 N 台集群 | 1/(N+1) 的键重新映射 | 仅新增服务器的 vnodes 区间内的键迁移 |
| 移除 1 台服务器 | 1/N 的键重新映射 | 被移除服务器的 vnodes 区间分配给顺时针下一个节点 |
| 典型值 (5→6 台) | ~17% 迁移 | 远好于取模哈希的 83% 迁移 |

**取模哈希 vs 一致性哈希**:
```
取模:    hash(key) % N    →  N 变化时几乎所有键重新映射
一致性:  hash_ring_lookup  →  仅 1/N 键重新映射
```

**使用场景**: 分布式缓存 (Memcached, Redis Cluster)、会话粘滞 (Sticky Sessions)、CDN 边缘节点选择。

### 5. 随机 (Random)

从健康服务器中随机选择一台。

**统计学特性**:
- 当请求量足够大时，分布趋近于均匀
- 方差与 1/√n 成正比
- 无需维护任何状态

**使用场景**: 服务器数量多且请求量大的场景，对公平性要求不极端的场景。

### 6. 两种选择的幂 (Power of Two Choices) — 理论参考

> 本实现未包含此算法，但在大规模分布式系统中具有重要意义。

```
算法:
1. 随机选择 2 台服务器
2. 比较两者的当前负载
3. 将请求分配给负载较轻的那台
```

**理论保证**: 仅需 O(log log n) 的额外查询即可达到接近最优的负载均衡效果。

## 健康检查 (Health Check)

负载均衡器定期检查后端服务器的健康状态：

- **主动健康检查**: LB 主动 ping 后端，检查响应
- **被动健康检查**: 观察实际请求的成功/失败率
- **熔断集成**: 当后端连续失败超过阈值时自动标记为 DOWN
- **恢复机制**: 健康检查通过后自动重新加入

## 加权变体

| 算法 | 权重支持 | 说明 |
|------|---------|------|
| Round-Robin | 否 | 均匀分配 |
| Weighted RR | 是 | SWRR 平滑分配 |
| Least Connections | 否 | 基于连接数 |
| Weighted LC | 是 | 加权最少连接: min(conn/weight) |
| Consistent Hash | 否 | 基于哈希 |
| Weighted CH | 是 | 为高权重服务器分配更多 vnodes |

## 性能对比

| 算法 | 选择复杂度 | 内存 | 公平性 | 会话保持 |
|------|-----------|------|--------|---------|
| Round-Robin | O(1) | O(1) | 好 | 否 |
| Weighted RR | O(N) | O(N) | 好 | 否 |
| Least Connections | O(N) | O(1) | 很好 | 否 |
| Consistent Hash | O(log N) | O(V*N) | 一般 | 是 |
| Random | O(1) | O(1) | 一般 | 否 |

## C 语言实现注记

- 使用 `uint32_t` 作为哈希值类型，FNV-1a 提供好的 32 位分布
- 虚拟节点数量 (150) 在均匀性和内存之间取平衡
- 环结构使用排序数组实现，查找为二分 O(log VN)
- 服务器状态使用 `bool healthy` 标记，支持运行时注入故障

## 参考资料

- NGINX: https://nginx.org/en/docs/http/ngx_http_upstream_module.html
- Envoy Load Balancing: https://www.envoyproxy.io/docs/envoy/latest/intro/arch_overview/upstream/load_balancing/overview
- Google Maglev: https://static.googleusercontent.com/media/research.google.com/en//pubs/archive/44824.pdf
- Consistent Hashing Paper: Karger et al., "Consistent Hashing and Random Trees", STOC 1997
- Power of Two Choices: Mitzenmacher, "The Power of Two Choices in Randomized Load Balancing", 2001
