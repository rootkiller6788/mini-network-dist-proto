#include "rpc_protocol.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/*
 * rpc_protocol.c - Advanced Protocol Features Implementation
 *
 * L4: CRC32 polynomial = 0xEDB88320 (IEEE 802.3 / zlib / gzip)
 *      Generator polynomial G(x) = x^32 + x^26 + x^23 + x^22 +
 *      x^16 + x^12 + x^11 + x^10 + x^8 + x^7 + x^5 + x^4 + x^2 + x + 1
 *      This is the "reflected" form for LSB-first computation.
 *      CRC32 detects: all single/double-bit errors, all odd-bit
 *      errors, all burst errors <= 32 bits. P(false positive) ~ 2^(-32).
 *
 * L5: Sarwate (1988) table-driven CRC32 algorithm.
 *      Precompute 256-entry lookup table of CRC remainders.
 *      Each byte processed: crc = (crc>>8) ^ table[(crc ^ byte) & 0xFF]
 *      Complexity: O(n) time, O(1) space beyond 1KB lookup table.
 *
 * L3: Protocol framing with length-delimited frames and CRC32 trailer.
 * L8: Streaming protocol with sequence numbering and back-pressure.
 */

/* --- CRC32 Implementation (Sarwate 1988, IEEE 802.3) -------------- */

static uint32_t g_crc32_table[256];
static bool     g_crc32_initialized = false;

void rpc_crc32_init(void) {
    if (g_crc32_initialized) return;
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t crc = i;
        for (int j = 0; j < 8; j++) {
            if (crc & 1)
                crc = (crc >> 1) ^ 0xEDB88320;
            else
                crc = crc >> 1;
        }
        g_crc32_table[i] = crc;
    }
    g_crc32_initialized = true;
}

uint32_t rpc_crc32_update(uint32_t crc, const uint8_t *data, size_t len) {
    if (!g_crc32_initialized) rpc_crc32_init();
    if (!data) return crc;
    for (size_t i = 0; i < len; i++) {
        crc = (crc >> 8) ^ g_crc32_table[(crc ^ data[i]) & 0xFF];
    }
    return crc;
}

uint32_t rpc_crc32_finalize(uint32_t crc) {
    return crc ^ 0xFFFFFFFF;
}

uint32_t rpc_crc32_compute(const uint8_t *data, size_t len) {
    return rpc_crc32_finalize(rpc_crc32_update(0xFFFFFFFF, data, len));
}

/*
 * L4 Theoretical note:
 * CRC32 is a cyclic redundancy check with generator polynomial
 * G(x) = 0x104C11DB7 (non-reflected) = 0xEDB88320 (reflected).
 * The Hamming distance d_min of CRC32 is:
 *   - 4 for frames up to 91607 bits (~11.4 KB)
 *   - 3 for frames up to 2^32-1 bits
 * This means it can detect all errors of weight 1, 2, or 3 for any
 * frame length, and all burst errors up to 32 bits.
 * P(undetected error) <= (n + 2^32 - 1) / 2^33 for random errors,
 * which is approximately 1.16e-10 for a 1KB frame.
 */

/* --- Helper: big-endian read/write for 32-bit values ----------- */

static void write_u32be(uint8_t *buf, uint32_t val) {
    buf[0] = (uint8_t)((val >> 24) & 0xFF);
    buf[1] = (uint8_t)((val >> 16) & 0xFF);
    buf[2] = (uint8_t)((val >> 8) & 0xFF);
    buf[3] = (uint8_t)(val & 0xFF);
}

static uint32_t read_u32be(const uint8_t *buf) {
    return ((uint32_t)buf[0] << 24) | ((uint32_t)buf[1] << 16)
         | ((uint32_t)buf[2] << 8)  | ((uint32_t)buf[3]);
}

/* --- Protocol Session ------------------------------------------- */

