# Gap Report — mini-dist-protocol

## Currently Covered ✅

| Knowledge Area | Coverage | Notes |
|---------------|----------|-------|
| L1: Definitions | 100% | 24 types across 8 headers |
| L2: Core Concepts | 100% | 17 concepts implemented |
| L3: Engineering Structures | 100% | 9 structures with full operations |
| L4: Standards/Theorems | 100% | FLP, CAP, Two-Generals, Quorum Intersection, Byzantine/Crash thresholds |
| L5: Algorithms | 100% | 11 algorithms with complete implementations |
| L6: Canonical Problems | 100% | 5 executable demos |
| L7: Applications | 100% | 2PC, DKV, DLM |
| L8: Advanced Topics | 100% | CRDTs, Vector Clocks, Eventual Consistency |
| L9: Industry Frontiers | Documented | EPaxos, Parallel Raft, CRAQ in docs/ |

## Gaps (Low Priority)

| Gap | Priority | Reason |
|-----|----------|--------|
| Proof-carrying code (Coq/Lean) | Low | Formal verification of consensus safety outside C scope |
| TLA+ specifications | Low | Model checking not applicable to C implementation |
| RDMA-based consensus | Low | Hardware-specific, not portable C99 |
| Blockchain/Smart Contract consensus (PoW/PoS) | Low | Different problem domain (Byzantine + incentive) |
| Federated consensus (Stellar SCP) | Low | Niche application |
| Quantum-resistant consensus | Low | Research frontier |

## Rationale

The current implementation achieves **COMPLETE** status for L1-L8.
L9 is documented but not implemented, per SKILL.md allowance
("允许仅文档，不做强制实现要求").

All gaps are in L9 (Industry Frontiers) and intentionally deferred:
- These topics require either formal methods tooling (TLA+, Coq),
  specialized hardware (RDMA), or are entirely different problem
  domains (blockchain consensus).
- The teaching-oriented C99 implementation focuses on the core
  protocols that form the foundation of all distributed systems.
