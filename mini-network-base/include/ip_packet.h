#ifndef IP_PACKET_H
#define IP_PACKET_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#define IP_VERSION_4     4
#define IP_DEFAULT_TTL   64
#define IP_HEADER_MIN_LEN 20
#define IP_MAX_PACKET    65535
#define IP_MTU_DEFAULT   1500
#define IP_MTU_ETHERNET  1500
#define IP_FRAG_OFFSET_MASK 0x1FFF

#define IP_FLAG_DF       0x4000
#define IP_FLAG_MF       0x2000
#define IP_FLAG_RESERVED 0x8000

#define IP_PROTO_TCP     6
#define IP_PROTO_UDP     17
#define IP_PROTO_ICMP    1
#define IP_PROTO_IGMP    2
#define IP_PROTO_OSPF    89
#define IP_PROTO_SCTP    132

#define IP_MAX_FRAGMENTS 64

typedef enum {
    IP_PROTOCOL_ICMP = 1,
    IP_PROTOCOL_TCP  = 6,
    IP_PROTOCOL_UDP  = 17,
    IP_PROTOCOL_SCTP = 132
} IPProtocol;

typedef struct {
    uint8_t  version_ihl;
    uint8_t  tos;
    uint16_t total_length;
    uint16_t id;
    uint16_t flags_frag_offset;
    uint8_t  ttl;
    uint8_t  protocol;
    uint16_t checksum;
    uint32_t src_addr;
    uint32_t dst_addr;
} IPHeader;

typedef struct {
    uint8_t  *data;
    size_t    length;
    uint16_t  offset;
    bool      more_fragments;
    uint16_t  packet_id;
} IPFragment;

typedef struct {
    IPFragment fragments[IP_MAX_FRAGMENTS];
    size_t     fragment_count;
    size_t     total_assembled;
    uint16_t   packet_id;
    bool       complete;
} IPFragmentBuffer;

void      ip_header_init(IPHeader *hdr);
int       ip_build_packet(uint8_t *packet, size_t *packet_len,
                          uint8_t protocol, uint32_t src_addr, uint32_t dst_addr,
                          const uint8_t *payload, size_t payload_len);
uint16_t  ip_checksum(const uint8_t *data, size_t len);
uint16_t  ip_checksum_header(IPHeader *hdr);
bool      ip_checksum_verify(IPHeader *hdr);
int       ip_fragment(const uint8_t *packet, size_t packet_len,
                      uint16_t mtu, IPFragment *frags, size_t *frag_count);
int       ip_reassemble(IPFragmentBuffer *buf, const IPFragment *frag,
                        uint8_t *assembled, size_t *assembled_len);
int       ip_reassemble_packet(IPFragment *frags, size_t frag_count,
                               uint8_t *packet, size_t *packet_len);
void      ip_print_header(IPHeader *hdr);
void      ip_print_addr(uint32_t addr);
int       ip_parse_header(const uint8_t *data, size_t len, IPHeader *hdr,
                          size_t *header_len);
uint8_t   ip_version(IPHeader *hdr);
uint8_t   ip_ihl(IPHeader *hdr);
bool      ip_flag_df(IPHeader *hdr);
bool      ip_flag_mf(IPHeader *hdr);
uint16_t  ip_frag_offset(IPHeader *hdr);
void      ip_fragment_buffer_init(IPFragmentBuffer *buf);
void      ip_fragment_buffer_free(IPFragmentBuffer *buf);

#endif