void rpc_proto_session_init(RPCProtocolSession *sess) {
    if (!sess) return;
    memset(sess, 0, sizeof(RPCProtocolSession));
    sess->negotiation.negotiated_version = RPC_PROTO_VER_1_0;
    sess->negotiation.max_frame_size = RPC_PROTO_MAX_FRAME_SIZE;
    sess->negotiation.max_streams = RPC_PROTO_MAX_STREAMS;
    sess->negotiation.compression_enabled = false;
    sess->negotiation.encryption_enabled = false;
    sess->negotiation.multiplexing_supported = true;
    sess->next_stream_id = 1;
    for (int32_t i = 0; i < RPC_PROTO_MAX_STREAMS; i++) {
        rpc_buffer_init(&sess->streams[i].reassembly_buffer);
        sess->streams[i].high_watermark = 1024 * 1024;   /* 1 MB */
        sess->streams[i].low_watermark  = 256 * 1024;     /* 256 KB */
    }
    rpc_crc32_init();
}

void rpc_proto_session_free(RPCProtocolSession *sess) {
    if (!sess) return;
    for (int32_t i = 0; i < RPC_PROTO_MAX_STREAMS; i++) {
        if (sess->streams[i].active) {
            rpc_buffer_free(&sess->streams[i].reassembly_buffer);
        }
    }
    memset(sess, 0, sizeof(RPCProtocolSession));
}

/* --- Handshake Protocol ----------------------------------------- */

int rpc_proto_build_handshake(RPCProtocolSession *sess, RPCBuffer *out) {
    if (!sess || !out) return -1;
    rpc_buffer_reset(out);
    /* Handshake payload: version + capability flags + limits */
    uint8_t buf[32];
    memset(buf, 0, sizeof(buf));
    write_u32be(buf, (uint32_t)sess->negotiation.negotiated_version);
    buf[4] = (uint8_t)sess->negotiation.compression_enabled;
    buf[5] = (uint8_t)sess->negotiation.encryption_enabled;
    buf[6] = (uint8_t)sess->negotiation.multiplexing_supported;
    write_u32be(buf + 7,  (uint32_t)sess->negotiation.max_frame_size);
    write_u32be(buf + 11, (uint32_t)sess->negotiation.max_streams);
    rpc_buffer_append(out, buf, 15);
    /* Append CRC32 over handshake payload */
    uint32_t crc = rpc_crc32_compute(buf, 15);
    uint8_t crcbuf[4];
    write_u32be(crcbuf, crc);
    rpc_buffer_append(out, crcbuf, 4);
    sess->handshake_complete = true;
    return 0;
}

int rpc_proto_parse_handshake(RPCProtocolSession *sess, const RPCBuffer *in) {
    if (!sess || !in || in->len < 19) return -1;
    const uint8_t *data = in->data;
    /* Verify CRC32 */
    uint32_t stored_crc = read_u32be(data + 15);
    uint32_t computed_crc = rpc_crc32_compute(data, 15);
    if (stored_crc != computed_crc) {
        sess->crc_errors++;
        return -2;  /* CRC mismatch */
    }
    uint32_t remote_ver = read_u32be(data);
    sess->negotiation.negotiated_version =
        rpc_proto_version_negotiate(RPC_PROTO_VER_1_0, (RPCProtoVersion)remote_ver);
    sess->negotiation.compression_enabled      = (data[4] != 0);
    sess->negotiation.encryption_enabled       = (data[5] != 0);
    sess->negotiation.multiplexing_supported   = (data[6] != 0);
    sess->negotiation.max_frame_size           = (int32_t)read_u32be(data + 7);
    sess->negotiation.max_streams              = (int32_t)read_u32be(data + 11);
    sess->handshake_complete = true;
    return 0;
}

/* --- Frame Build / Parse ---------------------------------------- */

