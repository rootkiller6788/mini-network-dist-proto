#include "icmp_proto.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* L4: ICMP Checksum (RFC 1071 - Computing the Internet Checksum)
 *
 * One's complement sum of 16-bit words, then one's complement of result.
 * Mathematical property: the sum of all 16-bit words including
 * the checksum field should equal 0xFFFF under one's complement.
 *
 * Algorithm:
 *   sum = 0
 *   for each 16-bit word w in data:
 *     sum += w
 *   while (sum >> 16):
 *     sum = (sum & 0xFFFF) + (sum >> 16)
 *   checksum = ~sum & 0xFFFF
 */

uint16_t icmp_checksum(const uint8_t *data, size_t len)
{
    if (!data) return 0;
    uint32_t sum = 0;
    size_t i;
    for (i = 0; i + 1 < len; i += 2) {
        sum += (uint32_t)((data[i] << 8) | data[i + 1]);
    }
    if (i < len) {
        sum += (uint32_t)(data[i] << 8);
    }
    while (sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }
    return (uint16_t)(~sum);
}

bool icmp_checksum_verify(const uint8_t *data, size_t len)
{
    if (!data || len < ICMP_HEADER_SIZE) return false;
    /* Compute checksum including the checksum field itself.
     * If correct, the one's complement sum of all words should be 0xFFFF. */
    uint16_t ck = icmp_checksum(data, len);
    return ck == 0;
}

void icmp_header_init(ICMPHeader *hdr, uint8_t type, uint8_t code)
{
    if (!hdr) return;
    memset(hdr, 0, sizeof(ICMPHeader));
    hdr->type = type;
    hdr->code = code;
    hdr->checksum = 0;
    hdr->rest = 0;
}

int icmp_parse_header(const uint8_t *data, size_t len, ICMPHeader *hdr)
{
    if (!data || !hdr || len < ICMP_HEADER_SIZE) return -1;
    hdr->type     = data[0];
    hdr->code     = data[1];
    hdr->checksum = (uint16_t)((data[2] << 8) | data[3]);
    hdr->rest     = (uint32_t)((data[4] << 24) | (data[5] << 16) |
                               (data[6] << 8)  |  data[7]);
    return 0;
}

/* L5: ICMP Echo Request / Ping (RFC 792)
 *
 * Ping (Packet Internet Groper) was created by Mike Muuss in 1983.
 * It uses ICMP Echo Request/Reply to measure round-trip time and
 * verify reachability. The algorithm:
 *
 *   1. Build Echo Request with identifier + sequence number + payload
 *   2. Send to target
 *   3. Receive Echo Reply, match identifier
 *   4. Calculate RTT = receive_time - send_time
 *
 * L4: RTT formula (Kleinrock's independence approximation):
 *   RTT_i = (1 - alpha) * RTT_{i-1} + alpha * M_i
 *   where alpha = 1/8 (same as TCP SRTT)
 */

int icmp_echo_build(uint16_t identifier, uint16_t seq_num,
                    const uint8_t *payload, size_t payload_len,
                    uint8_t *packet, size_t *packet_len)
{
    if (!packet || !packet_len) return -1;
    if (payload_len > ICMP_MAX_DATA) return -2;

    size_t total = ICMP_HEADER_SIZE + payload_len;
    if (total > *packet_len) return -3;

    packet[0] = ICMP_TYPE_ECHO_REQUEST;
    packet[1] = 0;
    packet[2] = 0;
    packet[3] = 0;
    packet[4] = (uint8_t)((identifier >> 8) & 0xFF);
    packet[5] = (uint8_t)(identifier & 0xFF);
    packet[6] = (uint8_t)((seq_num >> 8) & 0xFF);
    packet[7] = (uint8_t)(seq_num & 0xFF);

    if (payload && payload_len > 0) {
        memcpy(packet + ICMP_HEADER_SIZE, payload, payload_len);
    }

    uint16_t ck = icmp_checksum(packet, total);
    packet[2] = (uint8_t)((ck >> 8) & 0xFF);
    packet[3] = (uint8_t)(ck & 0xFF);

    *packet_len = total;
    return 0;
}

