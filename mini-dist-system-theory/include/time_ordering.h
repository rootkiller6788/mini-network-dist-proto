#ifndef TIME_ORDERING_H
#define TIME_ORDERING_H

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#define VECTOR_CLOCK_MAX_NODES 8

typedef struct {
    uint64_t counter;
} LamportClock;

typedef struct {
    uint64_t counters[VECTOR_CLOCK_MAX_NODES];
    int node_count;
} VectorClock;

typedef struct {
    uint64_t physical_time_us;
    uint64_t logical_counter;
} HybridLogicalClock;

typedef enum {
    VC_BEFORE,
    VC_AFTER,
    VC_CONCURRENT,
    VC_EQUAL
} VCCompareResult;

uint64_t lamport_increment(LamportClock *lc);
uint64_t lamport_tick(LamportClock *lc, uint64_t received_clock);
int lamport_compare(uint64_t a, uint64_t b);
void lamport_reset(LamportClock *lc);

void vector_clock_init(VectorClock *vc, int node_count);
uint64_t vector_clock_increment(VectorClock *vc, int node_id);
void vector_clock_merge(VectorClock *vc, const VectorClock *other);
VCCompareResult vector_clock_compare(const VectorClock *a, const VectorClock *b);
uint64_t vector_clock_get(const VectorClock *vc, int node_id);
bool vector_clock_equals(const VectorClock *a, const VectorClock *b);
const char *vc_compare_string(VCCompareResult result);

void hlc_tick(HybridLogicalClock *hlc);
void hlc_update(HybridLogicalClock *hlc, uint64_t wall_time_us);
void hlc_on_receive(HybridLogicalClock *hlc, uint64_t msg_physical, uint64_t msg_logical);
int hlc_compare(const HybridLogicalClock *a, const HybridLogicalClock *b);

#endif