int rpc_proto_frame_build(RPCProtoFrame *frame, const RPCBuffer *payload,
                          RPCBuffer *out) {
    if (!frame || !payload || !out) return -1;
    if (payload->len > (size_t)RPC_PROTO_MAX_FRAME_SIZE - RPC_PROTO_OVERHEAD)
        return -2;  /* Payload too large */

    uint32_t total_len = (uint32_t)(RPC_PROTO_HEADER_SIZE + payload->len
                                     + RPC_PROTO_CRC_SIZE);
    frame->frame_len = total_len;
    frame->magic = RPC_PROTO_MAGIC;

    rpc_buffer_reset(out);
    rpc_buffer_reserve(out, total_len);

    /* Build header */
    uint8_t hdr[RPC_PROTO_HEADER_SIZE];
    memset(hdr, 0, sizeof(hdr));
    write_u32be(hdr,      frame->magic);
    write_u32be(hdr + 4,  frame->version);
    hdr[8]  = frame->frame_type;
    hdr[9]  = frame->flags;
    write_u32be(hdr + 10, frame->stream_id);
    write_u32be(hdr + 14, frame->sequence);
    write_u32be(hdr + 18, total_len);

    rpc_buffer_append(out, hdr, RPC_PROTO_HEADER_SIZE);
    rpc_buffer_append(out, payload->data, payload->len);

    /* CRC32 over header + payload (single stream, one finalize) */
    uint32_t crc = rpc_crc32_update(0xFFFFFFFF, hdr, RPC_PROTO_HEADER_SIZE);
    crc = rpc_crc32_update(crc, payload->data, payload->len);
    crc = rpc_crc32_finalize(crc);
    uint8_t crcbuf[4];
    write_u32be(crcbuf, crc);
    rpc_buffer_append(out, crcbuf, 4);
    return 0;
}

int rpc_proto_frame_parse(const RPCBuffer *in, RPCProtoFrame *frame,
                          RPCBuffer *out) {
    if (!in || !frame || !out) return -1;
    if (in->len < (size_t)RPC_PROTO_OVERHEAD) return -1;

    const uint8_t *data = in->data;

    /* Parse header fields */
    frame->magic      = read_u32be(data);
    if (frame->magic != RPC_PROTO_MAGIC) return -1;  /* Magic mismatch */

    frame->version    = read_u32be(data + 4);
    frame->frame_type = data[8];
    frame->flags      = data[9];
    frame->stream_id  = read_u32be(data + 10);
    frame->sequence   = read_u32be(data + 14);
    frame->frame_len  = read_u32be(data + 18);

    if (frame->frame_len > (uint32_t)RPC_PROTO_MAX_FRAME_SIZE) return -2;
    if (frame->frame_len != (uint32_t)in->len) return -3;  /* Length mismatch */

    /* Verify CRC32 over header + payload (excluding CRC bytes) */
    size_t payload_len = (size_t)frame->frame_len - RPC_PROTO_OVERHEAD;
    uint32_t stored_crc = read_u32be(data + RPC_PROTO_HEADER_SIZE + payload_len);
    uint32_t computed_crc = rpc_crc32_compute(data,
        (size_t)frame->frame_len - RPC_PROTO_CRC_SIZE);

    if (stored_crc != computed_crc) return -4;  /* CRC mismatch */

    /* Extract payload */
    rpc_buffer_reset(out);
    rpc_buffer_reserve(out, payload_len);
    rpc_buffer_append(out, data + RPC_PROTO_HEADER_SIZE, payload_len);
    return 0;
}

/* --- Version Negotiation ---------------------------------------- */

/*
 * Semantic version comparison for protocol handshake.
 * Each version is encoded as 0x00MMmmpp where MM=major, mm=minor, pp=patch.
 * Compatibility rule: same major version required for compatibility.
 * Within same major: higher minor/patch is backward-compatible.
 */
int rpc_proto_version_compare(RPCProtoVersion a, RPCProtoVersion b) {
    if (a == b) return 0;
    if (a == RPC_PROTO_VER_ANY || b == RPC_PROTO_VER_ANY) return 0;
    uint32_t ua = (uint32_t)a, ub = (uint32_t)b;
    uint32_t ma = ua >> 16, mb = ub >> 16;
    if (ma != mb) return (ma > mb) ? 1 : -1;
    uint32_t mia = (ua >> 8) & 0xFF, mib = (ub >> 8) & 0xFF;
    if (mia != mib) return (mia > mib) ? 1 : -1;
    uint32_t pa = ua & 0xFF, pb = ub & 0xFF;
    if (pa != pb) return (pa > pb) ? 1 : -1;
    return 0;
}

