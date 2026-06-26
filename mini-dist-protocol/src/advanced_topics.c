#include "advanced_topics.h"
#include <string.h>
#include <stdio.h>

/* ================================================================
 * CRDT: G-Counter (Grow-only Counter)
 *
 * A state-based CRDT where the value is monotonically increasing.
 * Merge is the lattice join: element-wise maximum.
 * Each replica owns one slot in the count array.
 *
 * Reference: Shapiro et al. (2011) "Conflict-free Replicated
 *   Data Types", INRIA Research Report.
 * ================================================================ */

void gcrdt_init(GCounter *gc, int replica_id, int n) {
    gc->replica_id   = replica_id;
    gc->replica_count = n;
    for (int i = 0; i < GCRDT_MAX_REPLICAS; i++)
        gc->counts[i] = 0;
}

void gcrdt_inc(GCounter *gc) {
    if (gc->replica_id >= 0 && gc->replica_id < GCRDT_MAX_REPLICAS)
        gc->counts[gc->replica_id]++;
}

int gcrdt_query(const GCounter *gc) {
    int total = 0;
    for (int i = 0; i < gc->replica_count; i++)
        total += gc->counts[i];
    return total;
}

void gcrdt_merge(GCounter *dst, const GCounter *src) {
    /* Lattice join: element-wise max.
     * This is commutative, associative, and idempotent -
     * the three properties required for state-based CRDT merge. */
    for (int i = 0; i < dst->replica_count && i < GCRDT_MAX_REPLICAS; i++) {
        if (src->counts[i] > dst->counts[i])
            dst->counts[i] = src->counts[i];
    }
}


/* ================================================================
 * CRDT: PN-Counter (Positive-Negative Counter)
 *
 * Supports both increment and decrement by maintaining two
 * G-Counters: one for increments and one for decrements.
 * Value = sum(positive) - sum(negative).
 *
 * Reference: Shapiro et al. (2011) §3.3
 * ================================================================ */

void pncrdt_init(PNCounter *pn, int replica_id, int n) {
    pn->replica_id = replica_id;
    gcrdt_init(&pn->positive, replica_id, n);
    gcrdt_init(&pn->negative, replica_id, n);
}

void pncrdt_inc(PNCounter *pn) {
    gcrdt_inc(&pn->positive);
}

void pncrdt_dec(PNCounter *pn) {
    gcrdt_inc(&pn->negative);
}

int pncrdt_query(const PNCounter *pn) {
    return gcrdt_query(&pn->positive) - gcrdt_query(&pn->negative);
}

void pncrdt_merge(PNCounter *dst, const PNCounter *src) {
    gcrdt_merge(&dst->positive, &src->positive);
    gcrdt_merge(&dst->negative, &src->negative);
}


/* ================================================================
 * CRDT: G-Set (Grow-only Set)
 *
 * Elements can only be added, never removed.
 * Merge is set union.
 * ================================================================ */

void gset_init(GSet *gs) {
    gs->count = 0;
}

bool gset_add(GSet *gs, int element) {
    if (gset_contains(gs, element)) return true;  /* Already present */
    if (gs->count >= GSET_MAX_ELEMENTS) return false;
    gs->elements[gs->count++] = element;
    return true;
}

bool gset_contains(const GSet *gs, int element) {
    for (int i = 0; i < gs->count; i++)
        if (gs->elements[i] == element) return true;
    return false;
}

void gset_merge(GSet *dst, const GSet *src) {
    /* Union: add all elements from src not already in dst */
    for (int i = 0; i < src->count; i++) {
        gset_add(dst, src->elements[i]);
    }
}

int gset_size(const GSet *gs) {
    return gs->count;
}


/* ================================================================
 * CRDT: LWW-Element-Set (Last-Writer-Wins Set)
 *
 * Supports add and remove via timestamps.
 * An element is present if the latest timestamp is an ADD
 * (not tombstoned). Merge picks the latest timestamp for
 * each element.
 * ================================================================ */

void lww_init(LWWSet *lw) {
    lw->count = 0;
}

static LWWEntry *lww_find(LWWSet *lw, int element) {
    for (int i = 0; i < lw->count; i++)
        if (lw->entries[i].element == element) return &lw->entries[i];
    return NULL;
}

