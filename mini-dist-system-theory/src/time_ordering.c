#define _GNU_SOURCE
#include "time_ordering.h"
#include <string.h>
#include <time.h>

uint64_t lamport_increment(LamportClock *lc)
{
    lc->counter++;
    return lc->counter;
}

uint64_t lamport_tick(LamportClock *lc, uint64_t received_clock)
{
    if (received_clock > lc->counter) {
        lc->counter = received_clock;
    }
    lc->counter++;
    return lc->counter;
}

int lamport_compare(uint64_t a, uint64_t b)
{
    if (a < b) return -1;
    if (a > b) return 1;
    return 0;
}

void vector_clock_init(VectorClock *vc, int node_count)
{
    int i;
    vc->node_count = node_count;
    for (i = 0; i < VECTOR_CLOCK_MAX_NODES; i++) {
        vc->counters[i] = 0;
    }
}

uint64_t vector_clock_increment(VectorClock *vc, int node_id)
{
    if (node_id < 0 || node_id >= vc->node_count) return 0;
    vc->counters[node_id]++;
    return vc->counters[node_id];
}

void vector_clock_merge(VectorClock *vc, const VectorClock *other)
{
    int i;
    int n = vc->node_count > other->node_count ? vc->node_count : other->node_count;
    if (n > VECTOR_CLOCK_MAX_NODES) n = VECTOR_CLOCK_MAX_NODES;
    vc->node_count = n;
    for (i = 0; i < n; i++) {
        if (other->counters[i] > vc->counters[i]) {
            vc->counters[i] = other->counters[i];
        }
    }
}

VCCompareResult vector_clock_compare(const VectorClock *a, const VectorClock *b)
{
    int i;
    int n = a->node_count > b->node_count ? a->node_count : b->node_count;
    bool a_leq_b = true;
    bool b_leq_a = true;

    for (i = 0; i < n; i++) {
        uint64_t ai = (i < a->node_count) ? a->counters[i] : 0;
        uint64_t bi = (i < b->node_count) ? b->counters[i] : 0;
        if (ai > bi) a_leq_b = false;
        if (bi > ai) b_leq_a = false;
    }

    if (a_leq_b && b_leq_a) return VC_EQUAL;
    if (a_leq_b) return VC_BEFORE;
    if (b_leq_a) return VC_AFTER;
    return VC_CONCURRENT;
}

uint64_t vector_clock_get(const VectorClock *vc, int node_id)
{
    if (node_id < 0 || node_id >= vc->node_count) return 0;
    return vc->counters[node_id];
}

static uint64_t get_wall_time_us(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (uint64_t)ts.tv_sec * 1000000ULL + (uint64_t)ts.tv_nsec / 1000ULL;
}

void hlc_tick(HybridLogicalClock *hlc)
{
    uint64_t wall = get_wall_time_us();
    if (wall > hlc->physical_time_us) {
        hlc->physical_time_us = wall;
        hlc->logical_counter = 0;
    } else {
        hlc->logical_counter++;
    }
}

void hlc_update(HybridLogicalClock *hlc, uint64_t wall_time_us)
{
    if (wall_time_us > hlc->physical_time_us) {
        hlc->physical_time_us = wall_time_us;
        hlc->logical_counter = 0;
    } else {
        hlc->logical_counter++;
    }
}

void hlc_on_receive(HybridLogicalClock *hlc, uint64_t msg_physical, uint64_t msg_logical)
{
    uint64_t wall = get_wall_time_us();
    uint64_t max_pt = hlc->physical_time_us;
    if (msg_physical > max_pt) max_pt = msg_physical;
    if (wall > max_pt) max_pt = wall;

    hlc->physical_time_us = max_pt;

    if (max_pt == hlc->physical_time_us && max_pt == msg_physical) {
        uint64_t max_lc = hlc->logical_counter;
        if (msg_logical > max_lc) max_lc = msg_logical;
        hlc->logical_counter = max_lc + 1;
    } else if (max_pt == hlc->physical_time_us) {
        hlc->logical_counter++;
    } else {
        hlc->logical_counter = 0;
    }
}

int hlc_compare(const HybridLogicalClock *a, const HybridLogicalClock *b)
{
    if (a->physical_time_us < b->physical_time_us) return -1;
    if (a->physical_time_us > b->physical_time_us) return 1;
    if (a->logical_counter < b->logical_counter) return -1;
    if (a->logical_counter > b->logical_counter) return 1;
    return 0;
}

void lamport_reset(LamportClock *lc)
{
    lc->counter = 0;
}

bool vector_clock_equals(const VectorClock *a, const VectorClock *b)
{
    int i;
    int n = a->node_count > b->node_count ? a->node_count : b->node_count;
    for (i = 0; i < n; i++) {
        uint64_t ai = (i < a->node_count) ? a->counters[i] : 0;
        uint64_t bi = (i < b->node_count) ? b->counters[i] : 0;
        if (ai != bi) return false;
    }
    return true;
}

const char *vc_compare_string(VCCompareResult result)
{
    switch (result) {
        case VC_BEFORE:     return "happens-before";
        case VC_AFTER:      return "happens-after";
        case VC_CONCURRENT: return "concurrent";
        case VC_EQUAL:      return "equal";
        default:            return "unknown";
    }
}
