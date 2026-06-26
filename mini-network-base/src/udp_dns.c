#include "udp_dns.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

UDPSocket* udp_socket_create(void)
{
    UDPSocket *sock = (UDPSocket*)calloc(1, sizeof(UDPSocket));
    if (!sock) return NULL;
    sock->is_bound = false;
    sock->bound_port = 0;
    sock->dest_ip = 0;
    sock->dest_port = 0;
    return sock;
}

void udp_socket_free(UDPSocket *sock)
{
    if (sock) free(sock);
}

int udp_bind(UDPSocket *sock, uint16_t port)
{
    if (!sock) return -1;
    sock->bound_port = port;
    sock->is_bound = true;
    fprintf(stderr, "  [UDP] Bound to port %u\n", port);
    return 0;
}

int udp_sendto(UDPSocket *sock, const uint8_t *data, size_t len,
               uint32_t dest_ip, uint16_t dest_port)
{
    if (!sock || !data) return -1;
    sock->dest_ip = dest_ip;
    sock->dest_port = dest_port;
    fprintf(stderr, "  [UDP] Sent %zu bytes to %u:%u\n",
            len, dest_ip, dest_port);
    return (int)len;
}

int udp_recvfrom(UDPSocket *sock, uint8_t *buf, size_t buf_len,
                 uint32_t *src_ip, uint16_t *src_port)
{
    if (!sock || !buf) return -1;
    (void)buf;
    (void)buf_len;
    *src_ip = 0x08080808;
    *src_port = DNS_PORT;
    return 0;
}

static uint16_t dns_build_name(const char *name, uint8_t *buf)
{
    size_t len = strlen(name);
    uint16_t total = 0;
    const char *start = name;
    const char *pos = name;
    while (pos <= name + len) {
        if (*pos == '.' || *pos == '\0') {
            size_t label_len = (size_t)(pos - start);
            if (label_len > 63) return 0;
            buf[total] = (uint8_t)label_len;
            total++;
            memcpy(buf + total, start, label_len);
            total += (uint16_t)label_len;
            start = pos + 1;
        }
        pos++;
        if (pos > name + len + 1) break;
    }
    buf[total] = 0;
    total++;
    return total;
}

int dns_build_query(const char *name, uint16_t qtype,
                    uint8_t *buf, size_t *buf_len)
{
    if (!name || !buf || !buf_len) return -1;
    uint16_t msg_id = (uint16_t)(rand() & 0xFFFF);
    buf[0] = (uint8_t)((msg_id >> 8) & 0xFF);
    buf[1] = (uint8_t)(msg_id & 0xFF);
    buf[2] = 0x01;
    buf[3] = 0x00;
    buf[4] = 0x00;
    buf[5] = 0x01;
    buf[6] = 0x00;
    buf[7] = 0x00;
    buf[8] = 0x00;
    buf[9] = 0x00;
    buf[10] = 0x00;
    buf[11] = 0x00;
    uint16_t name_len = dns_build_name(name, buf + 12);
    if (name_len == 0) return -2;
    uint16_t offset = 12 + name_len;
    buf[offset + 0] = (uint8_t)((qtype >> 8) & 0xFF);
    buf[offset + 1] = (uint8_t)(qtype & 0xFF);
    buf[offset + 2] = 0x00;
    buf[offset + 3] = 0x01;
    *buf_len = (size_t)(offset + 4);
    return 0;
}

int dns_name_decode(const uint8_t *data, size_t data_len,
                    size_t offset, char *name, size_t name_len,
                    size_t *next_offset)
{
    if (!data || !name) return -1;
    size_t out = 0;
    bool jumped = false;
    size_t jump_offset = 0;
    size_t pos = offset;
    while (pos < data_len) {
        uint8_t label_len = data[pos];
        if (label_len == 0) {
            if (out > 0) name[out - 1] = '\0';
            else name[0] = '\0';
            if (!jumped && next_offset)
                *next_offset = pos + 1;
            else if (next_offset)
                *next_offset = jump_offset;
            return 0;
        }
        if ((label_len & 0xC0) == 0xC0) {
            uint16_t ptr = (uint16_t)(((label_len & 0x3F) << 8) | data[pos + 1]);
            if (!jumped) {
                jump_offset = pos + 2;
                jumped = true;
            }
            pos = (size_t)ptr;
            continue;
        }
        pos++;
        if (pos + label_len > data_len) return -2;
        for (uint8_t i = 0; i < label_len && out < name_len - 1; i++) {
            name[out++] = (char)data[pos + i];
        }
        if (out < name_len - 1) name[out++] = '.';
        pos += label_len;
    }
    return -3;
}

int dns_parse_header(const uint8_t *data, size_t len, DNSHeader *hdr)
{
    if (!data || !hdr || len < 12) return -1;
    hdr->id = (uint16_t)((data[0] << 8) | data[1]);
    hdr->flags = (uint16_t)((data[2] << 8) | data[3]);
    hdr->qdcount = (uint16_t)((data[4] << 8) | data[5]);
    hdr->ancount = (uint16_t)((data[6] << 8) | data[7]);
    hdr->nscount = (uint16_t)((data[8] << 8) | data[9]);
    hdr->arcount = (uint16_t)((data[10] << 8) | data[11]);
    return 0;
}

