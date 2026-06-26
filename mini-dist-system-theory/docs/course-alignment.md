# Course Alignment — mini-dist-system-theory

> Mapping to MIT 6.824, DDIA (Kleppmann), and Lynch's "Distributed Algorithms"

---

## MIT 6.824: Distributed Systems (Robert Morris)

| Module | 6.824 Topic | Corresponding Component |
|--------|------------|------------------------|
| CAP Theorem | Lecture 3: GFS, Lecture 4: Primary-Backup Replication | `cap_theorem.h/.c` |
| Logical Clocks | Lecture 2: RPC and Threads (causality), Lecture 7: Raft | `time_ordering.h/.c` |
| FLP Impossibility | Lecture 5: Fault Tolerance, Raft | `flp_impossibility.h/.c` |
| Byzantine Faults | Lecture 14: Byzantine Generals, PBFT | `byzantine.h/.c` |
| CRDTs | Not directly covered (6.824 focuses on consensus) | `crdt.h/.c` |

### Key 6.824 Lectures
- **L5: Fault Tolerance / Raft** — Consensus, leader election, log replication
- **L7: Raft** — Snapshots, log compaction
- **L14: Byzantine Fault Tolerance** — Oral messages, signed messages, PBFT
- **L16: Replication** — Chain replication, CRAQ

---

## DDIA: Designing Data-Intensive Applications (Martin Kleppmann)

| Module | DDIA Chapter | Corresponding Component |
|--------|-------------|------------------------|
| CAP Theorem | Ch 5: Replication (Leader-based, Multi-leader, Leaderless) | `cap_theorem.h/.c` |
| Logical Clocks | Ch 5: Replication (version vectors, causal ordering) | `time_ordering.h/.c` |
| FLP Impossibility | Ch 8: The Trouble with Distributed Systems | `flp_impossibility.h/.c` |
| Byzantine Faults | Ch 8: Byzantine Faults, Ch 9: Consistency and Consensus | `byzantine.h/.c` |
| CRDTs | Ch 5: Replication (CRDTs, conflict resolution), Ch 9: Consistency | `crdt.h/.c` |

### Key Chapters
- **Ch 5 (Replication)** — Leaders/followers, replication log formats, conflict resolution, version vectors, CRDTs (RiAk, Dynamo)
- **Ch 7 (Transactions)** — ACID, weak isolation levels, serializability
- **Ch 8 (Trouble with Distributed Systems)** — Faults, partial failures, unreliable networks, clocks, time ordering, FLP, Byzantine faults
- **Ch 9 (Consistency and Consensus)** — Linearizability, ordering guarantees, distributed transactions (2PC), consensus (Paxos, Raft)

---

## Lynch: "Distributed Algorithms" (Nancy Lynch, 1996)

| Module | Lynch Topic | Corresponding Component |
|--------|------------|------------------------|
| CAP Theorem | Ch 6: Atomic Objects, Ch 8: Atomic Read/Write Registers | `cap_theorem.h/.c` |
| Logical Clocks | Ch 18: Logical Time (Lamport clocks, vector clocks) | `time_ordering.h/.c` |
| FLP Impossibility | Ch 5: Asynchronous Consensus (FLP proof), Ch 6: Atomic Objects | `flp_impossibility.h/.c` |
| Byzantine Faults | Ch 7: Byzantine Agreement (PSY protocol, exponential information gathering) | `byzantine.h/.c` |
| CRDTs | Not covered (published later) | `crdt.h/.c` |

### Key Chapters
- **Ch 5: Asynchronous Consensus** — FLP impossibility proof in full detail
- **Ch 6: Atomic Objects** — I/O automata model, linearizability
- **Ch 7: Byzantine Agreement** — Oral messages (OM), signed messages (SM)
- **Ch 8: Atomic Read/Write Registers** — Quorum systems, ABD algorithm
- **Ch 18: Logical Time** — Happens-before relation, lamport clocks, vector clocks, consistent cuts

---

## Additional References

| Concept | Source | Paper / Resource |
|---------|--------|-----------------|
| CAP Theorem | Brewer (2000), Gilbert & Lynch (2002) | "CAP Twelve Years Later" (Brewer, 2012) |
| Lamport Clock | Lamport (1978) | "Time, Clocks, and the Ordering of Events in a Distributed System" |
| Vector Clock | Fidge (1988), Mattern (1989) | "Timestamps in Message-Passing Systems" |
| HLC | Kulkarni et al. (2014) | "Hybrid Logical Clocks" |
| TrueTime | Corbett et al. (2012) | "Spanner: Google's Globally-Distributed Database" |
| FLP | Fischer, Lynch, Paterson (1985) | "Impossibility of Distributed Consensus with One Faulty Process" |
| PBFT | Castro & Liskov (1999) | "Practical Byzantine Fault Tolerance" |
| CRDT | Shapiro et al. (2011) | "Conflict-Free Replicated Data Types" |
| Raft | Ongaro & Ousterhout (2014) | "In Search of an Understandable Consensus Algorithm" |
| Paxos | Lamport (1998) | "The Part-Time Parliament" |

---

## Learning Path

### Beginner
1. Start with Lamport Clocks (time_ordering)
2. Understand CAP Theorem (cap_theorem)
3. Explore CRDTs (crdt) — most practical for modern apps

### Intermediate
4. Study FLP Impossibility (flp_impossibility) — why consensus is hard
5. Dive into Byzantine Faults (byzantine) — security perspective
6. Implement composite CRDTs for real applications

### Advanced
7. Read the original FLP paper
8. Implement PBFT
9. Study Paxos Made Simple, then Raft
10. Understand Spanner's TrueTime and external consistency
