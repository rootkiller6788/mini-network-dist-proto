#include "rpc_encoding.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

void rpc_buffer_init(RPCBuffer *buf) {
    buf->capacity = RPC_BUFFER_INIT_CAPACITY;
    buf->data = (uint8_t *)malloc(buf->capacity);
    buf->len = 0;
    if (buf->data) memset(buf->data, 0, buf->capacity);
}

void rpc_buffer_free(RPCBuffer *buf) {
    if (buf->data) {
        free(buf->data);
        buf->data = NULL;
    }
    buf->len = 0;
    buf->capacity = 0;
}

void rpc_buffer_reserve(RPCBuffer *buf, size_t size) {
    if (size <= buf->capacity) return;
    size_t newcap = buf->capacity;
    while (newcap < size) newcap *= 2;
    uint8_t *newdata = (uint8_t *)realloc(buf->data, newcap);
    if (!newdata) return;
    buf->data = newdata;
    buf->capacity = newcap;
}

void rpc_buffer_append(RPCBuffer *buf, const uint8_t *src, size_t len) {
    rpc_buffer_reserve(buf, buf->len + len);
    memcpy(buf->data + buf->len, src, len);
    buf->len += len;
}

void rpc_buffer_reset(RPCBuffer *buf) {
    buf->len = 0;
    if (buf->data) buf->data[0] = 0;
}

void rpc_value_free(RPCValue *v) {
    if (!v) return;
    if (v->type == RPC_TYPE_STRING && v->value.v_string) {
        free(v->value.v_string);
        v->value.v_string = NULL;
    } else if (v->type == RPC_TYPE_ARRAY && v->value.v_array.data) {
        free(v->value.v_array.data);
        v->value.v_array.data = NULL;
    }
    v->type = RPC_TYPE_NULL;
}

void rpc_message_init(RPCMessage *msg) {
    msg->id = 0;
    memset(msg->method_name, 0, sizeof(msg->method_name));
    msg->param_count = 0;
    msg->return_type = RPC_TYPE_NULL;
    msg->is_request = true;
    msg->is_error = false;
    memset(msg->error_msg, 0, sizeof(msg->error_msg));
    for (int i = 0; i < RPC_MAX_PARAMS; i++) {
        msg->params[i].type = RPC_TYPE_NULL;
        msg->params[i].value.v_int32 = 0;
    }
}

void rpc_message_free(RPCMessage *msg) {
    for (int i = 0; i < msg->param_count; i++) {
        rpc_value_free(&msg->params[i]);
    }
    rpc_message_init(msg);
}

int32_t rpc_fnv1a_hash(const char *str, int32_t len) {
    int32_t hash = 2166136261;
    for (int32_t i = 0; i < len; i++) {
        hash ^= (uint8_t)str[i];
        hash *= 16777619;
    }
    return hash;
}

static void write_i32be(uint8_t *buf, int32_t val) {
    buf[0] = (uint8_t)((val >> 24) & 0xFF);
    buf[1] = (uint8_t)((val >> 16) & 0xFF);
    buf[2] = (uint8_t)((val >> 8) & 0xFF);
    buf[3] = (uint8_t)(val & 0xFF);
}

static int32_t read_i32be(const uint8_t *buf) {
    return ((int32_t)buf[0] << 24) | ((int32_t)buf[1] << 16)
         | ((int32_t)buf[2] << 8)  | ((int32_t)buf[3]);
}

static void write_i64be(uint8_t *buf, int64_t val) {
    write_i32be(buf, (int32_t)((val >> 32) & 0xFFFFFFFF));
    write_i32be(buf + 4, (int32_t)(val & 0xFFFFFFFF));
}

static int64_t read_i64be(const uint8_t *buf) {
    return ((int64_t)read_i32be(buf) << 32) | ((int64_t)(read_i32be(buf + 4)) & 0xFFFFFFFFLL);
}