int icmp_echo_reply(const uint8_t *request, size_t request_len,
                    uint8_t *reply, size_t *reply_len)
{
    if (!request || !reply || !reply_len) return -1;
    if (request_len < ICMP_HEADER_SIZE) return -2;
    if (request[0] != ICMP_TYPE_ECHO_REQUEST) return -3;
    if (request_len > *reply_len) return -4;

    /* Copy request data; change type to Echo Reply */
    memcpy(reply, request, request_len);
    reply[0] = ICMP_TYPE_ECHO_REPLY;
    reply[2] = 0;
    reply[3] = 0;

    uint16_t ck = icmp_checksum(reply, request_len);
    reply[2] = (uint8_t)((ck >> 8) & 0xFF);
    reply[3] = (uint8_t)(ck & 0xFF);

    *reply_len = request_len;
    return 0;
}

int icmp_echo_parse(const uint8_t *data, size_t len, ICMPEcho *echo)
{
    if (!data || !echo || len < ICMP_HEADER_SIZE) return -1;
    echo->hdr.type     = data[0];
    echo->hdr.code     = data[1];
    echo->hdr.checksum = (uint16_t)((data[2] << 8) | data[3]);
    echo->identifier    = (uint16_t)((data[4] << 8) | data[5]);
    echo->sequence_number = (uint16_t)((data[6] << 8) | data[7]);
    echo->data_len = len - ICMP_HEADER_SIZE;
    if (echo->data_len > ICMP_MAX_DATA) echo->data_len = ICMP_MAX_DATA;
    memcpy(echo->data, data + ICMP_HEADER_SIZE, echo->data_len);
    return 0;
}

/* L5: Traceroute (RFC 1393, updated by RFC 792)
 *
 * Invented by Van Jacobson in 1987. Exploits the TTL field:
 *   1. Send UDP packet with TTL=1 (or ICMP Echo with TTL=1)
 *   2. First router decrements TTL to 0, sends ICMP Time Exceeded
 *   3. Increment TTL and repeat until destination reached
 *   4. Record each hop from ICMP Time Exceeded responses
 *
 * This enables path tracing: discover every router along the
 * network path from source to destination. */

int icmp_time_exceeded_build(uint8_t code,
                             const uint8_t *original_datagram,
                             size_t original_len,
                             uint8_t *packet, size_t *packet_len)
{
    if (!original_datagram || !packet || !packet_len) return -1;
    if (code != ICMP_CODE_TTL_EXCEEDED &&
        code != ICMP_CODE_FRAG_REASSEMBLY_TIMEOUT) return -2;

    /* ICMP Time Exceeded includes: IP header + first 8 bytes of original */
    size_t embed_len = original_len;
    if (embed_len > 60 + 8) embed_len = 60 + 8;

    size_t total = ICMP_HEADER_SIZE + embed_len;
    if (total > *packet_len) return -3;

    packet[0] = ICMP_TYPE_TIME_EXCEEDED;
    packet[1] = code;
    packet[2] = 0;
    packet[3] = 0;
    packet[4] = 0;
    packet[5] = 0;
    packet[6] = 0;
    packet[7] = 0;
    memcpy(packet + ICMP_HEADER_SIZE, original_datagram, embed_len);

    uint16_t ck = icmp_checksum(packet, total);
    packet[2] = (uint8_t)((ck >> 8) & 0xFF);
    packet[3] = (uint8_t)(ck & 0xFF);

    *packet_len = total;
    return 0;
}

