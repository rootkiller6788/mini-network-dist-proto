#ifndef CRDT_H
#define CRDT_H

#include <stdbool.h>
#include <stdint.h>

#define CRDT_MAX_NODES 8
#define CRDT_MAX_ELEMENTS 256
#define CRDT_SET_SIZE 256
#define CRDT_LWW_VALUE_LEN 64
#define ORSET_TAG_LEN 32

typedef struct {
    uint64_t counters[CRDT_MAX_NODES];
    int node_count;
} GCounter;

typedef struct {
    GCounter inc;
    GCounter dec;
} PNCounter;

typedef struct {
    uint64_t bits[4];
} GSet;

typedef struct {
    uint64_t bits[4];
    uint64_t tombstone_bits[4];
} TwoPSet;

typedef struct {
    char tag[ORSET_TAG_LEN];
    int element;
} ORSetEntry;

typedef struct {
    ORSetEntry entries[CRDT_MAX_ELEMENTS];
    int count;
    int node_id;
    int seq_counter;
} ORSet;

typedef struct {
    char value[CRDT_LWW_VALUE_LEN];
    uint64_t timestamp;
    int node_id;
} LWWRegister;

void gc_init(GCounter *gc, int node_count);
void gc_inc(GCounter *gc, int node_id);
uint64_t gc_value(const GCounter *gc);
void gc_merge(GCounter *gc, const GCounter *other);

void pn_init(PNCounter *pn, int node_count);
void pn_inc(PNCounter *pn, int node_id);
void pn_dec(PNCounter *pn, int node_id);
int64_t pn_value(const PNCounter *pn);
void pn_merge(PNCounter *pn, const PNCounter *other);

void gset_init(GSet *gs);
void gset_add(GSet *gs, int element);
void gset_remove(GSet *gs, int element);
bool gset_contains(const GSet *gs, int element);
void gset_merge(GSet *gs, const GSet *other);

void twopset_init(TwoPSet *tps);
void twopset_add(TwoPSet *tps, int element);
void twopset_remove(TwoPSet *tps, int element);
bool twopset_contains(const TwoPSet *tps, int element);
void twopset_merge(TwoPSet *tps, const TwoPSet *other);

void orset_init(ORSet *ors, int node_id);
void ors_add(ORSet *ors, int element);
void ors_remove(ORSet *ors, int element);
int ors_count(const ORSet *ors);
bool ors_contains(const ORSet *ors, int element);
void ors_merge(ORSet *ors, const ORSet *other);

void lww_init(LWWRegister *lww, int node_id);
void lww_set(LWWRegister *lww, const char *value);
const char *lww_get(const LWWRegister *lww);
void lww_merge(LWWRegister *lww, const LWWRegister *other);

#endif
