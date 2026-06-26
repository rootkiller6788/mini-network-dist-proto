#ifndef CAP_THEOREM_H
#define CAP_THEOREM_H

#include <stdbool.h>
#include <stdint.h>

#define CAP_MAX_NODES 3
#define CAP_MAX_VALUE_LEN 64

typedef enum {
    CP_MODE,
    AP_MODE,
    CA_MODE
} CAPMode;

typedef struct {
    int id;
    char value[CAP_MAX_VALUE_LEN];
    bool online;
} CAPNode;

typedef struct {
    CAPNode nodes[CAP_MAX_NODES];
    bool partition_between[CAP_MAX_NODES][CAP_MAX_NODES];
    CAPMode mode;
    int node_count;
} DistributedStore;

void cap_configure(DistributedStore *store, CAPMode mode);
bool cap_write(DistributedStore *store, int node_id, const char *value);
bool cap_read(const DistributedStore *store, int node_id, char *out_value, size_t max_len);
void cap_create_partition(DistributedStore *store, int node_a, int node_b);
void cap_heal_partition(DistributedStore *store, int node_a, int node_b);
void cap_print_mode(const DistributedStore *store);

#endif
