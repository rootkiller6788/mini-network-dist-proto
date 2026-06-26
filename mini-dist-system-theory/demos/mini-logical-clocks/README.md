# Mini Logical Clocks & Time in Distributed Systems

> Deep dive into logical clocks: Lamport, Vector, Hybrid Logical Clock (HLC), and Google Spanner's TrueTime.

---

## 1. Why Logical Clocks?

In a distributed system, each node has its own physical clock. Clock drift, NTP synchronization errors, and network delays mean that **physical timestamps from different nodes are not comparable** reliably. Logical clocks solve this by establishing a **causal ordering** of events, independent of physical time.

A **logical clock** assigns a number to each event such that:
> If event `a` happens-before event `b`, then `C(a) < C(b)`.

The converse is not necessarily true: `C(a) < C(b)` does **not** imply `a → b`.

---

## 2. Lamport Clock (1978)

### Definition
A single integer counter per process. Rules:

1. **Increment on local event:** `C := C + 1`
2. **On receiving a message:** `C := max(C_local, C_msg) + 1`

### Properties
- **Total order:** Timestamps are comparable.
- **Consistency condition:** `a → b` ⇒ `L(a) < L(b)` holds.
- **Weakness:** Cannot detect concurrency. If `L(a) = L(b)`, events may be concurrent.