static int rpc_encode_value_json(RPCBuffer *out, const RPCValue *v) {
    char tmp[512];
    switch (v->type) {
    case RPC_TYPE_INT32:
        snprintf(tmp, sizeof(tmp), "%d", v->value.v_int32);
        rpc_buffer_append(out, (uint8_t *)tmp, strlen(tmp));
        break;
    case RPC_TYPE_INT64:
        snprintf(tmp, sizeof(tmp), "%lld", (long long)v->value.v_int64);
        rpc_buffer_append(out, (uint8_t *)tmp, strlen(tmp));
        break;
    case RPC_TYPE_STRING:
        rpc_buffer_append(out, (uint8_t *)"\"", 1);
        rpc_buffer_append(out, (uint8_t *)v->value.v_string, strlen(v->value.v_string));
        rpc_buffer_append(out, (uint8_t *)"\"", 1);
        break;
    case RPC_TYPE_BOOL:
        rpc_buffer_append(out, (uint8_t *)(v->value.v_bool ? "true" : "false"),
                          v->value.v_bool ? 4 : 5);
        break;
    case RPC_TYPE_FLOAT:
        snprintf(tmp, sizeof(tmp), "%g", v->value.v_float);
        rpc_buffer_append(out, (uint8_t *)tmp, strlen(tmp));
        break;
    case RPC_TYPE_ARRAY:
        rpc_buffer_append(out, (uint8_t *)"[", 1);
        for (int32_t i = 0; i < v->value.v_array.len; i++) {
            if (i > 0) rpc_buffer_append(out, (uint8_t *)",", 1);
            RPCValue ev;
            ev.type = v->value.v_array.elem_type;
            if (ev.type == RPC_TYPE_INT32) {
                ev.value.v_int32 = ((int32_t *)v->value.v_array.data)[i];
            } else {
                ev.value.v_int32 = 0;
            }
            rpc_encode_value_json(out, &ev);
        }
        rpc_buffer_append(out, (uint8_t *)"]", 1);
        break;
    case RPC_TYPE_NULL:
        rpc_buffer_append(out, (uint8_t *)"null", 4);
        break;
    default:
        return -1;
    }
    return 0;
}

int rpc_encode_json(const RPCMessage *msg, RPCBuffer *out) {
    rpc_buffer_reset(out);
    rpc_buffer_append(out, (uint8_t *)"{\"method\":\"", 11);
    rpc_buffer_append(out, (uint8_t *)msg->method_name, strlen(msg->method_name));

    char tmp[64];
    snprintf(tmp, sizeof(tmp), "\",\"params\":[");
    rpc_buffer_append(out, (uint8_t *)tmp, strlen(tmp));

    for (int32_t i = 0; i < msg->param_count; i++) {
        if (i > 0) rpc_buffer_append(out, (uint8_t *)",", 1);
        rpc_encode_value_json(out, &msg->params[i]);
    }

    snprintf(tmp, sizeof(tmp), "],\"id\":%d}", msg->id);
    rpc_buffer_append(out, (uint8_t *)tmp, strlen(tmp));
    return 0;
}

static int skip_ws(const uint8_t *data, int32_t *pos, int32_t len) {
    while (*pos < len && (data[*pos] == ' ' || data[*pos] == '\t'
           || data[*pos] == '\n' || data[*pos] == '\r')) {
        (*pos)++;
    }
    return *pos < len ? 0 : -1;
}

static int expect_char(const uint8_t *data, int32_t *pos, int32_t len, char c) {
    skip_ws(data, pos, len);
    if (*pos >= len || data[*pos] != (uint8_t)c) return -1;
    (*pos)++;
    return 0;
}

static int read_json_string(const uint8_t *data, int32_t *pos, int32_t len,
                            char *out, int32_t maxout) {
    if (expect_char(data, pos, len, '"') != 0) return -1;
    int32_t o = 0;
    while (*pos < len && o < maxout - 1) {
        if (data[*pos] == '"') { (*pos)++; out[o] = 0; return 0; }
        if (data[*pos] == '\\') { (*pos)++; }
        out[o++] = (char)data[*pos];
        (*pos)++;
    }
    return -1;
}

