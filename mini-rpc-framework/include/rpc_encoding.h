#ifndef RPC_ENCODING_H
#define RPC_ENCODING_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#define RPC_MAX_PARAMS 16
#define RPC_MAX_METHOD_NAME 128
#define RPC_BUFFER_INIT_CAPACITY 4096
#define RPC_MAX_ARRAY_LEN 64

typedef enum {
    RPC_CODEC_JSON   = 0,
    RPC_CODEC_MSGPACK = 1,
    RPC_CODEC_BINARY  = 2
} RPCCodec;

typedef enum {
    RPC_TYPE_INT32  = 0,
    RPC_TYPE_INT64  = 1,
    RPC_TYPE_STRING = 2,
    RPC_TYPE_BOOL   = 3,
    RPC_TYPE_FLOAT  = 4,
    RPC_TYPE_ARRAY  = 5,
    RPC_TYPE_NULL   = 6
} RPCValueType;

typedef struct {
    RPCValueType type;
    union {
        int32_t  v_int32;
        int64_t  v_int64;
        char    *v_string;
        bool     v_bool;
        double   v_float;
        struct {
            RPCValueType elem_type;
            void       *data;
            int32_t     len;
        } v_array;
    } value;
} RPCValue;

typedef struct {
    int32_t     id;
    char        method_name[RPC_MAX_METHOD_NAME];
    RPCValue    params[RPC_MAX_PARAMS];
    int32_t     param_count;
    RPCValueType return_type;
    bool        is_request;
    bool        is_error;
    char        error_msg[256];
} RPCMessage;

typedef struct {
    uint8_t *data;
    size_t   len;
    size_t   capacity;
} RPCBuffer;

void    rpc_buffer_init(RPCBuffer *buf);
void    rpc_buffer_free(RPCBuffer *buf);
void    rpc_buffer_reserve(RPCBuffer *buf, size_t size);
void    rpc_buffer_append(RPCBuffer *buf, const uint8_t *src, size_t len);
void    rpc_buffer_reset(RPCBuffer *buf);

void    rpc_value_free(RPCValue *v);
void    rpc_message_init(RPCMessage *msg);
void    rpc_message_free(RPCMessage *msg);

int32_t rpc_fnv1a_hash(const char *str, int32_t len);

int     rpc_encode_json(const RPCMessage *msg, RPCBuffer *out);
int     rpc_decode_json(const RPCBuffer *in, RPCMessage *msg);

int     rpc_encode_binary(const RPCMessage *msg, RPCBuffer *out);
int     rpc_decode_binary(const RPCBuffer *in, RPCMessage *msg);

int     rpc_encode_message(RPCCodec codec, const RPCMessage *msg, RPCBuffer *out);
int     rpc_decode_message(RPCCodec codec, RPCBuffer *in, RPCMessage *msg);

#endif
