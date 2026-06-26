#include "leader_election.h"
#include <stdio.h>
#include <string.h>

const char *bully_state_name(BullyState state) {
    switch (state) {
        case BULLY_IDLE:     return "IDLE";
        case BULLY_ELECTION: return "ELECTION";
        case BULLY_LEADER:   return "LEADER";
        default:             return "UNKNOWN";
    }
}

void bully_init(BullyCluster *cluster, int n) {
    cluster->node_count = n;
    for (int i = 0; i < n; i++) {
        cluster->nodes[i].id            = i;
        cluster->nodes[i].leader_id     = -1;
        cluster->nodes[i].state         = BULLY_IDLE;
        cluster->nodes[i].active        = true;
        cluster->nodes[i].message_count = 0;
    }
}

int bully_election(BullyCluster *cluster, int initiator_id) {
    int highest = initiator_id;
    bool any_response = false;

    for (int i = 0; i < cluster->node_count; i++) {
        if (i == initiator_id || !cluster->nodes[i].active) continue;

        cluster->nodes[i].state = BULLY_ELECTION;
        cluster->nodes[i].message_count++;
        cluster->nodes[initiator_id].message_count++;

        if (i > highest) {
            any_response = true;
            highest = i;
            int sub_winner = bully_election(cluster, i);
            if (sub_winner > highest) {
                highest = sub_winner;
            }
        }
    }

    if (!any_response && cluster->nodes[initiator_id].active) {
        bully_declare_leader(cluster, initiator_id);
    }

    return highest;
}

void bully_declare_leader(BullyCluster *cluster, int leader_id) {
    for (int i = 0; i < cluster->node_count; i++) {
        cluster->nodes[i].leader_id = leader_id;
        cluster->nodes[i].state     = BULLY_LEADER;
        if (i != leader_id) {
            cluster->nodes[i].message_count++;
        }
    }
    cluster->nodes[leader_id].state = BULLY_LEADER;
}

void bully_node_crash(BullyCluster *cluster, int node_id) {
    if (node_id < 0 || node_id >= cluster->node_count) return;
    cluster->nodes[node_id].active = false;
    cluster->nodes[node_id].state  = BULLY_IDLE;

    if (cluster->nodes[node_id].leader_id == node_id) {
        int highest_active = -1;
        for (int i = 0; i < cluster->node_count; i++) {
            if (cluster->nodes[i].active && i > highest_active) {
                highest_active = i;
            }
        }
        if (highest_active >= 0) {
            bully_election(cluster, highest_active);
        }
    }
}

void bully_node_recover(BullyCluster *cluster, int node_id) {
    if (node_id < 0 || node_id >= cluster->node_count) return;
    cluster->nodes[node_id].active = true;
    cluster->nodes[node_id].state  = BULLY_IDLE;

    if (node_id > cluster->nodes[0].leader_id) {
        bully_election(cluster, node_id);
    }
}

void ring_init(RingCluster *cluster, int n) {
    cluster->node_count = n;
    for (int i = 0; i < n; i++) {
        cluster->nodes[i].id            = i;
        cluster->nodes[i].leader_id     = -1;
        cluster->nodes[i].next_id       = (i + 1) % n;
        cluster->nodes[i].active        = true;
        cluster->nodes[i].token_owner   = false;
        cluster->nodes[i].message_count = 0;
        memset(&cluster->nodes[i].token, 0, sizeof(RingElectionToken));
        cluster->nodes[i].token.complete = false;
    }
}

int ring_election(RingCluster *cluster, int initiator_id) {
    RingElectionToken token;
    token.candidate_id = initiator_id;
    token.highest_id   = initiator_id;
    token.hop_count    = 0;
    token.complete     = false;

    cluster->nodes[initiator_id].token       = token;
    cluster->nodes[initiator_id].token_owner = true;
    cluster->nodes[initiator_id].message_count++;

    int current = initiator_id;

    while (!token.complete && token.hop_count < RING_MAX_NODES * 2) {
        int next = cluster->nodes[current].next_id;
        if (!cluster->nodes[next].active) {
            int skipped = 0;
            int candidate = next;
            do {
                candidate = (candidate + 1) % cluster->node_count;
                skipped++;
                if (skipped > cluster->node_count) break;
            } while (!cluster->nodes[candidate].active);
            if (skipped > cluster->node_count) break;
            next = candidate;
        }

        cluster->nodes[next].message_count++;
        cluster->nodes[next].token_owner = true;

        if (cluster->nodes[next].id > token.highest_id) {
            token.highest_id = cluster->nodes[next].id;
        }
        token.hop_count++;

        if (next == initiator_id) {
            token.complete = true;
            for (int i = 0; i < cluster->node_count; i++) {
                cluster->nodes[i].leader_id = token.highest_id;
                cluster->nodes[i].token_owner = false;
            }
            return token.highest_id;
        }

        cluster->nodes[next].token = token;
        current = next;
    }

    return -1;
}

