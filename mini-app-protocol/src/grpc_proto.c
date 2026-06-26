#include "grpc_proto.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

size_t grpc_encode_message(const uint8_t *payload, size_t payload_len,
                           bool compressed, uint8_t *out, size_t out_size)
{
    if (!payload || !out) return 0;

    size_t total = GRPC_MESSAGE_HEADER_SIZE + payload_len;
    if (total > out_size) return 0;

    out[0] = (uint8_t)(compressed ? 0x01 : 0x00);
    out[1] = (uint8_t)(payload_len >> 24);
    out[2] = (uint8_t)(payload_len >> 16);
    out[3] = (uint8_t)(payload_len >> 8);
    out[4] = (uint8_t)(payload_len);

    memcpy(out + GRPC_MESSAGE_HEADER_SIZE, payload, payload_len);

    return total;
}

int grpc_decode_message(const uint8_t *data, size_t len,
                        uint8_t **payload, size_t *payload_len,
                        bool *compressed)
{
    if (!data || !payload || !payload_len || !compressed) return -1;
    if (len < GRPC_MESSAGE_HEADER_SIZE) return -2;

    *compressed  = (data[0] == 0x01);
    *payload_len = ((uint32_t)data[1] << 24) |
                   ((uint32_t)data[2] << 16) |
                   ((uint32_t)data[3] << 8)  |
                    (uint32_t)data[4];

    if (len < GRPC_MESSAGE_HEADER_SIZE + *payload_len) return -3;

    *payload = (uint8_t *)(data + GRPC_MESSAGE_HEADER_SIZE);

    return 0;
}

int grpc_send_request(uint32_t call_id, const char *service,
                      const char *method, const uint8_t *payload,
                      size_t payload_len, uint8_t *out, size_t out_size,
                      size_t *written)
{
    if (!service || !method || !out || !written) return -1;

    size_t svc_len = strlen(service);
    size_t mtd_len = strlen(method);
    size_t total = 4 + svc_len + mtd_len + payload_len + GRPC_MESSAGE_HEADER_SIZE;

    if (total > out_size) return -2;

    size_t pos = 0;
    out[pos++] = (uint8_t)(call_id >> 24);
    out[pos++] = (uint8_t)(call_id >> 16);
    out[pos++] = (uint8_t)(call_id >> 8);
    out[pos++] = (uint8_t)(call_id);
    out[pos++] = (uint8_t)svc_len;
    memcpy(out + pos, service, svc_len); pos += svc_len;
    out[pos++] = (uint8_t)mtd_len;
    memcpy(out + pos, method, mtd_len); pos += mtd_len;

    if (payload && payload_len > 0) {
        size_t msg_len = grpc_encode_message(payload, payload_len, false,
                                             out + pos, out_size - pos);
        pos += msg_len;
    }

    *written = pos;
    return 0;
}

int grpc_send_response(uint32_t call_id, enum GRPCStatusCode status,
                       const uint8_t *payload, size_t payload_len,
                       uint8_t *out, size_t out_size, size_t *written)
{
    if (!out || !written) return -1;

    size_t total = 5 + payload_len + GRPC_MESSAGE_HEADER_SIZE;

    if (total > out_size) return -2;

    size_t pos = 0;
    out[pos++] = (uint8_t)(call_id >> 24);
    out[pos++] = (uint8_t)(call_id >> 16);
    out[pos++] = (uint8_t)(call_id >> 8);
    out[pos++] = (uint8_t)(call_id);
    out[pos++] = (uint8_t)status;

    if (payload && payload_len > 0) {
        size_t msg_len = grpc_encode_message(payload, payload_len, false,
                                             out + pos, out_size - pos);
        pos += msg_len;
    }

    *written = pos;
    return 0;
}

