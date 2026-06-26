#include "rpc_encoding.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void) {
    printf("=== RPC JSON Encode/Decode Demo ===\n\n");

    RPCMessage req;
    rpc_message_init(&req);
    req.id = 1;
    strncpy(req.method_name, "Calculator.add", RPC_MAX_METHOD_NAME - 1);
    req.param_count = 2;
    req.is_request = true;
    req.return_type = RPC_TYPE_INT32;

    req.params[0].type = RPC_TYPE_INT32;
    req.params[0].value.v_int32 = 10;

    req.params[1].type = RPC_TYPE_INT32;
    req.params[1].value.v_int32 = 20;

    RPCBuffer buf;
    rpc_buffer_init(&buf);
    printf("[1] Encoding Calculator.add(10, 20) as JSON...\n");

    if (rpc_encode_json(&req, &buf) == 0) {
        printf("    Encoded (%zu bytes): %s\n\n", buf.len, buf.data);
    } else {
        printf("    Encode failed!\n");
        rpc_buffer_free(&buf);
        return 1;
    }

    printf("[2] Decoding JSON response...\n");
    RPCMessage decoded;
    rpc_message_init(&decoded);

    if (rpc_decode_json(&buf, &decoded) == 0) {
        printf("    Decoded: id=%d method=%s params=%d\n",
               decoded.id, decoded.method_name, decoded.param_count);
        for (int32_t i = 0; i < decoded.param_count; i++) {
            if (decoded.params[i].type == RPC_TYPE_INT32) {
                printf("      param[%d] = %d (INT32)\n",
                       i, decoded.params[i].value.v_int32);
            }
        }
    } else {
        printf("    Decode failed!\n");
    }

    printf("\n[3] Round-trip test: encode → decode → encode\n");
    RPCBuffer buf2;
    rpc_buffer_init(&buf2);

    if (rpc_encode_json(&decoded, &buf2) == 0) {
        printf("    Re-encoded (%zu bytes): %s\n", buf2.len, buf2.data);
    }

    printf("\n[4] Testing decode error response...\n");
    RPCMessage err_resp;
    rpc_message_init(&err_resp);
    err_resp.id = 1;
    err_resp.is_request = false;
    err_resp.is_error = true;
    strncpy(err_resp.error_msg, "Division by zero", 255);

    RPCBuffer buf3;
    rpc_buffer_init(&buf3);

    RPCBuffer json_err;
    rpc_buffer_init(&json_err);
    rpc_buffer_append(&json_err,
        (uint8_t *)"{\"id\":1,\"error\":\"Division by zero\"}", 40);

    RPCMessage dec_err;
    rpc_message_init(&dec_err);
    if (rpc_decode_json(&json_err, &dec_err) == 0) {
        printf("    Decoded error: is_error=%d msg=\"%s\"\n",
               dec_err.is_error, dec_err.error_msg);
    }

    printf("\n[5] Testing binary encode/decode...\n");
    RPCBuffer binbuf;
    rpc_buffer_init(&binbuf);
    if (rpc_encode_binary(&req, &binbuf) == 0) {
        printf("    Binary encoded: %zu bytes\n", binbuf.len);
        for (size_t i = 0; i < binbuf.len && i < 32; i++) {
            printf("    %02x ", binbuf.data[i]);
        }
        printf("\n");

        RPCMessage dec_bin;
        rpc_message_init(&dec_bin);
        if (rpc_decode_binary(&binbuf, &dec_bin) == 0) {
            printf("    Binary decoded: params=%d\n", dec_bin.param_count);
            for (int32_t i = 0; i < dec_bin.param_count; i++) {
                if (dec_bin.params[i].type == RPC_TYPE_INT32) {
                    printf("      param[%d] = %d\n",
                           i, dec_bin.params[i].value.v_int32);
                }
            }
        }
        rpc_buffer_free(&binbuf);
    }

    printf("\n[6] Testing RPCValue types...\n");
    RPCMessage multi_req;
    rpc_message_init(&multi_req);
    multi_req.id = 2;
    strncpy(multi_req.method_name, "MultiType.test", RPC_MAX_METHOD_NAME - 1);
    multi_req.param_count = 4;

    multi_req.params[0].type = RPC_TYPE_BOOL;
    multi_req.params[0].value.v_bool = true;

    multi_req.params[1].type = RPC_TYPE_FLOAT;
    multi_req.params[1].value.v_float = 3.14159;

    multi_req.params[2].type = RPC_TYPE_STRING;
    multi_req.params[2].value.v_string = strdup("hello world");

    multi_req.params[3].type = RPC_TYPE_INT64;
    multi_req.params[3].value.v_int64 = 9223372036854775807LL;

    RPCBuffer multi_buf;
    rpc_buffer_init(&multi_buf);
    rpc_encode_json(&multi_req, &multi_buf);
    printf("    Multi-type JSON: %s\n", multi_buf.data);

    rpc_message_free(&decoded);
    rpc_message_free(&err_resp);
    rpc_message_free(&dec_err);
    rpc_message_free(&multi_req);
    rpc_buffer_free(&buf);
    rpc_buffer_free(&buf2);
    rpc_buffer_free(&buf3);
    rpc_buffer_free(&json_err);
    rpc_buffer_free(&multi_buf);

    printf("\n=== Demo Complete ===\n");
    return 0;
}
