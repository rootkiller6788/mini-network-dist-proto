# Distributed Systems Theory — A Primer

> Core concepts in distributed systems: time, ordering, consensus, CAP, Byzantine faults, and CRDTs. With C implementation pointers.

---

## 1. Why Distributed Systems Are Hard

A distributed system is a collection of independent computers that appears to its users as a single coherent system. The key challenges:

1. **Partial failures:** Some parts fail while others continue.
2. **Unreliable networks:** Messages can be delayed, lost, or reordered.
3. **No global clock:** Each node has its own clock with drift.
4. **No shared state:** All coordination must happen via messages.

These challenges lead to fundamental tradeoffs captured by theorems and impossibility results.

---

## 2. The CAP Theorem (Brewer, 2000)

> A distributed data store can provide only **two** of the following three guarantees simultaneously:
> - **C**onsistency: Every read receives the most recent write.
> - **A**vailability: Every request receives a (non-error) response.
> - **P**artition Tolerance: The system continues to operate despite network partitions.

### The Fine Print
- **P is not optional.** You must tolerate partitions in a distributed system.
- The real choice is **CP** (consistent during partition, sacrifice availability) or **AP** (available during partition, sacrifice consistency).
- In practice, this is a **continuous spectrum**, not a binary choice.

### Examples
- **CP systems:** HBase, MongoDB (configurable), Zookeeper, etcd
- **AP systems:** Cassandra, DynamoDB, Riak, CouchDB
- **CA systems:** Single-node databases (no real partitions)

### C Implementation
```c
DistributedStore store;
cap_configure(&store, CP_MODE); // or AP_MODE
cap_create_partition(&store, 0, 1);
cap_write(&store, 0, "data"); // CP rejects, AP accepts
```

---

## 3. Time and Ordering

### The Happens-Before Relation (→)
Defined by Lamport (1978):
1. If `a` and `b` are events in the **same process**, and `a` comes before `b`, then `a → b`.
2. If `a` is the **send** of a message and `b` is the **receive** of that message, then `a → b`.
3. If `a → b` and `b → c`, then `a → c` (transitivity).

### Lamport Clock
- Single counter per process.
- `a → b` ⇒ `L(a) < L(b)`.
- Cannot detect concurrency.

### Vector Clock
- Array of N counters (one per process).
- `a → b` **iff** `VC(a) < VC(b)`.
- Can detect concurrent events (incomparable vectors).

### Hybrid Logical Clock (HLC)
- Combines physical timestamps with logical counters.
- Provides both causal consistency and physical proximity.

### C Implementation
```c
// Lamport
LamportClock lc = {0};
lamport_increment(&lc);
lamport_tick(&lc, received_clock);

// Vector
VectorClock vc;
vector_clock_init(&vc, 3);
vector_clock_increment(&vc, node_id);
VCCompareResult r = vector_clock_compare(&a, &b);

// HLC
HybridLogicalClock hlc = {0, 0};
hlc_tick(&hlc);
```

---

## 4. Consensus and the FLP Impossibility

### The Consensus Problem
N processes must agree on a single value (0 or 1), satisfying:
- **Termination:** Every correct process eventually decides.
- **Agreement:** All correct processes decide the same value.
- **Validity:** The decided value was proposed by some process.

### FLP (Fischer, Lynch, Paterson, 1985)
> In an **asynchronous** system where even **one** process may crash, it is **impossible** to achieve consensus with deterministic protocols.

### Key Insight
The impossibility stems from the fact that in an asynchronous system, you cannot distinguish a crashed process from a slow one. This can lead to endless **bivalent states** where the outcome depends on message ordering.