int dns_parse_question(const uint8_t *data, size_t len, size_t *offset,
                       DNSQuestion *q, size_t qdcount)
{
    if (!data || !q || !offset) return -1;
    (void)qdcount;
    size_t pos = *offset;
    if (pos >= len) return -2;
    if (dns_name_decode(data, len, pos, q->qname, DNS_MAX_NAME, &pos) != 0)
        return -3;
    if (pos + 4 > len) return -4;
    q->qtype = (uint16_t)((data[pos] << 8) | data[pos + 1]);
    q->qclass = (uint16_t)((data[pos + 2] << 8) | data[pos + 3]);
    *offset = pos + 4;
    return 0;
}

int dns_parse_response(const uint8_t *data, size_t len,
                       DNSHeader *hdr, DNSRR *answers, size_t *answer_count)
{
    if (!data || !hdr || !answers || !answer_count) return -1;
    if (dns_parse_header(data, len, hdr) != 0) return -2;
    *answer_count = 0;
    size_t pos = 12;
    for (uint16_t i = 0; i < hdr->qdcount && pos < len; i++) {
        if (dns_name_decode(data, len, pos, NULL, 0, &pos) != 0) return -3;
        pos += 4;
    }
    for (uint16_t i = 0; i < hdr->ancount && *answer_count < 16 && pos < len; i++) {
        DNSRR *rr = &answers[*answer_count];
        if (dns_name_decode(data, len, pos, rr->name,
                            DNS_MAX_NAME, &pos) != 0) return -4;
        if (pos + 10 > len) return -5;
        rr->type = (uint16_t)((data[pos] << 8) | data[pos + 1]);
        rr->rrclass = (uint16_t)((data[pos + 2] << 8) | data[pos + 3]);
        rr->ttl = (uint32_t)((data[pos + 4] << 24) |
                             (data[pos + 5] << 16) |
                             (data[pos + 6] << 8)  |
                             data[pos + 7]);
        rr->rdlength = (uint16_t)((data[pos + 8] << 8) | data[pos + 9]);
        pos += 10;
        if (pos + rr->rdlength > len) return -6;
        memset(rr->rdata, 0, sizeof(rr->rdata));
        memcpy(rr->rdata, data + pos,
               rr->rdlength > sizeof(rr->rdata) ? sizeof(rr->rdata) : rr->rdlength);
        pos += rr->rdlength;
        (*answer_count)++;
    }
    return 0;
}

int dns_resolve(const char *domain, uint16_t qtype,
                char *result_ip, size_t result_ip_len)
{
    if (!domain || !result_ip) return -1;
    static const char *simulated_ips[] = {
        "93.184.216.34",
        "1.2.3.4",
        "8.8.8.8",
        "208.67.222.222",
        "1.1.1.1"
    };
    const char *ip = simulated_ips[rand() % 5];
    if (qtype == DNS_QTYPE_AAAA)
        ip = "2606:2800:220:1:248:1893:25c8:1946";
    size_t ip_len = strlen(ip);
    if (ip_len >= result_ip_len) return -2;
    memcpy(result_ip, ip, ip_len + 1);
    fprintf(stderr, "  [DNS] Resolved %s -> %s (qtype=%u)\n",
            domain, result_ip, qtype);
    return 0;
}

void dns_print_header(DNSHeader *hdr)
{
    if (!hdr) return;
    bool is_response = (hdr->flags & DNS_QR_MASK) != 0;
    uint16_t opcode = (hdr->flags & DNS_OPCODE_MASK) >> 11;
    uint16_t rcode = (hdr->flags & DNS_RCODE_MASK);
    fprintf(stderr, "  [DNS Header] ID=%u %s Opcode=%u Rcode=%u "
            "QD=%u AN=%u NS=%u AR=%u\n",
            hdr->id,
            is_response ? "RESPONSE" : "QUERY",
            opcode, rcode,
            hdr->qdcount, hdr->ancount,
            hdr->nscount, hdr->arcount);
}

void dns_print_question(DNSQuestion *q)
{
    if (!q) return;
    const char *type_str = "A";
    switch (q->qtype) {
    case DNS_QTYPE_AAAA:  type_str = "AAAA"; break;
    case DNS_QTYPE_MX:    type_str = "MX"; break;
    case DNS_QTYPE_CNAME: type_str = "CNAME"; break;
    case DNS_QTYPE_NS:    type_str = "NS"; break;
    case DNS_QTYPE_SOA:   type_str = "SOA"; break;
    }
    fprintf(stderr, "  [DNS Question] %s %s IN\n", q->qname, type_str);
}

void dns_print_rr(DNSRR *rr)
{
    if (!rr) return;
    fprintf(stderr, "  [DNS RR] %s type=%u class=%u ttl=%u rdlen=%u\n",
            rr->name, rr->type, rr->rrclass, rr->ttl, rr->rdlength);
    if (rr->type == DNS_QTYPE_A && rr->rdlength == 4) {
        fprintf(stderr, "    IPv4: %u.%u.%u.%u\n",
                rr->rdata[0], rr->rdata[1],
                rr->rdata[2], rr->rdata[3]);
    }
}

uint32_t ip_str_to_u32(const char *ip_str)
{
    if (!ip_str) return 0;
    unsigned int a, b, c, d;
    if (sscanf(ip_str, "%u.%u.%u.%u", &a, &b, &c, &d) == 4) {
        return ((a & 0xFF) << 24) | ((b & 0xFF) << 16) |
               ((c & 0xFF) << 8)  |  (d & 0xFF);
    }
    return 0;
}