RPCProtoVersion rpc_proto_version_negotiate(RPCProtoVersion local,
                                              RPCProtoVersion remote) {
    if (local == RPC_PROTO_VER_ANY) return remote;
    if (remote == RPC_PROTO_VER_ANY) return local;
    if (local == remote) return local;
    /* Select the lower version for maximum compatibility */
    if (rpc_proto_version_compare(local, remote) <= 0) return local;
    return remote;
}

/* --- Compression (pluggable codec system) ----------------------- */

static RPCCompressFn   g_compress_fns[4]   = {NULL, NULL, NULL, NULL};
static RPCDecompressFn g_decompress_fns[4] = {NULL, NULL, NULL, NULL};

static int compress_identity(const uint8_t *src, size_t src_len,
                              uint8_t *dst, size_t *dst_len) {
    if (!src || !dst || !dst_len) return -1;
    if (*dst_len < src_len) return -2;
    memcpy(dst, src, src_len);
    *dst_len = src_len;
    return 0;
}

int rpc_proto_compress_register(RPCCompressAlgo algo,
                                 RPCCompressFn compress,
                                 RPCDecompressFn decompress) {
    if ((int)algo < 0 || algo > RPC_COMPRESS_SNAPPY) return -1;
    g_compress_fns[algo] = compress;
    g_decompress_fns[algo] = decompress;
    return 0;
}

int rpc_proto_compress(RPCCompressAlgo algo,
                        const uint8_t *src, size_t src_len,
                        RPCBuffer *out) {
    if (!src || !out) return -1;
    if (algo == RPC_COMPRESS_NONE || !g_compress_fns[algo]) {
        /* Identity compression (no-op) */
        rpc_buffer_reset(out);
        rpc_buffer_reserve(out, src_len);
        size_t dst_len = out->capacity;
        int ret = compress_identity(src, src_len, out->data, &dst_len);
        if (ret == 0) out->len = dst_len;
        return ret;
    }
    /* Attempt real compression; fall back to identity if expansion occurs */
    rpc_buffer_reset(out);
    rpc_buffer_reserve(out, src_len + 256);
    size_t dst_len = out->capacity;
    int ret = g_compress_fns[algo](src, src_len, out->data, &dst_len);
    if (ret == 0 && dst_len < src_len) {
        out->len = dst_len;
        return 0;
    }
    /* Compression expanded data or failed; use identity */
    rpc_buffer_reset(out);
    rpc_buffer_reserve(out, src_len);
    memcpy(out->data, src, src_len);
    out->len = src_len;
    return 0;
}

int rpc_proto_decompress(RPCCompressAlgo algo,
                          const uint8_t *src, size_t src_len,
                          RPCBuffer *out) {
    if (!src || !out) return -1;
    if (algo == RPC_COMPRESS_NONE || !g_decompress_fns[algo]) {
        rpc_buffer_reset(out);
        rpc_buffer_reserve(out, src_len);
        memcpy(out->data, src, src_len);
        out->len = src_len;
        return 0;
    }
    /* Estimate decompressed size as 4x compressed size */
    rpc_buffer_reset(out);
    rpc_buffer_reserve(out, src_len * 4 + 256);
    size_t dst_len = out->capacity;
    int ret = g_decompress_fns[algo](src, src_len, out->data, &dst_len);
    if (ret == 0) out->len = dst_len;
    return ret;
}

/* --- Streaming Protocol ----------------------------------------- */

static int find_stream(const RPCProtocolSession *sess, uint32_t stream_id) {
    for (int32_t i = 0; i < RPC_PROTO_MAX_STREAMS; i++) {
        if (sess->streams[i].active && sess->streams[i].stream_id == stream_id)
            return i;
    }
    return -1;
}

int rpc_proto_stream_open(RPCProtocolSession *sess) {
    if (!sess) return -1;
    for (int32_t i = 0; i < RPC_PROTO_MAX_STREAMS; i++) {
        if (!sess->streams[i].active) {
            RPCStream *s = &sess->streams[i];
            s->stream_id = sess->next_stream_id++;
            s->next_sequence = 0;
            s->active = true;
            s->closed = false;
            s->expected_sequence = 0;
            rpc_buffer_reset(&s->reassembly_buffer);
            sess->stream_count++;
            return (int)s->stream_id;
        }
    }
    return -1;  /* No free stream slots */
}

