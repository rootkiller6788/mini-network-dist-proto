# mini-gossip — Gossip Protocols Deep Dive

> 基于 Demers et al. (1987), SWIM (Gupta 2002), CRDT Gossip

## 一、Gossip 协议概述

Gossip（流言）协议是一类基于随机对等交换的信息传播协议，
灵感来源于传染病传播模型。其核心思想是：每个节点周期性地
随机选择其他节点交换信息，最终全系统达成一致。

### 1.1 生物学类比

| 概念        | 传染病模型     | Gossip 协议          |
|------------|--------------|---------------------|
| 感染者      | Infected      | 持有新数据的节点      |
| 易感者      | Susceptible   | 尚未收到新数据的节点   |
| 恢复者      | Recovered     | 已传播过数据的节点    |
| 传播率      | R0            | Fanout (扇出因子)    |
| 群体免疫    | Herd immunity | 收敛阈值             |

### 1.2 关键属性

- **最终一致性** (Eventual Consistency)：如果系统停止更新，
  最终所有节点拥有相同数据集
- **可靠性** (Reliability)：即使部分节点失败，消息仍会传播
- **可扩展性** (Scalability)：每轮每节点 O(fanout) 开销
- **去中心化**：无单点故障，无 Leader 依赖

## 二、传播策略

### 2.1 PUSH（推送）

发送者主动将自己的数据推送给随机选择的对等节点。

```
Node A (有数据 d)                    Node B
     |                                  |
     |------ PUSH {d} ----------------->|
     |                                  | 接收并合并 d
```

- **优点**：新数据快速扩散（类似病毒传播）
- **缺点**：所有节点都有数据后，继续 PUSH 浪费带宽

### 2.2 PULL（拉取）

接收者从随机对等节点拉取数据。

```
Node A (需要数据)                    Node B
     |                                  |
     |------ PULL request ------------->|
     |<----- PUSH response {d} -------|
     | 接收并合并 d                     |
```

- **优点**：适合检测和修复缺失数据
- **缺点**：PULL 请求本身也是网络开销

### 2.3 PUSH-PULL（双向交换）

同时进行 PUSH 和 PULL，在单轮对话中完成双向数据交换。

```
Node A                               Node B
     |------ PUSH {A的数据} --------->|
     |<----- PUSH {B的数据} ---------|
     双方各自合并对方的数据
```

- **最优策略**：结合了两种模式的优点
- **收敛最快**：信息双向流动

### 2.4 传播效率比较

| 策略      | 收敛轮次 | 消息复杂度/轮 | 冗余度 |
|----------|---------|-------------|-------|
| PUSH     | O(log n) | O(n*fanout) | 高    |
| PULL     | O(log n) | O(n*fanout) | 低    |
| PUSH-PULL | O(log log n) | O(n*fanout) | 中    |

## 三、反熵 (Anti-Entropy)

### 3.1 概念

反熵是 Gossip 协议用于检测和修复副本之间不一致的核心机制。
熵 (entropy) 在此指副本之间数据的差异程度。

### 3.2 版本向量

```c
typedef struct {
    int      key;
    int      value;
    uint64_t version;
} GossipDataEntry;
```

每条数据附带一个单调递增的 **版本号** (version vector)：

- 新写入时：`version = ++node_version_clock`
- 合并时：保留版本号更大的条目 (max version wins)

这本质上是一种 **简化版 CRDT**（Conflict-free Replicated Data Type），
使用 Last-Writer-Wins (LWW) 策略解决冲突。

### 3.3 合并算法

```c
void gossip_on_receive(GossipNode *node, const GossipMessage *msg) {
    for (int i = 0; i < msg->entry_count; i++) {
        const GossipDataEntry *incoming = &msg->data_entries[i];

        for (int j = 0; j < node->data_count; j++) {
            if (node->data[j].key == incoming->key) {
                // 保留版本号更高的
                if (incoming->version > node->data[j].version) {
                    node->data[j] = *incoming;
                }
                found = true;
                break;
            }
        }

        // 新 key：直接添加
        if (!found && node->data_count < GOSSIP_MAX_DATA_KEYS) {
            node->data[node->data_count++] = *incoming;
        }
    }
}
```

## 四、SWIM 协议（扩展）

### 4.1 概述

SWIM (Scalable Weakly-consistent Infection-style Process Group
Membership Protocol) 将 Gossip 思想应用于 **成员检测** 和 **故障检测**。

### 4.2 SWIM 的 Gossip 属性

SWIM 使用 Gossip 风格的信息传播：
- **Piggyback**：将成员状态变化附加到 Ping/Ack 消息上
- **感染式传播**：成员状态变化像病毒一样在集群中传播
- **最终一致性**：所有节点最终对成员列表达成一致

### 4.3 故障检测流程

