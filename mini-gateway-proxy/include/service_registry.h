#ifndef SERVICE_REGISTRY_H
#define SERVICE_REGISTRY_H

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#define SR_MAX_SERVICES      64
#define SR_MAX_ENDPOINTS     16
#define SR_MAX_ADDR_LEN      256
#define SR_DEFAULT_TTL       30
#define SR_DEFAULT_INTERVAL  5

typedef enum {
    SR_HEALTHY,
    SR_UNHEALTHY,
    SR_DRAINING,
    SR_UNKNOWN
} SREndpointState;

typedef struct {
    char            address[SR_MAX_ADDR_LEN];
    int             port;
    SREndpointState state;
    int             failure_count;
    struct timespec last_heartbeat;
    struct timespec last_failure;
    int             weight;
    double          latency_ms;
} SREndpoint;

typedef struct {
    char        service_name[128];
    SREndpoint  endpoints[SR_MAX_ENDPOINTS];
    int         num_endpoints;
    int         ttl_seconds;
    struct timespec created_at;
    struct timespec last_refresh;
} SRService;

typedef struct {
    SRService   services[SR_MAX_SERVICES];
    int         num_services;
    char        domain_suffix[128];
    int         default_ttl;
} ServiceRegistry;

ServiceRegistry*  sr_init(const char *domain_suffix);
SRService*        sr_register_service(ServiceRegistry *sr, const char *name,
                                      int ttl_seconds);
int               sr_add_endpoint(SRService *svc, const char *addr, int port,
                                  int weight);
int               sr_remove_endpoint(SRService *svc, const char *addr, int port);
SRService*        sr_lookup_service(ServiceRegistry *sr, const char *name);
int               sr_resolve_dns_srv(ServiceRegistry *sr, const char *dns_name);
int               sr_heartbeat_ping(ServiceRegistry *sr, const char *service_name,
                                    const char *addr, int port, bool success);
void              sr_health_check(ServiceRegistry *sr);
int               sr_prune_expired(ServiceRegistry *sr);
int               sr_select_endpoint(SRService *svc, const char *strategy);
int               sr_select_round_robin(SRService *svc);
int               sr_select_failover(SRService *svc, int failed_index);
void              sr_print_registry(const ServiceRegistry *sr);

#endif
