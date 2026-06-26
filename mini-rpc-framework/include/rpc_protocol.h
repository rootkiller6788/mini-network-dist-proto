#ifndef RPC_PROTOCOL_H
#define RPC_PROTOCOL_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "rpc_encoding.h"

/*
 * rpc_protocol.h - Advanced Protocol Features
 *
 * L1 (Definitions): Protocol frame format, versioning, compression enums
 * L4 (Standards/Theorems): CRC32 error detection (polynomial: 0xEDB88320),
 *     Shannon's Theorem - channel capacity and error detection probability
 *     P(undetected error) <= 1/2^32 for CRC32
 * L5 (Algorithms): CRC32 (Sarwate 1988 table-driven), version comparison
 * L3 (Engineering): Framing with magic bytes + length prefix + CRC trailer
 * L8 (Advanced): Streaming protocol, compression pipeline
 */

/* --- L1: Core Definitions ------------------------------------------ */

#define RPC_PROTO_MAGIC          0x52504321  /* "RPC!" in big-endian */
#define RPC_PROTO_VERSION_MAJOR  1
#define RPC_PROTO_VERSION_MINOR  0
#define RPC_PROTO_VERSION_PATCH  0
#define RPC_PROTO_MAX_FRAME_SIZE (16 * 1024 * 1024)  /* 16 MB */
#define RPC_PROTO_MAX_CHUNK_SIZE (64 * 1024)          /* 64 KB per chunk */
#define RPC_PROTO_HEADER_SIZE    22
#define RPC_PROTO_CRC_SIZE       4
#define RPC_PROTO_OVERHEAD       (RPC_PROTO_HEADER_SIZE + RPC_PROTO_CRC_SIZE)

typedef enum {
    RPC_PROTO_VER_1_0 = 0x00010000,
    RPC_PROTO_VER_2_0 = 0x00020000,
    RPC_PROTO_VER_ANY  = 0xFFFFFFFF
} RPCProtoVersion;

typedef enum {
    RPC_COMPRESS_NONE   = 0,
    RPC_COMPRESS_ZLIB   = 1,
    RPC_COMPRESS_LZ4    = 2,
    RPC_COMPRESS_SNAPPY = 3
} RPCCompressAlgo;

typedef enum {
    RPC_FRAME_UNARY       = 0,
    RPC_FRAME_STREAM_DATA = 1,
    RPC_FRAME_STREAM_END  = 2,
    RPC_FRAME_KEEPALIVE   = 3,
    RPC_FRAME_HANDSHAKE   = 4
} RPCFrameType;

/*
 * Protocol Frame (wire format, big-endian):
 *
 * +-------------------------------------------------------------+
 * | Magic (4B)    | Version (4B) | Type (1B) | Flags (1B)      |
 * +-------------------------------------------------------------+
 * | Stream ID (4B)| Sequence (4B)| Frame Len (4B)              |
 * +-------------------------------------------------------------+
 * | Payload (variable, up to 16MB)                              |
 * +-------------------------------------------------------------+
 * | CRC32 (4B)                                                  |
 * +-------------------------------------------------------------+
 *
 * Total overhead: 22 bytes header + 4 bytes CRC = 26 bytes.
 */

typedef struct {
    uint32_t magic;        /* RPC_PROTO_MAGIC */
    uint32_t version;      /* RPCProtoVersion */
    uint8_t  frame_type;   /* RPCFrameType */
    uint8_t  flags;        /* bit0:compressed bit1:encrypted bit2:priority */
    uint32_t stream_id;
    uint32_t sequence;
    uint32_t frame_len;    /* total frame size including header+CRC */
} RPCProtoFrame;

typedef struct {
    RPCProtoVersion negotiated_version;
    bool             compression_enabled;
    RPCCompressAlgo  compression_algo;
    bool             encryption_enabled;
    bool             multiplexing_supported;
    int32_t          max_frame_size;
    int32_t          max_streams;
} RPCProtoNegotiation;

typedef struct {
    uint32_t  stream_id;
    uint32_t  next_sequence;
    bool      active;
    bool      closed;
    RPCBuffer reassembly_buffer;
    uint32_t  expected_sequence;
    size_t    high_watermark;
    size_t    low_watermark;
} RPCStream;

#define RPC_PROTO_MAX_STREAMS 64

typedef struct {
    RPCProtoNegotiation  negotiation;
    RPCStream            streams[RPC_PROTO_MAX_STREAMS];
    int32_t              stream_count;
    bool                 handshake_complete;
    uint32_t             next_stream_id;
    uint64_t             frames_sent;
    uint64_t             frames_received;
    uint64_t             crc_errors;
} RPCProtocolSession;

