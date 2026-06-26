#include "crdt.h"
#include <stdio.h>
#include <string.h>

void gc_init(GCounter *gc, int node_count)
{
    int i;
    gc->node_count = node_count;
    for (i = 0; i < CRDT_MAX_NODES; i++) {
        gc->counters[i] = 0;
    }
}

void gc_inc(GCounter *gc, int node_id)
{
    if (node_id >= 0 && node_id < gc->node_count) {
        gc->counters[node_id]++;
    }
}

uint64_t gc_value(const GCounter *gc)
{
    int i;
    uint64_t total = 0;
    for (i = 0; i < gc->node_count; i++) {
        total += gc->counters[i];
    }
    return total;
}

void gc_merge(GCounter *gc, const GCounter *other)
{
    int i;
    int n = gc->node_count > other->node_count ? gc->node_count : other->node_count;
    if (n > CRDT_MAX_NODES) n = CRDT_MAX_NODES;
    for (i = 0; i < n; i++) {
        if (other->counters[i] > gc->counters[i]) {
            gc->counters[i] = other->counters[i];
        }
    }
}

void pn_init(PNCounter *pn, int node_count)
{
    gc_init(&pn->inc, node_count);
    gc_init(&pn->dec, node_count);
}

void pn_inc(PNCounter *pn, int node_id)
{
    gc_inc(&pn->inc, node_id);
}

void pn_dec(PNCounter *pn, int node_id)
{
    gc_inc(&pn->dec, node_id);
}

int64_t pn_value(const PNCounter *pn)
{
    return (int64_t)gc_value(&pn->inc) - (int64_t)gc_value(&pn->dec);
}

void pn_merge(PNCounter *pn, const PNCounter *other)
{
    gc_merge(&pn->inc, &other->inc);
    gc_merge(&pn->dec, &other->dec);
}

static int bit_index(int element)
{
    if (element < 0 || element >= CRDT_SET_SIZE) return -1;
    return element;
}

void gset_init(GSet *gs)
{
    int i;
    for (i = 0; i < 4; i++) {
        gs->bits[i] = 0;
    }
}

void gset_add(GSet *gs, int element)
{
    int idx = bit_index(element);
    if (idx < 0) return;
    gs->bits[idx / 64] |= (1ULL << (idx % 64));
}

void gset_remove(GSet *gs, int element)
{
    int idx = bit_index(element);
    if (idx < 0) return;
    gs->bits[idx / 64] &= ~(1ULL << (idx % 64));
}

bool gset_contains(const GSet *gs, int element)
{
    int idx = bit_index(element);
    if (idx < 0) return false;
    return (gs->bits[idx / 64] & (1ULL << (idx % 64))) != 0;
}

void gset_merge(GSet *gs, const GSet *other)
{
    int i;
    for (i = 0; i < 4; i++) {
        gs->bits[i] |= other->bits[i];
    }
}

void twopset_init(TwoPSet *tps)
{
    int i;
    for (i = 0; i < 4; i++) {
        tps->bits[i] = 0;
        tps->tombstone_bits[i] = 0;
    }
}

void twopset_add(TwoPSet *tps, int element)
{
    int idx = bit_index(element);
    if (idx < 0) return;
    tps->bits[idx / 64] |= (1ULL << (idx % 64));
}

void twopset_remove(TwoPSet *tps, int element)
{
    int idx = bit_index(element);
    if (idx < 0) return;
    tps->tombstone_bits[idx / 64] |= (1ULL << (idx % 64));
}

bool twopset_contains(const TwoPSet *tps, int element)
{
    int idx = bit_index(element);
    if (idx < 0) return false;
    bool in_added = (tps->bits[idx / 64] & (1ULL << (idx % 64))) != 0;
    bool in_removed = (tps->tombstone_bits[idx / 64] & (1ULL << (idx % 64))) != 0;
    return in_added && !in_removed;
}

