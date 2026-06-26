# 分布式事务协议对比：2PC vs 3PC vs Saga vs TCC

## 目录

1. [背景与问题](#背景与问题)
2. [Two-Phase Commit (2PC)](#two-phase-commit-2pc)
3. [Three-Phase Commit (3PC)](#three-phase-commit-3pc)
4. [Saga 模式](#saga-模式)
5. [Try-Confirm-Cancel (TCC)](#try-confirm-cancel-tcc)
6. [协议对比矩阵](#协议对比矩阵)
7. [故障模型分析](#故障模型分析)
8. [性能权衡](#性能权衡)
9. [实际应用案例](#实际应用案例)

---

## 背景与问题

分布式系统中，一个业务操作经常需要跨越多个独立的数据存储或服务。CAP 定理告诉我们，在网络分区发生时，必须在一致性和可用性之间做出选择。

**核心问题**：如何保证跨多个节点的操作要么全部成功，要么全部失败（原子性）？

**经典场景**：
```
用户下单：
  ├── 扣减库存（Inventory Service）
  ├── 创建订单（Order Service）
  └── 扣款（Payment Service）
如果扣款失败，库存和订单必须回滚。
```

---

## Two-Phase Commit (2PC)

### 协议流程

```
协调者                              参与者
  |                                   |
  |------ PREPARE ------------------->|
  |      (prepare to commit?)        |
  |                                   | 写入 UNDO + REDO 日志
  |<----- VOTE: YES/NO --------------|
  |                                   |
  | 如果全部 YES:                       |
  |------ COMMIT -------------------->|  提交本地事务
  |<----- ACK -----------------------|
  |                                   |
  | 如果任一 NO:                        |
  |------ ABORT -------------------->|  回滚本地事务
  |<----- ACK -----------------------|
```

### 状态机

```
参与者状态:
  INIT → (收到 PREPARE) → READY → (收到 COMMIT) → COMMITTED
                           ↓
                      (收到 ABORT) → ABORTED

协调者状态:
  INIT → PREPARE → (全部 YES) → COMMIT → DONE
                 → (任一 NO)  → ABORT → DONE
```

### 优点

1. **强一致性**：所有参与者最终状态完全一致
2. **简单直观**：协议步骤清晰，易于理解和实现
3. **工业级成熟**：MySQL XA、PostgreSQL 2PC、JTA 等广泛支持
4. **原子性保证**：完美实现分布式 ACID 中的 A

### 缺点

1. **同步阻塞**：所有参与者必须等待最慢的节点
2. **协调者单点故障**：协调者在 COMMIT 阶段崩溃，参与者无限期阻塞
3. **锁持有时间长**：PREPARE 到 COMMIT 期间资源一直被锁定
4. **无法处理网络分区**：协调者与部分参与者断开时无法决策

### 协调者故障场景

```
时间线:
  T0: 协调者发送 PREPARE
  T1: 所有参与者回复 YES
  T2: 协调者发送 COMMIT 给 P1，然后崩溃
  T3: P1 收到 COMMIT 并提交
  T4: P2 没有收到 COMMIT，处于 READY 状态，持有锁
  T5: P2 永远等待...（阻塞）

解决方案: 3PC、Paxos-based commit
```

---

## Three-Phase Commit (3PC)

### 设计动机

2PC 的根本问题是：协调者崩溃时，参与者不知道是提交还是回滚。3PC 通过引入 **预提交（PRECOMMIT）** 阶段来解决此问题。

### 协议流程

```
Phase 1 — CanCommit:
  协调者 → 所有参与者: "Can you commit?"
  所有参与者 → 协调者: YES/NO

Phase 2 — PreCommit:
  如果全部 YES:
    协调者 → 所有参与者: "PreCommit" (准备提交，但不执行)
    参与者 → 协调者: ACK
  如果任一 NO:
    协调者 → 所有参与者: "ABORT"

Phase 3 — DoCommit:
  协调者 → 所有参与者: "COMMIT"
  参与者 → 协调者: ACK
```

### 超时处理（关键改进）

```
参与者超时处理:
  在 PREPARE 阶段超时 → ABORT（安全：协调者还未决定）
  在 PRECOMMIT 阶段超时 → COMMIT（安全：其他参与者已确认可以提交）
  在 COMMIT 阶段超时 → 等待恢复

  2PC 中：在 READY 阶段超时 → 阻塞等待协调者
  3PC 中：在 PRECOMMIT 阶段超时 → 自动提交（不需要协调者）
```

### 3PC vs 2PC 对比

| 方面 | 2PC | 3PC |
|------|-----|-----|
| 轮次 | 2 轮（PREPARE + COMMIT） | 3 轮（PREPARE + PRECOMMIT + COMMIT） |
| 阻塞时间 | READY → COMMIT 全阻塞 | PRECOMMIT → COMMIT 非阻塞 |
| 协调者故障 | 参与者阻塞 | 参与者自动提交 |
| 延迟 | 2 RTT | 3 RTT (+50% 延迟) |
| 网络开销 | 4N 条消息 | 6N 条消息 |
| 实现复杂度 | 简单 | 中等 |

### 3PC 的局限性

1. **网络分区问题**：CAP 定理下，网络分区时 3PC 无法同时保证可用性和一致性
2. **额外延迟**：多一轮通信代价在低延迟环境中尤为明显
3. **部分提交风险**：PRECOMMIT 超时自动提交可能导致与 ABORTED 节点不一致
4. **Keidar-Dolev 结论**：任何原子提交协议在存在网络故障时都有可能阻塞

---

## Saga 模式

### 概念起源

Saga 由 Hector Garcia-Molina 和 Kenneth Salem 于 1987 年提出，将长事务（LLT）分解为可交错的短事务序列。

### 核心模型

```
Saga = T1, T2, T3, ..., Tn  // 正向步骤
       C1, C2, C3, ..., Cn  // 对应补偿步骤

执行:
  成功: T1 → T2 → T3 → ... → Tn (全部成功)
  失败: T1 → T2 → T3(fail)
               ↓
        C2 → C1 (逆序补偿)
```

### 两种协调模式

#### 1. 编排模式（Choreography）

```
服务 A ──事件──→ 服务 B ──事件──→ 服务 C
  ↑                                  |
  └────── 补偿事件 ←───────── 补偿事件 ← (失败)
```

- 优点：去中心化，松耦合
- 缺点：流程复杂时难以理解和维护

#### 2. 编排器模式（Orchestration）

```
            Saga Orchestrator
           /        |        \
         指令       指令       指令
         ↓         ↓         ↓
      服务 A     服务 B     服务 C
```

- 优点：集中控制，流程清晰
- 缺点：编排器成为单点

### 补偿策略

| 策略 | 描述 | 示例 |
|------|------|------|
| **逆向操作** | 执行与正向相反的操作 | 取消已创建的订单 |
| **补偿交易** | 独立的补偿事务 | 退款操作 |
| **状态标记** | 标记记录为已取消 | 订单状态 = CANCELLED |
| **重试直到成功** | 对可重试步骤持续重试 | 发送通知邮件 |

### Saga 适用场景

1. **微服务**：跨多个独立服务的业务流程
2. **长事务**：人工审批流程、供应链管理
3. **异构系统**：不同数据库（MySQL + MongoDB + Redis）
4. **高吞吐量**：避免长期锁定资源

### Saga 局限性

1. **隔离性缺失**：并发 Saga 之间可能看到中间状态（脏读）
2. **补偿复杂性**：某些操作难以设计补偿（如发送邮件）
3. **无自动回滚**：需要显式编写补偿逻辑
4. **最终一致性**：不保证实时一致性

---

## Try-Confirm-Cancel (TCC)

### 三阶段语义

```
         TRY 阶段
        (资源预留)
        /         \
   全部成功      有失败
      /            \
  CONFIRM         CANCEL
 (确认操作)      (释放资源)
```

### 详细语义

| 操作 | 前置条件 | 行为 | 幂等性 |
|------|----------|------|--------|
| **TRY** | 无 | 预留资源（冻结库存、预扣额度） | 必须 |
| **CONFIRM** | 所有 TRY 成功 | 执行实际操作（扣库存、转账） | 必须 |
| **CANCEL** | TRY 阶段有失败 | 释放预留资源 | 必须 |

**关键要求**：TRY/CONFIRM/CANCEL 都必须实现**幂等性**，以支持重试。

### 实现示例

```c
// 库存资源
bool inventory_try(void *ctx) {
    // UPDATE inventory SET frozen = frozen + ? WHERE sku = ?
    // 检查可用库存 >= 冻结量
    return freeze_success;
}

bool inventory_confirm(void *ctx) {
    // UPDATE inventory SET quantity = quantity - frozen, frozen = 0
    return confirm_success;
}

bool inventory_cancel(void *ctx) {
    // UPDATE inventory SET frozen = 0
    return cancel_success;
}
```

### TCC vs 2PC 详细对比

| 维度 | 2PC | TCC |
|------|-----|-----|
| **抽象层次** | 基础设施层 | 业务应用层 |
| **资源锁定** | 整个 2PC 期间 | 仅 TRY 期间保留（业务隔离） |
| **对资源的侵入性** | 低（数据库原生支持） | 高（需实现业务接口） |
| **事务隔离级别** | 可串行化（强隔离） | 应用级隔离（可能有脏读） |
| **吞吐量** | 低（锁竞争） | 高（业务层并行） |
| **开发成本** | 低 | 高（3 个接口 + 幂等） |
| **空回滚** | 不需要处理 | 需处理（CANCEL 被 TRY 前调用） |
| **悬挂** | 不适用 | 需处理（CANCEL 后 TRY 才到达） |

### 异常场景处理

**空回滚（Empty Rollback）**：
```
TRY 超时 → 调用 CANCEL（释放未预留的资源）
CANCEL 必须支持"资源未预留"的情况
```

**悬挂（Suspension）**：
```
TRY 因网络延迟 → CANCEL 先到达释放资源 → TRY 后到达预留资源
TRY 必须检查是否已被 CANCEL
```

**幂等控制**：
```
同一 TRY 请求多次 → 只执行一次资源预留
通过全局唯一事务 ID + 状态机实现
```

---

## 协议对比矩阵

### 综合对比

| 特性 | 2PC | 3PC | Saga | TCC |
|------|-----|-----|------|-----|
| **一致性模型** | 强一致 | 强一致 | 最终一致 | 强一致 |
| **原子性** | 全局原子 | 全局原子 | 局部原子 | 全局原子 |
| **隔离性** | 串行化 | 串行化 | 无（脏读可能） | 应用级隔离 |
| **持久性** | 保证 | 保证 | 保证 | 保证 |
| **响应延迟** | 高（同步等） | 很高（3轮） | 低（异步） | 中 |
| **锁竞争** | 高 | 高 | 无 | 低 |
| **协调者故障** | 阻塞 | 不阻塞 | 可恢复 | 可恢复 |
| **参与者故障** | 回滚 | 回滚 | 补偿 | 取消 |
| **实现难度** | 低 | 中 | 中 | 高 |
| **业务侵入性** | 低 | 低 | 高 | 高 |
| **适用事务时长** | 毫秒~秒 | 毫秒~秒 | 秒~天 | 秒~分钟 |

### 选型指南

```
需要强一致性 + 短事务 + 数据库支持?
  → 2PC (XA Transaction)

需要强一致性 + 短事务 + 不能阻塞?
  → 3PC (需评估额外延迟成本)

微服务架构 + 长事务 + 高吞吐?
  → Saga (事件驱动或编排器)

业务层事务 + 性能敏感 + 可投入开发?
  → TCC (如 Seata TCC 模式)
```

---

## 故障模型分析

### 2PC 故障矩阵

| 故障点 | 阶段 | 影响 | 恢复 |
|--------|------|------|------|
| 协调者 | PREPARE 前 | 事务未开始 | 重试 |
| 协调者 | PREPARE 后 | 参与者阻塞 | 参与者超时 ABORT |
| 协调者 | COMMIT 中 | 部分参与者提交 | 需要手工介入或 3PC |
| 参与者 | PREPARE 前 | 协调者超时 | ABORT 事务 |
| 参与者 | READY 后 | 协调者等待 | 协调者决定 COMMIT/ABORT |

### Paxos Commit 简介

Google Spanner 使用基于 Paxos 的提交协议解决 2PC 的协调者单点问题：

- 每个分片是一个 Paxos 组
- 协调者是 Paxos Leader（可通过选举恢复）
- 提交日志通过 Paxos 复制到多数派
- 协调者故障时，新 Leader 可以从日志恢复并继续

---

## 性能权衡

### 延迟 vs 一致性

```
高一致性
    ↑
    |  2PC/3PC
    |     ×
    |    TCC
    |     ×
    |         
    |            Saga ×
    |
    └──────────────────→ 低延迟
```

### 吞吐量对比

```
Saga:   ████████████████████████████  (最高)
TCC:    █████████████████████         (高)
3PC:    ████████                      (低)
2PC:    ██████                        (最低)
```

---

## 实际应用案例

| 系统/公司 | 协议 | 场景 |
|-----------|------|------|
| Google Spanner | Paxos + 2PC | 全球分布式数据库 |
| Google Percolator | 2PC (无协调者) | 增量索引更新 |
| Seata (Alibaba) | AT / TCC / Saga / XA | 微服务事务框架 |
| AWS Step Functions | Saga | 无服务器工作流 |
| Uber | Saga (Cadence) | 微服务编排 |
| Netflix Conductor | Saga | 微服务工作流引擎 |

---

## 进一步阅读

- Garcia-Molina, H., Salem, K. (1987) "Sagas", ACM SIGMOD
- Skeen, D. (1981) "Nonblocking Commit Protocols", ACM SIGMOD
- Bernstein, P.A., Hadzilacos, V., Goodman, N. (1987) "Concurrency Control and Recovery in Database Systems"
- Corbett, J.C. et al. (2012) "Spanner: Google's Globally-Distributed Database", OSDI
- Peng, D., Dabek, F. (2010) "Percolator", OSDI
- CMU 15-721 (2023) Lecture 14-16 "Distributed Transactions"