int grpc_build_service_desc(const GRPCService *service,
                            char *out, size_t out_size)
{
    if (!service || !out) return -1;

    int n = snprintf(out, out_size,
                     "service %s {\n", service->name);
    if (n < 0) return -1;

    size_t pos = (size_t)n;

    for (size_t i = 0; i < service->method_count; i++) {
        const GRPCMethod *m = &service->methods[i];
        const char *stream;

        switch (m->streaming) {
        case GRPC_UNARY:         stream = ""; break;
        case GRPC_CLIENT_STREAM: stream = "stream "; break;
        case GRPC_SERVER_STREAM: stream = "returns (stream "; break;
        case GRPC_BIDI:          stream = "stream returns (stream "; break;
        default:                 stream = ""; break;
        }

        if (m->streaming == GRPC_SERVER_STREAM) {
            n = snprintf(out + pos, out_size - pos,
                         "  rpc %s(%s) returns (stream %s);\n",
                         m->name, m->request_type, m->response_type);
        } else if (m->streaming == GRPC_BIDI) {
            n = snprintf(out + pos, out_size - pos,
                         "  rpc %s(stream %s) returns (stream %s);\n",
                         m->name, m->request_type, m->response_type);
        } else {
            const char *prefix = m->streaming == GRPC_CLIENT_STREAM ? "stream " : "";
            n = snprintf(out + pos, out_size - pos,
                         "  rpc %s(%s%s) returns (%s);\n",
                         m->name, prefix, m->request_type, m->response_type);
        }

        if (n < 0) return -1;
        pos += (size_t)n;
    }

    n = snprintf(out + pos, out_size - pos, "}\n");
    if (n < 0) return -1;

    return 0;
}

void grpc_service_add_method(GRPCService *service, const char *name,
                             const char *req_type, const char *resp_type,
                             enum GRPCStreamingType streaming)
{
    if (!service || !name || !req_type || !resp_type) return;
    if (service->method_count >= GRPC_MAX_SERVICE_METHODS) return;

    GRPCMethod *m = &service->methods[service->method_count];
    snprintf(m->name, sizeof(m->name), "%s", name);
    snprintf(m->request_type, sizeof(m->request_type), "%s", req_type);
    snprintf(m->response_type, sizeof(m->response_type), "%s", resp_type);
    m->streaming = streaming;
    service->method_count++;
}

int grpc_kv_serialize(const char *key, const char *value,
                      uint8_t *out, size_t out_size, size_t *written)
{
    if (!key || !value || !out || !written) return -1;

    size_t key_len   = strlen(key);
    size_t value_len = strlen(value);
    size_t total     = 2 + key_len + value_len;

    if (total > out_size) return -2;

    out[0] = (uint8_t)key_len;
    memcpy(out + 1, key, key_len);
    out[1 + key_len] = (uint8_t)value_len;
    memcpy(out + 2 + key_len, value, value_len);

    *written = total;
    return 0;
}

int grpc_kv_deserialize(const uint8_t *data, size_t len,
                        char *key, size_t key_size,
                        char *value, size_t value_size)
{
    if (!data || !key || !value) return -1;
    if (len < 2) return -2;

    size_t key_len = (size_t)data[0];
    if (1 + key_len >= len) return -3;

    size_t value_len = (size_t)data[1 + key_len];
    size_t total     = 2 + key_len + value_len;

    if (total > len) return -4;

    if (key_len >= key_size || value_len >= value_size) return -5;

    memcpy(key, data + 1, key_len);
    key[key_len] = '\0';
    memcpy(value, data + 2 + key_len, value_len);
    value[value_len] = '\0';

    return 0;
}

void grpc_server_init(GRPCServer *server)
{
    if (!server) return;
    server->service_count = 0;
    memset(server->services, 0, sizeof(server->services));
}

void grpc_server_register(GRPCServer *server, const GRPCService *service)
{
    if (!server || !service) return;
    if (server->service_count >= 16) return;

    memcpy(&server->services[server->service_count], service,
           sizeof(GRPCService));
    server->service_count++;
}

const GRPCMethod *grpc_server_find_method(const GRPCServer *server,
                                          const char *service_name,
                                          const char *method_name)
{
    if (!server || !service_name || !method_name) return NULL;

    for (size_t i = 0; i < server->service_count; i++) {
        if (strcmp(server->services[i].name, service_name) != 0)
            continue;

        for (size_t j = 0; j < server->services[i].method_count; j++) {
            if (strcmp(server->services[i].methods[j].name, method_name) == 0)
                return &server->services[i].methods[j];
        }
    }

    return NULL;
}
