#ifndef ICMP_PROTO_H
#define ICMP_PROTO_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

/* L1 Definitions: ICMP (Internet Control Message Protocol) - RFC 792 */

#define ICMP_HEADER_SIZE      8
#define ICMP_MAX_DATA         1472
#define ICMP_ECHO_DATA_DEFAULT 56

/* ICMP Message Types (RFC 792, RFC 950, RFC 1191, RFC 4884) */
#define ICMP_TYPE_ECHO_REPLY              0
#define ICMP_TYPE_DEST_UNREACHABLE         3
#define ICMP_TYPE_SOURCE_QUENCH            4
#define ICMP_TYPE_REDIRECT                 5
#define ICMP_TYPE_ECHO_REQUEST             8
#define ICMP_TYPE_ROUTER_ADVERTISEMENT     9
#define ICMP_TYPE_ROUTER_SOLICITATION     10
#define ICMP_TYPE_TIME_EXCEEDED           11
#define ICMP_TYPE_PARAMETER_PROBLEM       12
#define ICMP_TYPE_TIMESTAMP               13
#define ICMP_TYPE_TIMESTAMP_REPLY         14

/* ICMP Destination Unreachable Codes (RFC 792) */
#define ICMP_CODE_NET_UNREACHABLE          0
#define ICMP_CODE_HOST_UNREACHABLE         1
#define ICMP_CODE_PROTOCOL_UNREACHABLE     2
#define ICMP_CODE_PORT_UNREACHABLE         3
#define ICMP_CODE_FRAG_NEEDED              4
#define ICMP_CODE_SOURCE_ROUTE_FAILED      5

/* ICMP Time Exceeded Codes */
#define ICMP_CODE_TTL_EXCEEDED             0
#define ICMP_CODE_FRAG_REASSEMBLY_TIMEOUT  1

/* L1: ICMP Header Format (RFC 792 3.1) */
typedef struct {
    uint8_t  type;
    uint8_t  code;
    uint16_t checksum;
    /* Rest of header depends on type/code:
     * For Echo: uint16_t identifier; uint16_t sequence_number;
     * For Dest Unreachable: uint32_t unused;
     * For Time Exceeded: uint32_t unused; */
    uint32_t rest;
} ICMPHeader;

/* L1: ICMP Echo (Ping) Message */
typedef struct {
    ICMPHeader hdr;
    uint16_t   identifier;
    uint16_t   sequence_number;
    uint8_t    data[ICMP_MAX_DATA];
    size_t     data_len;
} ICMPEcho;

/* L1: ICMP Error Message (Dest Unreachable / Time Exceeded) */
typedef struct {
    ICMPHeader hdr;
    uint32_t   unused;
    uint8_t    original_ip_header[60];
    uint8_t    original_first_8_bytes[8];
} ICMPError;

/* L2 Core Concepts: ICMP as network-layer control protocol.
 * ICMP provides error reporting and diagnostic functions
 * (RFC 792, J. Postel, 1981).
 *
 * Key concepts:
 * - Echo/Echo Reply: connectivity testing (ping)
 * - Dest Unreachable: path failure notification
 * - Time Exceeded: TTL expiry (traceroute)
 * - Source Quench: congestion notification (deprecated in RFC 6633) */

/* ── L5: ICMP Echo (Ping) Algorithm ── */

/* Build an ICMP Echo Request (ping) packet. Returns 0 on success. */
int  icmp_echo_build(uint16_t identifier, uint16_t seq_num,
                     const uint8_t *payload, size_t payload_len,
                     uint8_t *packet, size_t *packet_len);

/* Build an ICMP Echo Reply packet from a received Echo Request. */
int  icmp_echo_reply(const uint8_t *request, size_t request_len,
                     uint8_t *reply, size_t *reply_len);

/* Parse an ICMP Echo message from raw bytes. */
int  icmp_echo_parse(const uint8_t *data, size_t len, ICMPEcho *echo);

/* L5: ICMP checksum computation (RFC 1071 one's complement) */
uint16_t icmp_checksum(const uint8_t *data, size_t len);
bool     icmp_checksum_verify(const uint8_t *data, size_t len);

/* ── L5: Traceroute using TTL Exceeded ── */

/* Build an ICMP Time Exceeded message with embedded original datagram. */
int  icmp_time_exceeded_build(uint8_t code,
                              const uint8_t *original_datagram,
                              size_t original_len,
                              uint8_t *packet, size_t *packet_len);

/* Build ICMP Destination Unreachable message. */
int  icmp_dest_unreachable_build(uint8_t code,
                                 const uint8_t *original_datagram,
                                 size_t original_len,
                                 uint8_t *packet, size_t *packet_len);

/* ── L1: ICMP Header Utilities ── */

void        icmp_header_init(ICMPHeader *hdr, uint8_t type, uint8_t code);
int         icmp_parse_header(const uint8_t *data, size_t len, ICMPHeader *hdr);
void        icmp_print_header(const ICMPHeader *hdr);
void        icmp_print_echo(const ICMPEcho *echo);
const char* icmp_type_name(uint8_t type);
const char* icmp_code_name(uint8_t type, uint8_t code);

#endif