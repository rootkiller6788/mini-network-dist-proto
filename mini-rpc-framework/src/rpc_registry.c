#include "rpc_registry.h"
#include "rpc_transport.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

void registry_init(ServiceRegistry *reg) {
    reg->service_count = 0;
    for (int32_t i = 0; i < RPC_MAX_SERVICES; i++) {
        memset(&reg->services[i], 0, sizeof(ServiceDescriptor));
        reg->instance_counts[i] = 0;
        for (int32_t j = 0; j < RPC_MAX_INSTANCES; j++) {
            memset(&reg->instances[i][j], 0, sizeof(ServiceInstance));
        }
    }
}

static int find_service(ServiceRegistry *reg, const char *service_name) {
    for (int32_t i = 0; i < reg->service_count; i++) {
        if (strcmp(reg->services[i].service_name, service_name) == 0) return i;
    }
    return -1;
}

static int find_instance(ServiceInstance *instances, int32_t count,
                         const char *host, int32_t port) {
    for (int32_t i = 0; i < count; i++) {
        if (strcmp(instances[i].host, host) == 0 && instances[i].port == port) return i;
    }
    return -1;
}

int registry_register(ServiceRegistry *reg, const ServiceDescriptor *svc) {
    if (!reg || !svc) return -1;
    if (reg->service_count >= RPC_MAX_SERVICES) return -1;

    int32_t idx = find_service(reg, svc->service_name);
    if (idx >= 0) {
        memcpy(&reg->services[idx], svc, sizeof(ServiceDescriptor));
        return idx;
    }

    idx = reg->service_count;
    memcpy(&reg->services[idx], svc, sizeof(ServiceDescriptor));
    reg->service_count++;
    return idx;
}

int registry_unregister(ServiceRegistry *reg, const char *service_name) {
    if (!reg || !service_name) return -1;
    int32_t idx = find_service(reg, service_name);
    if (idx < 0) return -1;

    for (int32_t j = idx; j < reg->service_count - 1; j++) {
        memcpy(&reg->services[j], &reg->services[j + 1], sizeof(ServiceDescriptor));
        for (int32_t k = 0; k < RPC_MAX_INSTANCES; k++) {
            memcpy(&reg->instances[j][k], &reg->instances[j + 1][k],
                   sizeof(ServiceInstance));
        }
        reg->instance_counts[j] = reg->instance_counts[j + 1];
    }
    reg->service_count--;
    reg->instance_counts[reg->service_count] = 0;
    return 0;
}

int registry_add_instance(ServiceRegistry *reg, const char *service_name,
                           const char *host, int32_t port, int32_t weight) {
    if (!reg || !service_name || !host) return -1;

    int32_t sidx = find_service(reg, service_name);
    if (sidx < 0) {
        ServiceDescriptor sd;
        memset(&sd, 0, sizeof(sd));
        strncpy(sd.service_name, service_name, RPC_MAX_NAME_LEN - 1);
        sidx = registry_register(reg, &sd);
        if (sidx < 0) return -1;
    }

    if (reg->instance_counts[sidx] >= RPC_MAX_INSTANCES) return -1;

    int32_t iidx = reg->instance_counts[sidx];
    ServiceInstance *inst = &reg->instances[sidx][iidx];
    strncpy(inst->service_name, service_name, RPC_MAX_NAME_LEN - 1);
    strncpy(inst->host, host, 127);
    inst->port = port;
    inst->weight = weight > 0 ? weight : RPC_DEFAULT_WEIGHT;
    inst->healthy = true;
    inst->active_connections = 0;
    inst->last_heartbeat = (int32_t)time(NULL);
    memset(inst->metadata, 0, sizeof(inst->metadata));

    reg->instance_counts[sidx]++;
    return iidx;
}

int registry_remove_instance(ServiceRegistry *reg, const char *service_name,
                              const char *host, int32_t port) {
    if (!reg || !service_name || !host) return -1;

    int32_t sidx = find_service(reg, service_name);
    if (sidx < 0) return -1;

    int32_t iidx = find_instance(reg->instances[sidx], reg->instance_counts[sidx],
                                  host, port);
    if (iidx < 0) return -1;

    for (int32_t j = iidx; j < reg->instance_counts[sidx] - 1; j++) {
        memcpy(&reg->instances[sidx][j], &reg->instances[sidx][j + 1],
               sizeof(ServiceInstance));
    }
    reg->instance_counts[sidx]--;
    return 0;
}

int registry_discover(ServiceRegistry *reg, const char *service_name,
                       ServiceInstance *out, int32_t max_out) {
    if (!reg || !service_name || !out) return 0;

    int32_t sidx = find_service(reg, service_name);
    if (sidx < 0) return 0;

    int32_t count = reg->instance_counts[sidx];
    if (count > max_out) count = max_out;

    int32_t out_count = 0;
    for (int32_t i = 0; i < count; i++) {
        if (reg->instances[sidx][i].healthy) {
            memcpy(&out[out_count], &reg->instances[sidx][i],
                   sizeof(ServiceInstance));
            out_count++;
        }
    }
    return out_count;
}