### Usage
- Distributed mutual exclusion (Lamport's bakery algorithm)
- Total order broadcast
- Causality tracking in version vectors

### Implementation in C
```c
LamportClock lc = {0};
uint64_t t1 = lamport_increment(&lc);     // local event
uint64_t t2 = lamport_tick(&lc, msg_ts); // receive message
```

---

## 3. Vector Clock (Fidge/Mattern 1988)

### Definition
An array of N integers (one per process). Rules:

1. **Increment on local event:** `VC[i] := VC[i] + 1` for process `i`
2. **On receiving a message:** `VC[j] := max(VC[j], VC_msg[j])` for all `j`, then `VC[i]++`

### Partial Order Comparison
```
A < B (A happens-before B):
    For all i: A[i] <= B[i]
    AND exists j: A[j] < B[j]

A || B (concurrent):
    Neither A <= B nor B <= A
```

### Properties
- **Partial order:** Can detect concurrency.
- **Strong consistency:** `a → b` **iff** `VC(a) < VC(b)`.
- **Cost:** O(N) space and merge time per process.

### Use Cases
- Detecting causal dependencies in key-value stores (Dynamo, Riak)
- Optimistic replication conflict detection
- Git version history (DAG of commits)

### Implementation in C
```c
VectorClock vc;
vector_clock_init(&vc, 3);
vector_clock_increment(&vc, node_id);
vector_clock_merge(&vc, &received_vc);
VCCompareResult r = vector_clock_compare(&a, &b);
```

---

## 4. Comparison: Lamport vs Vector Clocks

| Property | Lamport Clock | Vector Clock |
|----------|--------------|-------------|
| Type | Scalar (integer) | Vector (array) |
| Space | O(1) | O(N) |
| Concurrency detection | No | Yes |
| `C(a) < C(b) ⇒ a→b` | No | Yes |
| `a→b ⇒ C(a) < C(b)` | Yes | Yes |
| Merge | Simple | Element-wise max |

---

## 5. Hybrid Logical Clock (HLC, 2014)

### Motivation
Physical clocks are useful for external consistency (real-world time), but unreliable inside a distributed system. **HLC** combines physical time with a logical counter to get the best of both worlds.

### Rules
```
Initially: pt = 0, l = 0

On send/local event:
    pt' = max(pt, physical_clock_now)
    if pt == pt': l++
    else: l = 0
    timestamp = (pt, l)

On receive(msg with pt_m, l_m):
    pt' = max(pt, physical_clock_now, pt_m)
    if pt == pt' == pt_m: l = max(l, l_m) + 1
    else if pt == pt': l++
    else: l = 0
```

### Properties
- **Physical proximity:** The HLC timestamp is always close to physical time (bounded by ε).
- **Causal consistency:** `a → b` ⇒ `HLC(a) < HLC(b)`.
- **Space:** O(1) per clock.

---

## 6. Google Spanner: TrueTime

### The Problem
Spanner is a globally-distributed SQL database. It needs **external consistency** (linearizability across datacenters) while using commodity hardware with loosely synchronized clocks.

### TrueTime Architecture
- **GPS + atomic clocks** in each datacenter.
- TrueTime provides an interval `[earliest, latest]` that contains the absolute time.
- Clock uncertainty ε is typically 1-7ms.

### How Spanner Commits
1. Each transaction gets a commit timestamp `s` from the coordinator's TrueTime.
2. The coordinator **waits** until `TT.now().earliest > s` (the "commit wait").
3. This guarantees that any later transaction reading at timestamp `t > s` will see the committed data.

### Key Insight
> Spanner does **not** use logical clocks for ordering. It uses **TrueTime intervals** with a deliberate wait (the ε uncertainty) to enforce external consistency. This is a tradeoff: latency (the wait) for global consistency without coordination.

### Comparison
| System | Clock Type | Consistency | Tradeoff |
|--------|-----------|-------------|----------|
| Spanner | TrueTime (physical) | External consistency | Commit wait (latency) |
| CockroachDB | HLC | Serializability | Clock skew bound |
| Dynamo | Vector Clock | Eventual consistency | Conflict resolution |

---

## 7. Version Vectors (Extension of Vector Clocks)

### Definition
A version vector tracks the version of data across replicas, not events. Each replica has a counter that increments only on writes to that replica.

### Rules
- `D_v[i]` = number of updates performed by replica `i` on data item `D`.
- On write to replica `i`: `D_v[i]++`
- On sync between replicas: element-wise max.

### Use in Riak
Riak uses **dotted version vectors** (DVV) to handle concurrent siblings. When a conflict is detected, the system stores all conflicting values as **siblings** and lets the application resolve them.

---

## 8. Interval Tree Clocks (ITC)

An extension of version vectors that supports dynamic replica addition/removal. Each node gets a portion of an ID space tree, and each event claims the smallest available leaf in the tree. This is used in some CRDT implementations.

---

## 9. Practical Summary

| Use Case | Recommended Clock |
|----------|-------------------|
| Simple event ordering in logs | Lamport Clock |
| Detecting concurrent updates in KV-store | Vector Clock / Version Vector |
| Geo-distributed SQL (serializability) | HLC (CockroachDB-style) |
| Global linearizability | TrueTime (Spanner) |
| CRDTs with dynamic membership | Interval Tree Clocks |

---

## 10. Code Example: Detecting Concurrent Writes

```c
VectorClock vc_server, vc_client;
vector_clock_init(&vc_server, 2);
vector_clock_init(&vc_client, 2);

// Server updates key "foo"
vector_clock_increment(&vc_server, 0);

// Client updates key "foo" without seeing server's update
vector_clock_increment(&vc_client, 1);

// Client sends update to server
VCCompareResult r = vector_clock_compare(&vc_client, &vc_server);
if (r == VC_CONCURRENT) {
    printf("Conflict detected! Need merge strategy.\n");
}
```

---

## Recommended Reading

1. **Lamport, L.** "Time, Clocks, and the Ordering of Events in a Distributed System" (1978)
2. **Fidge, C.J.** "Timestamps in Message-Passing Systems That Preserve the Partial Ordering" (1988)
3. **Kulkarni et al.** "Hybrid Logical Clocks" (2014)
4. **Corbett et al.** "Spanner: Google's Globally-Distributed Database" (OSDI 2012)
5. **Kleppmann, M.** "Designing Data-Intensive Applications" Ch 5 (Replication) and Ch 9 (Consistency)

---

## 11. Happens-Before Relation — Formal Definition

The **happens-before** relation (`→`) is the foundation of logical clocks. Defined by Lamport:

### Properties
1. **Program order:** If events `a` and `b` occur in the same process and `a` precedes `b`, then `a → b`.
2. **Message causality:** If `a` is the sending of a message and `b` is its receipt, then `a → b`.
3. **Transitivity:** If `a → b` and `b → c`, then `a → c`.

### Partial Order
Happens-before is a **strict partial order** (irreflexive, asymmetric, transitive). Events that are not related by `→` are **concurrent** (`a || b`).

### Relation to Logical Clocks
The **clock condition** states: `a → b` ⇒ `C(a) < C(b)`. The converse is what distinguishes clock types.

---

## 12. Consistent Cuts and Distributed Snapshots

### Definitions
- A **cut** `C` is a set of events across all processes.
- A cut is **consistent** if for any event `e` in `C`, all events `e' → e` are also in `C`.
- The **frontier** of a consistent cut is described by a vector clock.

### Chandy-Lamport Snapshot Algorithm (1985)
1. Initiator records its state and sends a **marker** on all outgoing channels.
2. On receiving a marker for the first time:
   - Record local state.
   - Mark the incoming channel as empty.
   - Send markers on all outgoing channels.
3. On receiving a marker on a channel that was not previously marked:
   - Record all messages received on that channel since local snapshot.

This algorithm creates a **consistent global snapshot** without pausing the distributed computation.

---

## 13. NTP and Clock Synchronization

### Network Time Protocol (NTP)
- Hierarchical system with **stratum** levels (0-15).
- Stratum 0: atomic clocks, GPS receivers.
- Stratum 1: directly synced to stratum 0.
- Synchronization uses round-trip time measurement to estimate offset.

### Accuracy
- Typical accuracy over LAN: **sub-millisecond**.
- Over WAN: **tens of milliseconds**.
- Key limitation: asymmetric network delays.

### Precision Time Protocol (PTP / IEEE 1588)
- Uses hardware timestamping for microsecond or even nanosecond accuracy.
- Required for Spanner-like systems and financial trading.

---

## 14. Clocks and the CAP Theorem

### The Role of Clocks
- **CP systems** (Paxos/Raft): Rely on logical time (term numbers, log indices) for ordering.
- **AP systems** (Dynamo/Cassandra): Rely on vector clocks for conflict detection.
- **Spanner (CA-ish):** Uses TrueTime for external consistency, effectively bridging CP and CA through precise physical clocks.

### Tradeoffs
| Approach | Consistency | Latency | Infrastructure |
|----------|------------|---------|----------------|
| Lamport/Logical | Causal | Low | None |
| Vector Clocks | Causal + Concurrency | Low | O(N) storage |
| HLC | Near-external | Low | Clock sync |
| TrueTime | External (Linearizability) | Commit wait (~7ms) | GPS + Atomic clocks |
| Paxos/Raft | Linearizability | RTT to leader | Quorum messages |

---

## 15. Practical Implementation Checklist

When implementing logical clocks in a real system:

- [ ] Choose clock type based on consistency requirements
- [ ] Decide on N (number of nodes) for vector clocks upfront
- [ ] Handle node addition/removal (ITC or re-initialize)
- [ ] Garbage-collect old vector clock entries
- [ ] For HLC: bound clock skew with NTP
- [ ] For TrueTime: measure ε accurately, never underestimate
- [ ] Test with network partitions and delayed messages
- [ ] Consider clock overflow (64-bit Lamport: ~ 10^19 events, practically infinite)