int icmp_dest_unreachable_build(uint8_t code,
                                const uint8_t *original_datagram,
                                size_t original_len,
                                uint8_t *packet, size_t *packet_len)
{
    if (!original_datagram || !packet || !packet_len) return -1;
    if (code > 15) return -2;

    size_t embed_len = original_len;
    if (embed_len > 60 + 8) embed_len = 60 + 8;

    size_t total = ICMP_HEADER_SIZE + embed_len;
    if (total > *packet_len) return -3;

    packet[0] = ICMP_TYPE_DEST_UNREACHABLE;
    packet[1] = code;
    packet[2] = 0;
    packet[3] = 0;
    packet[4] = 0;
    packet[5] = 0;
    packet[6] = 0;
    packet[7] = 0;
    memcpy(packet + ICMP_HEADER_SIZE, original_datagram, embed_len);

    uint16_t ck = icmp_checksum(packet, total);
    packet[2] = (uint8_t)((ck >> 8) & 0xFF);
    packet[3] = (uint8_t)(ck & 0xFF);

    *packet_len = total;
    return 0;
}

/* ── Name Lookup ── */

const char* icmp_type_name(uint8_t type)
{
    switch (type) {
    case ICMP_TYPE_ECHO_REPLY:          return "Echo Reply";
    case ICMP_TYPE_DEST_UNREACHABLE:    return "Destination Unreachable";
    case ICMP_TYPE_SOURCE_QUENCH:       return "Source Quench";
    case ICMP_TYPE_REDIRECT:            return "Redirect";
    case ICMP_TYPE_ECHO_REQUEST:        return "Echo Request";
    case ICMP_TYPE_ROUTER_ADVERTISEMENT: return "Router Advertisement";
    case ICMP_TYPE_ROUTER_SOLICITATION:  return "Router Solicitation";
    case ICMP_TYPE_TIME_EXCEEDED:       return "Time Exceeded";
    case ICMP_TYPE_PARAMETER_PROBLEM:   return "Parameter Problem";
    case ICMP_TYPE_TIMESTAMP:           return "Timestamp";
    case ICMP_TYPE_TIMESTAMP_REPLY:     return "Timestamp Reply";
    default:                            return "Unknown";
    }
}

const char* icmp_code_name(uint8_t type, uint8_t code)
{
    switch (type) {
    case ICMP_TYPE_DEST_UNREACHABLE:
        switch (code) {
        case ICMP_CODE_NET_UNREACHABLE:      return "Net Unreachable";
        case ICMP_CODE_HOST_UNREACHABLE:     return "Host Unreachable";
        case ICMP_CODE_PROTOCOL_UNREACHABLE: return "Protocol Unreachable";
        case ICMP_CODE_PORT_UNREACHABLE:     return "Port Unreachable";
        case ICMP_CODE_FRAG_NEEDED:          return "Fragmentation Needed";
        case ICMP_CODE_SOURCE_ROUTE_FAILED:  return "Source Route Failed";
        default:                             return "Unknown Code";
        }
    case ICMP_TYPE_TIME_EXCEEDED:
        switch (code) {
        case ICMP_CODE_TTL_EXCEEDED:             return "TTL Exceeded";
        case ICMP_CODE_FRAG_REASSEMBLY_TIMEOUT:  return "Frag Reassembly Timeout";
        default:                                 return "Unknown Code";
        }
    default: return "";
    }
}

void icmp_print_header(const ICMPHeader *hdr)
{
    if (!hdr) return;
    fprintf(stderr, "  [ICMP] Type=%u (%s) Code=%u (%s) Checksum=0x%04x\n",
            hdr->type, icmp_type_name(hdr->type),
            hdr->code, icmp_code_name(hdr->type, hdr->code),
            hdr->checksum);
}

void icmp_print_echo(const ICMPEcho *echo)
{
    if (!echo) return;
    fprintf(stderr, "  [ICMP Echo] %s id=%u seq=%u data=%zu bytes\n",
            echo->hdr.type == ICMP_TYPE_ECHO_REQUEST ? "Request" : "Reply",
            echo->identifier, echo->sequence_number, echo->data_len);
}