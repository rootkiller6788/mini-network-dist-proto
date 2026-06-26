#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "api_gateway.h"

static bool method_match(const APIRoute *route, const char *method)
{
    for (int i = 0; i < route->num_methods; i++) {
        if (strcmp(route->methods[i], method) == 0) return true;
        if (strcmp(route->methods[i], "ANY") == 0) return true;
    }
    return false;
}

APIGateway* gateway_init(void)
{
    APIGateway *gw = calloc(1, sizeof(APIGateway));
    if (!gw) return NULL;
    gw->num_routes = 0;
    gw->num_global_plugins = 0;
    printf("[gateway] API Gateway initialized\n");
    return gw;
}

int gateway_register_route(APIGateway *gw, const char *path,
                           const char *method,
                           const char *upstream, int port,
                           bool auth_required, int rate_limit_rps)
{
    if (!gw || !path || !method || !upstream) return -1;
    if (gw->num_routes >= AG_MAX_ROUTES) return -1;

    APIRoute *route = &gw->routes[gw->num_routes];
    memset(route, 0, sizeof(APIRoute));

    snprintf(route->path, AG_PATH_LEN, "%s", path);
    snprintf(route->methods[0], 16, "%s", method);
    route->num_methods = 1;
    snprintf(route->upstream_host, AG_SERVICE_LEN, "%s", upstream);
    route->upstream_port = port;
    route->auth_required = auth_required;
    route->rate_limit_rps = rate_limit_rps;
    route->num_plugins = 0;

    /* auto-create circuit breaker for this route */
    char cb_name[160];
    snprintf(cb_name, sizeof(cb_name), "%s:%s", path, method);
    route->circuit_breaker = cb_init(cb_name, 5, 3, 10000);

    /* auto-create rate limiter if rps > 0 */
    if (rate_limit_rps > 0) {
        route->rate_limiter = rl_init(RL_TOKEN_BUCKET,
                                      (double)rate_limit_rps,
                                      (double)rate_limit_rps * 2, 0);
    }

    gw->num_routes++;
    printf("[gateway] Registered route %s %s -> %s:%d "
           "(auth=%s, rps=%d)\n",
           method, path, upstream, port,
           auth_required ? "yes" : "no", rate_limit_rps);
    return 0;
}

int gateway_add_plugin(APIGateway *gw, GatewayPlugin plugin)
{
    if (!gw || gw->num_global_plugins >= AG_MAX_PLUGINS) return -1;
    gw->global_plugins[gw->num_global_plugins++] = plugin;
    printf("[gateway] Added global plugin %d\n", (int)plugin);
    return 0;
}

int gateway_add_route_plugin(APIRoute *route, GatewayPlugin plugin)
{
    if (!route || route->num_plugins >= AG_MAX_PLUGINS) return -1;
    route->plugins[route->num_plugins++] = plugin;
    return 0;
}

APIRoute* gateway_match_route(APIGateway *gw, const char *path,
                              const char *method)
{
    if (!gw || !path || !method) return NULL;

    APIRoute *best = NULL;
    size_t best_len = 0;

    for (int i = 0; i < gw->num_routes; i++) {
        APIRoute *route = &gw->routes[i];
        size_t route_len = strlen(route->path);

        if (route_len == 0) continue;

        /* exact match */
        if (strcmp(path, route->path) == 0) {
            if (method_match(route, method)) return route;
            if (route_len > best_len) {
                best = route;
                best_len = route_len;
            }
            continue;
        }

        /* wildcard match */
        if (route->path[route_len - 1] == '*') {
            char prefix[AG_PATH_LEN];
            snprintf(prefix, sizeof(prefix), "%.*s",
                     (int)(route_len - 1), route->path);
            if (strncmp(path, prefix, strlen(prefix)) == 0) {
                if (method_match(route, method)) return route;
                if (route_len > best_len) {
                    best = route;
                    best_len = route_len;
                }
            }
        }

        /* prefix match */
        if (strncmp(path, route->path, route_len) == 0 && route_len > best_len) {
            if (method_match(route, method)) return route;
            best = route;
            best_len = route_len;
        }
    }

    return best;
}

