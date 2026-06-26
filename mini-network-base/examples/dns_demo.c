#include "udp_dns.h"
#include "ip_packet.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

int main(void)
{
    printf("=== mini-network-base: DNS Demo ===\n\n");
    srand((unsigned int)time(NULL));

    uint8_t query_buf[512];
    size_t query_len = sizeof(query_buf);

    printf("Building DNS A-record query for 'example.com'...\n");
    if (dns_build_query("example.com", DNS_QTYPE_A,
                        query_buf, &query_len) != 0) {
        fprintf(stderr, "Failed to build DNS query\n");
        return 1;
    }
    printf("[DNS Query built] %zu bytes\n", query_len);

    printf("Query hex dump: ");
    for (size_t i = 0; i < query_len && i < 48; i++) {
        printf("%02x ", query_buf[i]);
    }
    printf("\n\n");

    DNSHeader query_hdr;
    if (dns_parse_header(query_buf, query_len, &query_hdr) == 0) {
        printf("[Parsed query header]:\n");
        dns_print_header(&query_hdr);
        printf("\n");
    }

    printf("Resolving example.com via simulated DNS...\n");
    char resolved_ip[64] = {0};
    if (dns_resolve("example.com", DNS_QTYPE_A,
                    resolved_ip, sizeof(resolved_ip)) == 0) {
        printf("[Resolved] example.com -> %s\n", resolved_ip);
    }
    printf("\n");

    printf("=== Building a simulated DNS response ===\n");
    uint8_t response[512];
    size_t resp_len = 0;

    uint16_t resp_id = (uint16_t)(rand() & 0xFFFF);
    response[0] = (uint8_t)((resp_id >> 8) & 0xFF);
    response[1] = (uint8_t)(resp_id & 0xFF);
    response[2] = 0x81;
    response[3] = 0x80;
    response[4] = 0x00;
    response[5] = 0x01;
    response[6] = 0x00;
    response[7] = 0x01;
    response[8] = 0x00;
    response[9] = 0x00;
    response[10] = 0x00;
    response[11] = 0x00;

    const char *domain = "example.com";
    size_t pos = 12;
    const char *start = domain;
    for (const char *p = domain; ; p++) {
        if (*p == '.' || *p == '\0') {
            size_t len = (size_t)(p - start);
            response[pos++] = (uint8_t)len;
            memcpy(&response[pos], start, len);
            pos += len;
            start = p + 1;
            if (*p == '\0') break;
        }
    }
    response[pos++] = 0x00;
    response[pos++] = 0x00;
    response[pos++] = 0x01;
    response[pos++] = 0x00;
    response[pos++] = 0x01;

    response[pos++] = 0xC0;
    response[pos++] = 0x0C;
    response[pos++] = 0x00;
    response[pos++] = 0x01;
    response[pos++] = 0x00;
    response[pos++] = 0x01;
    response[pos++] = 0x00;
    response[pos++] = 0x00;
    response[pos++] = 0x00;
    response[pos++] = 0x3C;
    response[pos++] = 0x00;
    response[pos++] = 0x04;
    response[pos++] = 93;
    response[pos++] = 184;
    response[pos++] = 216;
    response[pos++] = 34;
    resp_len = pos;

    printf("Simulated DNS response: %zu bytes\n", resp_len);

    DNSHeader resp_hdr;
    DNSRR answers[8];
    size_t answer_count = 0;
    if (dns_parse_response(response, resp_len,
                           &resp_hdr, answers, &answer_count) == 0) {
        printf("\n[Response Header]:\n");
        dns_print_header(&resp_hdr);

        printf("\n[Answer RRs: %zu]\n", answer_count);
        for (size_t i = 0; i < answer_count; i++) {
            dns_print_rr(&answers[i]);
            if (answers[i].type == DNS_QTYPE_A &&
                answers[i].rdlength == 4) {
                printf("    => Resolved: %u.%u.%u.%u\n",
                       answers[i].rdata[0], answers[i].rdata[1],
                       answers[i].rdata[2], answers[i].rdata[3]);
            }
        }
    }
    printf("\n");

    printf("=== Additional DNS queries ===\n");
    char ip[64];
    dns_resolve("www.google.com", DNS_QTYPE_A, ip, sizeof(ip));
    dns_resolve("github.com", DNS_QTYPE_A, ip, sizeof(ip));
    dns_resolve("example.com", DNS_QTYPE_AAAA, ip, sizeof(ip));
    printf("\n");

    uint8_t mx_query[512];
    size_t mx_len = sizeof(mx_query);
    dns_build_query("example.com", DNS_QTYPE_MX, mx_query, &mx_len);
    printf("MX query for example.com: %zu bytes\n", mx_len);
    printf("\n");

    printf("=== DNS Demo Complete ===\n");

    return 0;
}