bool ring_pass_token(RingCluster *cluster, int from_id) {
    if (from_id < 0 || from_id >= cluster->node_count) return false;
    if (!cluster->nodes[from_id].token_owner) return false;

    int next = cluster->nodes[from_id].next_id;
    cluster->nodes[next].token = cluster->nodes[from_id].token;
    cluster->nodes[next].token_owner = true;
    cluster->nodes[from_id].token_owner = false;
    cluster->nodes[from_id].message_count++;
    return true;
}

void ring_node_crash(RingCluster *cluster, int node_id) {
    if (node_id < 0 || node_id >= cluster->node_count) return;
    cluster->nodes[node_id].active = false;

    for (int i = 0; i < cluster->node_count; i++) {
        if (cluster->nodes[i].next_id == node_id) {
            int new_next = (node_id + 1) % cluster->node_count;
            while (!cluster->nodes[new_next].active &&
                   new_next != i) {
                new_next = (new_next + 1) % cluster->node_count;
            }
            cluster->nodes[i].next_id = new_next;
            break;
        }
    }

    if (cluster->nodes[node_id].leader_id == node_id) {
        int next_alive = (node_id + 1) % cluster->node_count;
        while (!cluster->nodes[next_alive].active) {
            next_alive = (next_alive + 1) % cluster->node_count;
        }
        ring_election(cluster, next_alive);
    }
}

void zk_init(ZKCluster *cluster, int n) {
    cluster->node_count    = n;
    cluster->next_sequence = 0;
    for (int i = 0; i < n; i++) {
        cluster->nodes[i].id            = i;
        cluster->nodes[i].sequence      = -1;
        cluster->nodes[i].leader_id     = -1;
        cluster->nodes[i].active        = true;
        cluster->nodes[i].message_count = 0;
    }
}

int zk_leader_election(ZKCluster *cluster) {
    for (int i = 0; i < cluster->node_count; i++) {
        if (cluster->nodes[i].active) {
            zk_create_ephemeral_sequential(cluster, i);
        }
    }

    int lowest_id   = -1;
    int lowest_seq  = 999999;

    for (int i = 0; i < cluster->node_count; i++) {
        if (!cluster->nodes[i].active) continue;
        if (cluster->nodes[i].sequence >= 0 &&
            cluster->nodes[i].sequence < lowest_seq) {
            lowest_seq = cluster->nodes[i].sequence;
            lowest_id  = cluster->nodes[i].id;
        }
    }

    if (lowest_id >= 0) {
        for (int i = 0; i < cluster->node_count; i++) {
            cluster->nodes[i].leader_id = lowest_id;
            zk_watch_leader(cluster, i);
        }
    }

    return lowest_id;
}

int zk_create_ephemeral_sequential(ZKCluster *cluster, int node_id) {
    if (node_id < 0 || node_id >= cluster->node_count) return -1;
    int seq = cluster->next_sequence++;
    cluster->nodes[node_id].sequence  = seq;
    cluster->nodes[node_id].message_count++;
    return seq;
}

void zk_watch_leader(ZKCluster *cluster, int node_id) {
    for (int i = 0; i < cluster->node_count; i++) {
        if (cluster->nodes[i].id == node_id) {
            cluster->nodes[i].message_count++;
            break;
        }
    }
}

void zk_node_crash(ZKCluster *cluster, int node_id) {
    if (node_id < 0 || node_id >= cluster->node_count) return;
    cluster->nodes[node_id].active   = false;
    cluster->nodes[node_id].sequence = -1;

    if (cluster->nodes[node_id].leader_id == node_id) {
        zk_leader_election(cluster);
    }
}

void bully_print(const BullyCluster *cluster) {
    printf("=== Bully Algorithm (%d nodes) ===\n", cluster->node_count);
    for (int i = 0; i < cluster->node_count; i++) {
        printf("  Node %d [%s] leader=%d active=%s msgs=%d\n",
               cluster->nodes[i].id,
               bully_state_name(cluster->nodes[i].state),
               cluster->nodes[i].leader_id,
               cluster->nodes[i].active ? "yes" : "no",
               cluster->nodes[i].message_count);
    }
}

void ring_print(const RingCluster *cluster) {
    printf("=== Ring Algorithm (%d nodes) ===\n", cluster->node_count);
    for (int i = 0; i < cluster->node_count; i++) {
        printf("  Node %d ->%d leader=%d active=%s msgs=%d token=%s\n",
               cluster->nodes[i].id,
               cluster->nodes[i].next_id,
               cluster->nodes[i].leader_id,
               cluster->nodes[i].active ? "yes" : "no",
               cluster->nodes[i].message_count,
               cluster->nodes[i].token_owner ? "yes" : "no");
    }
}

void zk_print(const ZKCluster *cluster) {
    printf("=== ZooKeeper-style Election (%d nodes) ===\n",
           cluster->node_count);
    for (int i = 0; i < cluster->node_count; i++) {
        printf("  Node %d seq=%d leader=%d active=%s msgs=%d\n",
               cluster->nodes[i].id,
               cluster->nodes[i].sequence,
               cluster->nodes[i].leader_id,
               cluster->nodes[i].active ? "yes" : "no",
               cluster->nodes[i].message_count);
    }
}

int bully_message_complexity(int n) {
    return n * n;
}

int ring_message_complexity(int n) {
    return 2 * n;
}

int zk_message_complexity(int n) {
    return 2 * n;
}
