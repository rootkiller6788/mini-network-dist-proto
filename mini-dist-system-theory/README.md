# mini-dist-system-theory — 分布式系统理论 (C 语言实现)

> 参考 MIT 6.824, Kleppmann DDIA, Lynch "Distributed Algorithms"

---

## Overview

A C99 library implementing core distributed systems theory concepts with working demonstrations. Five modules covering the fundamental theorems and data structures that underpin modern distributed systems.

---

## Modules

| # | Module | Header | Source | Concept |
|---|--------|--------|--------|---------|
| 1 | CAP Theorem | `include/cap_theorem.h` | `src/cap_theorem.c` | Consistency-Availability-Partition tradeoffs |
| 2 | Time & Ordering | `include/time_ordering.h` | `src/time_ordering.c` | Lamport/Vector/Hybrid Logical Clocks |
| 3 | FLP Impossibility | `include/flp_impossibility.h` | `src/flp_impossibility.c` | Impossibility of async consensus |
| 4 | CRDTs | `include/crdt.h` | `src/crdt.c` | Conflict-Free Replicated Data Types |
| 5 | Byzantine Faults | `include/byzantine.h` | `src/byzantine.c` | Byzantine Generals, OM algorithm |

---

## Quick Start

```bash
make all
bin/cap_demo
bin/vector_clock_demo
bin/crdt_demo
```

---

## Module Details

### 1. CAP Theorem (`cap_theorem.h`)

Implements a 3-node distributed store demonstrating the CAP tradeoff:

- **CP mode:** Quorum reads/writes. During partition, rejects writes to minority — sacrificing availability.
- **AP mode:** Local writes with async replication. During partition, accepts writes but nodes diverge — sacrificing consistency.

```c
#include "cap_theorem.h"

DistributedStore store;
cap_configure(&store, CP_MODE);
cap_write(&store, 0, "hello");
cap_create_partition(&store, 0, 1);  // simulate network split
cap_read(&store, 0, buf, sizeof(buf));
cap_heal_partition(&store, 0, 1);
```

### 2. Time & Ordering (`time_ordering.h`)

Implements three logical clock types:

| Clock | Type | Order | Concurrency Detection | Space |
|-------|------|-------|----------------------|-------|
| Lamport | Scalar | Total | No | O(1) |
| Vector | Array[N] | Partial | Yes | O(N) |
| HLC | (Physical, Logical) | Total + Physical proximity | No | O(1) |

```c
#include "time_ordering.h"

LamportClock lc = {0};
lamport_increment(&lc);
lamport_tick(&lc, received_clock);

VectorClock vc;
vector_clock_init(&vc, 3);
vector_clock_increment(&vc, node_id);
vector_clock_merge(&vc, &other);
VCCompareResult r = vector_clock_compare(&a, &b);
```

### 3. FLP Impossibility (`flp_impossibility.h`)

Simulates the FLP consensus protocol with 5 processes, demonstrating that even without crashes, message reordering can keep the system in a bivalent (undecided) state for arbitrarily long.

```c
#include "flp_impossibility.h"

FLPSystem sys;
int initial[] = {0, 0, 1, 0, 1};
flp_init(&sys, 5, initial);
int result = flp_run_until_decided(&sys, 100);
// result == -1 means still bivalent
```

### 4. CRDTs (`crdt.h`)

Implements five CRDT types:

- **G-Counter:** Grow-only counter (max-merge)
- **PN-Counter:** Support increment and decrement (two G-Counters)
- **G-Set / 2P-Set:** Grow-only and two-phase sets
- **OR-Set:** Observed-remove set with add-wins semantics
- **LWW-Register:** Last-writer-wins register

```c
#include "crdt.h"

GCounter gc;
gc_init(&gc, 3);
gc_inc(&gc, 0);

ORSet ors_a, ors_b;
orset_init(&ors_a, 0);
orset_init(&ors_b, 1);
ors_add(&ors_a, item_id);
ors_merge(&ors_a, &ors_b);  // eventual consistency
```

### 5. Byzantine Faults (`byzantine.h`)

Implements the Oral Messages (OM) algorithm for Byzantine agreement:

- 3 generals, 1 traitor: Consensus impossible (n ≤ 3f)
- 4 generals, 1 traitor: Consensus possible with OM(1)
- Demonstrates the n > 3f condition

```c
#include "byzantine.h"

ByzantineAgreement ba;
int values[] = {1, 0, 0, 0};
bool faulty[] = {false, true, false, false};
ByzantineBehavior bhv[] = {BYZ_HONEST, BYZ_TRAITOR, BYZ_HONEST, BYZ_HONEST};
byz_init_network(&ba, 4, values, faulty, bhv);
byz_om_algorithm(&ba, 0, 1);
```

---

## Demos

| Demo | Description |
|------|------------|
| `demos/mini-logical-clocks/` | Deep dive: Lamport, Vector, HLC, TrueTime |
| `demos/mini-crdts/` | Deep dive: CvRDT, CmRDT, PN-Counter, OR-Set, cart |

---

## Documentation

| Doc | Contents |
|-----|----------|
| `docs/course-alignment.md` | Map to MIT 6.824, DDIA, Lynch |
| `docs/distributed-theory-primer.md` | Time, ordering, consensus, CAP, Byzantine, CRDT overview |

---

## Example Output

```
$ bin/cap_demo
=== CAP Theorem Mode: CP (Consistent + Partition-tolerant, sacrifice A) ===
*** PARTITION created between node 0 and node 1 ***
[CP] Write REJECTED: node 0 can only reach 1/3 nodes (quorum=2)
*** PARTITION HEALED between node 0 and node 1 ***
[CP] Write ACCEPTED: 'hello' via node 0 (quorum=2)
```

---

## Build Requirements

- GCC (or any C99 compiler)
- GNU Make
- libc + libm

---

## License

MIT — Educational and reference implementation.
