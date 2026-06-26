#include "rpc_protocol.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

/*
 * rpc_protocol_demo.c - Protocol Features Demonstration
 *
 * Demonstrates:
 * - CRC32 computation and error detection (L4: Shannon's Theorem)
 * - Protocol framing with magic bytes + CRC32 trailer (L3)
 * - Version negotiation (L5)
 * - Streaming protocol with back-pressure (L8)
 * - PING/PONG keepalive
 */

int main(void) {
    printf("=== RPC Protocol Features Demo ===\n\n");

    /* ── CRC32 Demonstration ──────────────────────────────────── */
    printf("[1] CRC32 Error Detection (IEEE 802.3)\n");
    rpc_crc32_init();

    const char *message = "Critical RPC transaction: transfer(100, USD)";
    size_t msg_len = strlen(message);
    uint32_t crc = rpc_crc32_compute((const uint8_t *)message, msg_len);
    printf("    Message: \"%s\"\n", message);
    printf("    CRC32:   0x%08X\n\n", crc);

    /* Demonstrate error detection: flip one bit */
    char corrupted[256];
    strncpy(corrupted, message, sizeof(corrupted) - 1);
    corrupted[5] ^= 0x01;  /* Flip bit 0 of byte 5 */
    uint32_t crc_corrupt = rpc_crc32_compute(
        (const uint8_t *)corrupted, strlen(corrupted));
    printf("    Corrupted: \"%s\"\n", corrupted);
    printf("    CRC32:    0x%08X\n", crc_corrupt);
    printf("    CRC changed? %s (should differ for error detection)\n\n",
           crc != crc_corrupt ? "YES" : "NO");

    /* Shannon's bound */
    double bound = rpc_proto_error_bound(msg_len * 8);
    printf("    Shannon bound P(undetected) <= %.2e\n\n", bound);

    /* ── Protocol Framing ──────────────────────────────────────── */
    printf("[2] Protocol Frame Build & Parse\n");

    RPCProtoFrame frame;
    memset(&frame, 0, sizeof(frame));
    frame.version    = (uint32_t)RPC_PROTO_VER_1_0;
    frame.frame_type = RPC_FRAME_UNARY;
    frame.stream_id  = 0;
    frame.sequence   = 0;

    RPCBuffer payload;
    rpc_buffer_init(&payload);
    rpc_buffer_append(&payload, (const uint8_t *)message, msg_len);

    RPCBuffer framed;
    rpc_buffer_init(&framed);

    int ret = rpc_proto_frame_build(&frame, &payload, &framed);
    printf("    Frame built: %zu bytes (overhead=%d, payload=%zu)\n",
           framed.len, RPC_PROTO_OVERHEAD, msg_len);

    /* Show hex dump of frame */
    printf("    Frame hex (first 40 bytes): ");
    for (size_t i = 0; i < framed.len && i < 40; i++) {
        printf("%02x ", framed.data[i]);
    }
    printf("\n\n");

    /* Parse it back */
    RPCProtoFrame parsed;
    RPCBuffer parsed_payload;
    rpc_buffer_init(&parsed_payload);
    memset(&parsed, 0, sizeof(parsed));

    ret = rpc_proto_frame_parse(&framed, &parsed, &parsed_payload);
    printf("    Parse result: %d (0=success, -1=bad_magic, -4=crc_error)\n", ret);
    printf("    Parsed magic: 0x%08X\n", parsed.magic);
    printf("    Parsed payload: \"%.*s\"\n\n",
           (int)parsed_payload.len, parsed_payload.data);

    /* ── CRC Error Injection ────────────────────────────────────── */
    printf("[3] CRC Error Detection (deliberate corruption)\n");
    /* Corrupt a byte in the middle of the frame */
    size_t corrupt_pos = framed.len / 2;
    uint8_t orig_byte = framed.data[corrupt_pos];
    framed.data[corrupt_pos] ^= 0xFF;  /* Flip all bits of one byte */

    RPCProtoFrame bad_frame;
    RPCBuffer bad_payload;
    rpc_buffer_init(&bad_payload);
    memset(&bad_frame, 0, sizeof(bad_frame));

    int corrupt_ret = rpc_proto_frame_parse(&framed, &bad_frame, &bad_payload);
    printf("    After corrupting byte %zu (0x%02X -> 0x%02X):\n",
           corrupt_pos, orig_byte, framed.data[corrupt_pos]);
    printf("    Parse result: %d (expected -4 = CRC_ERROR)\n\n", corrupt_ret);

    /* Restore for later use */
    framed.data[corrupt_pos] = orig_byte;

    /* ── Version Negotiation ────────────────────────────────────── */
    printf("[4] Version Negotiation\n");
    RPCProtoVersion v1 = RPC_PROTO_VER_1_0;
    RPCProtoVersion v2 = RPC_PROTO_VER_2_0;

    printf("    Local=v1, Remote=v2 -> negotiated=%s\n",
           rpc_proto_version_negotiate(v1, v2) == v1 ? "v1" : "v2");
    printf("    Local=v2, Remote=v1 -> negotiated=%s\n",
           rpc_proto_version_negotiate(v2, v1) == v1 ? "v1" : "v2");
    printf("    Local=ANY, Remote=v2 -> negotiated=%s\n\n",
           rpc_proto_version_negotiate(RPC_PROTO_VER_ANY, v2) == v2 ? "v2" : "?");

    /* ── Streaming Protocol ─────────────────────────────────────── */
    printf("[5] Streaming Protocol Demo\n");
    RPCProtocolSession sess;
    rpc_proto_session_init(&sess);

    int sid = rpc_proto_stream_open(&sess);
    printf("    Stream opened: id=%d\n", sid);

    /* Send 3 chunks */
    const char *chunks[] = {
        "Chunk-1: Header data for stream processing",
        "Chunk-2: Body data with the main payload content",
        "Chunk-3: Footer data completing the stream"
    };

    for (int i = 0; i < 3; i++) {
        RPCBuffer chunk;
        rpc_buffer_init(&chunk);
        rpc_buffer_append(&chunk, (const uint8_t *)chunks[i],
                          strlen(chunks[i]));

        RPCBuffer stream_frame;
        rpc_buffer_init(&stream_frame);

        ret = rpc_proto_stream_send(&sess, (uint32_t)sid,
                                     &chunk, &stream_frame);
        printf("    Sent chunk %d: %zu bytes -> %zu framed (seq=%d)\n",
               i + 1, chunk.len, stream_frame.len, i);

        /* Simulate receiving side */
        RPCBuffer recv_chunk;
        rpc_buffer_init(&recv_chunk);
        RPCProtoFrame recv_frame;
        memset(&recv_frame, 0, sizeof(recv_frame));

        int recv_ret = rpc_proto_stream_recv(&sess, &stream_frame,
                                              &recv_chunk, &recv_frame);
        printf("      Received: %d bytes, seq=%d, ret=%d\n",
               (int)recv_chunk.len, recv_frame.sequence, recv_ret);

        /* Check back-pressure */
        bool bp = rpc_proto_stream_is_backpressured(
            &sess, (uint32_t)sid);
        printf("      Back-pressure: %s\n\n", bp ? "ACTIVE" : "inactive");

        rpc_buffer_free(&chunk);
        rpc_buffer_free(&stream_frame);
        rpc_buffer_free(&recv_chunk);
    }

    /* Close stream */
    RPCBuffer close_frame;
    rpc_buffer_init(&close_frame);
    ret = rpc_proto_stream_close(&sess, (uint32_t)sid, &close_frame);
    printf("    Stream closed: ret=%d\n\n", ret);

    /* ── Session Statistics ─────────────────────────────────────── */
    uint64_t sent, recv, crc_errs;
    rpc_proto_session_stats(&sess, &sent, &recv, &crc_errs);
    printf("[6] Session Statistics\n");
    printf("    Frames sent:     %llu\n", (unsigned long long)sent);
    printf("    Frames received: %llu\n", (unsigned long long)recv);
    printf("    CRC errors:      %llu\n", (unsigned long long)crc_errs);
    printf("    Error rate:      %.2e (expected < 2.33e-10)\n\n",
           sent > 0 ? (double)crc_errs / (double)sent : 0.0);

    /* ── PING/PONG ──────────────────────────────────────────────── */
    printf("[7] Keepalive PING/PONG\n");
    RPCBuffer ping_buf, pong_buf;
    rpc_buffer_init(&ping_buf);
    rpc_buffer_init(&pong_buf);

    rpc_proto_build_ping(0, &ping_buf);
    rpc_proto_build_pong(0, &pong_buf);

    RPCProtoFrame pf;
    RPCBuffer dummy;
    rpc_buffer_init(&dummy);
    memset(&pf, 0, sizeof(pf));

    rpc_proto_frame_parse(&ping_buf, &pf, &dummy);
    printf("    PING frame: type=%d, is_ping=%s\n",
           pf.frame_type, rpc_proto_is_ping(&pf) ? "yes" : "no");

    rpc_proto_frame_parse(&pong_buf, &pf, &dummy);
    printf("    PONG frame: type=%d, is_pong=%s\n",
           pf.frame_type, rpc_proto_is_pong(&pf) ? "yes" : "no");

    /* ── Cleanup ────────────────────────────────────────────────── */
    rpc_proto_session_free(&sess);
    rpc_buffer_free(&payload);
    rpc_buffer_free(&framed);
    rpc_buffer_free(&parsed_payload);
    rpc_buffer_free(&bad_payload);
    rpc_buffer_free(&close_frame);
    rpc_buffer_free(&ping_buf);
    rpc_buffer_free(&pong_buf);
    rpc_buffer_free(&dummy);

    printf("\n=== Protocol Demo Complete ===\n");
    return 0;
}