/*
 * CRC32 polynomial: 0xEDB88320 (IEEE 802.3, same as zlib/gzip).
 * This is the reflected polynomial representation of:
 *   x^32 + x^26 + x^23 + x^22 + x^16 + x^12 + x^11 + x^10 +
 *   x^8 + x^7 + x^5 + x^4 + x^2 + x + 1
 *
 * L4 Theorem: CRC32 detects all single-bit errors, all double-bit errors
 * for frames < 2^32-1 bits, all odd number of bit errors, all burst errors
 * of length <= 32 bits. Probability of missing random error ~ 2^(-32).
 */

/* --- CRC32 ---------------------------------------------------------- */

void     rpc_crc32_init(void);
uint32_t rpc_crc32_update(uint32_t crc, const uint8_t *data, size_t len);
uint32_t rpc_crc32_finalize(uint32_t crc);
uint32_t rpc_crc32_compute(const uint8_t *data, size_t len);

/* --- Protocol Session ------------------------------------------------ */

void rpc_proto_session_init(RPCProtocolSession *sess);
void rpc_proto_session_free(RPCProtocolSession *sess);

int  rpc_proto_build_handshake(RPCProtocolSession *sess, RPCBuffer *out);
int  rpc_proto_parse_handshake(RPCProtocolSession *sess, const RPCBuffer *in);

/* --- Framing -------------------------------------------------------- */

int  rpc_proto_frame_build(RPCProtoFrame *frame, const RPCBuffer *payload, RPCBuffer *out);
int  rpc_proto_frame_parse(const RPCBuffer *in, RPCProtoFrame *frame, RPCBuffer *out);

/* --- Version Negotiation -------------------------------------------- */

int             rpc_proto_version_compare(RPCProtoVersion a, RPCProtoVersion b);
RPCProtoVersion rpc_proto_version_negotiate(RPCProtoVersion local, RPCProtoVersion remote);

/* --- Compression ---------------------------------------------------- */

typedef int (*RPCCompressFn)(const uint8_t *src, size_t src_len, uint8_t *dst, size_t *dst_len);
typedef int (*RPCDecompressFn)(const uint8_t *src, size_t src_len, uint8_t *dst, size_t *dst_len);

int  rpc_proto_compress_register(RPCCompressAlgo algo, RPCCompressFn compress, RPCDecompressFn decompress);
int  rpc_proto_compress(RPCCompressAlgo algo, const uint8_t *src, size_t src_len, RPCBuffer *out);
int  rpc_proto_decompress(RPCCompressAlgo algo, const uint8_t *src, size_t src_len, RPCBuffer *out);

/* --- Streaming Protocol --------------------------------------------- */

int  rpc_proto_stream_open(RPCProtocolSession *sess);
int  rpc_proto_stream_send(RPCProtocolSession *sess, uint32_t stream_id, const RPCBuffer *chunk, RPCBuffer *framed_out);
int  rpc_proto_stream_recv(RPCProtocolSession *sess, const RPCBuffer *framed_in, RPCBuffer *chunk_out, RPCProtoFrame *frame_out);
int  rpc_proto_stream_close(RPCProtocolSession *sess, uint32_t stream_id, RPCBuffer *framed_out);

/*
 * L8 Advanced: Back-pressure management.
 * Watermark-based flow control to prevent memory exhaustion.
 */
void rpc_proto_stream_set_watermarks(uint32_t stream_id, size_t high_watermark, size_t low_watermark);
bool rpc_proto_stream_is_backpressured(RPCProtocolSession *sess, uint32_t stream_id);

/* --- Keepalive PING/PONG ------------------------------------------- */

int  rpc_proto_build_ping(uint32_t stream_id, RPCBuffer *out);
int  rpc_proto_build_pong(uint32_t stream_id, RPCBuffer *out);
bool rpc_proto_is_ping(const RPCProtoFrame *frame);
bool rpc_proto_is_pong(const RPCProtoFrame *frame);

/* --- Error Detection Statistics ------------------------------------ */

/*
 * Shannon's error detection theorem bound.
 * P_fp <= 2^(-32) for random errors in CRC32.
 * Returns the theoretical bound given frame length in bits.
 */
double rpc_proto_error_bound(size_t frame_bits);

void   rpc_proto_session_stats(const RPCProtocolSession *sess,
                                uint64_t *sent, uint64_t *recv, uint64_t *crc_err);

#endif /* RPC_PROTOCOL_H */