static int read_json_number(const uint8_t *data, int32_t *pos, int32_t len,
                            double *num) {
    skip_ws(data, pos, len);
    char tmp[64];
    int32_t o = 0;
    while (*pos < len && o < 63 && (data[*pos] == '-' || data[*pos] == '+'
           || (data[*pos] >= '0' && data[*pos] <= '9')
           || data[*pos] == '.' || data[*pos] == 'e' || data[*pos] == 'E')) {
        tmp[o++] = (char)data[*pos];
        (*pos)++;
    }
    tmp[o] = 0;
    *num = atof(tmp);
    return 0;
}

static int read_json_value(const uint8_t *data, int32_t *pos, int32_t len,
                           RPCValue *v) {
    skip_ws(data, pos, len);
    if (*pos >= len) return -1;

    if (data[*pos] == '"') {
        char strbuf[512];
        if (read_json_string(data, pos, len, strbuf, (int32_t)sizeof(strbuf)) != 0) return -1;
        v->type = RPC_TYPE_STRING;
        v->value.v_string = strdup(strbuf);
        return 0;
    }

    if (data[*pos] == 't' || data[*pos] == 'f') {
        v->type = RPC_TYPE_BOOL;
        if (memcmp(data + *pos, "true", 4) == 0) {
            v->value.v_bool = true;
            *pos += 4;
        } else if (memcmp(data + *pos, "false", 5) == 0) {
            v->value.v_bool = false;
            *pos += 5;
        } else return -1;
        return 0;
    }

    if (data[*pos] == 'n') {
        if (memcmp(data + *pos, "null", 4) == 0) {
            v->type = RPC_TYPE_NULL;
            *pos += 4;
            return 0;
        }
        return -1;
    }

    if (data[*pos] == '[') {
        (*pos)++;
        v->type = RPC_TYPE_ARRAY;
        v->value.v_array.elem_type = RPC_TYPE_INT32;
        v->value.v_array.len = 0;
        v->value.v_array.data = calloc(RPC_MAX_ARRAY_LEN, sizeof(int32_t));
        int32_t *arr = (int32_t *)v->value.v_array.data;

        skip_ws(data, pos, len);
        if (data[*pos] == ']') { (*pos)++; return 0; }

        while (*pos < len && v->value.v_array.len < RPC_MAX_ARRAY_LEN) {
            double num;
            if (read_json_number(data, pos, len, &num) == 0) {
                arr[v->value.v_array.len++] = (int32_t)num;
            }
            skip_ws(data, pos, len);
            if (data[*pos] == ',') { (*pos)++; skip_ws(data, pos, len); }
            else if (data[*pos] == ']') { (*pos)++; break; }
            else { (*pos)++; }
        }
        return 0;
    }

    {
        double num;
        if (read_json_number(data, pos, len, &num) == 0) {
            v->type = RPC_TYPE_INT32;
            v->value.v_int32 = (int32_t)num;
            return 0;
        }
    }

    return -1;
}

int rpc_decode_json(const RPCBuffer *in, RPCMessage *msg) {
    rpc_message_init(msg);
    int32_t pos = 0;
    int32_t len = (int32_t)in->len;
    const uint8_t *data = in->data;

    if (expect_char(data, &pos, len, '{') != 0) return -1;

    while (pos < len) {
        if (skip_ws(data, &pos, len) != 0) break;
        if (data[pos] == '}') { pos++; break; }

        char key[128];
        if (read_json_string(data, &pos, len, key, (int32_t)sizeof(key)) != 0) return -1;
        if (expect_char(data, &pos, len, ':') != 0) return -1;

        if (strcmp(key, "method") == 0) {
            char mname[RPC_MAX_METHOD_NAME];
            if (read_json_string(data, &pos, len, mname,
                                 (int32_t)sizeof(mname)) == 0) {
                strncpy(msg->method_name, mname, RPC_MAX_METHOD_NAME - 1);
            }
        } else if (strcmp(key, "id") == 0) {
            double idnum;
            if (read_json_number(data, &pos, len, &idnum) == 0) msg->id = (int32_t)idnum;
        } else if (strcmp(key, "params") == 0) {
            if (expect_char(data, &pos, len, '[') != 0) return -1;
            skip_ws(data, &pos, len);
            if (data[pos] == ']') { pos++; } else {
                while (pos < len && msg->param_count < RPC_MAX_PARAMS) {
                    if (read_json_value(data, &pos, len,
                                        &msg->params[msg->param_count]) == 0) {
                        msg->param_count++;
                    }
                    skip_ws(data, &pos, len);
                    if (data[pos] == ',') pos++;
                    else if (data[pos] == ']') { pos++; break; }
                }
            }
        } else if (strcmp(key, "error") == 0) {
            char errbuf[256];
            if (read_json_string(data, &pos, len, errbuf, (int32_t)sizeof(errbuf)) == 0) {
                msg->is_error = true;
                strncpy(msg->error_msg, errbuf, 255);
            }
        } else if (strcmp(key, "result") == 0) {
            msg->is_request = false;
            skip_ws(data, &pos, len);
        } else {
            RPCValue dummy;
            read_json_value(data, &pos, len, &dummy);
            rpc_value_free(&dummy);
        }

        skip_ws(data, &pos, len);
        if (pos < len && data[pos] == ',') pos++;
    }
    return 0;
}