int registry_discover_backend(DiscoveryBackend backend, const char *service_name,
                               ServiceInstance *out, int32_t max_out) {
    (void)max_out;
    if (!service_name || !out) return 0;

    switch (backend) {
    case DISCOVERY_STATIC:
        return 0;
    case DISCOVERY_DNS: {
        strncpy(out[0].service_name, service_name, RPC_MAX_NAME_LEN - 1);
        strncpy(out[0].host, "resolved-from-dns", 127);
        out[0].port = RPC_DEFAULT_PORT;
        out[0].weight = RPC_DEFAULT_WEIGHT;
        out[0].healthy = true;
        out[0].last_heartbeat = (int32_t)time(NULL);
        return 1;
    }
    case DISCOVERY_ETCD:
        strncpy(out[0].service_name, service_name, RPC_MAX_NAME_LEN - 1);
        strncpy(out[0].host, "discovered-from-etcd", 127);
        out[0].port = RPC_DEFAULT_PORT;
        out[0].weight = RPC_DEFAULT_WEIGHT;
        out[0].healthy = true;
        out[0].last_heartbeat = (int32_t)time(NULL);
        return 1;
    default:
        return 0;
    }
}

int registry_health_check(ServiceRegistry *reg) {
    if (!reg) return -1;
    int32_t now = (int32_t)time(NULL);
    int32_t unhealthy_count = 0;

    for (int32_t i = 0; i < reg->service_count; i++) {
        for (int32_t j = 0; j < reg->instance_counts[i]; j++) {
            ServiceInstance *inst = &reg->instances[i][j];
            if (now - inst->last_heartbeat > RPC_HEALTH_INTERVAL_S * 3) {
                if (inst->healthy) {
                    inst->healthy = false;
                    unhealthy_count++;
                }
            }
        }
    }
    return unhealthy_count;
}

int registry_heartbeat(ServiceRegistry *reg, const char *service_name,
                        const char *host, int32_t port) {
    if (!reg || !service_name || !host) return -1;

    int32_t sidx = find_service(reg, service_name);
    if (sidx < 0) return -1;

    int32_t iidx = find_instance(reg->instances[sidx], reg->instance_counts[sidx],
                                  host, port);
    if (iidx < 0) return -1;

    reg->instances[sidx][iidx].last_heartbeat = (int32_t)time(NULL);
    reg->instances[sidx][iidx].healthy = true;
    return 0;
}

static int32_t g_rr_cursor = 0;

int registry_lb_round_robin(ServiceRegistry *reg, const char *service_name,
                             int32_t *cursor) {
    if (!reg || !service_name) return -1;

    int32_t sidx = find_service(reg, service_name);
    if (sidx < 0) return -1;

    int32_t count = reg->instance_counts[sidx];
    if (count == 0) return -1;

    int32_t start = *cursor % count;
    for (int32_t attempt = 0; attempt < count; attempt++) {
        int32_t idx = (start + attempt) % count;
        if (reg->instances[sidx][idx].healthy) {
            *cursor = (idx + 1) % count;
            return idx;
        }
    }
    return -1;
}

int registry_lb_weighted(ServiceRegistry *reg, const char *service_name) {
    if (!reg || !service_name) return -1;

    int32_t sidx = find_service(reg, service_name);
    if (sidx < 0) return -1;

    int32_t count = reg->instance_counts[sidx];
    if (count == 0) return -1;

    int32_t total_weight = 0;
    for (int32_t i = 0; i < count; i++) {
        if (reg->instances[sidx][i].healthy)
            total_weight += reg->instances[sidx][i].weight;
    }
    if (total_weight <= 0) return registry_lb_round_robin(reg, service_name, &g_rr_cursor);

    int32_t rand_val = rand() % total_weight;
    int32_t accum = 0;
    for (int32_t i = 0; i < count; i++) {
        if (!reg->instances[sidx][i].healthy) continue;
        accum += reg->instances[sidx][i].weight;
        if (rand_val < accum) return i;
    }

    return registry_lb_round_robin(reg, service_name, &g_rr_cursor);
}

int registry_lb_select(ServiceRegistry *reg, const char *service_name) {
    if (!reg || !service_name) return -1;

    int32_t sidx = find_service(reg, service_name);
    if (sidx < 0) return -1;

    int32_t healthy_count = 0;
    for (int32_t i = 0; i < reg->instance_counts[sidx]; i++) {
        if (reg->instances[sidx][i].healthy) healthy_count++;
    }
    if (healthy_count == 0) return -1;

    if (healthy_count == 1) {
        for (int32_t i = 0; i < reg->instance_counts[sidx]; i++) {
            if (reg->instances[sidx][i].healthy) return i;
        }
    }

    return registry_lb_weighted(reg, service_name);
}

ServiceDescriptor *registry_lookup(ServiceRegistry *reg, const char *service_name) {
    if (!reg || !service_name) return NULL;
    int32_t idx = find_service(reg, service_name);
    if (idx < 0) return NULL;
    return &reg->services[idx];
}

void registry_print(ServiceRegistry *reg) {
    if (!reg) return;
    printf("=== Service Registry (%d services) ===\n", reg->service_count);
    for (int32_t i = 0; i < reg->service_count; i++) {
        printf("  Service: %s (v%d, methods:%d)\n",
               reg->services[i].service_name,
               reg->services[i].version,
               reg->services[i].method_count);
        for (int32_t j = 0; j < reg->instance_counts[i]; j++) {
            ServiceInstance *inst = &reg->instances[i][j];
            printf("    Instance: %s:%d weight=%d healthy=%s conns=%d\n",
                   inst->host, inst->port, inst->weight,
                   inst->healthy ? "yes" : "no",
                   inst->active_connections);
        }
    }
    printf("======================================\n");
}
