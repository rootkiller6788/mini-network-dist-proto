# 课程对标：CMU 15-721 & Google Percolator / Spanner

## 概述

本模块的核心设计概念对标 CMU 15-721 Advanced Database Systems (Spring 2023) 课程内容，特别是 Lecture 14-16: Distributed Transactions，以及 Google 的两篇关键论文：Percolator (2010) 和 Spanner (2012)。

---

## CMU 15-721 课程对照

### Lecture 14: Distributed OLTP Databases

| 课程概念 | 本模块实现 | 对应文件 |
|----------|-----------|----------|
| 2PC 协议 | `tpc_coordinator_prepare`, `tpc_coordinator_commit` | `include/two_pc.h`, `src/two_pc.c` |
| 参与者状态机 (INIT→READY→COMMITTED/ABORTED) | `TPCState` 枚举 | `include/two_pc.h` |
| 协调者故障处理 | `tpc_handle_timeout` | `src/two_pc.c` |
| Quorum-based commit | `redlock_acquire` 中的多数投票 | `src/dist_lock.c` |

15-721 中强调的 "NoSQL → NewSQL 演进" 路径：
- **H-Store/VoltDB**: 单分区事务 → 无分布式事务
- **Spanner**: Paxos-based 2PC
- **CockroachDB**: 并行提交

### Lecture 15: Distributed Transactions (Part 1)

| 课程概念 | 本模块实现 |
|----------|-----------|
| ACID vs BASE | `docs/distributed-transaction-models.md` |
| 乐观并发控制 (OCC) | 未实现（预留扩展空间） |
| 分布式死锁检测 | 通过租约超时机制 (`lock_handle_expiry`) 避免 |
| 两阶段锁 (2PL) + 2PC 组合 | 对比文档 `demos/mini-distributed-transactions/README.md` |

关键引用 — 15-721 课堂语录:
> "2PC is the only way to guarantee atomicity across multiple resource managers in a distributed system. Everything else (Saga, TCC) is compensating transactions — not true atomic commitment."

### Lecture 16: Distributed Transactions (Part 2)

| 课程概念 | 本模块实现 |
|----------|-----------|
| Saga 长事务 | `saga_execute`, 补偿逻辑 | `include/saga.h`, `src/saga.c` |
| 最终一致性 | Saga/TCC 的补偿机制 |
| TCC (Try-Confirm-Cancel) | `tcc_try_all`, `tcc_confirm_all`, `tcc_cancel_all` | `include/tcc.h`, `src/tcc.c` |
| 幂等性与重试 | `idempotent_retry_with_backoff` | `include/idempotency.h` |

---

## Google Percolator 论文对照

**论文**: Peng, D., Dabek, F. (2010) "Large-scale Incremental Processing Using Distributed Transactions and Notifications", OSDI 2010.

### Percolator 核心设计

Percolator 为 Google 的网页索引系统提供增量更新能力，在 Bigtable 之上实现了跨行、跨表的事务。

| Percolator 概念 | 本模块抽象 |
|----------------|-----------|
| **两阶段提交（无协调者）** | 2PC 模块（简化协调者模型） |
| **Timestamp Oracle (TSO)** | 未单独实现，时间戳概念体现在 `idempotency.h` |
| **Snapshot Isolation** | 文档中讨论与线性化对比 |
| **Lock 列族** | `DistLock` 结构体 |
| **Write 列族** | 文档对照说明 |
| **客户端驱动的提交协议** | 参与者投票机制 (`tpc_participant_vote`) |

### Percolator 事务流程对照

```
Percolator                         本模块
─────────                         ────────
PreWrite (获取锁 + 写数据)    →   tpc_coordinator_prepare
Primary 锁                    →   TPCPhase PREPARE
Commit (写 write 列)          →   tpc_coordinator_commit
Cleanup (异步清理锁)          →   lock_release
```

**Percolator 的创新点**（本模块中的体现）：
1. **无协调者架构**：每个客户端自行驱动事务提交
2. **Chubby 选主**：故障恢复通过锁租约机制模拟
3. **Lazy Cleanup**：非关键路径失败通过补偿处理（Saga）

### 为什么 Percolator 选择无协调者？

> "Percolator does not use a central coordinator because this would be a single point of failure and a bottleneck."

本模块在 API 设计中保持了灵活性：2PC 采用协调者模式（教学清晰），Saga/TCC 为无协调者（贴近 Percolator 理念）。

---

## Google Spanner 论文对照

**论文**: Corbett, J.C. et al. (2012) "Spanner: Google's Globally-Distributed Database", OSDI 2012.

### Spanner 核心特性

| Spanner 特性 | 本模块体现 |
|-------------|-----------|
| **TrueTime API** | 未直接实现（硬件依赖），在文档中讨论时钟不确定性 |
| **Paxos-based 2PC** | Redlock 算法中多节点投票 (`redlock_acquire`) |
| **外部一致性 (External Consistency)** | 分布式锁租约 + Fencing Token 文档分析 |
| **分布式事务隔离级别** | `docs/distributed-transaction-models.md` |
| **Multi-Paxos 复制** | Redlock 多节点 quorum 模型 |
| **目录/分片管理** | 未实现（超出本模块范围） |