static int rpc_encode_param_binary(RPCBuffer *out, const RPCValue *v) {
    uint8_t hdr = (uint8_t)v->type;
    rpc_buffer_append(out, &hdr, 1);

    uint8_t buf[16];
    switch (v->type) {
    case RPC_TYPE_INT32:
        write_i32be(buf, v->value.v_int32);
        rpc_buffer_append(out, buf, 4);
        break;
    case RPC_TYPE_INT64:
        write_i64be(buf, v->value.v_int64);
        rpc_buffer_append(out, buf, 8);
        break;
    case RPC_TYPE_STRING: {
        int32_t slen = (int32_t)strlen(v->value.v_string);
        write_i32be(buf, slen);
        rpc_buffer_append(out, buf, 4);
        rpc_buffer_append(out, (uint8_t *)v->value.v_string, (size_t)slen);
        break;
    }
    case RPC_TYPE_BOOL:
        buf[0] = v->value.v_bool ? 1 : 0;
        rpc_buffer_append(out, buf, 1);
        break;
    case RPC_TYPE_FLOAT: {
        float fv = (float)v->value.v_float;
        int32_t intbits;
        memcpy(&intbits, &fv, 4);
        write_i32be(buf, intbits);
        rpc_buffer_append(out, buf, 4);
        break;
    }
    case RPC_TYPE_ARRAY:
        write_i32be(buf, v->value.v_array.len);
        rpc_buffer_append(out, buf, 4);
        buf[0] = (uint8_t)v->value.v_array.elem_type;
        rpc_buffer_append(out, buf, 1);
        if (v->value.v_array.elem_type == RPC_TYPE_INT32) {
            for (int32_t i = 0; i < v->value.v_array.len; i++) {
                write_i32be(buf, ((int32_t *)v->value.v_array.data)[i]);
                rpc_buffer_append(out, buf, 4);
            }
        }
        break;
    default:
        break;
    }
    return 0;
}

int rpc_encode_binary(const RPCMessage *msg, RPCBuffer *out) {
    rpc_buffer_reset(out);

    uint8_t msg_type = msg->is_request ? (uint8_t)1 : (uint8_t)2;
    if (msg->is_error) msg_type = 3;
    rpc_buffer_append(out, &msg_type, 1);

    uint8_t len_placeholder[4] = {0, 0, 0, 0};
    size_t len_pos = out->len;
    rpc_buffer_append(out, len_placeholder, 4);

    int32_t mhash = rpc_fnv1a_hash(msg->method_name,
                                    (int32_t)strlen(msg->method_name));
    uint8_t hash_buf[4];
    write_i32be(hash_buf, mhash);
    rpc_buffer_append(out, hash_buf, 4);

    uint8_t pc = (uint8_t)msg->param_count;
    rpc_buffer_append(out, &pc, 1);

    for (int32_t i = 0; i < msg->param_count; i++) {
        rpc_encode_param_binary(out, &msg->params[i]);
    }

    size_t total_len = out->len - len_pos - 4;
    uint8_t total_buf[4];
    write_i32be(total_buf, (int32_t)total_len);
    memcpy(out->data + len_pos, total_buf, 4);

    return 0;
}

