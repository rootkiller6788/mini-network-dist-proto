#include "cap_theorem.h"
#include <stdio.h>
#include <string.h>

void cap_configure(DistributedStore *store, CAPMode mode)
{
    int i, j;
    store->mode = mode;
    store->node_count = CAP_MAX_NODES;
    for (i = 0; i < CAP_MAX_NODES; i++) {
        store->nodes[i].id = i;
        store->nodes[i].value[0] = '\0';
        store->nodes[i].online = true;
    }
    for (i = 0; i < CAP_MAX_NODES; i++) {
        for (j = 0; j < CAP_MAX_NODES; j++) {
            store->partition_between[i][j] = false;
        }
    }
}

static bool can_reach(const DistributedStore *store, int from, int to)
{
    if (from == to) return true;
    return !store->partition_between[from][to];
}

static int quorum_size(const DistributedStore *store)
{
    int online = 0;
    int i;
    for (i = 0; i < store->node_count; i++) {
        if (store->nodes[i].online) online++;
    }
    return (online / 2) + 1;
}

static int reachable_nodes(const DistributedStore *store, int node_id)
{
    int count = 0;
    int i;
    for (i = 0; i < store->node_count; i++) {
        if (store->nodes[i].online && can_reach(store, node_id, i)) {
            count++;
        }
    }
    return count;
}

bool cap_write(DistributedStore *store, int node_id, const char *value)
{
    int reachable = reachable_nodes(store, node_id);
    int quorum = quorum_size(store);
    int i;

    if (node_id < 0 || node_id >= store->node_count) return false;
    if (!store->nodes[node_id].online) return false;

    if (store->mode == CA_MODE) {
        for (i = 0; i < store->node_count; i++) {
            if (store->nodes[i].online) {
                strncpy(store->nodes[i].value, value, CAP_MAX_VALUE_LEN - 1);
                store->nodes[i].value[CAP_MAX_VALUE_LEN - 1] = '\0';
            }
        }
        printf("[CA] Wrote '%s' to all %d nodes\n", value, store->node_count);
        return true;
    }

    if (store->mode == CP_MODE) {
        if (reachable < quorum) {
            printf("[CP] Write REJECTED: node %d can only reach %d/%d nodes (quorum=%d)\n",
                   node_id, reachable, store->node_count, quorum);
            return false;
        }
        strncpy(store->nodes[node_id].value, value, CAP_MAX_VALUE_LEN - 1);
        store->nodes[node_id].value[CAP_MAX_VALUE_LEN - 1] = '\0';
        for (i = 0; i < store->node_count; i++) {
            if (i != node_id && store->nodes[i].online && can_reach(store, node_id, i)) {
                strncpy(store->nodes[i].value, value, CAP_MAX_VALUE_LEN - 1);
                store->nodes[i].value[CAP_MAX_VALUE_LEN - 1] = '\0';
            }
        }
        printf("[CP] Write ACCEPTED: '%s' via node %d (quorum=%d)\n", value, node_id, quorum);
        return true;
    }

    if (store->mode == AP_MODE) {
        strncpy(store->nodes[node_id].value, value, CAP_MAX_VALUE_LEN - 1);
        store->nodes[node_id].value[CAP_MAX_VALUE_LEN - 1] = '\0';
        printf("[AP] Write ACCEPTED locally on node %d: '%s'\n", node_id, value);
        for (i = 0; i < store->node_count; i++) {
            if (i != node_id && store->nodes[i].online && can_reach(store, node_id, i)) {
                strncpy(store->nodes[i].value, value, CAP_MAX_VALUE_LEN - 1);
                store->nodes[i].value[CAP_MAX_VALUE_LEN - 1] = '\0';
                printf("[AP] Async replicate: node %d <- node %d: '%s'\n", i, node_id, value);
            }
        }
        return true;
    }

    return false;
}

bool cap_read(const DistributedStore *store, int node_id, char *out_value, size_t max_len)
{
    int reachable = reachable_nodes(store, node_id);
    int quorum = quorum_size(store);
    int i;
    char best[CAP_MAX_VALUE_LEN] = {0};

    if (node_id < 0 || node_id >= store->node_count) return false;
    if (!store->nodes[node_id].online) return false;

    if (store->mode == CP_MODE) {
        if (reachable < quorum) {
            printf("[CP] Read REJECTED: node %d can only reach %d/%d (quorum=%d)\n",
                   node_id, reachable, store->node_count, quorum);
            if (out_value) out_value[0] = '\0';
            return false;
        }
        for (i = 0; i < store->node_count; i++) {
            if (store->nodes[i].online && can_reach(store, node_id, i)) {
                if (store->nodes[i].value[0] != '\0') {
                    strncpy(best, store->nodes[i].value, CAP_MAX_VALUE_LEN - 1);
                    break;
                }
            }
        }
        strncpy(out_value, best, max_len - 1);
        out_value[max_len - 1] = '\0';
        printf("[CP] Read from node %d: '%s' (quorum=%d)\n", node_id, out_value, quorum);
        return true;
    }

    if (store->mode == AP_MODE || store->mode == CA_MODE) {
        strncpy(out_value, store->nodes[node_id].value, max_len - 1);
        out_value[max_len - 1] = '\0';
        printf("[AP] Read from node %d: '%s' (local, may be stale)\n", node_id, out_value);
        return true;
    }

    return false;
}

void cap_create_partition(DistributedStore *store, int node_a, int node_b)
{
    if (node_a < 0 || node_a >= store->node_count) return;
    if (node_b < 0 || node_b >= store->node_count) return;
    if (node_a == node_b) return;

    store->partition_between[node_a][node_b] = true;
    store->partition_between[node_b][node_a] = true;
    printf("*** PARTITION created between node %d and node %d ***\n", node_a, node_b);
}

void cap_heal_partition(DistributedStore *store, int node_a, int node_b)
{
    if (node_a < 0 || node_a >= store->node_count) return;
    if (node_b < 0 || node_b >= store->node_count) return;
    if (node_a == node_b) return;

    store->partition_between[node_a][node_b] = false;
    store->partition_between[node_b][node_a] = false;
    printf("*** PARTITION HEALED between node %d and node %d ***\n", node_a, node_b);
}

void cap_print_mode(const DistributedStore *store)
{
    const char *mode_str;
    switch (store->mode) {
        case CP_MODE: mode_str = "CP (Consistent + Partition-tolerant, sacrifice A)"; break;
        case AP_MODE: mode_str = "AP (Available + Partition-tolerant, sacrifice C)"; break;
        case CA_MODE: mode_str = "CA (Consistent + Available, no partitions)"; break;
        default: mode_str = "UNKNOWN"; break;
    }
    printf("=== CAP Theorem Mode: %s ===\n", mode_str);
    printf("Nodes: %d\n", store->node_count);
    int i, j;
    for (i = 0; i < store->node_count; i++) {
        printf("  Node %d: online=%s value='%s'\n",
               i, store->nodes[i].online ? "yes" : "no", store->nodes[i].value);
    }
    printf("Partitions:\n");
    bool any = false;
    for (i = 0; i < store->node_count; i++) {
        for (j = i + 1; j < store->node_count; j++) {
            if (store->partition_between[i][j]) {
                printf("  %d <--|--> %d\n", i, j);
                any = true;
            }
        }
    }
    if (!any) printf("  (none)\n");
    printf("====================================\n");
}
