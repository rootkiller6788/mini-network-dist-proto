# 共识协议对比 — Raft vs Paxos vs Zab vs VR

## 目录

1. [概述](#概述)
2. [Raft](#raft)
3. [Multi-Paxos](#multi-paxos)
4. [Zab (ZooKeeper Atomic Broadcast)](#zab)
5. [Viewstamped Replication (VR)](#viewstamped-replication)
6. [全面对比](#全面对比)
7. [选择指南](#选择指南)

---

## 概述

分布式共识是使一组节点就某个值达成一致的协议。所有共识协议必须满足：

- **Safety（安全性）**：任何两个已决定的值不能不同
- **Liveness（活性）**：最终会达成决定（在有足够多数节点存活时）
- **FLP 不可能性**：异步网络中，确定性共识无法同时保证安全和活性
  → 所有实用协议依赖随机化（部分同步模型）

---

## Raft

### 设计哲学

Raft 为 **可理解性** 而设计。它将共识分解为三个独立子问题：
1. **Leader Election**：任期 (term) + 随机超时
2. **Log Replication**：AppendEntries + 一致性检查
3. **Safety**：选举限制 + 提交规则

### 核心特性

| 特性 | 描述 |
|-----|------|
| 领导结构 | 强 Leader（所有日志从 Leader 到 Follower） |
| 成员变更 | Joint Consensus（两阶段） |
| 日志流向 | 单向：Leader → Follower |
| 恢复机制 | Snapshot (可选) |
| 复杂度 | 相对简单（Paxos 的 1/3 代码量） |

### 优势

- **可理解性**：设计目标，适合教学
- **实现简单**：状态空间小，逻辑清晰
- **工程成熟**：etcd, Consul, CockroachDB, TiKV
- **日志单向流**：Leader 不需要从 Follower 学习

### 劣势

- **单 Leader 瓶颈**：所有写入通过 Leader
- **不适合广域网**：Leader 必须是网络延迟最低的节点
- **只读优化复杂**：需要 ReadIndex 或 Lease

---

## Multi-Paxos

### 设计哲学

Basic Paxos 是共识的理论基础，Multi-Paxos 将 Leader 优化融合进来，
形成实用的多实例共识协议。

### 两阶段

```
阶段 1（Leader Election）：
  Leader → Acceptors: Prepare(n)
  Acceptors → Leader:  Promise(n, 之前已接受的值)

阶段 2（Log Replication）：
  Leader → Acceptors: Accept(n, value)
  Acceptors → Learners: Accepted(n, value)
```

Multi-Paxos 优化：Leader 选举后跳过阶段 1，直接进入阶段 2。

### 核心特性

| 特性 | 描述 |
|-----|------|
| 领导结构 | 弱 Leader（理论上任何人都可以提案） |
| 成员变更 | 无标准方法（各实现自制） |
| 日志流向 | 双向（Leader 可能在 Phase 1 学习旧值） |
| 恢复机制 | Multi-Paxos 无标准恢复 |
| 复杂度 | 高（多种状态和角色交织） |

### 优势

- **理论基础强大**：被形式化验证
- **灵活性**：可适应多种拓扑
- **学术影响力**：几乎所有共识协议都从中衍生

### 劣势

- **缺少标准实现**：各实现差异大
- **实现困难**：角落案例多
- **Leader 选举混乱**：无明确 Leader 选举协议

---

## Zab (ZooKeeper Atomic Broadcast)

### 设计哲学

Zab 是专为 ZooKeeper 设计的原子广播协议，强调 **FIFO 顺序** 和
**主从架构**。与 Paxos 不同，Zab 使用类似 TCP 的序列号机制。

### 核心特性

| 特性 | 描述 |
|-----|------|
| 领导结构 | 强 Leader（Primary） |
| 成员变更 | 固定集群（不允许动态变更） |
| 日志流向 | 单向：Leader → Follower |
| 恢复机制 | 明确的两阶段恢复 |
| 顺序保证 | FIFO + 因果顺序 |

### Zab 协议阶段

```
阶段 1（Discovery）：
  新 Leader 提议 epoch，收集 Follower 的确认

阶段 2（Synchronization）：
  Leader 将缺失的事务发送给 Follower

阶段 3（Broadcast）：
  Leader 广播新事务，Follower 确认
```

### 与 Raft 的相似点

| Zab 概念 | Raft 等价 |
|---------|----------|
| Epoch (zxid high 32) | Term |
| Counter (zxid low 32) | Log Index |
| ACK from quorum | AppendEntries majority |
| CEPOCH proposal | New leader elected |

### 优势

- **高效**：专为 ZooKeeper 的工作负载优化
- **顺序保证**：强 FIFO 顺序
- **生产验证**：数千 ZooKeeper 集群运行中

### 劣势

- **耦合 ZooKeeper**：难以独立使用
- **无动态成员**：不支持运行时增删节点
- **代码复杂**：比 Raft 更复杂的实现

---

## Viewstamped Replication (VR)

### 设计哲学

VR 是最早的复制状态机协议之一（1988），早于 Paxos，最近重新引起兴趣。
VR 使用 **View** 概念组织协议阶段。

### 三种子协议

1. **Normal Operation**：Primary 处理请求，复制到 Backup
2. **View Change**：选举新的 Primary
3. **Recovery**：故障节点恢复后追赶上最新状态

### 核心特性

| 特性 | 描述 |
|-----|------|
| 领导结构 | 强 Leader (Primary) |
| 成员变更 | View Change 协议 |
| 日志流向 | 单向：Primary → Backup |
| 恢复机制 | Recovery 协议 |
| View 概念 | 类似 Raft term |

### 优势

- **悠久历史**：经过时间考验
- **协议简单**：与 Raft 同级可理解性
- **完整恢复**：明确的故障恢复机制

### 劣势

- **文档较少**：知名度低于 Raft/Paxos
- **实现较少**：没有广泛使用的开源实现
- **社区小**：缺乏工具和测试框架

---

## 全面对比

### 属性对比表

| 属性 | Raft | Multi-Paxos | Zab | VR |
|-----|------|------------|-----|-----|
| 诞生时间 | 2014 | 1998/2001 | 2010 | 1988 |
| 可理解性 | ★★★★★ | ★★ | ★★★ | ★★★★ |
| Leader 强度 | 强 | 弱 | 强 | 强 |
| 成员变更 | Joint Consensus | 无标准 | 不支持 | View Change |
| 标准化程度 | 高 | 低 | 中 | 中 |
| 开源实现 | etcd/TiKV/Consul | 多种变体 | ZooKeeper | 少数 |
| 论文澄清 | 完整博士论文 | 两篇论文 | 一篇论文 | 一篇论文 |
| 测试框架 | Jepsen/PORs | 无标准 | Jepsen | 无 |
| 日志流向 | 单向 | 双向 | 单向 | 单向 |
| 快照机制 | 有 | 无标准 | 有 | 有 |

### 性能对比

| 指标 | Raft | Multi-Paxos | Zab |
|-----|------|------------|-----|
| 写延迟 (3 节点) | ~2 RTT | ~2 RTT | ~2 RTT |
| 读延迟 (线性) | ~1 RTT | ~1 RTT | ~1 RTT |
| 吞吐量限制 | Leader 带宽 | Leader 带宽 | Leader 带宽 |
| 故障恢复时间 | ~选举超时 | ~选举超时 | ~election epoch |

### 复杂度对比

```
实现代码行数（典型 Go 实现）：
  Raft:          ~2,000 行
  VR:            ~3,000 行
  Multi-Paxos:   ~6,000 行
  Zab:           ~10,000 行（ZooKeeper 全栈）
```

---

## 选择指南

### 选择 Raft 的场景

- 学习和教学（本项目的选择）
- 新建分布式系统
- 需要社区支持和工具
- 团队规模小，需要快速理解实现

### 选择 Multi-Paxos 的场景

- 需要多 Leader 写入（EPaxos 变体）
- 学术研究
- 理论基础重要的场景

### 选择 Zab 的场景

- 需要层级命名空间（ZooKeeper 数据模型）
- 需要强顺序保证
- 已有 ZooKeeper 依赖

### 选择 Viewstamped Replication 的场景

- 追求协议简洁性
- 学术对比研究
- 完整的故障恢复需求

---

## 参考文献

1. Ongaro & Ousterhout (2014). "In Search of an Understandable Consensus Algorithm." USENIX ATC.
2. Lamport (2001). "Paxos Made Simple." ACM SIGACT News.
3. Junqueira et al. (2011). "Zab: High-performance broadcast for primary-backup systems." DSN.
4. Liskov & Cowling (2012). "Viewstamped Replication Revisited." MIT Tech Report.
5. Howard (2019). "Distributed consensus revised." PhD Thesis, Cambridge.
6. Van Renesse et al. (2015). "Vive La Difference: Paxos vs. Viewstamped Replication vs. Zab." IEEE TDSC.