static int rpc_decode_param_binary(const uint8_t *data, int32_t *pos,
                                   int32_t len, RPCValue *v) {
    if (*pos >= len) return -1;
    v->type = (RPCValueType)data[*pos];
    (*pos)++;

    switch (v->type) {
    case RPC_TYPE_INT32:
        if (*pos + 4 > len) return -1;
        v->value.v_int32 = read_i32be(data + *pos);
        *pos += 4;
        break;
    case RPC_TYPE_INT64:
        if (*pos + 8 > len) return -1;
        v->value.v_int64 = read_i64be(data + *pos);
        *pos += 8;
        break;
    case RPC_TYPE_STRING: {
        if (*pos + 4 > len) return -1;
        int32_t slen = read_i32be(data + *pos);
        *pos += 4;
        if (*pos + slen > len) return -1;
        v->value.v_string = (char *)malloc((size_t)slen + 1);
        memcpy(v->value.v_string, data + *pos, (size_t)slen);
        v->value.v_string[slen] = 0;
        *pos += slen;
        break;
    }
    case RPC_TYPE_BOOL:
        v->value.v_bool = (data[*pos] != 0);
        *pos += 1;
        break;
    case RPC_TYPE_FLOAT: {
        if (*pos + 4 > len) return -1;
        int32_t intbits = read_i32be(data + *pos);
        float fv;
        memcpy(&fv, &intbits, 4);
        v->value.v_float = (double)fv;
        *pos += 4;
        break;
    }
    case RPC_TYPE_ARRAY: {
        if (*pos + 4 > len) return -1;
        int32_t arrlen = read_i32be(data + *pos);
        *pos += 4;
        if (*pos >= len) return -1;
        RPCValueType et = (RPCValueType)data[*pos];
        (*pos)++;
        v->value.v_array.elem_type = et;
        v->value.v_array.len = arrlen;
        if (arrlen > RPC_MAX_ARRAY_LEN) arrlen = RPC_MAX_ARRAY_LEN;
        if (et == RPC_TYPE_INT32) {
            v->value.v_array.data = calloc((size_t)RPC_MAX_ARRAY_LEN, sizeof(int32_t));
            int32_t *arr = (int32_t *)v->value.v_array.data;
            for (int32_t i = 0; i < arrlen && *pos + 4 <= len; i++) {
                arr[i] = read_i32be(data + *pos);
                *pos += 4;
            }
        }
        break;
    }
    default:
        break;
    }
    return 0;
}

int rpc_decode_binary(const RPCBuffer *in, RPCMessage *msg) {
    rpc_message_init(msg);
    int32_t pos = 0;
    int32_t len = (int32_t)in->len;
    const uint8_t *data = in->data;

    if (len < 10) return -1;

    uint8_t mtype = data[pos++];
    msg->is_request = (mtype == 1);
    msg->is_error = (mtype == 3);

    pos += 4; /* skip msg_len */
    pos += 4; /* skip method_hash */

    uint8_t pcount = data[pos++];
    msg->param_count = (int32_t)pcount;
    if (msg->param_count > RPC_MAX_PARAMS) msg->param_count = RPC_MAX_PARAMS;

    for (int32_t i = 0; i < msg->param_count; i++) {
        if (rpc_decode_param_binary(data, &pos, len, &msg->params[i]) != 0) break;
    }

    return 0;
}

int rpc_encode_message(RPCCodec codec, const RPCMessage *msg, RPCBuffer *out) {
    switch (codec) {
    case RPC_CODEC_JSON:   return rpc_encode_json(msg, out);
    case RPC_CODEC_BINARY: return rpc_encode_binary(msg, out);
    case RPC_CODEC_MSGPACK:
    default: return -1;
    }
}

int rpc_decode_message(RPCCodec codec, RPCBuffer *in, RPCMessage *msg) {
    switch (codec) {
    case RPC_CODEC_JSON:   return rpc_decode_json(in, msg);
    case RPC_CODEC_BINARY: return rpc_decode_binary(in, msg);
    case RPC_CODEC_MSGPACK:
    default: return -1;
    }
}