bool lww_add(LWWSet *lw, int element, uint64_t timestamp) {
    LWWEntry *entry = lww_find(lw, element);
    if (entry) {
        if (timestamp > entry->timestamp) {
            entry->timestamp  = timestamp;
            entry->tombstone  = false;
        }
        return true;
    }
    if (lw->count >= LWW_MAX_ELEMENTS) return false;
    lw->entries[lw->count].element   = element;
    lw->entries[lw->count].timestamp = timestamp;
    lw->entries[lw->count].tombstone = false;
    lw->count++;
    return true;
}

bool lww_remove(LWWSet *lw, int element, uint64_t timestamp) {
    LWWEntry *entry = lww_find(lw, element);
    if (entry) {
        if (timestamp > entry->timestamp) {
            entry->timestamp  = timestamp;
            entry->tombstone  = true;
        }
        return true;
    }
    /* Element not yet seen: add as tombstone */
    if (lw->count >= LWW_MAX_ELEMENTS) return false;
    lw->entries[lw->count].element   = element;
    lw->entries[lw->count].timestamp = timestamp;
    lw->entries[lw->count].tombstone = true;
    lw->count++;
    return true;
}

bool lww_contains(const LWWSet *lw, uint64_t current_time) {
    /* An element is contained if it exists AND is not tombstoned */
    (void)current_time;
    for (int i = 0; i < lw->count; i++) {
        if (!lw->entries[i].tombstone) return true;
    }
    return false;
}

void lww_merge(LWWSet *dst, const LWWSet *src) {
    /* For each entry in src, if it exists in dst with an older
     * timestamp, update it. Otherwise add it. */
    for (int i = 0; i < src->count; i++) {
        LWWEntry *existing = lww_find(dst, src->entries[i].element);
        if (existing) {
            if (src->entries[i].timestamp > existing->timestamp) {
                existing->timestamp = src->entries[i].timestamp;
                existing->tombstone = src->entries[i].tombstone;
            }
        } else {
            if (dst->count < LWW_MAX_ELEMENTS) {
                dst->entries[dst->count++] = src->entries[i];
            }
        }
    }
}

int lww_size(const LWWSet *lw, uint64_t current_time) {
    (void)current_time;
    int size = 0;
    for (int i = 0; i < lw->count; i++)
        if (!lw->entries[i].tombstone) size++;
    return size;
}


/* ================================================================
 * Vector Clocks
 *
 * Implements Lamport's happens-before partial ordering using
 * vector clocks. Each process increments its own counter on
 * events and merges on message receipt.
 *
 * Reference:
 *   Fidge (1988) "Timestamps in Message-Passing Systems"
 *   Mattern (1989) "Virtual Time and Global States"
 *   Schwarz & Mattern (1994) "Detecting Causal Relationships"
 * ================================================================ */

void vc_init(VectorClock *vc, int n) {
    vc->process_count = n;
    for (int i = 0; i < VC_MAX_PROCESSES; i++)
        vc->clocks[i] = 0;
}

void vc_tick(VectorClock *vc, int process_id) {
    if (process_id >= 0 && process_id < vc->process_count)
        vc->clocks[process_id]++;
}

void vc_merge(VectorClock *dst, const VectorClock *src) {
    /* Element-wise maximum for clock merge.
     * After merge, dst dominates both original clocks. */
    int n = dst->process_count;
    if (src->process_count < n) n = src->process_count;
    for (int i = 0; i < n; i++) {
        if (src->clocks[i] > dst->clocks[i])
            dst->clocks[i] = src->clocks[i];
    }
}

VCRelation vc_compare(const VectorClock *a, const VectorClock *b) {
    int n = a->process_count;
    if (b->process_count < n) n = b->process_count;

    bool a_le_b = true;  /* for all i: a[i] <= b[i] */
    bool b_le_a = true;  /* for all i: b[i] <= a[i] */

    for (int i = 0; i < n; i++) {
        if (a->clocks[i] > b->clocks[i]) a_le_b = false;
        if (b->clocks[i] > a->clocks[i]) b_le_a = false;
    }

    if (a_le_b && b_le_a) return VC_EQUAL;
    if (a_le_b)           return VC_BEFORE;    /* a happened before b */
    if (b_le_a)           return VC_AFTER;     /* a happened after b */
    return VC_CONCURRENT;
}

