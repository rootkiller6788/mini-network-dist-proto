#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "api_gateway.h"

static void print_separator(const char *title)
{
    printf("\n--------------------------------------------------\n");
    printf("  %s\n", title);
    printf("--------------------------------------------------\n");
}

static void demo_route_registration(APIGateway *gw)
{
    print_separator("Demo 1: Route Registration");

    gateway_register_route(gw, "/users", "GET", "user-service", 8080, true, 100);
    gateway_register_route(gw, "/users", "POST", "user-service", 8080, true, 50);
    gateway_register_route(gw, "/users", "PUT", "user-service", 8080, true, 50);
    gateway_register_route(gw, "/users", "DELETE", "user-service", 8080, true, 30);
    gateway_register_route(gw, "/orders", "GET", "order-service", 8081, true, 200);
    gateway_register_route(gw, "/orders", "POST", "order-service", 8081, true, 100);
    gateway_register_route(gw, "/products", "GET", "product-service", 8082, false, 500);
    gateway_register_route(gw, "/health", "GET", "health-service", 8083, false, 0);

    gateway_add_plugin(gw, GP_LOGGING);
    gateway_add_plugin(gw, GP_CORS);

    gateway_print_routes(gw);
}

static void demo_successful_request(APIGateway *gw)
{
    print_separator("Demo 2: Successful Request Flow");

    GatewayRequest req;
    memset(&req, 0, sizeof(req));
    snprintf(req.method, sizeof(req.method), "GET");
    snprintf(req.path, sizeof(req.path), "/users");
    snprintf(req.client_ip, sizeof(req.client_ip), "192.168.1.100");
    snprintf(req.auth_token, sizeof(req.auth_token), "Bearer eyJhbGciOiJI");

    int ret = gateway_handle_request(gw, &req);
    printf("  Result: status=%d, blocked=%s\n",
           req.status_code, req.blocked ? "yes" : "no");
    printf("  Response: %.*s\n",
           (int)strcspn(req.response, "\r"), req.response);
}

static void demo_auth_failure(APIGateway *gw)
{
    print_separator("Demo 3: Authentication Failure");

    GatewayRequest req;
    memset(&req, 0, sizeof(req));
    snprintf(req.method, sizeof(req.method), "POST");
    snprintf(req.path, sizeof(req.path), "/users");
    snprintf(req.client_ip, sizeof(req.client_ip), "10.0.0.55");
    /* no auth token */

    int ret = gateway_handle_request(gw, &req);
    printf("  Result: status=%d, blocked=%s\n",
           req.status_code, req.blocked ? "yes" : "no");
    printf("  Expected: 401 Unauthorized\n");
}

static void demo_rate_limiting(APIGateway *gw)
{
    print_separator("Demo 4: Rate Limiting");

    GatewayRequest req;
    memset(&req, 0, sizeof(req));
    snprintf(req.method, sizeof(req.method), "GET");
    snprintf(req.path, sizeof(req.path), "/users");
    snprintf(req.client_ip, sizeof(req.client_ip), "192.168.1.1");
    snprintf(req.auth_token, sizeof(req.auth_token), "Bearer test123");

    int allowed = 0, rejected = 0;
    for (int i = 0; i < 20; i++) {
        /* need fresh request each iteration; rate limiter is stateful */
        GatewayRequest r;
        memset(&r, 0, sizeof(r));
        snprintf(r.method, sizeof(r.method), "GET");
        snprintf(r.path, sizeof(r.path), "/users");
        snprintf(r.client_ip, sizeof(r.client_ip), "192.168.1.1");
        snprintf(r.auth_token, sizeof(r.auth_token), "Bearer test123");

        int ret = gateway_handle_request(gw, &r);
        if (r.blocked) rejected++;
        else allowed++;
    }
    printf("  Rate limit 100 rps on /users GET:\n");
    printf("  Allowed: %d, Rejected: %d\n", allowed, rejected);
}

