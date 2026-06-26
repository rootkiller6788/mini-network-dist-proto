# 分布式锁：原理、实现与对比

## 目录

1. [为什么需要分布式锁](#为什么需要分布式锁)
2. [分布式锁基本要求](#分布式锁基本要求)
3. [Redis Redlock 算法](#redis-redlock-算法)
4. [ZooKeeper 分布式锁](#zookeeper-分布式锁)
5. [etcd 分布式锁](#etcd-分布式锁)
6. [Fencing Token（栅栏令牌）](#fencing-token栅栏令牌)
7. [其他实现方案](#其他实现方案)
8. [方案对比矩阵](#方案对比矩阵)
9. [脑裂问题与防护](#脑裂问题与防护)
10. [性能与可用性权衡](#性能与可用性权衡)
11. [实际应用案例](#实际应用案例)

---

## 为什么需要分布式锁

在分布式系统中，多个进程或服务实例可能同时访问共享资源。分布式锁提供跨进程的互斥访问。

### 典型场景

1. **定时任务去重**：多个服务实例抢锁执行定时任务，确保只有一个实例执行
2. **库存扣减**：防止超卖，同一商品同时只能一个进程扣减
3. **配置热更新**：多个节点需协调，确保只有一个节点发布配置变更
4. **分布式 ID 生成**：确保区间分配不冲突
5. **缓存重建**：防止缓存击穿，确保只有一个线程重建热点缓存

### 单机锁 vs 分布式锁

| 特性 | 单机锁 (mutex) | 分布式锁 |
|------|---------------|----------|
| 作用范围 | 单进程 | 跨进程/跨机器 |
| 实现方式 | 系统调用 (futex) | 外部协调服务 |
| 可靠性 | OS 保证 | 依赖外部服务 |
| 延迟 | 微秒级 | 毫秒级 |
| 死锁检测 | OS 级别 | 需设计租约 |

---

## 分布式锁基本要求

### 必要条件

1. **互斥性（Mutual Exclusion）**：任意时刻只有一个客户端持有锁
2. **无死锁（Deadlock Free）**：客户端崩溃后锁能自动释放（租约）
3. **容错性（Fault Tolerance）**：锁服务部分节点故障时仍可正常工作
4. **释放安全（Release Safety）**：只有锁持有者才能释放锁

### 可选的增强特性

5. **可重入（Reentrancy）**：同一客户端可多次获取同一把锁
6. **阻塞/非阻塞**：获取失败时可选择等待或立即返回
7. **公平性（Fairness）**：按请求顺序分配锁（FIFO 队列）
8. **锁续期（Lease Renewal）**：处理执行时间不确定的任务

---

## Redis Redlock 算法

### 背景

Redis 作者 antirez 提出的分布式锁算法，旨在解决单一 Redis 实例故障时的可靠性问题。

### 算法描述

#### 获取锁

```
function redlock_acquire(resource, lease_time_ms):
    start_time = now()
    
    for each node in RedisNodes:
        ok = node.SET(resource, unique_value, NX, PX, lease_time_ms)
        if ok: acquired_count++
    
    elapsed = now() - start_time
    
    if acquired_count >= N/2+1 AND elapsed < lease_time_ms:
        return unique_value  // 锁获取成功
    else:
        // 锁获取失败，在所有节点上释放
        for each node in RedisNodes:
            node.DEL(resource, unique_value)
        return FAILED

lease_time = lease_time_ms - elapsed
```

#### 释放锁

```
function redlock_release(resource, unique_value):
    for each node in RedisNodes:
        if node.GET(resource) == unique_value:
            node.DEL(resource)
```

**注意**：释放时需要校验 unique_value（Lua 脚本原子执行），防止释放他人持有的锁。

### 安全分析

| 故障场景 | 影响 | 防护 |
|----------|------|------|
| 单节点宕机 | 多数节点仍可用 | N=5, 容忍 2 节点故障 |
| 时钟跳跃 | 租约提前过期 | 依赖 NTP 同步，存在争议 |
| 网络分区 | 多数分区可继续工作 | 少数分区无法获取锁 |
| 客户端 GC 暂停 | 锁过期被他人获取 | Fencing Token 机制 |

### Martin Kleppmann 的批评

Redlock 存在以下争议（详见 Martin Kleppmann 的 *"How to do distributed locking"* 博客）：

1. **时钟假设**：Redlock 依赖时钟同步（NTP），时钟跳跃可能破坏安全性
2. **缺乏 Fencing Token**：持有锁的客户端 GC 暂停后，其他客户端可能获取同一把锁
3. **延迟假设**：网络延迟可能使 TTL 计算失准

### 需要锁的场景：正确性 vs 效率

```
效率需求（Efficiency）:
  目的: 防止重复工作
  后果: 多花一点资源但不影响正确性
  方案: Redis 单点 SET NX EX (够用)

正确性需求（Correctness）:
  目的: 防止不一致
  后果: 可能导致数据错误/财务损失
  方案: ZooKeeper/Fencing Token
```

---

## ZooKeeper 分布式锁

### 实现原理

#### 1. 基于临时顺序节点

```
/locks/
  ├── lock-0000000001  (Client A, 获得锁)
  ├── lock-0000000002  (Client B, 等待)
  └── lock-0000000003  (Client C, 等待)
```

**算法步骤**：
1. 所有客户端在 `/locks/` 下创建**临时顺序节点**
2. 获取 `/locks/` 下所有子节点，按序号排序
3. 如果当前客户端创建的节点是最小编号的，则获得锁
4. 否则，对前一个编号的节点注册 **Watch**，等待通知
5. 锁持有者释放锁（删除节点）或崩溃（ZK 自动删除临时节点）

#### 2. ZooKeeper 锁的优势

- **公平性**：FIFO 顺序保证，先到先得
- **自动释放**：客户端崩溃/断连 → 临时节点自动删除 → 锁自动释放
- **阻塞通知**：Watch 机制，无轮询开销
- **一致性**：ZAB 协议保证 CP（强一致性）

#### 3. ZooKeeper 锁的劣势

- **复杂性**：需要部署和维护 ZK 集群（最少 3 节点）
- **性能**：写操作需 Leader 确认，吞吐低于 Redis
- **羊群效应**：需要正确实现 Watch（监听的应该是前一个节点而非整个目录）
- **不可重入**：需要额外实现

#### 4. 正确实现（避免羊群效应）

```python
# 错误: 所有等待者监听 /locks/ 目录
# 正确: 每个等待者只监听前一个节点

def acquire_lock():
    my_node = create_ephemeral_sequential("/locks/lock-")
    children = get_children("/locks/")
    children.sort()
    
    if my_node == children[0]:
        return True  # 获得锁
    
    prev_node = children[children.index(my_node) - 1]
    watch(prev_node)  # 只监听前一个节点
    wait_for_notification()
    return True
```

---

## etcd 分布式锁

### 实现原理

etcd 使用 Raft 共识算法，基于租约（Lease）机制实现分布式锁。

#### 基本流程

```
1. 创建租约:
   lease = etcd.LeaseGrant(TTL=30s)

2. 事务写入（CAS）:
   txn = etcd.Txn()
   txn.If(etcd.Compare(etcd.CreateRevision(key), "=", 0))
      .Then(etcd.OpPut(key, owner, etcd.WithLease(lease)))
      .Else(etcd.OpGet(key))
   txn.Commit()

3. 心跳续约:
   etcd.LeaseKeepAlive(lease_id)  // 后台协程持续续约

4. 释放锁:
   etcd.LeaseRevoke(lease_id)     // 或删除 key
```

#### etcd vs ZooKeeper 对比

| 特性 | etcd | ZooKeeper |
|------|------|-----------|
| 共识算法 | Raft | ZAB |
| 数据模型 | KV Store | 层次化 ZNode |
| 语言 | Go | Java |
| 部署 | 单二进制 | 需 JVM |
| 客户端 | gRPC API | 原生 ZK 客户端 |
| 性能 | 更高（Go 实现） | 中等 |
| 锁实现 | Lease + CAS | 临时顺序节点 + Watch |
| Watch | gRPC 流式 Watch | 一次性 Watch |
| 社区 | Kubernetes 生态 | Apache Hadoop 生态 |

---

## Fencing Token（栅栏令牌）

### 问题：脑裂场景

```
时间线:
  T0: Client A 获取锁，token=33
  T1: Client A 开始写存储
  T2: Client A 发生 Full GC（STW 暂停 10 秒）
  T3: Client A 的锁过期
  T4: Client B 获取锁，token=34
  T5: Client B 开始写存储（覆盖 Client A 的数据？）
  T6: Client A GC 结束，继续写入（使用 token=33）
  T7: 存储收到写请求 token=33 → 拒绝！（token 小于已见到的最大 token）
```

### 实现机制

每次获取锁时，锁服务返回一个**单调递增的令牌**（Fencing Token）：

```
Lock Service:
  acquire_lock("resource-X") → {owner: "A", token: 42}
  acquire_lock("resource-X") → {owner: "B", token: 43}

Storage:
  write(key, value, token):
    if token < max_seen_token:
      reject  // 拒绝过期令牌
```

### Fencing Token 的关键属性

1. **单调递增**：每个令牌必须严格大于前一个
2. **服务端校验**：存储系统必须校验令牌
3. **原子返回**：获取锁和分配令牌必须是原子操作

### 各方案对令牌的支持

| 方案 | 原生支持 | 实现方式 |
|------|----------|----------|
| ZooKeeper | 是 | ZNode 的 CZxid 可作为令牌 |
| etcd | 是 | ModRevision 可作为令牌 |
| Redis (Redlock) | 否 | 需自行实现 |

---

## 其他实现方案

### 1. 数据库分布式锁

```sql
-- 获取锁
INSERT INTO distributed_locks (lock_name, owner, expire_at)
VALUES ('resource-X', 'client-A', NOW() + INTERVAL 30 SECOND)
ON CONFLICT DO NOTHING;

-- 释放锁
DELETE FROM distributed_locks
WHERE lock_name = 'resource-X' AND owner = 'client-A';

-- 续约
UPDATE distributed_locks
SET expire_at = NOW() + INTERVAL 30 SECOND
WHERE lock_name = 'resource-X' AND owner = 'client-A';
```

**优点**：无需额外组件，利用现有数据库
**缺点**：性能差，锁粒度粗，无 Watch 通知

### 2. 基于文件的锁（NFS/共享存储）

使用文件系统原子操作（`O_CREAT | O_EXCL`）作为锁机制。

**优点**：极度简单
**缺点**：不可靠（NFS 锁有已知问题），无租约

### 3. Chubby（Google）

Google 的内部分布式锁服务，通过 Paxos 实现的粗粒度锁服务。

**设计原则**：
- 粗粒度锁（小时级持有）
- 主要为选主和命名服务而非细粒度数据访问
- 对可靠性和可用性要求极高

---

## 方案对比矩阵

### 综合对比

| 维度 | Redis Redlock | ZooKeeper | etcd | 数据库 | Chubby |
|------|--------------|-----------|------|--------|--------|
| **一致性** | 最终一致 | 强一致 (CP) | 强一致 (CP) | 取决于 DB | 强一致 |
| **可用性** | 高 (AP) | 中等 | 中等 | 取决于 DB | 高 |
| **性能** | 高（微秒~毫秒） | 中 | 中 | 低 | 中 |
| **公平性** | 无 | 是（FIFO） | 可配置 | 无 | 是 |
| **自动释放** | TTL | 临时节点 | 租约 | 轮询清理 | 租约 |
| **Fencing Token** | 否（需自实现） | 是（zxid） | 是（revision） | 自增 ID | 是 |
| **部署成本** | 低 | 中（3+节点） | 低 | 已有 | 高 |
| **运维复杂度** | 低 | 中 | 低-中 | 已有 | 高 |
| **编程模型** | 简单 | 中等 | 简单 | 简单 | RPC |
| **社区活跃度** | 极高 | 中 | 高 | N/A | 内部 |

### 场景推荐

| 场景 | 推荐方案 | 原因 |
|------|----------|------|
| 简单去重/防重复 | Redis 单点 | 够用，简单 |
| 高并发 + 可接受短锁 | Redis Redlock | 高性能 |
| 要求强一致性 | ZooKeeper / etcd | CP 系统 |
| 已有 ZooKeeper | ZooKeeper | 复用基础设施 |
| K8s 环境 | etcd / ConfigMap | 原生集成 |
| 数据正确性关键 | Fencing Token + 任一种锁 | 防护脑裂 |
| 低运维成本 | Redis / 数据库 | 已有组件 |

---

## 脑裂问题与防护

### 脑裂的根本原因

分布式锁的持有者无法精确知道锁是否仍有效：

```
Client A 认为持有锁 → 实际锁已过期 → Client B 获得了锁
```

时间线上的三个不确定性：
1. **网络延迟**：A 不知道消息何时到达锁服务
2. **进程暂停**：GC、OS 调度使 A 暂停
3. **时钟偏移**：不同机器的时钟不一致

### 防护策略分层

```
第1层: 合理的 TTL（租约时间）
  锁的 TTL 应大于典型的处理时间

第2层: 心跳续约
  定期续约，处理完成后主动释放

第3层: Fencing Token
  存储系统校验令牌，拒绝过期写入

第4层: 存储乐观锁（版本号）
  写操作携带版本号 (CAS)
```

### 无法完全解决

Martin Kleppmann 在 "How to do distributed locking" 中指出的根本困境：

> "Neither Redis Redlock nor ZooKeeper can provide perfect mutual exclusion guarantees without support from the resource being locked."

**真正的分布式锁 = 锁协调 + 存储验证（Fencing Token）**

---

## 性能与可用性权衡

### 性能基准参考

```
单机 Redis SET NX EX:  ~10K ops/s
Redis Cluster Redlock:  ~3K ops/s (5节点)
ZooKeeper 锁:          ~0.5K ops/s
etcd 锁:               ~1K ops/s
数据库 (PostgreSQL):   ~0.2K ops/s
```

### CAP 选择

```
一致性(C)
    ↑
    │  ZooKeeper
    │  etcd
    │
    │         Redis Redlock
    │
    │              Redis 单点
    └──────────────────→ 可用性(A) + 性能(P)
```

---

## 实际应用案例

| 公司/系统 | 方案 | 用途 |
|-----------|------|------|
| Google Chubby | Paxos-based | 粗粒度锁、选主、命名服务 |
| Apache Curator | ZooKeeper | 分布式锁、Leader Election |
| Redisson | Redis | Java 分布式锁框架 |
| Kubernetes | etcd | Leader Election、资源协调 |
| Netflix Dynomite | Redis Cluster | 分布式锁（多区域） |
| 美团 | Redis + ZooKeeper | 两级锁（热点 + 冷数据） |
| 阿里 Seata | 数据库/Redis | 全局事务锁 |

---

## 实现参考

本项目的 `dist_lock.h` 提供了:

- 基于租约的锁管理器 (`LockManager`)
- 心跳续约 (`lock_renew_lease`)
- 等待队列 (`lock_wait_queue`)
- 自动过期处理 (`lock_handle_expiry`)
- Redlock 算法 (`redlock_acquire`)

---

## 参考文献

- Kleppmann, M. (2016) "How to do distributed locking" - https://martin.kleppmann.com/2016/02/08/how-to-do-distributed-locking.html
- antirez (Redis) "Distributed locks with Redis" - https://redis.io/docs/latest/develop/use/patterns/distributed-locks/
- Hunt, P. et al. (2010) "ZooKeeper: Wait-free coordination for Internet-scale systems", USENIX ATC
- Burrows, M. (2006) "The Chubby Lock Service for Loosely-Coupled Distributed Systems", OSDI
- CMU 15-712 Advanced and Distributed Operating Systems