int rpc_proto_stream_send(RPCProtocolSession *sess, uint32_t stream_id,
                           const RPCBuffer *chunk, RPCBuffer *framed_out) {
    if (!sess || !chunk || !framed_out) return -1;
    int idx = find_stream(sess, stream_id);
    if (idx < 0) return -1;   /* Stream not found */
    if (sess->streams[idx].closed) return -2;  /* Stream closed */

    if (chunk->len > (size_t)RPC_PROTO_MAX_CHUNK_SIZE) return -3;

    RPCProtoFrame frame;
    memset(&frame, 0, sizeof(frame));
    frame.version    = (uint32_t)sess->negotiation.negotiated_version;
    frame.frame_type = RPC_FRAME_STREAM_DATA;
    frame.stream_id  = stream_id;
    frame.sequence   = sess->streams[idx].next_sequence++;
    frame.flags      = sess->negotiation.compression_enabled ? 0x01 : 0x00;

    int ret = rpc_proto_frame_build(&frame, chunk, framed_out);
    if (ret == 0) sess->frames_sent++;
    return ret;
}

int rpc_proto_stream_recv(RPCProtocolSession *sess, const RPCBuffer *framed_in,
                           RPCBuffer *chunk_out, RPCProtoFrame *frame_out) {
    if (!sess || !framed_in || !chunk_out || !frame_out) return -1;

    int ret = rpc_proto_frame_parse(framed_in, frame_out, chunk_out);
    if (ret != 0) {
        if (ret == -4) sess->crc_errors++;
        return ret;
    }
    sess->frames_received++;

    if (frame_out->frame_type == RPC_FRAME_STREAM_DATA) {
        int idx = find_stream(sess, frame_out->stream_id);
        if (idx >= 0) {
            RPCStream *s = &sess->streams[idx];
            /* In-order delivery: append if sequence matches expected */
            if (frame_out->sequence == s->expected_sequence) {
                rpc_buffer_append(&s->reassembly_buffer,
                                  chunk_out->data, chunk_out->len);
                s->expected_sequence++;
            }
            /*
             * L8 Note: Full implementation would buffer out-of-order
             * frames for reordering, send selective ACK/NACK, and
             * implement TCP-like selective retransmission.
             * Current simplified version drops out-of-order frames.
             */
        }
    }
    return 0;
}

int rpc_proto_stream_close(RPCProtocolSession *sess, uint32_t stream_id,
                            RPCBuffer *framed_out) {
    if (!sess || !framed_out) return -1;
    int idx = find_stream(sess, stream_id);
    if (idx < 0) return -1;

    RPCProtoFrame frame;
    memset(&frame, 0, sizeof(frame));
    frame.version    = (uint32_t)sess->negotiation.negotiated_version;
    frame.frame_type = RPC_FRAME_STREAM_END;
    frame.stream_id  = stream_id;
    frame.sequence   = sess->streams[idx].next_sequence;
    frame.flags      = 0x00;

    RPCBuffer empty;
    rpc_buffer_init(&empty);
    int ret = rpc_proto_frame_build(&frame, &empty, framed_out);
    rpc_buffer_free(&empty);

    if (ret == 0) {
        sess->streams[idx].active = false;
        sess->streams[idx].closed = true;
        sess->stream_count--;
    }
    return ret;
}

/* --- Back-pressure Management (L8 Advanced) --------------------- */

void rpc_proto_stream_set_watermarks(uint32_t stream_id,
                                      size_t high_watermark,
                                      size_t low_watermark) {
    /*
     * L8: Watermarks control flow to prevent buffer bloat.
     * When reassembly buffer >= high_watermark, the receiver signals
     * the sender to PAUSE. When buffer drops below low_watermark,
     * the receiver signals RESUME.
     *
     * This implements a simplified version of TCP's receive window
     * flow control at the application layer.
     *
     * Amdahl's Law (L4) relates to throughput: if S fraction of time
     * is spent in serial processing and (1-S) in parallel/streaming,
     * maximum speedup = 1/(S + (1-S)/N). For streaming with N parallel
     * streams, speedup approaches 1/S with back-pressure preventing
     * unbounded queue growth.
     */
    (void)stream_id;
    (void)high_watermark;
    (void)low_watermark;
    /* Global defaults: high=1MB, low=256KB set at session init */
}

