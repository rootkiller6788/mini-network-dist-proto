# mini-raft — Raft Consensus Deep Dive

> 基于 Ongaro (2014) "In Search of an Understandable Consensus Algorithm"

## 概述

Raft 是一种为可理解性而设计的共识算法，将共识分解为三个独立子问题：
领导选举、日志复制和安全性保证。

## 一、领导选举 (Leader Election)

### 1.1 Term (任期)

每个 Raft 节点维护一个单调递增的 `current_term`，充当逻辑时钟。
Term 在以下情况递增：

- 节点成为 Candidate 时：`current_term++`
- 收到更高 term 的消息时：更新自身 term 并转为 Follower

> **关键规则**：如果收到 RPC 的 term 小于当前 term，拒绝该请求。
> 这防止了过期消息干扰集群状态。

### 1.2 状态转换

```
          选举超时              收到更高term
  FOLLOWER ------> CANDIDATE ----------> FOLLOWER
     ^                 |                     ^
     |   赢得选举      |                     |
     +-----------------+      分割投票       |
     |                                     |
     +<---- 收到更高term的AppendEntries ----+
     |
  LEADER (心跳)
```

### 1.3 随机化超时 (Randomized Timeout)

**范围**：150ms ~ 300ms（本实现默认值）

```c
uint64_t raft_random_timeout(void) {
    return RAFT_ELECTION_TIMEOUT_MIN_MS +
           (rand() % (RAFT_ELECTION_TIMEOUT_MAX_MS - 
            RAFT_ELECTION_TIMEOUT_MIN_MS + 1));
}
```

**原理**：
- 如果所有节点使用相同的超时时间，多个节点可能同时超时，
  导致分割投票 (split vote)，无法选出 Leader。
- 随机化确保大概率只有一个节点超时并发起选举。
- 超时时间的下限必须大于心跳间隔，防止不必要的选举。

### 1.4 投票规则 (RequestVote)

Candidate 向所有其他节点发送 `RequestVote` RPC：

```c
typedef struct {
    uint64_t term;
    int      candidate_id;
    uint64_t last_log_index;
    uint64_t last_log_term;
} RequestVoteRPC;
```

接收方按以下条件决定是否投票：
1. `rpc.term < current_term` → 拒绝（过期请求）
2. `rpc.term > current_term` → 更新 term，转为 Follower
3. `voted_for == -1 || voted_for == candidate_id` → 可以投票
4. **日志比较**：Candidate 的日志至少和自己一样新

   ```
   (rpc.last_log_term > my_last_term) ||
   (rpc.last_log_term == my_last_term && 
    rpc.last_log_index >= my_last_index)
   ```

> **安全性**：第 4 条是 Raft 的 **Election Restriction**，
> 确保只有拥有所有已提交日志的节点才能成为 Leader。
> 这避免了已提交的日志被新 Leader 覆盖。

### 1.5 选举流程

1. Follower 选举超时 → 自增 term，投票给自己，转为 Candidate
2. 并行发送 `RequestVote` 给所有节点
3. 获得多数票 → 转为 Leader，立即发送心跳
4. 收到 `AppendEntries`（更高 term）→ 转为 Follower
5. 选举超时再次触发 → 重启选举

### 1.6 Quorum 计算

```c
int raft_quorum_size(int cluster_size) {
    return (cluster_size / 2) + 1;
}
```

| 集群大小 | Quorum | 容错数 |
|---------|--------|-------|
| 3       | 2      | 1     |
| 5       | 3      | 2     |
| 7       | 4      | 3     |

## 二、日志复制 (Log Replication)

### 2.1 LogEntry 结构

```c
typedef struct {
    uint64_t term;
    int      command;
} LogEntry;
```

每个日志条目包含：
- **term**：创建该条目的任期号
- **command**：状态机命令

日志索引从 1 开始（索引 0 为虚拟占位）。

### 2.2 AppendEntries RPC

Leader 通过 `AppendEntries` RPC 实现：
1. **日志复制**：将新条目复制到所有 Follower
2. **心跳**：`entries_count == 0` 时为空心跳，防止 Follower 超时