```
Node A（检测者）                      Node B（被检测者）
     |                                    |
     |------ PING ----------------------->|
     |                                    |
     |     超时 (SWIM_PING_TIMEOUT_MS)    |
     |                                    |
     |------ INDIRECT_PING -> Node C ---- >|
     |------ INDIRECT_PING -> Node D ---- >|
     |                                    |
     |     仍然无响应                       |
     |                                    |
     |  标记为 SUSPECTED                   |
     |                                    |
     |     传播 SUSPECTED 状态             |
     |      (piggyback on pings)          |
     |                                    |
     |     等待 SWIM_SUSPECT_TIMEOUT_MS    |
     |                                    |
     |     其他节点确认故障 → 标记 DEAD     |
```

### 4.4 状态传播

```c
void swim_disseminate(SWIMCluster *cluster) {
    for (int i = 0; i < cluster->member_count; i++) {
        int target = swim_random_member(cluster, i);

        SWIMMessage msg;
        msg.type = SWIM_MSG_PING;
        // Piggyback 所有状态变化
        for (int j = 0; j < cluster->member_count; j++) {
            if (members[j].state == SWIM_SUSPECTED || 
                members[j].state == SWIM_DEAD) {
                msg.membership_changes[msg.changes_count++] = members[j];
            }
        }
        swim_on_receive(cluster, &msg);
    }
}
```

## 五、CRDT 与 Gossip

### 5.1 CRDT 类型

Gossip 协议天然适合传播 CRDT：

| CRDT 类型   | 合并操作          | 示例用途       |
|------------|-------------------|---------------|
| G-Counter  | max()             | 分布式计数      |
| PN-Counter | max(+), max(-)   | 增减计数器      |
| G-Set      | union()           | 集合添加       |
| 2P-Set     | add/remove set    | 集合增删       |
| LWW-Register | max(version)   | 最后写入者胜    |
| OR-Set     | unique tags       | 可观察删除集合  |

### 5.2 LWW-Register 示例

本实现的数据模型就是 LWW-Register：
- 每个 key 对应一个 (value, version) 对
- 合并时取 version 最大者
- 写入时版本号单调递增

```
Node 0: key=1, value=A, version=1
Node 1: key=1, value=B, version=2

合并后 (两者交换数据):
Node 0: key=1, value=B, version=2  (max version wins)
Node 1: key=1, value=B, version=2
```

## 六、Rumor Mongering（谣言传播）

### 6.1 概念

Rumor Mongering（谣言散播）是最早的 Gossip 变体之一。

**流程**：
1. 节点收到新数据（被"感染"）
2. 经过 k 轮 PUSH 后，节点变为"已恢复"（不再传播）
3. 如果接收到已恢复节点发来的数据请求，重新激活

### 6.2 数学模型

对于 n 个节点的网络，fanout=k：
- 期望每轮传播率：R0 ≈ k
- 期望收敛轮次：≈ log_k(n)
- 部分节点未收到消息的概率：≈ n × e^(-k)

### 6.3 本实现中的体现

```c
#define GOSSIP_FANOUT 3

void gossip_spread(GossipNode *nodes, int n, 
                   GossipMessageType strategy) {
    for (int i = 0; i < n; i++) {
        for (int f = 0; f < GOSSIP_FANOUT; f++) {
            int target = gossip_select_peer(&nodes[i]);
            // ... 交换数据
        }
    }
}
```

## 七、拓扑结构的影响

### 7.1 环形拓扑

- 邻居数：2（固定）
- 收敛速度：慢 (O(n) 轮)
- 消息复杂度：O(n) per round
- 容错性：差（切断环需要重建）

### 7.2 全连接拓扑

- 邻居数：n-1
- 收敛速度：快 (O(log n) 轮)
- 消息复杂度：O(n²) per round
- 容错性：好（多条冗余路径）

### 7.3 随机拓扑（default）

- 邻居数：可配置 (degree)
- 收敛速度：较快
- 消息复杂度：O(n * degree)
- 容错性：良好
- 扩展性：好（类似 "小世界" 网络）

## 八、收敛性分析

### 8.1 收敛检测

```c
bool gossip_all_synced(const GossipNode *nodes, int n) {
    int ref = 任何活跃节点;
    for each other node:
        if data_count != ref.data_count → 未同步
        if 任何 key 的 version != ref[key].version → 未同步
    return true; // 全部一致
}
```

### 8.2 收敛时间

| 网络     | 节点数 | Fanout | 理论收敛轮次 | 实测收敛轮次 |
|---------|-------|--------|------------|------------|
| 全连接   | 5     | 3      | ~2         | 1-2       |
| 全连接   | 10    | 3      | ~2         | 2-3       |
| 随机     | 10    | 2      | ~3         | 3-5       |
| 环形     | 10    | 2      | ~5         | 5-8       |

## 九、参考资料

- Demers, A. et al. (1987). "Epidemic Algorithms for Replicated Database Maintenance." ACM PODC.
- Birman, K. (2007). "The promise, and limitations, of gossip protocols." ACM SIGOPS.
- Gupta, I. et al. (2002). "SWIM: Scalable Weakly-consistent Infection-style Process Group Membership Protocol." DSN.
- Almeida, P.S. et al. (2015). "CRDTs: Consistency without concurrency control." INRIA Research Report.
- Jelasity, M. (2011). "Gossip." In Self-Organising Software.
