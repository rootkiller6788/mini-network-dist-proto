#ifndef UDP_DNS_H
#define UDP_DNS_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#define UDP_MAX_PAYLOAD 65507
#define DNS_MAX_NAME    256
#define DNS_MAX_LABELS  128
#define DNS_PORT        53
#define DNS_UDP_PORT    53

#define DNS_QTYPE_A     1
#define DNS_QTYPE_AAAA  28
#define DNS_QTYPE_MX    15
#define DNS_QTYPE_CNAME 5
#define DNS_QTYPE_NS    2
#define DNS_QTYPE_SOA   6

#define DNS_QCLASS_IN   1

#define DNS_QR_MASK     0x8000
#define DNS_OPCODE_MASK 0x7800
#define DNS_RCODE_MASK  0x000F

#define DNS_RCODE_NOERROR  0
#define DNS_RCODE_FORMERR  1
#define DNS_RCODE_SERVFAIL 2
#define DNS_RCODE_NXDOMAIN 3
#define DNS_RCODE_NOTIMP   4
#define DNS_RCODE_REFUSED  5

typedef enum {
    DNS_QR_QUERY    = 0,
    DNS_QR_RESPONSE = 1
} DNSQRFlag;

typedef enum {
    DNS_OPCODE_QUERY  = 0,
    DNS_OPCODE_IQUERY = 1,
    DNS_OPCODE_STATUS = 2
} DNSOpcode;

typedef enum {
    DNS_RCODE_NOERR  = 0,
    DNS_RCODE_FORM   = 1,
    DNS_RCODE_SERV   = 2,
    DNS_RCODE_NX     = 3,
    DNS_RCODE_NIMP   = 4,
    DNS_RCODE_REF    = 5
} DNSRcode;

typedef struct {
    uint16_t id;
    uint16_t flags;
    uint16_t qdcount;
    uint16_t ancount;
    uint16_t nscount;
    uint16_t arcount;
} DNSHeader;

typedef struct {
    char     qname[DNS_MAX_NAME];
    uint16_t qtype;
    uint16_t qclass;
} DNSQuestion;

typedef struct {
    char     name[DNS_MAX_NAME];
    uint16_t type;
    uint16_t rrclass;
    uint32_t ttl;
    uint16_t rdlength;
    uint8_t  rdata[512];
} DNSRR;

typedef struct {
    uint16_t bound_port;
    uint32_t dest_ip;
    uint16_t dest_port;
    bool     is_bound;
} UDPSocket;

UDPSocket* udp_socket_create(void);
int        udp_bind(UDPSocket *sock, uint16_t port);
int        udp_sendto(UDPSocket *sock, const uint8_t *data, size_t len,
                      uint32_t dest_ip, uint16_t dest_port);
int        udp_recvfrom(UDPSocket *sock, uint8_t *buf, size_t buf_len,
                        uint32_t *src_ip, uint16_t *src_port);
void       udp_socket_free(UDPSocket *sock);

int        dns_build_query(const char *name, uint16_t qtype,
                           uint8_t *buf, size_t *buf_len);
int        dns_parse_header(const uint8_t *data, size_t len, DNSHeader *hdr);
int        dns_parse_question(const uint8_t *data, size_t len, size_t *offset,
                              DNSQuestion *q, size_t qdcount);
int        dns_parse_response(const uint8_t *data, size_t len,
                              DNSHeader *hdr, DNSRR *answers, size_t *answer_count);
int        dns_resolve(const char *domain, uint16_t qtype,
                       char *result_ip, size_t result_ip_len);
int        dns_name_decode(const uint8_t *data, size_t data_len,
                           size_t offset, char *name, size_t name_len,
                           size_t *next_offset);
void       dns_print_header(DNSHeader *hdr);
void       dns_print_question(DNSQuestion *q);
void       dns_print_rr(DNSRR *rr);

uint32_t   ip_str_to_u32(const char *ip_str);

#endif