```c
typedef struct {
    uint64_t term;
    int      leader_id;
    uint64_t prev_log_index;   // 新条目之前的位置
    uint64_t prev_log_term;     // 该位置的 term（一致性检查）
    LogEntry entries[128];      // 要复制的条目（可能为空）
    int      entries_count;
    uint64_t leader_commit;     // Leader 的 commit_index
} AppendEntriesRPC;
```

### 2.3 日志一致性检查

Follower 收到 `AppendEntries` 时执行一致性检查：

```c
static bool raft_log_ok(const RaftNode *node, 
                         uint64_t prev_log_index,
                         uint64_t prev_log_term) {
    if (prev_log_index == 0) return true;
    if (prev_log_index > node->log_count) return false;
    return node->log[prev_log_index - 1].term == prev_log_term;
}
```

- 如果 `prev_log_index` 处日志条目的 term 与 `prev_log_term` 匹配 → 成功
- 如果不匹配 → 拒绝，Leader 递减 `next_index` 重试

> 这种 **试探性回退** 策略保证了最终一致性：Leader 逐步回退直到
> 找到与 Follower 日志匹配的位置，然后从此处复制缺失的条目。

### 2.4 提交 (Commit)

只有当一条日志被复制到 **多数节点** 后，Leader 才能提交它：

```c
for (uint64_t n = last_log_index; n > commit_index; n--) {
    if (log[n-1].term != current_term) continue;
    int matched = 1;
    for (int i = 0; i < cluster_size; i++) {
        if (i == node->id || !nodes[i].active) continue;
        if (match_index[i] >= n) matched++;
    }
    if (matched >= quorum) {
        commit_index = n;
        break;
    }
}
```

> **关键限制**：Leader 只能提交 **自己任期内的日志**。
> 这防止了 Figure 8 中描述的已提交日志被覆盖问题。

### 2.5 日志复制流程图

```
Leader                          Follower A              Follower B
  |                                |                       |
  |--AppendEntries(prev=3,t=2)--->|                       |
  |                                |--ok, append entries-->|
  |--AppendEntries(prev=3,t=2)----|---------------------->|
  |                                |                       |--ok
  |                                |                       |
  |  收到多数确认 → commit_index=4|                       |
  |                                |                       |
  |--AppendEntries(commit=4)------>|--commit entries 1-4-->|
  |--AppendEntries(commit=4)---------------------------->|--commit 1-4
```

## 三、安全性 (Safety)

### 3.1 Election Restriction

确保 Leader 拥有所有已提交的日志条目：
- Follower 只投票给日志至少和自己一样新的 Candidate
- 新 Leader 永远不会覆盖已提交日志

### 3.2 Committing Previous Terms

- Leader 只能通过复制自己任期内的日志来 **间接** 提交之前任期的日志
- 这条规则消除了 Figure 8 中的反例

### 3.3 集群成员变更

Raft 使用 **Joint Consensus** 进行安全的成员变更（本简化实现中未包含）。

### 3.4 快照 (Snapshot)

当日志过长时，创建快照压缩日志（本实现中未包含）。

## 四、实现要点

### 4.1 核心数据结构

```c
typedef struct {
    int         id;
    RaftState   state;           // FOLLOWER/CANDIDATE/LEADER
    uint64_t    current_term;
    int         voted_for;
    LogEntry    log[MAX_LOG];
    int         log_count;
    uint64_t    commit_index;
    uint64_t    last_applied;
    uint64_t    next_index[MAX_NODES];   // Leader 专用
    uint64_t    match_index[MAX_NODES];  // Leader 专用
    // ... 定时器、配置等
} RaftNode;
```

### 4.2 定时器管理

三种定时器驱动 Raft 状态机：
1. **选举超时** (150-300ms)：Follower/Candidate 驱动
2. **心跳间隔** (50ms)：Leader 驱动
3. **重试超时**：AppendEntries 失败时回退

### 4.3 本实现简化

与完整 Raft 实现的区别：
- 单线程模拟（无真正网络 RPC）
- 无持久化存储
- 无快照机制
- 无成员变更 (Joint Consensus)
- 简化的状态机（仅整型命令）

## 五、参考资料

- Ongaro, D. and Ousterhout, J. (2014). "In Search of an Understandable Consensus Algorithm." USENIX ATC.
- MIT 6.824: Distributed Systems (Lab 2: Raft)
- Raft Visualization: https://raft.github.io/