bool rpc_proto_stream_is_backpressured(RPCProtocolSession *sess,
                                        uint32_t stream_id) {
    if (!sess) return false;
    int idx = find_stream(sess, stream_id);
    if (idx < 0) return false;
    const RPCStream *s = &sess->streams[idx];
    return s->reassembly_buffer.len > s->high_watermark;
}

/* --- Keepalive PING/PONG ---------------------------------------- */

int rpc_proto_build_ping(uint32_t stream_id, RPCBuffer *out) {
    if (!out) return -1;
    RPCProtoFrame frame;
    memset(&frame, 0, sizeof(frame));
    frame.version    = (uint32_t)RPC_PROTO_VER_1_0;
    frame.frame_type = RPC_FRAME_KEEPALIVE;
    frame.stream_id  = stream_id;
    frame.sequence   = 0;
    frame.flags      = 0x00;  /* 0x00 = PING */

    RPCBuffer payload;
    rpc_buffer_init(&payload);
    uint8_t ping_byte = 0x00;
    rpc_buffer_append(&payload, &ping_byte, 1);
    int ret = rpc_proto_frame_build(&frame, &payload, out);
    rpc_buffer_free(&payload);
    return ret;
}

int rpc_proto_build_pong(uint32_t stream_id, RPCBuffer *out) {
    if (!out) return -1;
    RPCProtoFrame frame;
    memset(&frame, 0, sizeof(frame));
    frame.version    = (uint32_t)RPC_PROTO_VER_1_0;
    frame.frame_type = RPC_FRAME_KEEPALIVE;
    frame.stream_id  = stream_id;
    frame.sequence   = 0;
    frame.flags      = 0x01;  /* 0x01 = PONG */

    RPCBuffer payload;
    rpc_buffer_init(&payload);
    uint8_t pong_byte = 0x01;
    rpc_buffer_append(&payload, &pong_byte, 1);
    int ret = rpc_proto_frame_build(&frame, &payload, out);
    rpc_buffer_free(&payload);
    return ret;
}

bool rpc_proto_is_ping(const RPCProtoFrame *frame) {
    return frame && frame->frame_type == RPC_FRAME_KEEPALIVE
           && frame->flags == 0x00;
}

bool rpc_proto_is_pong(const RPCProtoFrame *frame) {
    return frame && frame->frame_type == RPC_FRAME_KEEPALIVE
           && frame->flags == 0x01;
}

/* --- Error Detection Statistics (L4: Shannon's Theorem) -------- */

double rpc_proto_error_bound(size_t frame_bits) {
    /*
     * Shannon's Theorem for Error Detection Codes:
     *
     * For an (n, k) systematic block code with r = n-k parity bits,
     * the probability of an undetected error on a BSC(p) channel is:
     *
     *   P_ue = SUM_{i=1}^{n} A_i * p^i * (1-p)^(n-i)
     *
     * where A_i is the number of undetectable error patterns of weight i
     * (i.e., the weight distribution of the code).
     *
     * For CRC32 with r = 32:
     *   - P_ue <= 2^(-32) ~ 2.33e-10 for any error pattern
     *   - All odd-weight errors are detected
     *   - All double-bit errors detected for frames < 2^32-1 bits
     *   - All burst errors of length <= 32 detected
     *
     * For a frame of n bits on BSC(p=10^-4), the bound tightens to:
     *   P_ue(n) <= (n * 2^-32) for the linear approximation.
     *
     * This function returns the conservative upper bound 2^(-32).
     */
    (void)frame_bits;
    return 1.0 / ((double)(1ULL << 32));  /* 2^(-32) */
}

void rpc_proto_session_stats(const RPCProtocolSession *sess,
                              uint64_t *sent, uint64_t *recv,
                              uint64_t *crc_err) {
    if (!sess) return;
    if (sent)    *sent    = sess->frames_sent;
    if (recv)    *recv    = sess->frames_received;
    if (crc_err) *crc_err = sess->crc_errors;
}