const char *vc_relation_name(VCRelation rel) {
    switch (rel) {
        case VC_BEFORE:     return "BEFORE (happens-before)";
        case VC_AFTER:      return "AFTER (happens-after)";
        case VC_CONCURRENT: return "CONCURRENT (no ordering)";
        case VC_EQUAL:      return "EQUAL";
        default:            return "UNKNOWN";
    }
}


/* ================================================================
 * Eventual Consistency: Read Repair + Hinted Handoff
 *
 * When a read detects stale replicas, it repairs them (read repair).
 * When a write targets an offline replica, another node stores
 * a hint and delivers it when the replica reconnects.
 *
 * References:
 *   Dynamo (DeCandia et al., SOSP 2007)
 *   Cassandra (Lakshman & Malik, SIGOPS 2010)
 * ================================================================ */

void ec_init(ECSystem *ec, int n, int w_quorum, int r_quorum) {
    ec->replica_count = n;
    ec->write_quorum  = w_quorum > 0 ? w_quorum : n / 2 + 1;
    ec->read_quorum   = r_quorum > 0 ? r_quorum : n / 2 + 1;

    for (int i = 0; i < n; i++) {
        ec->replicas[i].replica_id = i;
        ec->replicas[i].data_count = 0;
        ec->replicas[i].online     = true;
        ec->replicas[i].hint_count = 0;
        for (int j = 0; j < EC_MAX_DATA; j++) {
            ec->replicas[i].hints[j]       = 0;
            ec->replicas[i].hint_targets[j] = -1;
        }
    }
}

void ec_disconnect(ECSystem *ec, int replica_id) {
    if (replica_id >= 0 && replica_id < ec->replica_count)
        ec->replicas[replica_id].online = false;
}

void ec_reconnect(ECSystem *ec, int replica_id) {
    if (replica_id < 0 || replica_id >= ec->replica_count) return;
    ec->replicas[replica_id].online = true;

    /* Deliver any pending hinted handoffs TO this replica */
    for (int i = 0; i < ec->replica_count; i++) {
        if (i != replica_id && ec->replicas[i].online) {
            ec_hinted_handoff(ec, i, replica_id);
        }
    }

    /* Read repair: catch up from online replicas */
    for (int j = 0; j < ec->replicas[replica_id].data_count; j++) {
        ec_read_repair(ec, ec->replicas[replica_id].data[j].key);
    }
}

static ECDataEntry *ec_find_entry(ECReplica *rep, int key) {
    for (int i = 0; i < rep->data_count; i++)
        if (rep->data[i].key == key) return &rep->data[i];
    return NULL;
}

bool ec_write(ECSystem *ec, int key, int value, uint64_t version) {
    int written = 0;
    int pending_hints[EC_MAX_REPLICAS];
    int hint_count = 0;

    for (int i = 0; i < ec->replica_count; i++) {
        if (ec->replicas[i].online) {
            ECDataEntry *entry = ec_find_entry(&ec->replicas[i], key);
            if (entry) {
                if (version >= entry->version) {
                    entry->value   = value;
                    entry->version = version;
                }
            } else {
                if (ec->replicas[i].data_count < EC_MAX_DATA) {
                    int idx = ec->replicas[i].data_count++;
                    ec->replicas[i].data[idx].key     = key;
                    ec->replicas[i].data[idx].value   = value;
                    ec->replicas[i].data[idx].version = version;
                }
            }
            written++;
        } else {
            /* Offline: need hinted handoff */
            if (hint_count < EC_MAX_REPLICAS)
                pending_hints[hint_count++] = i;
        }
    }

    /* Store hints on a healthy replica for offline nodes */
    if (hint_count > 0 && written > 0) {
        /* Pick the first online replica as hint holder */
        int holder = -1;
        for (int i = 0; i < ec->replica_count; i++) {
            if (ec->replicas[i].online) { holder = i; break; }
        }
        if (holder >= 0) {
            ECReplica *rep = &ec->replicas[holder];
            for (int h = 0; h < hint_count; h++) {
                if (rep->hint_count < EC_MAX_DATA) {
                    rep->hints[rep->hint_count]       = value;
                    rep->hint_targets[rep->hint_count] = pending_hints[h];
                    rep->hint_count++;
                }
            }
        }
    }

    return written >= ec->write_quorum;
}

