#include "ip_packet.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

void ip_header_init(IPHeader *hdr)
{
    if (!hdr) return;
    memset(hdr, 0, sizeof(IPHeader));
    hdr->version_ihl = (IP_VERSION_4 << 4) | 5;
    hdr->ttl = IP_DEFAULT_TTL;
    hdr->id = (uint16_t)(rand() & 0xFFFF);
}

uint8_t ip_version(IPHeader *hdr)
{
    if (!hdr) return 0;
    return (hdr->version_ihl >> 4) & 0x0F;
}

uint8_t ip_ihl(IPHeader *hdr)
{
    if (!hdr) return 0;
    return hdr->version_ihl & 0x0F;
}

bool ip_flag_df(IPHeader *hdr)
{
    if (!hdr) return false;
    return (hdr->flags_frag_offset & IP_FLAG_DF) != 0;
}

bool ip_flag_mf(IPHeader *hdr)
{
    if (!hdr) return false;
    return (hdr->flags_frag_offset & IP_FLAG_MF) != 0;
}

uint16_t ip_frag_offset(IPHeader *hdr)
{
    if (!hdr) return 0;
    return hdr->flags_frag_offset & IP_FRAG_OFFSET_MASK;
}

uint16_t ip_checksum(const uint8_t *data, size_t len)
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

uint16_t ip_checksum_header(IPHeader *hdr)
{
    if (!hdr) return 0;
    IPHeader tmp;
    memcpy(&tmp, hdr, sizeof(IPHeader));
    tmp.checksum = 0;
    return ip_checksum((const uint8_t*)&tmp, sizeof(IPHeader));
}

bool ip_checksum_verify(IPHeader *hdr)
{
    if (!hdr) return false;
    uint16_t computed = ip_checksum_header(hdr);
    return computed == hdr->checksum ||
           ip_checksum((const uint8_t*)hdr, sizeof(IPHeader)) == 0;
}

int ip_build_packet(uint8_t *packet, size_t *packet_len,
                    uint8_t protocol, uint32_t src_addr, uint32_t dst_addr,
                    const uint8_t *payload, size_t payload_len)
{
    if (!packet || !packet_len || !payload) return -1;
    IPHeader hdr;
    ip_header_init(&hdr);
    hdr.tos = 0;
    hdr.protocol = protocol;
    hdr.src_addr = src_addr;
    hdr.dst_addr = dst_addr;
    hdr.flags_frag_offset = 0;
    hdr.total_length = (uint16_t)(sizeof(IPHeader) + payload_len);
    if (hdr.total_length < payload_len) return -3;
    hdr.checksum = 0;
    hdr.checksum = ip_checksum_header(&hdr);
    memcpy(packet, &hdr, sizeof(IPHeader));
    memcpy(packet + sizeof(IPHeader), payload, payload_len);
    *packet_len = sizeof(IPHeader) + payload_len;
    return 0;
}

int ip_parse_header(const uint8_t *data, size_t len, IPHeader *hdr,
                    size_t *header_len)
{
    if (!data || !hdr || len < sizeof(IPHeader)) return -1;
    memcpy(hdr, data, sizeof(IPHeader));
    if (ip_version(hdr) != IP_VERSION_4) return -2;
    size_t ihl_bytes = (size_t)ip_ihl(hdr) * 4;
    if (ihl_bytes < sizeof(IPHeader) || ihl_bytes > len) return -3;
    if (header_len) *header_len = ihl_bytes;
    return 0;
}

int ip_fragment(const uint8_t *packet, size_t packet_len,
                uint16_t mtu, IPFragment *frags, size_t *frag_count)
{
    if (!packet || !frags || !frag_count) return -1;
    IPHeader orig_hdr;
    if (packet_len < sizeof(IPHeader)) return -2;
    memcpy(&orig_hdr, packet, sizeof(IPHeader));
    size_t header_len = (size_t)ip_ihl(&orig_hdr) * 4;
    size_t payload_len = packet_len - header_len;
    if (payload_len == 0) {
        frags[0].data = (uint8_t*)malloc(packet_len);
        if (!frags[0].data) return -3;
        memcpy(frags[0].data, packet, packet_len);
        frags[0].length = packet_len;
        frags[0].offset = 0;
        frags[0].more_fragments = false;
        frags[0].packet_id = orig_hdr.id;
        *frag_count = 1;
        return 0;
    }
    size_t max_payload = (size_t)(mtu - header_len);
    max_payload = (max_payload / 8) * 8;
    if (max_payload == 0) return -4;
    size_t count = 0;
    size_t offset = 0;
    uint8_t *payload = (uint8_t*)packet + header_len;
    while (offset < payload_len && count < IP_MAX_FRAGMENTS) {
        size_t chunk = payload_len - offset;
        if (chunk > max_payload) chunk = max_payload;
        size_t frag_total = header_len + chunk;
        frags[count].data = (uint8_t*)malloc(frag_total);
        if (!frags[count].data) return -5;
        IPHeader fhdr;
        memcpy(&fhdr, &orig_hdr, header_len);
        fhdr.total_length = (uint16_t)frag_total;
        fhdr.flags_frag_offset = (uint16_t)(offset / 8);
        if (offset + chunk < payload_len)
            fhdr.flags_frag_offset |= IP_FLAG_MF;
        fhdr.checksum = 0;
        fhdr.checksum = ip_checksum_header(&fhdr);
        memcpy(frags[count].data, &fhdr, header_len);
        memcpy(frags[count].data + header_len, payload + offset, chunk);
        frags[count].length = fhdr.total_length;
        frags[count].offset = (uint16_t)(offset / 8);
        frags[count].more_fragments = (offset + chunk < payload_len);
        frags[count].packet_id = orig_hdr.id;
        offset += chunk;
        count++;
    }
    *frag_count = count;
    return 0;
}

