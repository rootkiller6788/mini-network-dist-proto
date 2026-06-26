# Mini CRDTs — Conflict-Free Replicated Data Types Deep Dive

> A comprehensive guide to CRDTs: state-based (CvRDT), operation-based (CmRDT), PN-Counter, OR-Set, LWW-Register, and a CRDT shopping cart.

---

## 1. What Are CRDTs?

**Conflict-Free Replicated Data Types** (CRDTs) are data structures that:
- Can be **replicated** across multiple nodes.
- Can be **updated independently** and concurrently.
- Can be **merged** (reconciled) without coordination, always converging to the same state.

### Why CRDTs?
Traditional replicated systems use consensus (Paxos/Raft) for strong consistency. CRDTs offer **eventual consistency with automatic conflict resolution** — no rollbacks, no conflict resolution UI, no lost updates.

### Mathematical Foundation
CRDTs are based on **join-semilattices**: a partially ordered set where any two elements have a least upper bound (LUB). Merging two CRDT states = computing their join (LUB).

---

## 2. State-Based CRDTs (CvRDT — Convergent)

### Definition
- **State** is propagated between replicas (gossip protocol).
- **Merge** function must be: **commutative**, **associative**, **idempotent**.
- Replicas periodically send their full state or deltas.

### Properties
```
S_merge(a, b) = S_merge(b, a)        (commutative)
S_merge(a, S_merge(b, c)) = S_merge(S_merge(a, b), c)  (associative)
S_merge(a, a) = a                    (idempotent)
```

### Examples
- G-Counter, PN-Counter, G-Set, 2P-Set, OR-Set, LWW-Register

### Advantages
- Simple to implement.
- Works over unreliable gossip protocols.

### Disadvantages
- Full state transmission overhead.
- Merge function must be designed carefully.

---

## 3. Operation-Based CRDTs (CmRDT — Commutative)

