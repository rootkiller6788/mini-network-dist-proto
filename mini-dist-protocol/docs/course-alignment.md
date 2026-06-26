# 课程对齐 — 分布式协议与经典教材对照

## 一、Raft 论文对照

### Ongaro & Ousterhout (2014)

USENIX ATC '14: "In Search of an Understandable Consensus Algorithm"

| Raft 概念             | 论文章节 | 本实现对应                          |
|----------------------|---------|-----------------------------------|
| Leader Election      | §5.2    | `raft_become_candidate()`, `raft_handle_request_vote()` |
| Log Replication      | §5.3    | `raft_handle_append_entries()`, AppendEntriesRPC |
| Safety               | §5.4    | Election restriction (日志比较)     |
| Follower Crash       | §5.5    | `raft_isolate_node()`, `raft_reconnect_node()` |
| Randomized Timeout   | §5.2    | `raft_random_timeout()` (150-300ms) |
| Terms                | §5.1    | `RaftNode.current_term`            |
| Committed Entries    | §5.3    | `RaftNode.commit_index`            |
| Next/Match Index     | §5.3    | `next_index[]`, `match_index[]`    |

### Figure 8 问题

论文中著名的 Figure 8 反例：已提交条目被覆盖。
本实现通过 **只能提交当前 term 的条目** 规则来防止。

```c
// 只考虑当前 term 的日志条目是否达到多数派
if (log[n-1].term != current_term) continue;
```

### 扩展阅读
- Raft PhD Thesis (Ongaro 2014)
- Raft 可视化: https://raft.github.io/
- Raft 动画: http://thesecretlivesofdata.com/raft/

---

## 二、Paxos 论文对照

### Lamport (1998, 2001)

ACM TOCS: "The Part-Time Parliament" (1998)
SIGACT News: "Paxos Made Simple" (2001)

| Paxos 概念          | 论文对应         | 本实现                            |
|--------------------|----------------|---------------------------------|
| Proposer           | 提案者 (§2.2)   | `PaxosProposer`                 |
| Acceptor           | 接受者 (§2.2)   | `PaxosAcceptor`                 |
| Learner            | 学习者 (§2.2)   | `PaxosLearner`                  |
| Phase 1a (Prepare) | 准备请求        | `paxos_prepare()`               |
| Phase 1b (Promise) | 承诺响应        | `paxos_promise()`               |
| Phase 2a (Accept)  | 接受请求        | `paxos_accept()`                |
| Phase 2b (Accepted)| 已接受响应       | `paxos_accepted()`              |
| Multi-Paxos        | 多实例 Paxos    | `multi_paxos_replicate()`       |
| Leader Election     | 领导选举        | `multi_paxos_become_leader()`   |

### Paxos 与 Raft 的关系

Raft 是 Paxos 的教学友好重构：
- Raft 拆分共识为三部分，Paxos 中三个角色混合
- Raft 的 Leader Election = Paxos 的 Prepare 阶段演变
- Raft 的 Log = Paxos 的实例序列

---

## 三、MIT 6.824 课程对照

MIT 6.824 Distributed Systems (Spring 2020+)

| 6.824 实验 | 对应主题               | 本模块实现            |
|-----------|----------------------|---------------------|
| Lab 1     | MapReduce             | 非本模块范围           |
| Lab 2A    | Raft Leader Election  | `raft.h` + `raft.c` |
| Lab 2B    | Raft Log Replication  | `raft_handle_append_entries()` |
| Lab 2C    | Raft Persistence      | 简化版（无持久化）       |
| Lab 2D    | Raft Log Compaction   | 未实现 (Snapshot)      |
| Lab 3A    | KV Store (Raft-based) | 基于 Raft 构建          |
| Lab 4A    | Sharded KV            | 非本模块范围            |

### Raft Lab 要点

Lab 2 的核心挑战：
1. **定时器管理**：正确实现随机超时
2. **日志一致性**：AppendEntries 的正确性和回溯
3. **选举限制**：避免覆盖已提交日志
4. **并发安全**：正确处理并发的 RPC

本实现简化了并发和持久化，但保留了核心算法逻辑。

---

## 四、Gossip 论文对照

| 论文 | 年份 | 贡献 | 实现对应 |
|-----|------|-----|---------|
| Demers et al. | 1987 | Epidemic algorithms | `gossip_spread()` |
| Birman | 2007 | Gossip limitations | topology selection |
| Gupta et al. | 2002 | SWIM protocol | `swim.h` + `swim.c` |
| Almeida et al. | 2015 | CRDT theory | `GossipDataEntry.version` |
| Jelasity | 2011 | Gossip overview | anti-entropy merge |

---

## 五、Leader Election 论文对照

| 算法 | 来源 | 复杂度 | 对应实现 |
|-----|------|-------|---------|
| Bully | Garcia-Molina (1982) | O(n²) | `bully_election()` |
| Ring | Chang & Roberts (1979) | O(2n) | `ring_election()` |
| ZK | Hunt et al. (2010) | O(2n) | `zk_leader_election()` |

---

## 六、推荐学习路径

1. **入门**：阅读 Raft 论文 §5.1-5.2，运行 `raft_demo`
2. **深入**：阅读 §5.3-5.4 日志复制和安全性，修改参数观察行为
3. **比较**：阅读 "Paxos Made Simple"，运行 `leader_election_demo`
4. **扩展**：学习 Gossip/SWIM，理解最终一致性与强一致性的区别
5. **进阶**：MIT 6.824 Lab 2 完整实现，包括持久化和日志压缩