void ip_fragment_buffer_init(IPFragmentBuffer *buf)
{
    if (!buf) return;
    memset(buf, 0, sizeof(IPFragmentBuffer));
    buf->complete = false;
}

void ip_fragment_buffer_free(IPFragmentBuffer *buf)
{
    if (!buf) return;
    for (size_t i = 0; i < buf->fragment_count; i++) {
        if (buf->fragments[i].data) {
            free(buf->fragments[i].data);
            buf->fragments[i].data = NULL;
        }
    }
    buf->fragment_count = 0;
    buf->complete = false;
}

static int compare_frags_by_offset(const void *a, const void *b)
{
    IPFragment *fa = (IPFragment*)a;
    IPFragment *fb = (IPFragment*)b;
    if (fa->offset < fb->offset) return -1;
    if (fa->offset > fb->offset) return 1;
    return 0;
}

int ip_reassemble(IPFragmentBuffer *buf, const IPFragment *frag,
                  uint8_t *assembled, size_t *assembled_len)
{
    if (!buf || !frag) return -1;
    if (buf->fragment_count >= IP_MAX_FRAGMENTS) return -2;
    for (size_t i = 0; i < buf->fragment_count; i++) {
        if (buf->fragments[i].offset == frag->offset) return 0;
    }
    buf->fragments[buf->fragment_count] = *frag;
    buf->packet_id = frag->packet_id;
    buf->fragment_count++;
    IPHeader last_hdr;
    IPHeader first_hdr;
    memcpy(&first_hdr, buf->fragments[0].data, sizeof(IPHeader));
    memcpy(&last_hdr, frag->data, sizeof(IPHeader));
    if (!frag->more_fragments) {
        buf->total_assembled = (size_t)frag->offset * 8 +
                               (frag->length - (size_t)ip_ihl(&last_hdr) * 4);
    } else if (frag->offset == 0) {
        uint16_t orig_tot = (uint16_t)((first_hdr.flags_frag_offset & 0xE000) |
                                       (first_hdr.total_length));
        (void)orig_tot;
    }
    bool has_last = false;
    for (size_t i = 0; i < buf->fragment_count; i++) {
        if (!buf->fragments[i].more_fragments) {
            has_last = true;
            break;
        }
    }
    if (!has_last) return 1;
    qsort(buf->fragments, buf->fragment_count,
          sizeof(IPFragment), compare_frags_by_offset);
    if (assembled && assembled_len) {
        size_t total = 0;
        for (size_t i = 0; i < buf->fragment_count; i++) {
            size_t hlen = (size_t)ip_ihl(&first_hdr) * 4;
            size_t payload = buf->fragments[i].length - hlen;
            if (total + payload > *assembled_len) return -3;
            memcpy(assembled + total,
                   buf->fragments[i].data + hlen, payload);
            total += payload;
        }
        *assembled_len = total;
    }
    buf->complete = true;
    return 0;
}

int ip_reassemble_packet(IPFragment *frags, size_t frag_count,
                         uint8_t *packet, size_t *packet_len)
{
    if (!frags || !packet || !packet_len) return -1;
    if (frag_count == 0) return -2;
    qsort(frags, frag_count, sizeof(IPFragment), compare_frags_by_offset);
    IPHeader hdr;
    memcpy(&hdr, frags[0].data, sizeof(IPHeader));
    size_t hlen = (size_t)ip_ihl(&hdr) * 4;
    size_t total_payload = 0;
    for (size_t i = 0; i < frag_count; i++) {
        size_t payload = frags[i].length - hlen;
        if (total_payload + payload > *packet_len) return -3;
        memcpy(packet + sizeof(IPHeader) + total_payload,
               frags[i].data + hlen, payload);
        total_payload += payload;
    }
    hdr.total_length = (uint16_t)(hlen + total_payload);
    hdr.flags_frag_offset = 0;
    hdr.checksum = 0;
    hdr.checksum = ip_checksum_header(&hdr);
    memcpy(packet, &hdr, hlen);
    *packet_len = hlen + total_payload;
    return 0;
}

void ip_print_header(IPHeader *hdr)
{
    if (!hdr) return;
    fprintf(stderr, "  [IP] Ver=%u IHL=%u TOS=%u TotalLen=%u ID=%u\n",
            ip_version(hdr), ip_ihl(hdr), hdr->tos,
            hdr->total_length, hdr->id);
    fprintf(stderr, "       Flags=");
    if (ip_flag_df(hdr)) fprintf(stderr, "DF ");
    if (ip_flag_mf(hdr)) fprintf(stderr, "MF");
    if (!ip_flag_df(hdr) && !ip_flag_mf(hdr)) fprintf(stderr, "none");
    fprintf(stderr, " FragOff=%u\n", ip_frag_offset(hdr));
    fprintf(stderr, "       TTL=%u Proto=%u Checksum=0x%04x\n",
            hdr->ttl, hdr->protocol, hdr->checksum);
    fprintf(stderr, "       SRC: ");
    ip_print_addr(hdr->src_addr);
    fprintf(stderr, " DST: ");
    ip_print_addr(hdr->dst_addr);
    fprintf(stderr, "\n");
}

void ip_print_addr(uint32_t addr)
{
    fprintf(stderr, "%u.%u.%u.%u",
            (unsigned)((addr >> 24) & 0xFF),
            (unsigned)((addr >> 16) & 0xFF),
            (unsigned)((addr >> 8) & 0xFF),
            (unsigned)(addr & 0xFF));
}