static void demo_route_not_found(APIGateway *gw)
{
    print_separator("Demo 5: Route Not Found (404)");

    GatewayRequest req;
    memset(&req, 0, sizeof(req));
    snprintf(req.method, sizeof(req.method), "GET");
    snprintf(req.path, sizeof(req.path), "/nonexistent");
    snprintf(req.client_ip, sizeof(req.client_ip), "172.16.0.1");

    int ret = gateway_handle_request(gw, &req);
    printf("  Result: status=%d, blocked=%s\n",
           req.status_code, req.blocked ? "yes" : "no");
    printf("  Expected: 404 Not Found\n");
}

static void demo_plugin_chain(APIGateway *gw)
{
    print_separator("Demo 6: Plugin Chain Execution");

    GatewayRequest req;
    memset(&req, 0, sizeof(req));
    snprintf(req.method, sizeof(req.method), "GET");
    snprintf(req.path, sizeof(req.path), "/products");
    snprintf(req.client_ip, sizeof(req.client_ip), "10.10.10.10");

    APIRoute *route = gateway_match_route(gw, "/products", "GET");
    if (route) {
        gateway_add_route_plugin(route, GP_TRANSFORM);
        gateway_add_route_plugin(route, GP_CACHE);
    }

    printf("  Global plugins: LOGGING, CORS\n");
    printf("  Route plugins: TRANSFORM, CACHE\n");
    printf("  Execution order: LOGGING -> CORS -> TRANSFORM -> CACHE\n\n");

    int ret = gateway_handle_request(gw, &req);
    printf("  Result: status=%d\n", req.status_code);
}

static void demo_circuit_breaker_integration(APIGateway *gw)
{
    print_separator("Demo 7: Circuit Breaker Integration");

    printf("  Each route automatically has a circuit breaker\n");
    printf("  When upstream fails repeatedly, CB opens and fast-fails\n");

    GatewayRequest req;
    memset(&req, 0, sizeof(req));
    snprintf(req.method, sizeof(req.method), "POST");
    snprintf(req.path, sizeof(req.path), "/orders");
    snprintf(req.client_ip, sizeof(req.client_ip), "10.0.0.1");
    snprintf(req.auth_token, sizeof(req.auth_token), "Bearer ordertoken");

    APIRoute *route = gateway_match_route(gw, "/orders", "POST");
    if (route && route->circuit_breaker) {
        cb_print_state(route->circuit_breaker);
    }

    int ret = gateway_handle_request(gw, &req);
    printf("  Request result: status=%d\n", req.status_code);
}

static void demo_no_auth_route(APIGateway *gw)
{
    print_separator("Demo 8: Public Route (No Auth)");

    GatewayRequest req;
    memset(&req, 0, sizeof(req));
    snprintf(req.method, sizeof(req.method), "GET");
    snprintf(req.path, sizeof(req.path), "/products");
    snprintf(req.client_ip, sizeof(req.client_ip), "8.8.8.8");
    /* no auth token needed */

    int ret = gateway_handle_request(gw, &req);
    printf("  Result: status=%d, blocked=%s\n",
           req.status_code, req.blocked ? "yes" : "no");
    printf("  /products GET is public (auth_required=false)\n");
}

int main(void)
{
    printf("============================================\n");
    printf("  API Gateway Demo\n");
    printf("============================================\n");
    printf("Routes: /users CRUD, /orders CRUD, /products GET, /health GET\n");
    printf("Features: Auth, Rate Limit, Circuit Breaker, Plugin Chain\n");

    APIGateway *gw = gateway_init();

    demo_route_registration(gw);
    demo_successful_request(gw);
    demo_auth_failure(gw);
    demo_rate_limiting(gw);
    demo_route_not_found(gw);
    demo_plugin_chain(gw);
    demo_circuit_breaker_integration(gw);
    demo_no_auth_route(gw);

    printf("\n============================================\n");
    printf("  Demo Complete\n");
    printf("============================================\n");

    free(gw);
    return 0;
}
