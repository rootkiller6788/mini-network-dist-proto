# Knowledge Graph — mini-dist-protocol

## L1: Definitions (Complete ✅)

| Entry | Type | File | Description |
|-------|------|------|-------------|
| RaftNode | struct | include/raft.h | Raft server state: id, term, log, commit_index |
| RaftState | enum | include/raft.h | FOLLOWER/CANDIDATE/LEADER |
| AppendEntriesRPC | struct | include/raft.h | Log replication RPC frame |
| RequestVoteRPC | struct | include/raft.h | Vote request RPC frame |
| LogEntry | struct | include/raft.h | Single log entry: term + command |
| PaxosProposer | struct | include/paxos.h | Proposer state: proposal_num, value |
| PaxosAcceptor | struct | include/paxos.h | Acceptor: promised_num, accepted_num/value |
| PaxosLearner | struct | include/paxos.h | Learner: learned_value, learned_round |
| GossipNode | struct | include/gossip.h | Epidemic node with version clock |
| GossipMessage | struct | include/gossip.h | PUSH/PULL/PUSH-PULL message |
| GossipDataEntry | struct | include/gossip.h | Key-value-version triple |
| SWIMMember | struct | include/swim.h | Group member with incarnation number |
| SWIMCluster | struct | include/swim.h | SWIM membership set |
| SWIMMessage | struct | include/swim.h | PING/ACK/INDIRECT_PING/JOIN/LEAVE |
| BullyNode/RingNode/ZKNode | struct | include/leader_election.h | Three leader election algorithms |
| FLPSystem/FLPProcess | struct | include/consensus_theorems.h | FLP impossibility model |
| CAPSystem/CAPClassification | struct/enum | include/consensus_theorems.h | CAP theorem types |
| DTXCoordinator/DTXParticipant | struct | include/distributed_apps.h | Two-Phase Commit types |
| DKVStore/DKVReplica/DKVEntry | struct | include/distributed_apps.h | Distributed KV Store |
| DLMManager/DLMLock | struct | include/distributed_apps.h | Lease-based lock manager |
| GCounter/PNCounter/GSet/LWWSet | struct | include/advanced_topics.h | CRDT data types |
| VectorClock/VCRelation | struct/enum | include/advanced_topics.h | Vector clock with partial order |
| ECSystem/ECReplica | struct | include/advanced_topics.h | Eventual consistency system |

## L2: Core Concepts (Complete ✅)

| Concept | Implementation | Reference |
|---------|---------------|-----------|
| Raft Leader Election | src/raft.c: raft_become_candidate, raft_tick | Ongaro 2014 §5.2 |
| Raft Log Replication | src/raft.c: raft_handle_append_entries | Ongaro 2014 §5.3 |
| Raft Safety | src/raft.c: raft_handle_request_vote (log recency check) | Ongaro 2014 §5.4 |
| Paxos Phase 1 (Prepare) | src/paxos.c: paxos_prepare, paxos_promise | Lamport 2001 |
| Paxos Phase 2 (Accept) | src/paxos.c: paxos_accept, paxos_accepted | Lamport 2001 |
| Multi-Paxos | src/paxos.c: multi_paxos_become_leader, multi_paxos_replicate | Lamport 2001 |
| Gossip Epidemic Spread | src/gossip.c: gossip_spread (PUSH/PULL/PUSH-PULL) | Demers 1987 |
| Anti-Entropy | src/gossip.c: gossip_converge, gossip_on_receive | Demers 1987 |
| SWIM Failure Detection | src/swim.c: swim_ping, swim_indirect_ping | Gupta 2002 |
| SWIM Dissemination | src/swim.c: swim_disseminate, swim_on_receive | Gupta 2002 |
| Bully Algorithm | src/leader_election.c: bully_election | Garcia-Molina 1982 |
| Ring Election | src/leader_election.c: ring_election | Chang & Roberts 1979 |
| ZK-style Election | src/leader_election.c: zk_leader_election | Hunt 2010 |
| Two-Phase Commit | src/distributed_apps.c: dtx_prepare, dtx_commit | Gray 1978 |
| Causal Consistency | src/distributed_apps.c: dkv_causal_past | Ahamad 1995 |
| Lease-based Locking | src/distributed_apps.c: dlm_acquire, dlm_renew | Gray & Cheriton 1989 |
| CRDT State-based Merge | src/advanced_topics.c: gcrdt_merge, pncrdt_merge | Shapiro 2011 |
| Vector Clock Ordering | src/advanced_topics.c: vc_compare, vc_merge | Fidge 1988, Mattern 1989 |

## L3: Engineering Structures (Complete ✅)

