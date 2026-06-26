#include "dht.h"
#include <stdio.h>

int main(void)
{
    ChordRing ring;
    chord_init(&ring, 256);
    chord_join(&ring, 10);
    chord_join(&ring, 60);
    chord_join(&ring, 120);
    chord_join(&ring, 220);

    printf("=== Chord DHT Demo ===\n");
    printf("4 nodes in a 256-slot ring\n");

    int keys[] = {25, 55, 100, 200};
    int i;
    for (i = 0; i < 4; i++) {
        int owner = chord_lookup(&ring, keys[i]);
        printf("Key %d -> hash %d -> Node %d\n", keys[i], dht_hash(keys[i], 256), owner);
    }

    printf("\n--- Consistent Hashing with Virtual Nodes ---\n");
    ConsistentHashRing chr;
    ch_init(&chr, 256);
    ch_add_node(&chr, 1, 4);
    ch_add_node(&chr, 2, 4);
    ch_add_node(&chr, 3, 4);
    printf("3 physical nodes with 4 vnodes each\n");
    printf("Key 50 -> Physical Node %d\n", ch_lookup(&chr, 50));
    printf("Key 128 -> Physical Node %d\n", ch_lookup(&chr, 128));

    return 0;
}
