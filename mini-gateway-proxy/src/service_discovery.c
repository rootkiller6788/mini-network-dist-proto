#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "service_registry.h"

ServiceRegistry* sr_init(const char *domain_suffix)
{
    ServiceRegistry *sr = calloc(1, sizeof(ServiceRegistry));
    if (!sr) return NULL;
    sr->num_services = 0;
    sr->default_ttl = SR_DEFAULT_TTL;
    if (domain_suffix) snprintf(sr->domain_suffix, sizeof(sr->domain_suffix),
                                 "%s", domain_suffix);
    printf("[sr] Service registry initialized (domain=%s)\n",
           sr->domain_suffix[0] ? sr->domain_suffix : "(none)");
    return sr;
}

SRService* sr_register_service(ServiceRegistry *sr, const char *name,
                                int ttl_seconds)
{
    if (!sr || !name || sr->num_services >= SR_MAX_SERVICES) return NULL;
    for (int i = 0; i < sr->num_services; i++) {
        if (strcmp(sr->services[i].service_name, name) == 0)
            return &sr->services[i];
    }
    SRService *svc = &sr->services[sr->num_services++];
    memset(svc, 0, sizeof(SRService));
    snprintf(svc->service_name, sizeof(svc->service_name), "%s", name);
    svc->ttl_seconds = ttl_seconds > 0 ? ttl_seconds : sr->default_ttl;
    clock_gettime(CLOCK_MONOTONIC, &svc->created_at);
    svc->last_refresh = svc->created_at;
    printf("[sr] Registered service '%s' (ttl=%ds)\n", name, svc->ttl_seconds);
    return svc;
}

int sr_add_endpoint(SRService *svc, const char *addr, int port, int weight)
{
    if (!svc || !addr || svc->num_endpoints >= SR_MAX_ENDPOINTS) return -1;
    for (int i = 0; i < svc->num_endpoints; i++) {
        if (strcmp(svc->endpoints[i].address, addr) == 0 &&
            svc->endpoints[i].port == port) {
            svc->endpoints[i].weight = weight;
            return i;
        }
    }
    SREndpoint *ep = &svc->endpoints[svc->num_endpoints++];
    memset(ep, 0, sizeof(SREndpoint));
    snprintf(ep->address, SR_MAX_ADDR_LEN, "%s", addr);
    ep->port = port;
    ep->weight = weight > 0 ? weight : 1;
    ep->state = SR_HEALTHY;
    clock_gettime(CLOCK_MONOTONIC, &ep->last_heartbeat);
    printf("[sr] Added endpoint %s:%d to '%s' (weight=%d)\n",
           addr, port, svc->service_name, weight);
    return svc->num_endpoints - 1;
}

int sr_remove_endpoint(SRService *svc, const char *addr, int port)
{
    if (!svc || !addr) return -1;
    for (int i = 0; i < svc->num_endpoints; i++) {
        if (strcmp(svc->endpoints[i].address, addr) == 0 &&
            svc->endpoints[i].port == port) {
            memmove(&svc->endpoints[i], &svc->endpoints[i + 1],
                    (size_t)(svc->num_endpoints - i - 1) * sizeof(SREndpoint));
            svc->num_endpoints--;
            printf("[sr] Removed endpoint %s:%d from '%s'\n",
                   addr, port, svc->service_name);
            return 0;
        }
    }
    return -1;
}

SRService* sr_lookup_service(ServiceRegistry *sr, const char *name)
{
    if (!sr || !name) return NULL;
    for (int i = 0; i < sr->num_services; i++)
        if (strcmp(sr->services[i].service_name, name) == 0)
            return &sr->services[i];
    return NULL;
}

/*
 * Simulated DNS SRV record resolution.
 * In a real deployment, this would query DNS for SRV records.
 * Knowledge: DNS SRV records (RFC 2782) enable service discovery
 * without hardcoded IPs. Format: _service._proto.name TTL class SRV priority weight port target
 */
int sr_resolve_dns_srv(ServiceRegistry *sr, const char *dns_name)
{
    if (!sr || !dns_name) return -1;
    char name_copy[256];
    snprintf(name_copy, sizeof(name_copy), "%s", dns_name);
    char *proto_start = strstr(name_copy, "._tcp.");
    if (!proto_start) {
        char *dot = strchr(name_copy, '.');
        if (dot) *dot = '\0';
    } else {
        *proto_start = '\0';
    }
    SRService *svc = sr_register_service(sr, name_copy, sr->default_ttl);
    if (!svc) return -1;
    printf("[sr] DNS SRV resolution for '%s' (simulated)\n", dns_name);
    return svc->num_endpoints;
}

int sr_heartbeat_ping(ServiceRegistry *sr, const char *service_name,
                      const char *addr, int port, bool success)
{
    if (!sr || !service_name || !addr) return -1;
    SRService *svc = sr_lookup_service(sr, service_name);
    if (!svc) return -1;
    for (int i = 0; i < svc->num_endpoints; i++) {
        SREndpoint *ep = &svc->endpoints[i];
        if (strcmp(ep->address, addr) == 0 && ep->port == port) {
            clock_gettime(CLOCK_MONOTONIC, &ep->last_heartbeat);
            if (success) {
                ep->failure_count = 0;
                if (ep->state == SR_UNHEALTHY || ep->state == SR_UNKNOWN)
                    ep->state = SR_HEALTHY;
            } else {
                ep->failure_count++;
                clock_gettime(CLOCK_MONOTONIC, &ep->last_failure);
                if (ep->failure_count >= 3 && ep->state == SR_HEALTHY) {
                    ep->state = SR_UNHEALTHY;
                    printf("[sr] %s:%d in '%s' marked UNHEALTHY (failures=%d)\n",
                           addr, port, service_name, ep->failure_count);
                }
            }
            return 0;
        }
    }
    return -1;
}

