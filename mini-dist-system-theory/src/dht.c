/* dht.c - Distributed Hash Tables & Consistent Hashing. Ref: Chord (Stoica 2001), MIT 6.824. */
#include "dht.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int dht_hash(int key, int space) {
    unsigned int h = (unsigned int)key * 2654435761U;
    return (int)(h % (unsigned int)space);
}
int dht_distance(int a, int b, int space) {
    if (a <= b)
        return b - a;
    return space - a + b;
}
bool dht_in_range(int key, int start, int end, int space, bool inclusive_end) {
    (void)space;
    if (start < end) {
        if (inclusive_end)
            return key > start && key <= end;
        else
            return key > start && key < end;
    }
    if (inclusive_end)
        return key > start || key <= end;
    else
        return key > start || key < end;
}
int dht_find_successor(const ChordRing *ring, int key) {
    int i, best = -1, best_dist = ring->hash_space + 1;
    for (i = 0; i < ring->node_count; i++) {
        if (!ring->nodes[i].active)
            continue;
        int dist = (ring->nodes[i].id - key + ring->hash_space) % ring->hash_space;
        if (dist < best_dist) {
            best_dist = dist;
            best = i;
        }
    }
    return best;
}
void chord_init(ChordRing *ring, int hash_space) {
    ring->node_count = 0;
    ring->hash_space = hash_space > 0 ? hash_space : DHT_HASH_SPACE;
    ring->replication_factor = 3;
    int i, j;
    for (i = 0; i < DHT_MAX_NODES; i++) {
        ring->nodes[i].active = false;
        ring->nodes[i].key_count = 0;
        for (j = 0; j < DHT_FINGER_TABLE_SIZE; j++) {
            ring->finger_table[i][j].start = -1;
            ring->finger_table[i][j].node_id = -1;
        }
    }
}
int chord_join(ChordRing *ring, int node_id) {
    int i;
    if (ring->node_count >= DHT_MAX_NODES)
        return -1;
    for (i = 0; i < ring->node_count; i++) {
        if (ring->nodes[i].id == node_id && ring->nodes[i].active)
            return -1;
    }
    ring->nodes[ring->node_count].id = node_id;
    ring->nodes[ring->node_count].active = true;
    ring->nodes[ring->node_count].key_count = 0;
    ring->node_count++;
    chord_build_finger_tables(ring);
    return ring->node_count - 1;
}
int chord_leave(ChordRing *ring, int node_id) {
    int i, j;
    for (i = 0; i < ring->node_count; i++) {
        if (ring->nodes[i].id == node_id && ring->nodes[i].active) {
            int succ;
            for (succ = 0; succ < ring->node_count; succ++) {
                if (succ != i && ring->nodes[succ].active)
                    break;
            }
            if (succ < ring->node_count) {
                for (j = 0; j < ring->nodes[i].key_count; j++) {
                    int key = ring->nodes[i].keys[j];
                    if (ring->nodes[succ].key_count < DHT_MAX_NODES)
                        ring->nodes[succ].keys[ring->nodes[succ].key_count++] = key;
                }
            }
            ring->nodes[i].active = false;
            ring->nodes[i].key_count = 0;
            chord_build_finger_tables(ring);
            return 0;
        }
    }
    return -1;
}
int chord_lookup(const ChordRing *ring, int key) {
    int pos = dht_hash(key, ring->hash_space);
    int si = dht_find_successor(ring, pos);
    if (si < 0)
        return -1;
    return ring->nodes[si].id;
}
void chord_store(ChordRing *ring, int node_id, int key) {
    int i;
    for (i = 0; i < ring->node_count; i++) {
        if (ring->nodes[i].id == node_id && ring->nodes[i].active) {
            if (ring->nodes[i].key_count >= DHT_MAX_NODES)
                return;
            int j;
            for (j = 0; j < ring->nodes[i].key_count; j++) {
                if (ring->nodes[i].keys[j] == key)
                    return;
            }
            ring->nodes[i].keys[ring->nodes[i].key_count++] = key;
            return;
        }
    }
}
bool chord_has_key(const ChordRing *ring, int node_id, int key) {
    int i;
    for (i = 0; i < ring->node_count; i++) {
        if (ring->nodes[i].id == node_id && ring->nodes[i].active) {
            int j;
            for (j = 0; j < ring->nodes[i].key_count; j++) {
                if (ring->nodes[i].keys[j] == key)
                    return true;
            }
            return false;
        }
    }
    return false;
}
void chord_build_finger_tables(ChordRing *ring) {
    int i;
    for (i = 0; i < ring->node_count; i++) {
        if (!ring->nodes[i].active)
            continue;
        int k;
        for (k = 0; k < DHT_FINGER_TABLE_SIZE; k++) {
            int start = (ring->nodes[i].id + (1 << k)) % ring->hash_space;
            ring->finger_table[i][k].start = start;
            ring->finger_table[i][k].node_id = chord_lookup(ring, start);
        }
    }
}
int chord_finger_lookup(const ChordRing *ring, int node_id, int key) {
    int i;
    for (i = 0; i < ring->node_count; i++) {
        if (ring->nodes[i].id == node_id && ring->nodes[i].active) {
            int k;
            for (k = DHT_FINGER_TABLE_SIZE - 1; k >= 0; k--) {
                int finger = ring->finger_table[i][k].node_id;
                int start = ring->finger_table[i][k].start;
                if (finger >= 0 && dht_in_range(start, node_id, finger, ring->hash_space, true))
                    return finger;
            }
            return chord_lookup(ring, key);
        }
    }
    return -1;
}
void ch_init(ConsistentHashRing *chr, int total_slots) {
    chr->vnode_count = 0;
    chr->total_slots = total_slots > 0 ? total_slots : DHT_HASH_SPACE;
}
void ch_add_node(ConsistentHashRing *chr, int physical_node, int num_vnodes) {
    int v;
    for (v = 0; v < num_vnodes; v++) {
        if (chr->vnode_count >= DHT_MAX_NODES * DHT_MAX_VNODES)
            return;
        VirtualNode *vn = &chr->vnodes[chr->vnode_count];
        vn->vnode_id = chr->vnode_count;
        vn->physical_node = physical_node;
        vn->position = dht_hash(physical_node * 1000 + v, chr->total_slots);
        chr->vnode_count++;
    }
}
void ch_remove_node(ConsistentHashRing *chr, int physical_node) {
    int read = 0, write = 0;
    while (read < chr->vnode_count) {
        if (chr->vnodes[read].physical_node != physical_node) {
            chr->vnodes[write] = chr->vnodes[read];
            write++;
        }
        read++;
    }
    chr->vnode_count = write;
}
int ch_lookup(const ConsistentHashRing *chr, int key) {
    int hash = dht_hash(key, chr->total_slots);
    int best_idx = -1, best_dist = chr->total_slots + 1, i;
    for (i = 0; i < chr->vnode_count; i++) {
        int dist = (chr->vnodes[i].position - hash + chr->total_slots) % chr->total_slots;
        if (dist < best_dist) {
            best_dist = dist;
            best_idx = i;
        }
    }
    if (best_idx < 0)
        return -1;
    return chr->vnodes[best_idx].physical_node;
}
double ch_load_variance(const ConsistentHashRing *chr, int physical_nodes[], int node_count) {
    int i;
    int *loads = (int *)calloc((size_t)node_count, sizeof(int));
    if (!loads)
        return -1.0;
    for (i = 0; i < chr->vnode_count; i++) {
        int pn = chr->vnodes[i].physical_node;
        int j;
        for (j = 0; j < node_count; j++) {
            if (physical_nodes[j] == pn) {
                loads[j]++;
                break;
            }
        }
    }
    double mean = (double)chr->vnode_count / node_count, var = 0.0;
    for (i = 0; i < node_count; i++) {
        double diff = loads[i] - mean;
        var += diff * diff;
    }
    var /= node_count;
    free(loads);
    return var;
}
void dht_print_ring(const ChordRing *ring) {
    int i, j;
    printf("=== Chord Ring (space=%d, nodes=%d, rf=%d) ===\n", ring->hash_space, ring->node_count,
           ring->replication_factor);
    for (i = 0; i < ring->node_count; i++) {
        if (!ring->nodes[i].active)
            continue;
        printf("  Node %d: keys={", ring->nodes[i].id);
        for (j = 0; j < ring->nodes[i].key_count; j++)
            printf("%s%d", j > 0 ? ", " : "", ring->nodes[i].keys[j]);
        printf("} fingers={");
        for (j = 0; j < DHT_FINGER_TABLE_SIZE; j++) {
            if (ring->finger_table[i][j].node_id >= 0)
                printf("%s%d", j > 0 ? ", " : "", ring->finger_table[i][j].node_id);
        }
        printf("}\n");
    }
}
void ch_print_ring(const ConsistentHashRing *chr) {
    int i;
    printf("=== Consistent Hash Ring (vnode_count=%d, slots=%d) ===\n", chr->vnode_count,
           chr->total_slots);
    for (i = 0; i < chr->vnode_count; i++)
        printf("  vnode %d: physical=%d position=%d\n", chr->vnodes[i].vnode_id,
               chr->vnodes[i].physical_node, chr->vnodes[i].position);
}
const char *dht_node_status(bool active) {
    return active ? "ACTIVE" : "INACTIVE";
}
int dht_rebalance_score(const ChordRing *ring) {
    int i, max_kc = 0, min_kc = DHT_MAX_NODES + 1, active_count = 0;
    for (i = 0; i < ring->node_count; i++) {
        if (!ring->nodes[i].active)
            continue;
        active_count++;
        int kc = ring->nodes[i].key_count;
        if (kc > max_kc)
            max_kc = kc;
        if (kc < min_kc)
            min_kc = kc;
    }
    if (active_count == 0)
        return 100;
    if (min_kc == DHT_MAX_NODES + 1)
        min_kc = 0;
    if (max_kc == 0)
        return 100;
    return 100 - ((max_kc - min_kc) * 100 / max_kc);
}
