#ifndef ADVANCED_TOPICS_H
#define ADVANCED_TOPICS_H

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

/* ================================================================
 * L8: Advanced Topics in Distributed Systems
 *
 * CRDTs (Conflict-free Replicated Data Types):
 *   G-Counter, PN-Counter, G-Set, LWW-Element-Set
 * Vector Clocks: Partial ordering, concurrent detection
 * Eventual Consistency: Read Repair, Hinted Handoff
 * ================================================================ */

/* --- CRDT: G-Counter (Grow-only Counter) --- */

#define GCRDT_MAX_REPLICAS 8

typedef struct {
    int  counts[GCRDT_MAX_REPLICAS];
    int  replica_count;
    int  replica_id;
} GCounter;

void gcrdt_init(GCounter *gc, int replica_id, int n);
void gcrdt_inc(GCounter *gc);
int  gcrdt_query(const GCounter *gc);
void gcrdt_merge(GCounter *dst, const GCounter *src);

/* --- CRDT: PN-Counter (Positive-Negative Counter) --- */

typedef struct {
    GCounter positive;
    GCounter negative;
    int      replica_id;
} PNCounter;

void pncrdt_init(PNCounter *pn, int replica_id, int n);
void pncrdt_inc(PNCounter *pn);
void pncrdt_dec(PNCounter *pn);
int  pncrdt_query(const PNCounter *pn);
void pncrdt_merge(PNCounter *dst, const PNCounter *src);

/* --- CRDT: G-Set (Grow-only Set) --- */

#define GSET_MAX_ELEMENTS 64

typedef struct {
    int  elements[GSET_MAX_ELEMENTS];
    int  count;
} GSet;

void gset_init(GSet *gs);
bool gset_add(GSet *gs, int element);
bool gset_contains(const GSet *gs, int element);
void gset_merge(GSet *dst, const GSet *src);
int  gset_size(const GSet *gs);

/* --- CRDT: LWW-Element-Set (Last-Writer-Wins) --- */

#define LWW_MAX_ELEMENTS 64

typedef struct {
    int      element;
    uint64_t timestamp;
    bool     tombstone;  /* true = removed */
} LWWEntry;

typedef struct {
    LWWEntry entries[LWW_MAX_ELEMENTS];
    int      count;
} LWWSet;

void lww_init(LWWSet *lw);
bool lww_add(LWWSet *lw, int element, uint64_t timestamp);
bool lww_remove(LWWSet *lw, int element, uint64_t timestamp);
bool lww_contains(const LWWSet *lw, uint64_t current_time);
void lww_merge(LWWSet *dst, const LWWSet *src);
int  lww_size(const LWWSet *lw, uint64_t current_time);

/* --- Vector Clocks --- */

#define VC_MAX_PROCESSES 8

typedef struct {
    int  clocks[VC_MAX_PROCESSES];
    int  process_count;
} VectorClock;

typedef enum {
    VC_BEFORE,
    VC_AFTER,
    VC_CONCURRENT,
    VC_EQUAL
} VCRelation;

void vc_init(VectorClock *vc, int n);
void vc_tick(VectorClock *vc, int process_id);
void vc_merge(VectorClock *dst, const VectorClock *src);
VCRelation vc_compare(const VectorClock *a, const VectorClock *b);
const char *vc_relation_name(VCRelation rel);

/* --- Eventual Consistency: Read Repair + Hinted Handoff --- */

#define EC_MAX_REPLICAS  8
#define EC_MAX_DATA      32

typedef struct {
    int       key;
    int       value;
    uint64_t  version;
} ECDataEntry;

typedef struct {
    ECDataEntry data[EC_MAX_DATA];
    int         data_count;
    int         replica_id;
    bool        online;
    uint64_t    hints[EC_MAX_DATA];    /* hinted values for offline replicas */
    int         hint_targets[EC_MAX_DATA];
    int         hint_count;
} ECReplica;

typedef struct {
    ECReplica replicas[EC_MAX_REPLICAS];
    int       replica_count;
    int       write_quorum;
    int       read_quorum;
} ECSystem;

void ec_init(ECSystem *ec, int n, int w_quorum, int r_quorum);
bool ec_write(ECSystem *ec, int key, int value, uint64_t version);
bool ec_read(ECSystem *ec, int key, int *value, uint64_t *version);
void ec_read_repair(ECSystem *ec, int key);
bool ec_hinted_handoff(ECSystem *ec, int from_replica, int to_replica);
void ec_reconnect(ECSystem *ec, int replica_id);
void ec_disconnect(ECSystem *ec, int replica_id);
void ec_print_state(const ECSystem *ec);

#endif
