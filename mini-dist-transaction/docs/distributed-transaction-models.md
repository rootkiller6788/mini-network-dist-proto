# 分布式事务模型：ACID vs BASE & 隔离级别与一致性模型

## 目录

1. [ACID 与 BASE 的哲学分歧](#acid-与-base-的哲学分歧)
2. [分布式场景下的 ACID 挑战](#分布式场景下的-acid-挑战)
3. [分布式事务隔离级别](#分布式事务隔离级别)
4. [线性化与串行化的关系](#线性化与串行化的关系)
5. [分布式一致性模型](#分布式一致性模型)
6. [CAP 定理与 PACELC](#cap-定理与-pacelc)
7. [实际系统中的权衡](#实际系统中的权衡)
8. [总结对比](#总结对比)

---

## ACID 与 BASE 的哲学分歧

### ACID 模型

由数据库领域先驱 Jim Gray (1981) 和 Andreas Reuter (1983) 形式化：

| 特性 | 定义 | 分布式下的含义 |
|------|------|---------------|
| **Atomicity** (原子性) | All-or-nothing | 所有节点要么全部提交，要么全部回滚 |
| **Consistency** (一致性) | 事务前后数据满足所有约束 | 分布式约束验证（外键、唯一性） |
| **Isolation** (隔离性) | 并发事务互不干扰 | 跨节点并发控制（锁、MVCC） |
| **Durability** (持久性) | 提交后数据不丢失 | 多副本写确认（WAL、Raft 日志） |

**核心原则**：事务是原子的、隔离的恢复单元。

```
ACID 的世界观:
┌──────────────────────────────────┐
│  事务是一个不可分割的"原子单元"    │
│  要么完全成功，要么完全失败        │
│  系统始终处于一致状态             │
└──────────────────────────────────┘
```

### BASE 模型

由 Eric Brewer (CAP 定理提出者) 和 Dan Pritchett (eBay 架构师) 推广：

| 特性 | 定义 | 实现方式 |
|------|------|---------|
| **B**asically Available (基本可用) | 系统大部分时间可用 | 冗余、降级服务 |
| **S**oft state (软状态) | 状态可以不同步 | 无连接、异步复制 |
| **E**ventually consistent (最终一致性) | 最终达到一致 | Gossip、反熵、读修复 |

**核心原则**：放宽 ACID 约束以获取更好的可用性和性能。

```
BASE 的世界观:
┌──────────────────────────────────┐
│  系统大部分时间可用               │
│  数据可能在短时间内不一致          │
│  但最终会收敛到一致状态            │
└──────────────────────────────────┘
```

### ACID vs BASE 的系统选择

| 系统 | 模型 | 典型场景 |
|------|------|---------|
| Google Spanner | ACID | 金融交易、广告计费 |
| Amazon DynamoDB | BASE | 购物车、商品目录 |
| CockroachDB | ACID (Serializable) | OLTP 业务 |
| Cassandra | BASE (Tunable) | 时序数据、日志 |
| PostgreSQL (单机) | ACID | 通用 OLTP |
| MongoDB | ACID (多文档) + BASE | 灵活应用 |

---

## 分布式场景下的 ACID 挑战

### 挑战 1: 原子性 → 需要分布式提交协议

**单机**：WAL (Write-Ahead Logging) 直接回滚
**分布式**：需要协调者收集各节点决定（本模块的 2PC/3PC）

```
单机: BEGIN → SQL1 → SQL2 → COMMIT (或 ROLLBACK)
                  └── WAL 保证原子性

分布式: BEGIN TXN
          node1.prepare() ──┐
          node2.prepare() ──┤── 全部 OK?
          node3.prepare() ──┘
        COMMIT / ROLLBACK ALL
```

### 挑战 2: 一致性 → 约束验证跨节点

```
跨节点外键:
  Orders.user_id → Users.id (在不同分片上)
  └── 需要在 COMMIT 时跨节点验证

分布式唯一约束:
  INSERT INTO users (email) VALUES ('x@y.com')
  └── 需要全局检查唯一性（锁或协调者）
```

### 挑战 3: 隔离性 → 并发控制跨节点

| 机制 | 单机 | 分布式 | 复杂度 |
|------|------|--------|--------|
| 2PL (两阶段锁) | 本地锁管理器 | 分布式锁管理器 | 高（死锁检测） |
| OCC (乐观) | 本地版本号/时间戳 | 全局时间戳 (TSO) | 中 |
| MVCC | 本地版本链 | 全局版本号 (HLC) | 高 |
| Snapshot Isolation | 本地快照 | 全局快照（时钟同步） | 很高 |

### 挑战 4: 持久性 → 多副本确认

```
单机: WAL fsync → disk → 持久化完成
分布式: WAL fsync × N nodes + 多数派确认
  └── 需要共识协议 (Raft/Paxos) 确保副本一致性
```

---

## 分布式事务隔离级别

### ANSI SQL 隔离级别回顾

从 SQL-92 标准扩展，包含分布式环境特有的问题：

| 级别 | 脏读 | 不可重复读 | 幻读 | 写偏斜 | 实现复杂度 |
|------|------|-----------|------|--------|-----------|
| **Read Uncommitted** | 是 | 是 | 是 | 是 | 最低 |
| **Read Committed** | 否 | 是 | 是 | 是 | 低 |
| **Repeatable Read** | 否 | 否 | 是* | 是* | 中 |
| **Snapshot Isolation** | 否 | 否 | 否* | 是 | 中 |
| **Serializable** | 否 | 否 | 否 | 否 | 高 |

*取决于具体实现

### 分布式场景新增的隔离问题

#### 1. 跨分片写偏斜 (Cross-Shard Write Skew)

```sql
-- 约束: 至少有一个管理员在职
-- T1: SELECT COUNT(*) FROM admins WHERE status='active'
--      返回 1（有一个管理员）
-- T2: SELECT COUNT(*) FROM admins WHERE status='active'
--      返回 1（有一个管理员）
-- T1: UPDATE admins SET status='inactive' WHERE id=1  (在 shard A)
-- T2: UPDATE admins SET status='inactive' WHERE id=2  (在 shard B)
-- 两个事务并行提交 → 0 个管理员在职 → 约束违反！
```

**解决**：
- 串行化：全局排序所有冲突事务
- 谓词锁 (Predicate Locking)：Spanner 采用
- 物化冲突 (Materialized Conflicts)：CockroachDB 采用

#### 2. 因果反转 (Causal Reversal)

```
时间线:
  Client A: write(x=1) → write(y=2)
  Client B: read(y=2) → read(x=0)

在因果一致性下，B 观察到 y=2 但 x=0
→ y=2 因果依赖于 x=1，但 B 没有观察到 x=1
```

**解决**：HLC (Hybrid Logical Clocks) 或 TrueTime (Spanner)

#### 3. 读己之写一致性 (Read-Your-Writes)

用户在副本 A 写入，立即从副本 B 读取→应看到刚写入的数据。

| 方案 | 实现方式 |
|------|---------|
| 主从复制 | 强制从主库读取 |
| 多主 | 会话粘性 + 版本号 |
| CRDT | 合并语义自动保证 |

### Spanner 的隔离级别

Spanner 提供三种隔离级别，全部基于 TrueTime：

```c
// 概念映射到本模块
ReadWriteTxn {
    // 1. 获取读时间戳
    read_ts = TrueTime.now().latest

    // 2. 读取所有需要的数据（快照读）
    data = read_all_at(read_ts)

    // 3. 执行事务逻辑
    result = process(data)

    // 4. 2PC 提交 + 等待提交时间戳 ≥ TrueTime
    commit_ts = max(TrueTime.now().latest, max_participant_ts)
    wait_until_true_time_passes(commit_ts)
    commit(result)
}
```

| Spanner 级别 | 对应标准 | 锁获取时机 | TrueTime 依赖 |
|-------------|---------|-----------|--------------|
| Read Only | Serializable | 无锁 | 读时间戳 |
| Snapshot Read | Repeatable Read | 无锁 | 快照时间戳 |
| Read Write | Serializable (Strict) | 2PL + 2PC | 提交等待 |

---

## 线性化与串行化的关系

这两个术语经常混淆，但描述的是不同的概念。

### 线性化 (Linearizability)

定义：一个分布式系统使每个操作看起来都在某个时间点**原子地**执行，并且效果与操作的实际顺序一致。

```
本质：单个对象的"看起来像瞬间完成"

时间线:
  Client A:  write(x, 1) ────────┐
  Client B:             read(x) →│ 1  (如果发生在 write 之后)
  Client C:                       read(x) → 1  (因果传递)

  如果 B 读到了 1，那么 C 也必须读到 1
```

### 串行化 (Serializability)

定义：并发事务的执行结果与某个顺序执行的结果相同。

```
本质：多个对象的"等价于串行执行"

事务:
  T1: read(A) → write(B)
  T2: read(B) → write(A)

  串行化: T1 在 T2 之前执行 或 T2 在 T1 之前执行
         不能交叉 (T1 读 A → T2 写 A → T1 写 B)
```

### 核心区别

| 维度 | 线性化 | 串行化 |
|------|--------|--------|
| **作用对象** | 单个对象/寄存器 | 多个对象的集合 |
| **关注点** | 实时顺序 (real-time ordering) | 事务隔离 |
| **保证** | 最近写入的值对所有读者可见 | 事务结果等价于串行执行 |
| **并发单位** | 单个操作 | 事务（多个操作） |

### 严格串行化 (Strict Serializability)

**严格串行化 = 串行化 + 线性化**

这是最强的正确性保证：
- 事务执行结果等价于串行执行（串行化）
- 串行顺序与实时时间顺序一致（线性化）

Spanner 的 External Consistency 本质就是严格串行化。

```
举例:
  时刻 T1: Txn A 提交 (写入 x=1)
  时刻 T2: Txn B 提交 (读取 x, 写入 y)
  时刻 T3: Txn C 读取 x, y

  严格串行化保证:
  - A < B < C 的串行顺序与实时顺序一致
  - C 一定读到 x=1 和 B 写入的 y 值
```

### 隔离级别与一致性模型的关系

```
隔离级别 (Isolation)              一致性模型 (Consistency)
─────────────────────            ───────────────────────
关注事务间的干扰                  关注多副本的状态一致性

Read Uncommitted                 Eventual Consistency
Read Committed                   Causal Consistency
Repeatable Read                  Sequential Consistency
Snapshot Isolation               ──────────────
Serializable                     Linearizability
Strict Serializable              Strict Serializability
                                 (Spanner's External Consistency)

交集: 高隔离级别需要底层实现线性化
```

---

## 分布式一致性模型

### 一致性模型谱系

```
强                                         弱
├────────────────────────────────────────────┤
Strict Serializability                      Eventual
├── Linearizability                        Consistency
│     ├── Sequential
│     │     ├── Causal
│     │           ├── PRAM (FIFO)
│     │                 ├── Read-Your-Writes
│     │                       ├── Monotonic Reads
│     │                             └── Eventual
```

### 各模型定义与示例

#### 1. Eventual Consistency (最终一致性)

```
如果不再有新的更新，最终所有副本都会一致。

Amazon S3, DNS, Cassandra (默认)
```

#### 2. Causal Consistency (因果一致性)

```
如果 A → B (A 因果先于 B)，则所有进程观察到 A 在 B 之前。

NEED: 追踪因果关系向量
│
├── 显式因果 (Vector Clocks)
└── 隐式因果 (TrueTime, HLC)

COPS (Lloyd et al., SOSP 2011)
```

#### 3. Sequential Consistency

```
所有操作的执行结果与某些全局顺序一致，
且每个进程内操作顺序保持。

等价于: 所有操作串行化执行，但不需要与实时时间一致。

ZooKeeper (单一 Leader 保证)
```

#### 4. Linearizability

```
所有操作的执行结果与某些全局顺序一致，
且该顺序与实时时间顺序一致（操作必须在其调用和返回之间生效）。

CAP 定理中的 "Consistency" 就是指 Linearizability。

etcd, Consul, Spanner (External Consistency)
```

### 实际系统的一致性保证

| 系统 | 一致性模型 | 实现机制 |
|------|-----------|---------|
| Google Spanner | Strict Serializability | TrueTime + Paxos |
| CockroachDB | Serializable | HLC + Parallel Commit |
| ZooKeeper | Sequential (Linearizable writes) | ZAB |
| etcd | Linearizable | Raft |
| DynamoDB | Eventual (可配置) | Quorum reads/writes |
| Cassandra | Tunable (Eventual → Linearizable) | Quorum + Hinted Handoff |
| MongoDB | Causal (可升级到 Linearizable) | Logical Clocks |

---

## CAP 定理与 PACELC

### CAP 定理 (Brewer 2000, 证明: Gilbert & Lynch 2002)

> 一个分布式系统在发生网络分区 (P) 时，只能满足一致性 (C) 或可用性 (A) 之一。

```
        C (Consistency)
       /\
      /  \
     /    \
    /  CA  \       在分区时:
   /        \      CA 系统 → 拒绝请求 (牺牲 A)
  /----------\     CP 系统 → 返回旧数据 (牺牲 C)
 /     CP     \    AP 系统 → 返回不一致数据 (牺牲 C)
/    AP   P    \
────────────────
```

### PACELC 定理 (Abadi 2012)

扩展 CAP：不仅考虑分区存在时，还考虑分区不存在时。

```
当存在网络 Partition (P) 时:
  系统必须在 Availability 和 Consistency 之间选择

Else (E，无分区时):
  系统必须在 Latency 和 Consistency 之间选择
```

| 系统 | PACELC 分类 | 说明 |
|------|------------|------|
| Spanner | PC/EL | 分区选 C，正常选 L（TrueTime 等待） |
| DynamoDB | PA/EL | 分区选 A，正常选 L（异步复制） |
| ZooKeeper | PC/EC | 分区选 C，正常选 C（写入所有 followers） |
| Cassandra | PA/EL | 分区选 A，正常选 L（最终一致） |

---

## 实际系统中的权衡

### 延迟 vs 一致性 (PACELC 的 "EL" 部分)

```
每增加一个副本的同步确认：
  延迟 + RTT(副本)
  吞吐量 ÷ (1 + 副本数)  [近似]

Google Spanner 的选择:
  跨数据中心 Paxos 组 → 每笔写操作 ~10-100ms (取决于数据中心距离)
  但严格串行化保证了正确性 → 适合金融、广告计费
```

### 可用性 vs 一致性 (CAP 的 "P" 部分)

```
Amazon 的哲学 (Dynamo):
  "宁可卖出去两件商品 (超卖)，也不能让用户看到错误页面"

Google 的哲学 (Spanner/F1):
  "广告计费数据错误比暂时不可用严重得多"
```

### 本模块中的权衡实现

| 模块 | 一致性等级 | 可用性 | 延迟 |
|------|-----------|--------|------|
| 2PC | 强一致 | 低（阻塞） | 高（同步） |
| 3PC | 强一致 | 中（非阻塞） | 很高（3轮） |
| Saga | 最终一致 | 高 | 低（异步） |
| TCC | 强一致 | 中-高 | 中 |
| DistLock (Redlock) | 可配置 | 高 | 中 |

---

## 总结对比

### 核心概念速查

```
分布式事务 = 多节点原子性操作集合
           + 一致性约束维护
           + 故障恢复保证

并发控制 ≈ 隔离级别 (Serializable, SI, RR, RC)
分布式一致性 ≈ Linearizability, Sequential, Eventual
正确性 = 并发控制 (隔离) + 副本控制 (一致性)
```

### 选型决策树

```
需要所有节点全部成功或全部失败？
  ├── 是 → 需要原子提交 (2PC/3PC/TCC)
  │   ├── 短事务 (<1s) → 2PC 或 3PC
  │   ├── 中事务 (1s~1min) → TCC
  │   └── 长事务 (>1min) → Saga
  └── 否 → 不需要分布式事务
      ├── 可接受最终一致 → BASE
      └── 必须强一致 → 至少单分片事务

需要跨地域部署？
  ├── 是 → Saga (异步) 或 Spanner (TrueTime 基础设施)
  └── 否 → 2PC/TCC (数据中心内延迟可接受)
```

---

## 参考文献

1. Gray, J. (1981) "The Transaction Concept: Virtues and Limitations", VLDB
2. Brewer, E.A. (2000) "Towards Robust Distributed Systems", PODC Keynote
3. Gilbert, S., Lynch, N. (2002) "Brewer's Conjecture and the Feasibility of Consistent, Available, Partition-Tolerant Web Services", ACM SIGACT News
4. Abadi, D. (2012) "Consistency Tradeoffs in Modern Distributed Database System Design", IEEE Computer
5. Herlihy, M.P., Wing, J.M. (1990) "Linearizability: A Correctness Condition for Concurrent Objects", ACM TOPLAS
6. Berenson, H. et al. (1995) "A Critique of ANSI SQL Isolation Levels", ACM SIGMOD
7. Adya, A. et al. (2000) "Generalized Isolation Level Definitions", ICDE
8. Bailis, P. et al. (2014) "Highly Available Transactions: Virtues and Limitations", VLDB
9. Kleppmann, M. (2017) "Designing Data-Intensive Applications", O'Reilly Media (Chapters 7-9)
10. CMU 15-721 (2023) Lecture 15: "Distributed Transactions Part 1"
