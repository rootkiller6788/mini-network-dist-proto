#include "rpc_registry.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int find_service(ServiceRegistry *reg, const char *service_name);

int main(void) {
    printf("=== RPC Registry Demo ===\n\n");

    ServiceRegistry reg;
    registry_init(&reg);

    printf("[1] Registering 3 services...\n");

    ServiceDescriptor calc_svc;
    memset(&calc_svc, 0, sizeof(calc_svc));
    strncpy(calc_svc.service_name, "Calculator", RPC_MAX_NAME_LEN - 1);
    calc_svc.version = 1;
    calc_svc.method_count = 3;
    strncpy(calc_svc.methods[0].name, "add", RPC_MAX_NAME_LEN - 1);
    strncpy(calc_svc.methods[1].name, "subtract", RPC_MAX_NAME_LEN - 1);
    strncpy(calc_svc.methods[2].name, "multiply", RPC_MAX_NAME_LEN - 1);
    registry_register(&reg, &calc_svc);

    ServiceDescriptor auth_svc;
    memset(&auth_svc, 0, sizeof(auth_svc));
    strncpy(auth_svc.service_name, "AuthService", RPC_MAX_NAME_LEN - 1);
    auth_svc.version = 2;
    auth_svc.method_count = 2;
    strncpy(auth_svc.methods[0].name, "login", RPC_MAX_NAME_LEN - 1);
    strncpy(auth_svc.methods[1].name, "logout", RPC_MAX_NAME_LEN - 1);
    registry_register(&reg, &auth_svc);

    ServiceDescriptor user_svc;
    memset(&user_svc, 0, sizeof(user_svc));
    strncpy(user_svc.service_name, "UserService", RPC_MAX_NAME_LEN - 1);
    user_svc.version = 1;
    user_svc.method_count = 4;
    strncpy(user_svc.methods[0].name, "getUser", RPC_MAX_NAME_LEN - 1);
    strncpy(user_svc.methods[1].name, "createUser", RPC_MAX_NAME_LEN - 1);
    strncpy(user_svc.methods[2].name, "updateUser", RPC_MAX_NAME_LEN - 1);
    strncpy(user_svc.methods[3].name, "deleteUser", RPC_MAX_NAME_LEN - 1);
    registry_register(&reg, &user_svc);

    printf("    Registered %d services\n\n", reg.service_count);

    printf("[2] Adding service instances...\n");

    registry_add_instance(&reg, "Calculator", "192.168.1.10", 8080, 100);
    registry_add_instance(&reg, "Calculator", "192.168.1.11", 8080, 100);
    registry_add_instance(&reg, "Calculator", "192.168.1.12", 8080, 50);

    registry_add_instance(&reg, "AuthService", "192.168.1.20", 9090, 100);
    registry_add_instance(&reg, "AuthService", "192.168.1.21", 9090, 200);

    registry_add_instance(&reg, "UserService", "192.168.1.30", 7070, 100);
    registry_add_instance(&reg, "UserService", "192.168.1.31", 7070, 100);
    registry_add_instance(&reg, "UserService", "192.168.1.32", 7070, 100);
    registry_add_instance(&reg, "UserService", "192.168.1.33", 7070, 50);

    printf("    Instances added\n\n");

    printf("[3] Discovering Calculator instances...\n");
    ServiceInstance instances[16];
    int found = registry_discover(&reg, "Calculator", instances, 16);
    printf("    Found %d healthy instances:\n", found);
    for (int i = 0; i < found; i++) {
        printf("      %s:%d (weight=%d, healthy=%s)\n",
               instances[i].host, instances[i].port,
               instances[i].weight,
               instances[i].healthy ? "yes" : "no");
    }

    printf("\n[4] Client-side load balancing (Weighted)...\n");
    printf("    Selecting Calculator (10 iterations):\n");
    for (int iter = 0; iter < 10; iter++) {
        int32_t idx = registry_lb_select(&reg, "Calculator");
        if (idx >= 0) {
            printf("      [%d] Selected: %s:%d (weight=%d)\n",
                   iter, reg.instances[0][idx].host,
                   reg.instances[0][idx].port,
                   reg.instances[0][idx].weight);
        }
    }

    printf("\n[5] Round-robin load balancing...\n");
    int32_t rr_cursor = 0;
    for (int iter = 0; iter < 8; iter++) {
        int32_t idx = registry_lb_round_robin(&reg, "UserService", &rr_cursor);
        if (idx >= 0) {
            printf("      RR[%d] -> %s:%d\n",
                   iter, reg.instances[2][idx].host,
                   reg.instances[2][idx].port);
        }
    }

    printf("\n[6] Health check simulation...\n");
    printf("    Setting AuthService:192.168.1.21 as unhealthy (stale heartbeat)\n");
    int auth_idx = find_service(&reg, "AuthService");
    if (auth_idx >= 0) {
        for (int j = 0; j < reg.instance_counts[auth_idx]; j++) {
            if (strcmp(reg.instances[auth_idx][j].host, "192.168.1.21") == 0) {
                reg.instances[auth_idx][j].last_heartbeat = 0;
            }
        }
    }
    int unhealthy = registry_health_check(&reg);
    printf("    Unhealthy instances found: %d\n\n", unhealthy);

    printf("    AuthService instances after health check:\n");
    ServiceInstance auth_inst[16];
    found = registry_discover(&reg, "AuthService", auth_inst, 16);
    for (int i = 0; i < found; i++) {
        printf("      %s:%d (healthy=%s)\n",
               auth_inst[i].host, auth_inst[i].port,
               auth_inst[i].healthy ? "yes" : "no");
    }

    printf("\n[7] Discovery backends...\n");
    ServiceInstance dns_inst[16];
    found = registry_discover_backend(DISCOVERY_DNS, "Calculator",
                                       dns_inst, 16);
    printf("    DNS backend: found %d mock instances\n", found);

    ServiceInstance etcd_inst[16];
    found = registry_discover_backend(DISCOVERY_ETCD, "Calculator",
                                       etcd_inst, 16);
    printf("    ETCD backend: found %d mock instances\n", found);

    printf("\n[8] Full registry print:\n");
    registry_print(&reg);

    printf("\n=== Demo Complete ===\n");
    return 0;
}

static int find_service(ServiceRegistry *reg, const char *service_name) {
    for (int32_t i = 0; i < reg->service_count; i++) {
        if (strcmp(reg->services[i].service_name, service_name) == 0) return i;
    }
    return -1;
}
