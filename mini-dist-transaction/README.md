# Mini Distributed Transactions — 分布式事务 (C 语言实现)

> 参考 CMU 15-721 Advanced Database Systems, Google Percolator, Google Spanner, Seata

## 概述

mini-dist-transaction 是一个用 C99 实现的分布式事务库，涵盖主流分布式事务协议：

| 模块 | 头文件 | 核心协议 |
|------|--------|----------|
| **Two-Phase Commit** | `two_pc.h` | 2PC + 3PC 原子提交协议 |
| **Saga** | `saga.h` | 长事务补偿模式 |
| **Try-Confirm-Cancel** | `tcc.h` | 资源预留/确认/取消 |
| **Distributed Lock** | `dist_lock.h` | 分布式锁 + Redlock |
| **Idempotency** | `idempotency.h` | 幂等性 + 指数退避重试 |

## 快速开始

```bash
make all
./bin/two_pc_demo
./bin/saga_demo
./bin/dist_lock_demo
```

## 模块详解

### 1. Two-Phase Commit（两阶段提交）

**核心思想**：协调者向所有参与者发送 PREPARE，收集投票，全部 YES 则 COMMIT，任一 NO 则 ABORT。

```
Phase 1 (PREPARE):
  Coordinator → Participants: "Can you commit?"
  Participants → Coordinator: YES/NO (write undo+redo logs)

Phase 2 (COMMIT/ABORT):
  If all YES: Coordinator → Participants: "COMMIT"
  If any NO:  Coordinator → Participants: "ABORT"
```

**缺陷**：
- 协调者单点故障：协调者在 COMMIT 阶段崩溃，参与者阻塞等待
- 同步阻塞：所有参与者必须等待最慢的节点

**Three-Phase Commit（3PC）改进**：
- 引入 PRECOMMIT 阶段，协调者故障后参与者可以超时自动提交
- 代价：额外一轮通信，延迟增加 50%

```c
TPCCoordinator coord;
tpc_coordinator_init(&coord, txn_id);
tpc_coordinator_add_participant(&coord, &participant);
bool ok = tpc_coordinator_prepare(&coord) && tpc_coordinator_commit(&coord);
```

### 2. Saga（长事务）

**核心思想**：将长事务拆分为多个本地事务的序列，每个步骤有对应的补偿操作。

如果第 k 步失败，则对 1..k-1 步按逆序执行补偿操作。

```
T1 → T2 → T3(fail)
      ↓
C2 → C1  (reverse compensation)
```

**适用场景**：
- 微服务架构中的跨服务事务
- 长时间运行的业务流程（如旅行预订）
- 无法使用 ACID 事务的异构系统

**示例：预订酒店 → 预订机票 → 支付 → 如果支付失败，取消机票，取消酒店**

```c
SagaTransaction txn;
saga_transaction_init(&txn, txn_id);
saga_transaction_add_step(&txn, &book_hotel_step);
saga_transaction_add_step(&txn, &book_flight_step);
saga_transaction_add_step(&txn, &charge_payment_step);
saga_execute(&txn, contexts);
```

### 3. Try-Confirm-Cancel（TCC）

**核心思想**：两阶段资源预约模式，资源提供方需实现三个接口。

| 阶段 | 操作 | 说明 |
|------|------|------|
| TRY | 预留资源 | 冻结库存、预占额度，不执行实际变更 |
| CONFIRM | 确认操作 | 所有 TRY 成功后，执行实际业务操作 |
| CANCEL | 取消操作 | 任一 TRY 失败后，释放已预留的资源 |

**TCC vs 2PC**：

| 特性 | 2PC | TCC |
|------|-----|-----|
| 层面 | 数据库/资源层 | 业务/应用层 |
| 侵入性 | 低（数据库支持） | 高（需实现3个接口） |
| 锁持有 | 整个事务期间 | 仅在 TRY 阶段 |
| 性能 | 较差（资源锁定） | 较好（业务层面隔离） |

```c
TCCTransaction txn;
tcc_transaction_init(&txn, "order-create");
tcc_execute(&txn, contexts);
```

### 4. Distributed Lock（分布式锁）

**核心特性**：
- 基于租约（lease）的锁机制
- 心跳续约防止锁过期
- 自动过期处理
- Redlock 算法：在 N 个独立节点上获取多数锁

**Redlock 算法步骤**：
1. 获取当前时间戳
2. 依次尝试在 N 个节点上获取锁（SET NX EX）
3. 计算获取锁的总耗时
4. 若获取了多数锁（N/2+1）且总耗时 < 锁有效期，则成功
5. 若失败，在所有节点上释放锁

**对比其他方案**：
- **ZooKeeper**：临时顺序节点 + Watch 机制。优点：自动释放，公平队列。缺点：依赖 ZK 集群。
- **etcd**：租约 + CAS。优点：强一致性 Raft 协议。缺点：性能低于 Redis。
- **Fencing Token**：每次获取锁时返回单调递增 token，防止脑裂写入。

```c
LockManager lm;
lock_manager_init(&lm);
lock_acquire(&lm, "resource", "owner", lease_ms, now_ms);
lock_renew_lease(&lm, "resource", "owner", extend_ms, now_ms);
lock_release(&lm, "resource", "owner");
```

### 5. Idempotency（幂等性）

**核心概念**：同一请求重复执行产生相同结果。

**实现机制**：
- 请求去重缓存（LRU 淘汰）
- 请求哈希指纹识别
- 指数退避重试 + 抖动

**重试策略**：
```
attempt 0:         immediate
attempt 1: 100ms + jitter
attempt 2: 200ms + jitter
attempt 3: 400ms + jitter
attempt 4: 800ms + jitter
...
max: 30s
```

```c
IdempotentStore store;
idempotent_store_init(&store);
IdempotentStatus st = idempotent_check(&store, req_id, ...);
if (st != REQ_PROCESSED) { /* do work */ idempotent_record(...); }
```

## 协议对比总结

| 协议 | 一致性 | 性能 | 适用场景 | 复杂度 |
|------|--------|------|----------|--------|
| 2PC | 强一致性 | 低 | 短事务、同构数据库 | 低 |
| 3PC | 强一致性 | 较低 | 需要非阻塞的短事务 | 中 |
| Saga | 最终一致性 | 高 | 长事务、微服务 | 中 |
| TCC | 强一致性 | 中 | 业务层分布式事务 | 高 |

## 构建

```bash
make all        # 构建所有演示程序
make clean      # 清理构建产物
```

## 许可

参考实现，仅供学习用途。

## 参考文献

- Mohan, C., Lindsay, B., Obermarck, R. (1986) "Transaction Management in the R* Distributed Database Management System"
- Skeen, D. (1981) "Nonblocking Commit Protocols"
- Garcia-Molina, H., Salem, K. (1987) "Sagas"
- Peng, D., Dabek, F. (2010) "Large-scale Incremental Processing Using Distributed Transactions and Notifications" (Percolator)
- Corbett, J.C. et al. (2012) "Spanner: Google's Globally-Distributed Database"
- CMU 15-721 (Spring 2023) "Advanced Database Systems"