### Definition
- **Operations** (not state) are propagated.
- Operations must be **commutative** (order doesn't matter).
- Requires **reliable causal broadcast** (exactly-once, causal delivery).

### Properties
- Operations are applied at source and then broadcast.
- No merge function needed — apply the operation at each replica.
- Requires stronger network guarantees than state-based.

### Examples
- OR-Set with unique element IDs
- Counter with unique operation IDs

### Advantages
- Smaller messages (just delta operations).
- No merge overhead.

### Disadvantages
- Requires reliable broadcasting (exactly-once delivery).
- Harder to implement correctly.

---

## 4. Delta-State CRDTs (δ-CRDT)

A hybrid approach:
- Send only **delta-mutators** (recent changes) instead of full state.
- Deltas are joined at the receiver.
- Combines small message sizes of CmRDT with the tolerance of CvRDT.

---

## 5. G-Counter (Grow-Only Counter)

### State: `counters[N]` — one per replica

### Operations
```
inc(i): counters[i]++
value(): sum(counters)
merge(other): for each j: counters[j] = max(counters[j], other.counters[j])
```

### Why It Works
- Each replica owns its own slot (per-replica increment).
- Merge is element-wise max — monotonic, commutative, idempotent.
- Value is sum of all slots — monotonic.

### Limitation
Cannot decrement (growing only).

### C Usage
```c
GCounter gc;
gc_init(&gc, 3);
gc_inc(&gc, 0); // replica A increments
gc_inc(&gc, 1); // replica B increments
uint64_t v = gc_value(&gc); // 2
```

---

## 6. PN-Counter (Positive-Negative Counter)

### State: Two G-Counters — `inc` and `dec`

### Operations
```
inc(i): inc.counters[i]++
dec(i): dec.counters[i]++
value(): sum(inc.counters) - sum(dec.counters)
merge(other): inc.merge(other.inc); dec.merge(other.dec)
```

### Why It Works
- Increments and decrements are tracked separately.
- Since each is monotonic, the combination is a valid CvRDT.
- `value == inc - dec` is always correct after merge.

### C Usage
```c
PNCounter pn;
pn_init(&pn, 3);
pn_inc(&pn, 0);
pn_inc(&pn, 0);
pn_dec(&pn, 1);
int64_t v = pn_value(&pn); // 1
```

---

## 7. G-Set (Grow-Only Set)

### State: A bitset or hash set

### Operations
```
add(e): add e to set
contains(e): check if e is in set
merge(other): union of both sets
```

### Why It Works
- Elements are only added (never removed).
- Set union is monotonic, commutative, idempotent.

### Limitation
Cannot remove elements.

---

## 8. 2P-Set (Two-Phase Set)

### State: `added_set` + `removed_set` (tombstones)

### Operations
```
add(e): add e to added_set
remove(e): add e to removed_set
contains(e): e in added_set AND e not in removed_set
merge(other): union of added sets, union of removed sets
```

### Why It Works
- Both sets only grow.
- An element removed once can never be added again.
- "Remove wins" semantics.

### Limitation
Once removed, element cannot be re-added.

---

## 9. OR-Set (Observed-Remove Set / Add-Wins Set)

### State: Set of `(element, unique_tag)` pairs

### Operations
```
add(e): add (e, new_unique_tag()) to set
remove(e): remove all (e, tag) currently observed
merge(other): union of all pairs (unique tags prevent duplicates)
```

### Why It Works
- Each `add` creates a **unique tag**.
- Removing removes only tags seen at that time.
- Concurrent `add` and `remove` of the same element: **add wins** (because the concurrent add creates a new tag not seen by the remover).

### C Usage
```c
ORSet ors;
orset_init(&ors, replica_id);
ors_add(&ors, item);
ors_remove(&ors, item);
ors_merge(&ors, &other_replica);
bool present = ors_contains(&ors, item);
```

### Add-Wins vs Remove-Wins
- **OR-Set**: Add-wins (concurrent add wins over concurrent remove).
- **2P-Set**: Remove-wins (once removed, cannot re-add).
- Which to use depends on application semantics.

---

## 10. LWW-Register (Last-Writer-Wins Register)

### State: `(value, timestamp, node_id)`

### Operations
```
assign(v): value = v; timestamp = now() or increment counter; node_id = my_id
merge(other):
    if other.timestamp > this.timestamp or
       (other.timestamp == this.timestamp and other.node_id > this.node_id):
        take other's value
```

### Why It Works
- Timestamps are totally ordered.
- Conflicts are resolved deterministically (latest timestamp wins).
- Tiebreaker: highest node_id ensures convergence.

### Limitation
Data loss: earlier writes are silently overwritten.

### C Usage
```c
LWWRegister reg;
lww_init(&reg, 0);
lww_set(&reg, "hello");
lww_merge(&reg, &other_reg);
printf("%s\n", lww_get(&reg));
```

---

## 11. CRDT Shopping Cart (Composite CRDT)

### Design
A shopping cart can be modeled as an OR-Set mapping product IDs to quantities:

```
Cart = ORSet<(product_id, quantity)>
```

### Operations
- `add_item(product_id, quantity=1)` — ORS add
- `remove_item(product_id)` — ORS remove
- `update_quantity(product_id, new_quantity)` — ORS remove old + ORS add new
- `merge(cart_a, cart_b)` — ORS merge with add-wins

### Why Add-Wins?
If two users concurrently add and remove the same item from different devices, the add should win — it's better to have an extra item in the cart than lose one the user wanted.

### Notes
- For decrement/increment quantity, a PN-Counter per product is more precise.
- Composite CRDTs can be built by nesting CRDTs.

---

## 12. CRDT Comparison Table

| Type | Mutations | Merge | Conflict Resolution | Space |
|------|-----------|-------|---------------------|-------|
| G-Counter | Inc only | Element-wise max | None | O(N) |
| PN-Counter | Inc, Dec | Element-wise max | None | O(N) |
| G-Set | Add only | Union | None | O(|elements|) |
| 2P-Set | Add, Remove | Union | Remove wins | O(|elements|) |
| OR-Set | Add, Remove | Union (unique tags) | Add wins | O(|elements| * tags) |
| LWW-Register | Assign | Latest timestamp | Last-write wins | O(1) |
| MV-Register | Assign | Union of versions | Application-resolved | O(|versions|) |

---

## 13. When to Use CRDTs

### Good Fit
- Collaborative editing (Google Docs uses CRDT-like OT)
- Distributed counters (likes, views, inventory tracking)
- Multi-device data sync (shopping carts, notes)
- Edge computing with intermittent connectivity
- IoT sensor data aggregation

### Not a Good Fit
- Strong consistency requirements (bank transfers, inventory reservation)
- Transactions spanning multiple keys
- Complex invariants (foreign key constraints)

---

## 14. Implementing a New CRDT

### Steps
1. Define the state (what data to track).
2. Define operations (how to mutate).
3. Define merge (how to combine two states).
4. Prove the merge is commutative, associative, idempotent.
5. Prove that operations are monotonic with respect to the merge.
6. Test with concurrent operations.

### Verification Checklist
```
✓ Merge(a, b) == Merge(b, a)
✓ Merge(a, Merge(b, c)) == Merge(Merge(a, b), c)
✓ Merge(a, a) == a
✓ For any operation op: state <= Merge(state, op(state_other))
```

---

## Recommended Reading

1. **Shapiro et al.** "Conflict-Free Replicated Data Types" (2011)
2. **Shapiro et al.** "A Comprehensive Study of Convergent and Commutative Replicated Data Types" (2011)
3. **Gomes et al.** "Delta-CRDTs: Specifying State-based CRDTs in a Few Lines" (2017)
4. **Kleppmann, M.** "Designing Data-Intensive Applications" Ch 5 (Replication), Ch 9 (Consistency)
5. **Baqer et al.** "On CRDTs in Practice" (2023)
6. **Riak Docs** — CRDTs in production (PN-Counter, OR-Set, Map, etc.)
