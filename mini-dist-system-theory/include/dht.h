#ifndef DHT_H
#define DHT_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#define DHT_MAX_NODES 64
#define DHT_HASH_SPACE 256
#define DHT_FINGER_TABLE_SIZE 8
#define DHT_MAX_VNODES 4

typedef struct {
    int id;
    bool active;
    int keys[DHT_MAX_NODES];
    int key_count;
} DHTNode;

typedef struct {
    int start;
    int node_id;
} DHTFingerEntry;

typedef struct {
    DHTNode nodes[DHT_MAX_NODES];
    int node_count;
    int hash_space;
    int replication_factor;
    DHTFingerEntry finger_table[DHT_MAX_NODES][DHT_FINGER_TABLE_SIZE];
} ChordRing;

/* Consistent hashing primitives */
int  dht_hash(int key, int space);
int  dht_distance(int a, int b, int space);
bool dht_in_range(int key, int start, int end, int space, bool inclusive_end);
int  dht_find_successor(const ChordRing *ring, int key);

/* Chord ring operations */
void chord_init(ChordRing *ring, int hash_space);
int  chord_join(ChordRing *ring, int node_id);
int  chord_leave(ChordRing *ring, int node_id);
int  chord_lookup(const ChordRing *ring, int key);
void chord_store(ChordRing *ring, int node_id, int key);
bool chord_has_key(const ChordRing *ring, int node_id, int key);
void chord_build_finger_tables(ChordRing *ring);
int  chord_finger_lookup(const ChordRing *ring, int node_id, int key);

/* Consistent hashing with virtual nodes */
typedef struct {
    int vnode_id;
    int physical_node;
    int position;
} VirtualNode;

typedef struct {
    VirtualNode vnodes[DHT_MAX_NODES * DHT_MAX_VNODES];
    int vnode_count;
    int total_slots;
} ConsistentHashRing;

void ch_init(ConsistentHashRing *chr, int total_slots);
void ch_add_node(ConsistentHashRing *chr, int physical_node, int num_vnodes);
void ch_remove_node(ConsistentHashRing *chr, int physical_node);
int  ch_lookup(const ConsistentHashRing *chr, int key);
double ch_load_variance(const ConsistentHashRing *chr, int physical_nodes[], int node_count);

/* Utilities */
void dht_print_ring(const ChordRing *ring);
void ch_print_ring(const ConsistentHashRing *chr);
const char *dht_node_status(bool active);
int  dht_rebalance_score(const ChordRing *ring);

#endif