### TrueTime 与 本模块的时间管理

```c
// 本模块使用相对时间（毫秒级）
lock_acquire(&lm, resource, owner, lease_ms, now_ms);
lock_renew_lease(&lm, resource, owner, extend_ms, now_ms);

// Spanner 使用绝对时间 + 误差边界
// TT.now() 返回 [earliest, latest] 区间
// 提交等待时间 = 2 * average_error
```

### Spanner 的 2PC + Paxos 组合

```
Spanner 提交流程:
  1. 协调者 Leader (Paxos) 发送 PREPARE
  2. 参与者 Leader (Paxos) 执行 PREPARE，通过 Paxos 日志复制
  3. 参与者回复 VOTE (YES/NO)，Paxos 复制 VOTE
  4. 协调者收集所有 VOTE：
     - 全部 YES → COMMIT (Paxos 日志)
     - 任一 NO → ABORT (Paxos 日志)
  5. 各参与者执行 COMMIT/ABORT

本模块简化：
  1. 协调者调用 tpc_coordinator_prepare
  2. 各参与者调用 tpc_participant_vote
  3. 协调者调用 tpc_coordinator_commit 或 abort
  (省略 Paxos 复制，展示核心协议逻辑)
```

---

## Seata (Alibaba) 框架对照

Seata 提供四种分布式事务模式，本模块实现了对应的简化版本：

| Seata 模式 | 本模块 | 关系 |
|-----------|--------|------|
| **AT 模式** (自动补偿) | 未实现 | 依赖 undo_log 表，超出本模块范围 |
| **TCC 模式** | `tcc.h` / `tcc.c` | 完整实现 Try/Confirm/Cancel |
| **Saga 模式** | `saga.h` / `saga.c` | 完整实现编排 + 补偿 |
| **XA 模式** | `two_pc.h` / `two_pc.c` | 2PC 协议 + 3PC 扩展 |

---

## 课程其他参考资料

### 共识协议基础

- **Raft (Ongaro 2014)**: Redlock 中的 quorum 概念源于 Raft 多数投票
- **Paxos (Lamport 1998)**: Spanner 和 Chubby 的基础
- **ZAB (ZooKeeper)**: 分布式锁文档中的对比分析

### 隔离级别扩展

- **Snapshot Isolation**: Percolator 的核心隔离级别
- **Serializable Snapshot Isolation (SSI)**: CockroachDB 的实现（本模块未覆盖）
- **Strict Serializability**: Spanner 的承诺，本模块在文档中讨论

### 工业界时间线

```
1976: 2PC 首次提出 (Gray)
1981: 3PC (Skeen)
1987: Saga (Garcia-Molina)
2006: Chubby (Burrows)
2010: Percolator (Peng, Dabek)
2012: Spanner (Corbett)
2019: Seata 开源 (Alibaba)
```

---

## 未覆盖但课程涉及的主题

| 主题 | 原因 |
|------|------|
| Calvin (确定性数据库) | 需要修改数据库引擎，超出范围 |
| OCC 与 Timestamp Ordering | 关注点在提交协议，非并发控制 |
| 多版本并发控制 (MVCC) | 关注在事务协议层面 |
| 分布式死锁检测 (Waits-For Graph) | 通过租约超时避免而非检测 |
| Spanner TrueTime 的具体实现 | 依赖硬件（GPS + 原子钟） |
| Percolator 的 Notifications | Bigtable 特定功能 |

---

## 推荐阅读顺序

1. 本模块 `README.md` — 了解 5 个模块
2. `demos/mini-distributed-transactions/README.md` — 协议对比
3. `demos/mini-distributed-lock/README.md` — 锁机制深入
4. `docs/distributed-transaction-models.md` — 理论模型
5. CMU 15-721 Lecture 14-16 视频 — 课程讲解
6. Spanner 论文 (OSDI 2012) — 工业级实现
7. Percolator 论文 (OSDI 2010) — 无协调者事务

---

## 参考文献

1. CMU 15-721 (Spring 2023) - Advanced Database Systems. https://15721.courses.cs.cmu.edu/
2. Corbett, J.C. et al. (2012) "Spanner: Google's Globally-Distributed Database", OSDI 2012
3. Peng, D., Dabek, F. (2010) "Large-scale Incremental Processing Using Distributed Transactions and Notifications", OSDI 2010
4. Burrows, M. (2006) "The Chubby Lock Service for Loosely-Coupled Distributed Systems", OSDI 2006
5. Lamport, L. (1998) "The Part-Time Parliament", ACM TOCS
6. Ongaro, D., Ousterhout, J. (2014) "In Search of an Understandable Consensus Algorithm", USENIX ATC
7. Garcia-Molina, H., Salem, K. (1987) "Sagas", ACM SIGMOD
8. Skeen, D. (1981) "Nonblocking Commit Protocols", ACM SIGMOD
9. Bernstein, P.A., Hadzilacos, V., Goodman, N. (1987) "Concurrency Control and Recovery in Database Systems", Addison-Wesley
10. Kleppmann, M. (2017) "Designing Data-Intensive Applications", O'Reilly Media