### Practical Implications
- FLP shows why practical consensus protocols (Paxos, Raft) use:
  - **Randomization** (Ben-Or's algorithm)
  - **Failure detectors** (eventual synchrony)
  - **Quorums and timeouts**

### C Implementation
```c
FLPSystem sys;
int initial_values[] = {0, 0, 1, 0, 1};
flp_init(&sys, 5, initial_values);
int result = flp_run_until_decided(&sys, 100);
if (result < 0) printf("Still bivalent (as FLP predicts)\n");
```

---

## 5. Byzantine Fault Tolerance

### Byzantine Faults (Lamport, Shostak, Pease, 1982)
A Byzantine fault is an arbitrary failure where a node can behave arbitrarily — sending contradictory messages, lying, or crashing.

### The Byzantine Generals Problem
N generals must agree on whether to attack or retreat. Some generals are **traitors** who try to prevent consensus.

### Key Results
- **With oral messages:** Consensus requires `N > 3f` where `f` is the number of faulty nodes.
- **With signed messages:** Consensus is possible for any `N > f` (but requires public-key cryptography).
- **PBFT (Castro & Liskov, 1999):** Practical BFT for `N = 3f + 1`.

### OM(m) Algorithm (Oral Messages)
```
OM(0):
    Commander sends value to all lieutenants.
    Each lieutenant uses the value if received, or default.

OM(m), m > 0:
    Commander sends value to all lieutenants.
    For each lieutenant i:
        v_i = value from commander
        v_j = OM(m-1) with lieutenant i as commander (for each other j)
        Final decision: majority(v_i, v_1, v_2, ...)
```

### C Implementation
```c
ByzantineAgreement ba;
int values[] = {1, 0, 0, 0};
bool faulty[] = {false, true, false, false};
ByzantineBehavior bhv[] = {BYZ_HONEST, BYZ_TRAITOR, BYZ_HONEST, BYZ_HONEST};
byz_init_network(&ba, 4, values, faulty, bhv);
byz_om_algorithm(&ba, 0, 1); // OM(1) with 4 generals
```

---

## 6. CRDTs — Conflict-Free Replicated Data Types

### Motivation
Traditional replicated databases require coordination (locks, consensus) for consistency. CRDTs offer **strong eventual consistency** with **automatic conflict resolution**.

### Mathematical Property
CRDT states form a **join-semilattice**. The merge function is the **least upper bound** (LUB/join) of two states.

### State-Based (CvRDT)
- State is propagated; merge at receiver.
- Merge must be **commutative**, **associative**, **idempotent**.
- Works over unreliable protocols (gossip).

### Operation-Based (CmRDT)
- Operations are propagated.
- Operations must be **commutative**.
- Requires reliable causal broadcast.

### Common CRDT Types

| Type | Operations | Merge | Use Case |
|------|-----------|-------|----------|
| G-Counter | Increment only | Max | Page views |
| PN-Counter | Increment, decrement | Max inc/dec | Inventory count |
| G-Set | Add only | Union | Unique visitor set |
| 2P-Set | Add, remove (once) | Union | Membership (non-rejoin) |
| OR-Set | Add, remove (re-addable) | Union (tagged) | Shopping cart |
| LWW-Register | Assign | Latest timestamp | User profile |
| MV-Register | Assign | Union of versions | Application-resolved |

### C Implementation
```c
GCounter gc;
gc_init(&gc, 3);
gc_inc(&gc, 0);

ORSet ors;
orset_init(&ors, 0);
ors_add(&ors, item_id);
ors_merge(&ors, &replica_b);
```

---

## 7. Relationship Between Concepts

```
CAP Theorem
    └─ Tradeoff between Consistency and Availability
         └─ Solved by CRDTs (AP, eventual consistency)
              └─ Requires Vector Clocks (causality)
    └─ Strong consistency (CP) requires Consensus
         └─ FLP shows consensus is impossible in theory
              └─ Solved by Paxos/Raft (with synchrony assumptions)
                   └─ Byzantine consensus (BFT) for adversarial faults
```

---

## 8. Practical Tradeoffs Summary

| Goal | Approach | Cost |
|------|---------|------|
| Strong consistency | CP + Consensus (Paxos/Raft) | Latency, availability during partitions |
| High availability | AP + CRDTs | Eventual consistency, conflicts |
| Adversarial resilience | BFT (PBFT/Tendermint) | 3f+1 overhead, latency |
| Global consistency | TrueTime + commit wait | 7ms latency overhead |

---

## Recommended Reading List (Start Here)

1. **Beginner:** Kleppmann DDIA Chapters 5, 8, 9
2. **Clocks:** Lamport "Time, Clocks, and the Ordering of Events" (1978)
3. **Consensus:** Ongaro "In Search of an Understandable Consensus Algorithm" (Raft, 2014)
4. **CRDTs:** Shapiro "Conflict-Free Replicated Data Types" (2011)
5. **Advanced:** Fischer, Lynch, Paterson "Impossibility of Distributed Consensus" (1985)
6. **Bitcoin/Blockchain:** Nakamoto "Bitcoin: A Peer-to-Peer Electronic Cash System" (2008) — Nakamoto Consensus as probabilistic BFT
