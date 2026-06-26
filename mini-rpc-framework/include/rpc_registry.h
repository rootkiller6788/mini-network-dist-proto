#ifndef RPC_REGISTRY_H
#define RPC_REGISTRY_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "rpc_encoding.h"

#define RPC_MAX_SERVICES      64
#define RPC_MAX_METHODS       32
#define RPC_MAX_INSTANCES     16
#define RPC_MAX_NAME_LEN      128
#define RPC_DEFAULT_WEIGHT    100
#define RPC_HEALTH_INTERVAL_S 10

typedef int (*RPCHandlerFn)(const RPCMessage *req, RPCMessage *resp);

typedef struct {
    char          name[RPC_MAX_NAME_LEN];
    RPCHandlerFn  handler;
    int32_t       timeout_ms;
} MethodDescriptor;

typedef struct {
    char              service_name[RPC_MAX_NAME_LEN];
    MethodDescriptor  methods[RPC_MAX_METHODS];
    int32_t           method_count;
    int32_t           version;
    char              namespace_[RPC_MAX_NAME_LEN];
} ServiceDescriptor;

typedef struct {
    char    service_name[RPC_MAX_NAME_LEN];
    char    host[128];
    int32_t port;
    int32_t weight;
    bool    healthy;
    int32_t active_connections;
    int32_t last_heartbeat;
    char    metadata[256];
} ServiceInstance;

typedef struct {
    ServiceDescriptor  services[RPC_MAX_SERVICES];
    int32_t            service_count;
    ServiceInstance    instances[RPC_MAX_SERVICES][RPC_MAX_INSTANCES];
    int32_t            instance_counts[RPC_MAX_SERVICES];
} ServiceRegistry;

typedef enum {
    DISCOVERY_STATIC = 0,
    DISCOVERY_DNS    = 1,
    DISCOVERY_ETCD   = 2
} DiscoveryBackend;

void registry_init(ServiceRegistry *reg);

int  registry_register(ServiceRegistry *reg, const ServiceDescriptor *svc);
int  registry_unregister(ServiceRegistry *reg, const char *service_name);

int  registry_add_instance(ServiceRegistry *reg, const char *service_name,
                           const char *host, int32_t port, int32_t weight);
int  registry_remove_instance(ServiceRegistry *reg, const char *service_name,
                              const char *host, int32_t port);

int  registry_discover(ServiceRegistry *reg, const char *service_name,
                       ServiceInstance *out, int32_t max_out);

int  registry_discover_backend(DiscoveryBackend backend, const char *service_name,
                               ServiceInstance *out, int32_t max_out);

int  registry_health_check(ServiceRegistry *reg);
int  registry_heartbeat(ServiceRegistry *reg, const char *service_name,
                        const char *host, int32_t port);

int  registry_lb_select(ServiceRegistry *reg, const char *service_name);
int  registry_lb_round_robin(ServiceRegistry *reg, const char *service_name,
                             int32_t *cursor);
int  registry_lb_weighted(ServiceRegistry *reg, const char *service_name);

ServiceDescriptor *registry_lookup(ServiceRegistry *reg, const char *service_name);
void               registry_print(ServiceRegistry *reg);

#endif