int gateway_handle_request(APIGateway *gw, GatewayRequest *req)
{
    if (!gw || !req) return -1;

    printf("[gateway] Handling %s %s from %s\n",
           req->method, req->path, req->client_ip);

    /* match route */
    APIRoute *route = gateway_match_route(gw, req->path, req->method);
    if (!route) {
        req->blocked = true;
        req->status_code = 404;
        snprintf(req->response, sizeof(req->response),
                 "HTTP/1.1 404 Not Found\r\n"
                 "Content-Type: text/plain\r\n"
                 "Content-Length: 13\r\n\r\n"
                 "404 Not Found");
        req->response_len = (int)strlen(req->response);
        printf("[gateway] No route matched for %s %s\n",
               req->method, req->path);
        return -1;
    }
    req->route = route;

    /* run global plugin chain */
    for (int i = 0; i < gw->num_global_plugins; i++) {
        int ret = gateway_plugin_run(gw->global_plugins[i], req);
        if (ret < 0 || req->blocked) {
            printf("[gateway] Blocked by global plugin %d\n",
                   (int)gw->global_plugins[i]);
            return -1;
        }
    }

    /* run route plugin chain */
    for (int i = 0; i < route->num_plugins; i++) {
        int ret = gateway_plugin_run(route->plugins[i], req);
        if (ret < 0 || req->blocked) {
            printf("[gateway] Blocked by route plugin %d\n",
                   (int)route->plugins[i]);
            return -1;
        }
    }

    /* auth check */
    if (route->auth_required) {
        int ret = gateway_plugin_auth(req);
        if (ret < 0) {
            req->blocked = true;
            req->status_code = 401;
            snprintf(req->response, sizeof(req->response),
                     "HTTP/1.1 401 Unauthorized\r\n"
                     "Content-Type: text/plain\r\n"
                     "Content-Length: 26\r\n\r\n"
                     "401 Unauthorized");
            req->response_len = (int)strlen(req->response);
            return -1;
        }
    }

    /* rate limit check */
    if (route->rate_limiter) {
        if (!rl_allow(route->rate_limiter)) {
            req->blocked = true;
            req->status_code = 429;
            snprintf(req->response, sizeof(req->response),
                     "HTTP/1.1 429 Too Many Requests\r\n"
                     "Content-Type: text/plain\r\n"
                     "Content-Length: 25\r\n\r\n"
                     "429 Too Many Requests");
            req->response_len = (int)strlen(req->response);
            printf("[gateway] Rate limited %s %s\n", req->method, req->path);
            return -1;
        }
    }

    /* circuit breaker proxy to upstream */
    if (route->circuit_breaker) {
        struct {
            GatewayRequest *r;
            APIRoute *route;
        } ctx = { req, route };

        /* use cb_call to wrap the forward-to-upstream */
        int ret = cb_call(route->circuit_breaker, NULL, &ctx);
        if (ret < 0) {
            req->blocked = true;
            req->status_code = 503;
            snprintf(req->response, sizeof(req->response),
                     "HTTP/1.1 503 Service Unavailable\r\n"
                     "Content-Type: text/plain\r\n"
                     "Content-Length: 24\r\n\r\n"
                     "503 Service Unavailable");
            req->response_len = (int)strlen(req->response);
            printf("[gateway] Circuit breaker open for %s\n", req->path);
            return -1;
        }
    }

    /* success - simulate upstream response */
    snprintf(req->response, sizeof(req->response),
             "HTTP/1.1 200 OK\r\n"
             "Content-Type: application/json\r\n"
             "Content-Length: 4\r\n\r\n"
             "{}");
    req->response_len = (int)strlen(req->response);
    req->status_code = 200;

    printf("[gateway] Forwarded %s %s -> %s:%d (200)\n",
           req->method, req->path,
           route->upstream_host, route->upstream_port);
    return 0;
}

int gateway_plugin_run(GatewayPlugin plugin, GatewayRequest *req)
{
    switch (plugin) {
    case GP_AUTH:       return gateway_plugin_auth(req);
    case GP_RATE_LIMIT: return gateway_plugin_rate_limit(req);
    case GP_LOGGING:    return gateway_plugin_logging(req);
    case GP_CORS:       return gateway_plugin_cors(req);
    case GP_TRANSFORM:  return gateway_plugin_transform(req);
    case GP_CACHE:      return gateway_plugin_cache(req);
    default:            return -1;
    }
}

int gateway_plugin_auth(GatewayRequest *req)
{
    if (!req) return -1;
    if (strlen(req->auth_token) == 0) {
        printf("[plugin:auth] Missing auth token for %s %s\n",
               req->method, req->path);
        return -1;
    }
    printf("[plugin:auth] Validating token '%s' for %s\n",
           req->auth_token, req->path);
    return (req->auth_token[0] != '\0') ? 0 : -1;
}

int gateway_plugin_rate_limit(GatewayRequest *req)
{
    if (!req) return -1;
    printf("[plugin:rate_limit] Checking rate limit for %s\n", req->client_ip);
    return 0;
}

int gateway_plugin_logging(GatewayRequest *req)
{
    if (!req) return -1;
    printf("[plugin:logging] %s %s from %s\n",
           req->method, req->path, req->client_ip);
    return 0;
}

int gateway_plugin_cors(GatewayRequest *req)
{
    if (!req) return -1;
    printf("[plugin:cors] Adding CORS headers\n");
    return 0;
}

int gateway_plugin_transform(GatewayRequest *req)
{
    if (!req) return -1;
    printf("[plugin:transform] Transforming request for %s\n", req->path);
    return 0;
}

int gateway_plugin_cache(GatewayRequest *req)
{
    if (!req) return -1;
    printf("[plugin:cache] Checking cache for %s\n", req->path);
    return 0;
}

void gateway_print_routes(const APIGateway *gw)
{
    if (!gw) return;

    printf("=== API Gateway Routes ===\n");
    printf("Total routes: %d\n", gw->num_routes);
    printf("Global plugins: %d\n", gw->num_global_plugins);

    for (int i = 0; i < gw->num_routes; i++) {
        const APIRoute *r = &gw->routes[i];
        printf("  [%d] %s", i, r->methods[0]);
        for (int j = 1; j < r->num_methods; j++) {
            printf(",%s", r->methods[j]);
        }
        printf(" %-24s -> %s:%d", r->path,
               r->upstream_host, r->upstream_port);
        if (r->auth_required) printf(" [AUTH]");
        if (r->rate_limit_rps > 0) printf(" [RPS:%d]", r->rate_limit_rps);
        if (r->circuit_breaker) printf(" [CB]");
        if (r->rate_limiter) printf(" [RL]");
        printf("\n");
    }
    printf("===========================\n");
}