| Structure | Data Types | Operations |
|-----------|-----------|------------|
| Raft State Machine | RaftNode + LogEntry[] | raft_tick (FOLLOWER→CANDIDATE→LEADER) |
| Paxos Roles | Proposer + Acceptor + Learner | Prepare→Promise→Accept→Accepted→Learn |
| Gossip Topology | GossipNode.neighbors[] (Ring/Full/Random) | gossip_spread (fanout-based) |
| SWIM Protocol Period | SWIMCluster.protocol_time_ms | swim_tick (periodic ping + dissemination) |
| Leader Election Ring | RingNode.next_id (circular linked) | ring_election (token circulation) |
| Two-Phase Commit FSM | DTXState (7 states) | dtx_prepare→dtx_commit/dtx_abort |
| DLM Lease Manager | DLMLock with TTL expiry | dlm_tick (lease expiration) |
| CRDT Lattice | GCounter.counts[] (vector) | Element-wise max (join-semilattice) |
| EC Read Repair | ECReplica with hints[] | ec_read_repair (latest-version push) |

## L4: Standards/Theorems (Complete ✅)

| Theorem | Code Verification | Reference |
|---------|------------------|-----------|
| FLP Impossibility | flp_is_bivalent, flp_run_adversary | Fisher, Lynch, Paterson (1985) |
| CAP Theorem | cap_classify, cap_partition_experiment | Brewer (2000), Gilbert & Lynch (2002) |
| Two-Generals Paradox | two_generals_simulate | Gray (1978) |
| Quorum Intersection | quorum_intersection_min, quorum_enumerate_all | Lamport (1998) |
| Byzantine Quorum | byzantine_quorum_threshold (n > 3f) | Castro & Liskov (1999) |
| Crash Fault Quorum | crash_fault_quorum_threshold (n > 2f) | Lamport (1998) |

**Key Formulas:**
- Quorum size: q = ⌊n/2⌋ + 1
- Quorum intersection minimum: 2q − n
- Byzantine threshold: n ≥ 3f + 1
- Crash fault threshold: n ≥ 2f + 1
- CAP: Under partition, choose C or A (not both)

## L5: Algorithms/Methods (Complete ✅)

| Algorithm | Implementation | Complexity |
|-----------|---------------|------------|
| Raft Consensus | src/raft.c (387 lines) | O(N) messages per round |
| Multi-Paxos | src/paxos.c (213 lines) | O(N) Phase 2 only |
| Gossip Epidemic | src/gossip.c (298 lines) | O(log N) rounds to converge |
| SWIM Membership | src/swim.c (410 lines) | O(1) per protocol period |
| Bully Election | src/leader_election.c | O(N²) messages |
| Ring Election | src/leader_election.c | O(2N) messages |
| ZK-style Election | src/leader_election.c | O(2N) messages |
| Two-Phase Commit | src/distributed_apps.c | O(N) messages |
| CRDT Merge (G-Counter) | src/advanced_topics.c | O(N) merge, O(1) query |
| Vector Clock Compare | src/advanced_topics.c | O(N) comparison |
| Read Repair | src/advanced_topics.c | O(N) per key |

## L6: Canonical Problems (Complete ✅)

| Problem | Example | Description |
|---------|---------|-------------|
| Raft 3-Node Cluster | examples/raft_demo.c | Leader election + log replication + partition |
| Gossip Convergence | examples/gossip_demo.c | 5-node full mesh with version-vector anti-entropy |
| Leader Election Comparison | examples/leader_election_demo.c | Bully vs Ring vs ZK-style comparison |
| Multi-Paxos Replication | examples/paxos_demo.c | Basic Paxos + Multi-Paxos with leader bypass |
| SWIM Failure Detection | examples/swim_demo.c | Join/Leave/Suspect/Dead lifecycle |

## L7: Applications (Complete ✅ — 3 applications)

| Application | Implementation | Reference |
|-------------|---------------|-----------|
| Distributed Transactions (2PC) | src/distributed_apps.c: DTXCoordinator | Gray 1978 |
| Distributed KV Store | src/distributed_apps.c: DKVStore with version vectors | Dynamo (SOSP 2007) |
| Distributed Lock Manager | src/distributed_apps.c: DLMManager with leases | Chubby (OSDI 2006) |

## L8: Advanced Topics (Complete ✅)

| Topic | Implementation | Reference |
|-------|---------------|-----------|
| CRDT G-Counter | src/advanced_topics.c: gcrdt_* | Shapiro 2011 |
| CRDT PN-Counter | src/advanced_topics.c: pncrdt_* | Shapiro 2011 |
| CRDT G-Set | src/advanced_topics.c: gset_* | Shapiro 2011 |
| CRDT LWW-Element-Set | src/advanced_topics.c: lww_* | Shapiro 2011 |
| Vector Clocks | src/advanced_topics.c: vc_* | Fidge 1988, Mattern 1989 |
| Eventual Consistency | src/advanced_topics.c: ec_* (read repair + hinted handoff) | Dynamo 2007, Cassandra 2010 |

## L9: Industry Frontiers (Partial — documented only)

| Topic | Documentation | Status |
|-------|--------------|--------|
| AI Compiler (MLIR/Triton) | N/A | Not in scope for dist-protocol |
| Confidential Computing | N/A | Not in scope |
| Quantum Consensus | N/A | Not in scope |
| Geo-distributed Consensus (EPaxos) | docs/consensus-protocols.md | Documented |
| Raft variants (Parallel Raft, CRAQ) | docs/consensus-protocols.md | Documented |