void twopset_merge(TwoPSet *tps, const TwoPSet *other)
{
    int i;
    for (i = 0; i < 4; i++) {
        tps->bits[i] |= other->bits[i];
        tps->tombstone_bits[i] |= other->tombstone_bits[i];
    }
}

void orset_init(ORSet *ors, int node_id)
{
    ors->count = 0;
    ors->node_id = node_id;
    ors->seq_counter = 0;
}

void ors_add(ORSet *ors, int element)
{
    int i;
    ORSetEntry *e;
    char tag[ORSET_TAG_LEN];

    for (i = 0; i < ors->count; i++) {
        if (ors->entries[i].element == element) {
            return;
        }
    }

    if (ors->count >= CRDT_MAX_ELEMENTS) return;

    e = &ors->entries[ors->count];
    e->element = element;
    snprintf(e->tag, ORSET_TAG_LEN, "n%d-s%d", ors->node_id, ors->seq_counter++);
    ors->count++;
}

static void ors_remove_internal(ORSet *ors, int element)
{
    int i;
    if (ors->count >= CRDT_MAX_ELEMENTS) return;

    for (i = 0; i < ors->count; i++) {
        if (ors->entries[i].element == element &&
            ors->entries[i].tag[0] != '!') {
            ors->entries[i].element = -1;
            ors->entries[i].tag[0] = '!';
            return;
        }
    }
}

void ors_remove(ORSet *ors, int element)
{
    ors_remove_internal(ors, element);
}

bool ors_contains(const ORSet *ors, int element)
{
    int i;
    for (i = 0; i < ors->count; i++) {
        if (ors->entries[i].element == element &&
            ors->entries[i].tag[0] != '!') {
            return true;
        }
    }
    return false;
}

int ors_count(const ORSet *ors)
{
    int i, count = 0;
    for (i = 0; i < ors->count; i++) {
        if (ors->entries[i].element >= 0 &&
            ors->entries[i].tag[0] != '!') {
            count++;
        }
    }
    return count;
}

void ors_merge(ORSet *ors, const ORSet *other)
{
    int i, j;
    bool found;
    ORSetEntry *e;

    for (i = 0; i < other->count; i++) {
        e = &other->entries[i];
        found = false;

        for (j = 0; j < ors->count; j++) {
            if (strcmp(ors->entries[j].tag, e->tag) == 0) {
                found = true;
                break;
            }
        }

        if (!found && ors->count < CRDT_MAX_ELEMENTS) {
            ors->entries[ors->count] = *e;
            ors->count++;
        }
    }

    for (i = 0; i < ors->count; i++) {
        for (j = 0; j < ors->count; j++) {
            if (i != j && strcmp(ors->entries[i].tag, ors->entries[j].tag) == 0) {
                if (ors->entries[j].tag[0] != '!' &&
                    ors->entries[i].tag[0] == '!') {
                    ors->entries[j] = ors->entries[i];
                }
            }
        }
    }
}

void lww_init(LWWRegister *lww, int node_id)
{
    lww->value[0] = '\0';
    lww->timestamp = 0;
    lww->node_id = node_id;
}

void lww_set(LWWRegister *lww, const char *value)
{
    lww->timestamp++;
    strncpy(lww->value, value, CRDT_LWW_VALUE_LEN - 1);
    lww->value[CRDT_LWW_VALUE_LEN - 1] = '\0';
}

const char *lww_get(const LWWRegister *lww)
{
    return lww->value;
}

void lww_merge(LWWRegister *lww, const LWWRegister *other)
{
    if (other->timestamp > lww->timestamp ||
        (other->timestamp == lww->timestamp && other->node_id > lww->node_id)) {
        lww->timestamp = other->timestamp;
        lww->node_id = other->node_id;
        strncpy(lww->value, other->value, CRDT_LWW_VALUE_LEN - 1);
        lww->value[CRDT_LWW_VALUE_LEN - 1] = '\0';
    }
}
