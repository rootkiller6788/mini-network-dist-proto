# Course Dependency Tree — mini-dist-protocol

## Prerequisites

```
State Machines (MIT 6.004)
    └── Raft Consensus
            ├── Log Replication (AppendEntries)
            ├── Leader Election (RequestVote)
            └── Membership Changes

Networking Basics (Stanford CS 144)
    └── Gossip Protocols
            ├── Epidemic Spread (PUSH/PULL/PUSH-PULL)
            └── Anti-Entropy (Version Vectors)

Distributed Systems Theory (MIT 6.824)
    ├── FLP Impossibility
    ├── CAP Theorem
    ├── Two-Generals Paradox
    ├── Quorum Intersection
    ├── Paxos (Basic + Multi)
    ├── Two-Phase Commit
    └── SWIM Membership

Advanced OS (CMU 15-410)
    ├── Distributed Lock Manager (Leases)
    └── Eventual Consistency (Read Repair)

Database Systems (Stanford CS 245, CMU 15-445)
    ├── Distributed Transactions (2PC)
    └── Distributed KV Store (Causal Consistency)

Advanced Distributed Systems (CMU 15-721, MIT 6.824)
    ├── CRDTs (G-Counter, PN-Counter, G-Set, LWW-Set)
    ├── Vector Clocks
    └── Viewstamped Replication (documented, not implemented)

Parallel Systems (Berkeley CS 267)
    └── Leader Election Algorithms (Bully, Ring, ZK-style)

Security (MIT 6.858)
    └── Byzantine Fault Tolerance (quorum thresholds)
```

## Module Dependencies Within mini-dist-protocol

```
include/raft.h         →  src/raft.c
include/paxos.h        →  src/paxos.c
include/gossip.h       →  src/gossip.c
include/swim.h         →  src/swim.c
include/leader_election.h → src/leader_election.c

include/consensus_theorems.h → src/consensus_theorems.c
    ├── depends on: concepts from raft.h (quorum), paxos.h (acceptors)
    └── self-contained implementation

include/distributed_apps.h → src/distributed_apps.c
    ├── depends on: concepts from raft.h (consensus), paxos.h (agreement)
    └── self-contained implementation (no link dependency)

include/advanced_topics.h → src/advanced_topics.c
    ├── depends on: concepts from gossip.h (dissemination)
    └── self-contained implementation (no link dependency)
```

## Build Order

```
1. raft.o, paxos.o, gossip.o, swim.o, leader_election.o (parallel)
2. consensus_theorems.o, distributed_apps.o, advanced_topics.o (parallel)
3. Demo executables (link with respective .o)
4. Test executables (link with respective .o)
```
