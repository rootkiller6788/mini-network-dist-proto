# mini-dist-protocol — 分布式协议 (C 语言实现)

> 参考 Raft Paper (Ongaro 2014), Paxos Made Simple (Lamport), SWIM, Gossip Protocols

一套用 ANSI C99 编写的核心分布式协议教学实现，涵盖共识、Gossip 传播、
成员检测和领导选举四大领域。

## 模块总览

| 模块 | 文件 | 协议 | 用途 |
|-----|------|------|-----|
| **Raft** | `include/raft.h` + `src/raft.c` | Raft Consensus | 领导选举 + 日志复制 |
| **Paxos** | `include/paxos.h` + `src/paxos.c` | Multi-Paxos | Basic Paxos + Multi-Paxos |
| **Gossip** | `include/gossip.h` + `src/gossip.c` | Epidemic Protocol | 流言传播 + 反熵 (Anti-Entropy) |
| **SWIM** | `include/swim.h` + `src/swim.c` | SWIM Membership | 故障检测 + 成员管理 |
| **Leader Election** | `include/leader_election.h` + `src/leader_election.c` | Bully / Ring / ZK-style | 三种选举算法对比 |

## 快速开始

### 构建

```bash
make          # 构建所有目标
make raft     # 仅构建 Raft demo
make gossip   # 仅构建 Gossip demo
make leader   # 仅构建 Leader election demo
make clean    # 清理构建产物
```

### 运行

```bash
# Raft 共识 (3 节点集群)
make run-raft

# Gossip 传播 (5 节点反熵)
make run-gossip

# 领导选举算法对比
make run-leader
```

### 依赖

- GCC (支持 C99)
- GNU Make
- libc + libm

## 目录结构

```
mini-dist-protocol/
├── include/
│   ├── raft.h              # Raft 共识协议
│   ├── paxos.h             # Multi-Paxos 协议
│   ├── gossip.h            # Gossip 传播协议
│   ├── swim.h              # SWIM 成员协议
│   └── leader_election.h   # 领导选举算法
├── src/
│   ├── raft.c              # Raft 实现 (250+ 行)
│   ├── paxos.c             # Paxos 实现 (130+ 行)
│   ├── gossip.c            # Gossip 实现 (130+ 行)
│   ├── swim.c              # SWIM 实现 (130+ 行)
│   └── leader_election.c   # 选举实现 (130+ 行)
├── examples/
│   ├── raft_demo.c         # 3 节点 Raft 集群演示
│   ├── gossip_demo.c       # 5 节点 Gossip 收敛演示
│   └── leader_election_demo.c # 三种选举算法对比
├── demos/
│   ├── mini-raft/README.md     # Raft 深入解析
│   └── mini-gossip/README.md   # Gossip 协议深入解析
├── docs/
│   ├── course-alignment.md     # 课程与论文对照
│   └── consensus-protocols.md  # 共识协议全面对比
├── Makefile
└── README.md
```

## Raft 共识协议

实现 Ongaro (2014) Raft 共识算法的核心部分：

- **Leader Election**：随机化选举超时 (150-300ms)，Term 驱动的状态机
- **Log Replication**：AppendEntries RPC，一致性检查，Leader 回退
- **Safety**：选举限制，Leader 只提交自己任期的日志
- **集群规模**：3-5 节点，Quorum = ⌊n/2⌋+1

```c
// 创建 3 节点集群
RaftNode nodes[3];
raft_init_cluster(nodes, 3);

// 驱动状态机
raft_tick(nodes, 3, 10);  // 每 10ms 推进

// 提交命令
raft_submit_command(&nodes[leader], nodes, 42);
```

## Paxos 共识协议

实现 Lamport 的 Multi-Paxos：

- **Basic Paxos**：两阶段 (Prepare/Promise + Accept/Accepted)
- **Multi-Paxos**：Leader 选举后跳过 Phase 1
- **与 Raft 对比**：展示两种共识的关系和差异

## Gossip 传播协议

实现 Demers et al. 的 Epidemic 算法：

- **PUSH/PULL/PUSH-PULL** 三种策略
- **反熵合并**：版本向量 + Last-Writer-Wins
- **拓扑支持**：Ring / Full Mesh / Random
- **收敛分析**：事件一致性模拟

## SWIM 成员协议

实现 Gupta et al. 的 SWIM：

- **直接 Ping**：周期性探测邻居
- **间接 Ping**：通过 k 个对等节点间接探测
- **状态传播**：Piggyback 成员变化于 Ping 消息
- **故障检测**：ALIVE → SUSPECTED → DEAD 状态机

## Leader Election 算法对比

| 算法 | 复杂度 | 消息数 (N=5) | 特点 |
|-----|-------|-------------|------|
| Bully | O(N²) | N² | 最高 ID 获胜 |
| Ring | O(2N) | 2N | Token 沿环传递 |
| ZK-style | O(2N) | 2N | 最低序列号获胜 |

## 设计原则

- **C99 Only**：仅依赖 libc + libm
- **教学优先**：优先可读性而非生产级效率
- **单线程模拟**：无需网络库，适合理解协议核心
- **宏配置**：所有参数通过 `#define` 可调
- **无外部依赖**：自包含，复制即可编译

## 许可证

MIT License — 自由使用、修改、分发

---

## 参考文献

- Ongaro, D. (2014). *Consensus: Bridging Theory and Practice*. PhD Thesis, Stanford.
- Lamport, L. (2001). *Paxos Made Simple*. ACM SIGACT News.
- Gupta, I. et al. (2002). *SWIM: Scalable Weakly-consistent Infection-style Process Group Membership Protocol*.
- Demers, A. et al. (1987). *Epidemic Algorithms for Replicated Database Maintenance*.
- MIT 6.824: Distributed Systems — https://pdos.csail.mit.edu/6.824/
