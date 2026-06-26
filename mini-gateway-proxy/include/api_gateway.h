#ifndef API_GATEWAY_H
#define API_GATEWAY_H

#include <stdbool.h>
#include <stdint.h>
#include "circuit_breaker.h"
#include "rate_limiter.h"

#define AG_MAX_ROUTES         128
#define AG_MAX_PLUGINS        16
#define AG_MAX_METHODS        8
#define AG_PATH_LEN           256
#define AG_SERVICE_LEN        256

typedef enum {
    GP_AUTH,
    GP_RATE_LIMIT,
    GP_LOGGING,
    GP_CORS,
    GP_TRANSFORM,
    GP_CACHE
} GatewayPlugin;

typedef struct {
    char     path[AG_PATH_LEN];
    char     methods[AG_MAX_METHODS][16];
    int      num_methods;
    char     upstream_host[AG_SERVICE_LEN];
    int      upstream_port;
    bool     auth_required;
    int      rate_limit_rps;
    GatewayPlugin plugins[AG_MAX_PLUGINS];
    int      num_plugins;
    CBCircuit *circuit_breaker;
    RateLimiter *rate_limiter;
} APIRoute;

typedef struct {
    APIRoute       routes[AG_MAX_ROUTES];
    int            num_routes;
    GatewayPlugin  global_plugins[AG_MAX_PLUGINS];
    int            num_global_plugins;
} APIGateway;

typedef struct {
    APIRoute     *route;
    char          method[16];
    char          path[AG_PATH_LEN];
    char          headers[4][256];
    int           num_headers;
    char          body[4096];
    int           body_len;
    bool          blocked;
    int           status_code;
    char          response[8192];
    int           response_len;
    char          client_ip[64];
    char          auth_token[256];
} GatewayRequest;

APIGateway*     gateway_init(void);
int             gateway_register_route(APIGateway *gw, const char *path,
                                       const char *method,
                                       const char *upstream, int port,
                                       bool auth_required, int rate_limit_rps);
int             gateway_add_plugin(APIGateway *gw, GatewayPlugin plugin);
int             gateway_add_route_plugin(APIRoute *route, GatewayPlugin plugin);
int             gateway_handle_request(APIGateway *gw, GatewayRequest *req);
int             gateway_plugin_run(GatewayPlugin plugin, GatewayRequest *req);
APIRoute*       gateway_match_route(APIGateway *gw, const char *path,
                                    const char *method);
int             gateway_plugin_auth(GatewayRequest *req);
int             gateway_plugin_rate_limit(GatewayRequest *req);
int             gateway_plugin_logging(GatewayRequest *req);
int             gateway_plugin_cors(GatewayRequest *req);
int             gateway_plugin_transform(GatewayRequest *req);
int             gateway_plugin_cache(GatewayRequest *req);
void            gateway_print_routes(const APIGateway *gw);

#endif