bool ec_read(ECSystem *ec, int key, int *value, uint64_t *version) {
    int      best_val = 0;
    uint64_t best_ver = 0;
    int      responses = 0;

    for (int i = 0; i < ec->replica_count; i++) {
        if (!ec->replicas[i].online) continue;
        ECDataEntry *entry = ec_find_entry(&ec->replicas[i], key);
        if (entry) {
            responses++;
            if (entry->version > best_ver) {
                best_ver = entry->version;
                best_val = entry->value;
            }
        }
    }

    if (responses >= ec->read_quorum) {
        if (value)   *value   = best_val;
        if (version) *version = best_ver;
        return true;
    }
    return false;
}

void ec_read_repair(ECSystem *ec, int key) {
    /* Find the most recent version across all online replicas
     * and push it to any replica with a stale version. */
    int      best_val = 0;
    uint64_t best_ver = 0;
    bool     found    = false;

    for (int i = 0; i < ec->replica_count; i++) {
        if (!ec->replicas[i].online) continue;
        ECDataEntry *entry = ec_find_entry(&ec->replicas[i], key);
        if (entry && entry->version > best_ver) {
            best_ver = entry->version;
            best_val = entry->value;
            found    = true;
        }
    }

    if (!found) return;

    /* Push the latest version to all online replicas */
    for (int i = 0; i < ec->replica_count; i++) {
        if (!ec->replicas[i].online) continue;
        ECDataEntry *entry = ec_find_entry(&ec->replicas[i], key);
        if (entry) {
            if (entry->version < best_ver) {
                entry->value   = best_val;
                entry->version = best_ver;
            }
        } else {
            if (ec->replicas[i].data_count < EC_MAX_DATA) {
                int idx = ec->replicas[i].data_count++;
                ec->replicas[i].data[idx].key     = key;
                ec->replicas[i].data[idx].value   = best_val;
                ec->replicas[i].data[idx].version = best_ver;
            }
        }
    }
}

bool ec_hinted_handoff(ECSystem *ec, int from_replica, int to_replica) {
    if (from_replica < 0 || from_replica >= ec->replica_count) return false;
    if (to_replica   < 0 || to_replica   >= ec->replica_count) return false;

    ECReplica *src = &ec->replicas[from_replica];
    ECReplica *dst = &ec->replicas[to_replica];

    if (!dst->online) return false;

    bool delivered = false;

    /* Deliver hints destined for to_replica */
    for (int i = 0; i < src->hint_count; i++) {
        if (src->hint_targets[i] == to_replica) {
            /* Apply the hinted value */
            if (dst->data_count < EC_MAX_DATA) {
                dst->data[dst->data_count].key     = i; /* Use index as key */
                dst->data[dst->data_count].value   = (int)src->hints[i];
                dst->data[dst->data_count].version = (uint64_t)i;
                dst->data_count++;
            }
            /* Remove the delivered hint */
            src->hints[i] = src->hints[--src->hint_count];
            src->hint_targets[i] = src->hint_targets[src->hint_count];
            i--;
            delivered = true;
        }
    }

    return delivered;
}

void ec_print_state(const ECSystem *ec) {
    printf("=== EC System (%d replicas, W=%d R=%d) ===\n",
           ec->replica_count, ec->write_quorum, ec->read_quorum);
    for (int i = 0; i < ec->replica_count; i++) {
        const ECReplica *r = &ec->replicas[i];
        printf("  Replica %d [%s] data=%d hints=%d\n",
               r->replica_id, r->online ? "ONLINE" : "OFFLINE",
               r->data_count, r->hint_count);
        for (int j = 0; j < r->data_count; j++) {
            printf("    key=%d val=%d ver=%llu\n",
                   r->data[j].key, r->data[j].value,
                   (unsigned long long)r->data[j].version);
        }
    }
}