/*
 * Periodic health check with TTL eviction.
 * Implements the SWIM gossip protocol's health check principles:
 * - Direct ping for health verification
 * - TTL-based expiration (failure detection)
 * - Automatic draining of expired endpoints
 *
 * Relates to CAP theorem: in a network partition, choosing between
 * Consistency (immediate health updates) vs Availability (stale but usable endpoints).
 * This implementation prioritizes Availability: unhealthy endpoints stay
 * registered until TTL expires or explicit ping fails.
 */
void sr_health_check(ServiceRegistry *sr)
{
    if (!sr) return;
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    printf("[sr] Health check: %d services\n", sr->num_services);
    for (int i = 0; i < sr->num_services; i++) {
        SRService *svc = &sr->services[i];
        for (int j = 0; j < svc->num_endpoints; j++) {
            SREndpoint *ep = &svc->endpoints[j];
            uint64_t elapsed = (uint64_t)(now.tv_sec - ep->last_heartbeat.tv_sec);
            if (elapsed > (uint64_t)svc->ttl_seconds * 2 && ep->state == SR_HEALTHY) {
                ep->state = SR_UNHEALTHY;
                printf("[sr] %s:%d heartbeat expired (%llus), marked UNHEALTHY\n",
                       ep->address, ep->port, (unsigned long long)elapsed);
            }
            if (elapsed > (uint64_t)svc->ttl_seconds * 4) {
                ep->state = SR_UNKNOWN;
                printf("[sr] %s:%d presumed dead (%llus), marked UNKNOWN\n",
                       ep->address, ep->port, (unsigned long long)elapsed);
            }
        }
    }
}

int sr_prune_expired(ServiceRegistry *sr)
{
    if (!sr) return -1;
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    int pruned = 0;
    for (int i = 0; i < sr->num_services; i++) {
        SRService *svc = &sr->services[i];
        int j = 0;
        while (j < svc->num_endpoints) {
            SREndpoint *ep = &svc->endpoints[j];
            uint64_t elapsed = (uint64_t)(now.tv_sec - ep->last_heartbeat.tv_sec);
            if (elapsed > (uint64_t)svc->ttl_seconds * 5) {
                memmove(&svc->endpoints[j], &svc->endpoints[j + 1],
                        (size_t)(svc->num_endpoints - j - 1) * sizeof(SREndpoint));
                svc->num_endpoints--;
                pruned++;
            } else {
                j++;
            }
        }
    }
    if (pruned > 0) printf("[sr] Pruned %d expired endpoints\n", pruned);
    return pruned;
}

int sr_select_endpoint(SRService *svc, const char *strategy)
{
    if (!svc || svc->num_endpoints == 0) return -1;
    if (strategy && strcmp(strategy, "failover") == 0)
        return sr_select_failover(svc, -1);
    return sr_select_round_robin(svc);
}

int sr_select_round_robin(SRService *svc)
{
    if (!svc || svc->num_endpoints == 0) return -1;
    static int rr_counter = 0;
    for (int tries = 0; tries < svc->num_endpoints; tries++) {
        int idx = rr_counter % svc->num_endpoints;
        rr_counter++;
        if (svc->endpoints[idx].state == SR_HEALTHY) return idx;
    }
    return 0;
}

int sr_select_failover(SRService *svc, int failed_index)
{
    if (!svc || svc->num_endpoints == 0) return -1;
    int start = (failed_index >= 0 && failed_index < svc->num_endpoints)
                ? (failed_index + 1) % svc->num_endpoints : 0;
    for (int i = 0; i < svc->num_endpoints; i++) {
        int idx = (start + i) % svc->num_endpoints;
        if (svc->endpoints[idx].state == SR_HEALTHY) return idx;
    }
    return 0;
}

void sr_print_registry(const ServiceRegistry *sr)
{
    if (!sr) return;
    printf("=== Service Registry ===\n");
    printf("Domain suffix: %s\n", sr->domain_suffix[0] ? sr->domain_suffix : "(none)");
    printf("Services: %d\n", sr->num_services);
    for (int i = 0; i < sr->num_services; i++) {
        const SRService *svc = &sr->services[i];
        printf("  [%s] %d endpoints (ttl=%ds)\n",
               svc->service_name, svc->num_endpoints, svc->ttl_seconds);
        for (int j = 0; j < svc->num_endpoints; j++) {
            const SREndpoint *ep = &svc->endpoints[j];
            const char *state_str = "UNKNOWN";
            switch (ep->state) {
            case SR_HEALTHY: state_str = "UP"; break;
            case SR_UNHEALTHY: state_str = "DOWN"; break;
            case SR_DRAINING: state_str = "DRAIN"; break;
            default: break;
            }
            printf("    %s:%d weight=%d state=%s failures=%d\n",
                   ep->address, ep->port, ep->weight, state_str, ep->failure_count);
        }
    }
    printf("========================\n");
}