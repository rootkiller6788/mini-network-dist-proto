#ifndef GRPC_PROTO_H
#define GRPC_PROTO_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#define GRPC_MAX_SERVICE_METHODS 16
#define GRPC_MAX_SERVICE_NAME   128
#define GRPC_MAX_METHOD_NAME    128
#define GRPC_MAX_MESSAGE_SIZE   4194304
#define GRPC_MESSAGE_HEADER_SIZE 5

enum GRPCStreamingType {
    GRPC_UNARY,
    GRPC_CLIENT_STREAM,
    GRPC_SERVER_STREAM,
    GRPC_BIDI
};

enum GRPCStatusCode {
    GRPC_OK                  = 0,
    GRPC_CANCELLED           = 1,
    GRPC_UNKNOWN             = 2,
    GRPC_INVALID_ARGUMENT    = 3,
    GRPC_DEADLINE_EXCEEDED   = 4,
    GRPC_NOT_FOUND           = 5,
    GRPC_ALREADY_EXISTS      = 6,
    GRPC_PERMISSION_DENIED   = 7,
    GRPC_RESOURCE_EXHAUSTED  = 8,
    GRPC_FAILED_PRECONDITION = 9,
    GRPC_ABORTED             = 10,
    GRPC_OUT_OF_RANGE        = 11,
    GRPC_UNIMPLEMENTED       = 12,
    GRPC_INTERNAL            = 13,
    GRPC_UNAVAILABLE         = 14,
    GRPC_DATA_LOSS           = 15,
    GRPC_UNAUTHENTICATED     = 16
};

typedef struct {
    char name[GRPC_MAX_METHOD_NAME];
    char request_type[64];
    char response_type[64];
    enum GRPCStreamingType streaming;
} GRPCMethod;

typedef struct {
    char        name[GRPC_MAX_SERVICE_NAME];
    GRPCMethod  methods[GRPC_MAX_SERVICE_METHODS];
    size_t      method_count;
} GRPCService;

typedef struct {
    bool     compressed;
    uint32_t length;
    uint8_t *data;
} GRPCMessage;

typedef struct {
    uint8_t   compressed    : 1;
    uint8_t   reserved      : 7;
    uint32_t  length;
} GRPCMessageHeader;

typedef struct {
    uint32_t call_id;
    char     service_name[GRPC_MAX_SERVICE_NAME];
    char     method_name[GRPC_MAX_METHOD_NAME];
    uint8_t *request_data;
    size_t   request_len;
    uint8_t *response_data;
    size_t   response_len;
} GRPCCall;

typedef struct {
    GRPCService services[16];
    size_t      service_count;
} GRPCServer;

size_t  grpc_encode_message(const uint8_t *payload, size_t payload_len,
                            bool compressed, uint8_t *out, size_t out_size);
int     grpc_decode_message(const uint8_t *data, size_t len,
                            uint8_t **payload, size_t *payload_len,
                            bool *compressed);
int     grpc_send_request(uint32_t call_id, const char *service,
                          const char *method, const uint8_t *payload,
                          size_t payload_len, uint8_t *out, size_t out_size,
                          size_t *written);
int     grpc_send_response(uint32_t call_id, enum GRPCStatusCode status,
                           const uint8_t *payload, size_t payload_len,
                           uint8_t *out, size_t out_size, size_t *written);
int     grpc_build_service_desc(const GRPCService *service,
                                char *out, size_t out_size);
void    grpc_service_add_method(GRPCService *service, const char *name,
                                const char *req_type, const char *resp_type,
                                enum GRPCStreamingType streaming);
int     grpc_kv_serialize(const char *key, const char *value,
                          uint8_t *out, size_t out_size, size_t *written);
int     grpc_kv_deserialize(const uint8_t *data, size_t len,
                            char *key, size_t key_size,
                            char *value, size_t value_size);
void    grpc_server_init(GRPCServer *server);
void    grpc_server_register(GRPCServer *server, const GRPCService *service);
const GRPCMethod *grpc_server_find_method(const GRPCServer *server,
                                          const char *service_name,
                                          const char *method_name);

#endif
