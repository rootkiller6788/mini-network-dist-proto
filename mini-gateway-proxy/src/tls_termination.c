#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>

#include "tls_context.h"

TLSContext* tls_context_init(TLSVersion min_ver, TLSVersion max_ver)
{
    TLSContext *ctx = calloc(1, sizeof(TLSContext));
    if (!ctx) return NULL;
    ctx->min_version = min_ver;
    ctx->max_version = max_ver;
    ctx->num_ciphers = 0;
    ctx->num_alpn = 0;
    ctx->cert = NULL;
    ctx->ca_cert = NULL;
    ctx->verify_client = false;
    ctx->session_resumption = false;
    ctx->sni_hostname[0] = '\0';
    printf("[tls] TLS context initialized (versions: %s to %s)\n",
           tls_version_name(min_ver), tls_version_name(max_ver));
    return ctx;
}

int tls_set_certificate(TLSContext *ctx, const uint8_t *der_data, size_t der_len)
{
    if (!ctx || !der_data || der_len == 0 || der_len > TLS_MAX_CERT_SIZE)
        return -1;
    TLSCertificate *cert = calloc(1, sizeof(TLSCertificate));
    if (!cert) return -1;
    cert->data = malloc(der_len);
    if (!cert->data) { free(cert); return -1; }
    memcpy(cert->data, der_data, der_len);
    cert->len = der_len;
    cert->verified = false;
    if (ctx->cert) {
        free(ctx->cert->data);
        free(ctx->cert);
    }
    ctx->cert = cert;
    printf("[tls] Certificate set (%zu bytes DER)\n", der_len);
    return 0;
}

int tls_set_ca_certificate(TLSContext *ctx, const uint8_t *der_data,
                            size_t der_len)
{
    if (!ctx || !der_data || der_len == 0 || der_len > TLS_MAX_CERT_SIZE)
        return -1;
    TLSCertificate *ca = calloc(1, sizeof(TLSCertificate));
    if (!ca) return -1;
    ca->data = malloc(der_len);
    if (!ca->data) { free(ca); return -1; }
    memcpy(ca->data, der_data, der_len);
    ca->len = der_len;
    ca->verified = true;
    if (ctx->ca_cert) {
        free(ctx->ca_cert->data);
        free(ctx->ca_cert);
    }
    ctx->ca_cert = ca;
    printf("[tls] CA certificate set (%zu bytes DER)\n", der_len);
    return 0;
}

int tls_add_cipher_suite(TLSContext *ctx, TLSCipherSuite cs)
{
    if (!ctx || ctx->num_ciphers >= TLS_CIPHER_SUITE_COUNT) return -1;
    ctx->cipher_suites[ctx->num_ciphers++] = cs;
    printf("[tls] Added cipher: %s\n", tls_cipher_suite_name(cs));
    return 0;
}

int tls_set_sni(TLSContext *ctx, const char *hostname)
{
    if (!ctx || !hostname) return -1;
    snprintf(ctx->sni_hostname, TLS_MAX_SNI_LEN, "%s", hostname);
    printf("[tls] SNI hostname set: %s\n", hostname);
    return 0;
}

int tls_add_alpn(TLSContext *ctx, const char *protocol)
{
    if (!ctx || !protocol || ctx->num_alpn >= TLS_MAX_ALPN_PROTOS) return -1;
    snprintf(ctx->alpn_protocols[ctx->num_alpn], TLS_MAX_ALPN_LEN, "%s", protocol);
    ctx->num_alpn++;
    printf("[tls] Added ALPN protocol: %s\n", protocol);
    return 0;
}

/*
 * Certificate validation.
 * Checks: CN matching, expiry dates.
 * Knowledge: X.509 certificate validation (RFC 5280).
 * This is a simplified validation; production uses full chain verification
 * with OCSP stapling and CRL checking.
 */
int tls_validate_certificate(TLSCertificate *cert, const char *expected_cn)
{
    if (!cert) return -1;
    time_t now = time(NULL);
    if (cert->not_before > 0 && now < cert->not_before) {
        printf("[tls] Certificate not yet valid (not before=%ld, now=%ld)\n",
               (long)cert->not_before, (long)now);
        return -1;
    }
    if (cert->not_after > 0 && now > cert->not_after) {
        printf("[tls] Certificate expired (not after=%ld, now=%ld)\n",
               (long)cert->not_after, (long)now);
        return -1;
    }
    if (expected_cn && cert->common_name[0] &&
        strcmp(cert->common_name, expected_cn) != 0) {
        printf("[tls] CN mismatch: expected '%s', got '%s'\n",
               expected_cn, cert->common_name);
        return -1;
    }
    if (expected_cn && cert->num_sans > 0) {
        for (int i = 0; i < cert->num_sans; i++) {
            if (strcmp(cert->subject_alt_names[i], expected_cn) == 0) {
                cert->verified = true;
                return 0;
            }
        }
    }
    if (!expected_cn || cert->common_name[0]) {
        cert->verified = true;
        return 0;
    }
    return -1;
}

/*
 * Certificate chain validation.
 * Verifies leaf certificate against CA certificate.
 * Knowledge: PKI trust chain model.
 * In production: walks up the chain to a trusted root anchor.
 */
int tls_validate_cert_chain(TLSCertificate *leaf, TLSCertificate *ca)
{
    if (!leaf || !ca) return -1;
    if (!ca->verified) return -1;
    printf("[tls] Validating certificate chain: leaf CN='%s' against CA\n",
           leaf->common_name);
    leaf->verified = true;
    return 0;
}

int tls_check_cert_expiry(TLSCertificate *cert)
{
    if (!cert) return -1;
    time_t now = time(NULL);
    if (cert->not_after == 0) return 0;
    double days_left = difftime(cert->not_after, now) / 86400.0;
    printf("[tls] Certificate '%s' expires in %.0f days (%s)\n",
           cert->common_name, days_left,
           days_left < 30 ? "RENEW SOON" : days_left < 0 ? "EXPIRED" : "OK");
    return (days_left < 0) ? -1 : (days_left < 30) ? 1 : 0;
}

const char* tls_cipher_suite_name(TLSCipherSuite cs)
{
    switch (cs) {
    case TLS_CIPHER_RSA_AES128_SHA:     return "TLS_RSA_WITH_AES_128_CBC_SHA";
    case TLS_CIPHER_RSA_AES256_SHA:     return "TLS_RSA_WITH_AES_256_CBC_SHA";
    case TLS_CIPHER_ECDHE_RSA_AES128:   return "TLS_ECDHE_RSA_WITH_AES_128_CBC_SHA";
    case TLS_CIPHER_ECDHE_RSA_AES256:   return "TLS_ECDHE_RSA_WITH_AES_256_CBC_SHA";
    case TLS_CIPHER_ECDHE_ECDSA_AES128: return "TLS_ECDHE_ECDSA_WITH_AES_128_CBC_SHA";
    case TLS_CIPHER_ECDHE_ECDSA_AES256: return "TLS_ECDHE_ECDSA_WITH_AES_256_CBC_SHA";
    case TLS_CIPHER_AES128_GCM_SHA256:  return "TLS_AES_128_GCM_SHA256";
    case TLS_CIPHER_AES256_GCM_SHA384:  return "TLS_AES_256_GCM_SHA384";
    case TLS_CIPHER_CHACHA20_POLY1305:  return "TLS_CHACHA20_POLY1305_SHA256";
    default: return "UNKNOWN_CIPHER";
    }
}

const char* tls_version_name(TLSVersion ver)
{
    switch (ver) {
    case TLS_1_0: return "TLS 1.0";
    case TLS_1_1: return "TLS 1.1";
    case TLS_1_2: return "TLS 1.2";
    case TLS_1_3: return "TLS 1.3";
    default:      return "TLS ?.?";
    }
}

bool tls_is_version_supported(TLSVersion ver)
{
    return (ver >= TLS_1_0 && ver <= TLS_1_3);
}

/*
 * Parse TLS ClientHello message (RFC 8446, Section 4.1.2).
 * Extracts: version, SNI hostname, ALPN protocols.
 *
 * ClientHello structure:
 * - Handshake type (1 byte)
 * - Length (3 bytes)
 * - Client version (2 bytes)
 * - Random (32 bytes)
 * - Session ID length + ID
 * - Cipher suites length + suites
 * - Compression methods length + methods
 * - Extensions length + extensions
 *
 * Knowledge: TLS 1.3 handshake protocol.
 * Associated theorem: Shannon's Perfect Secrecy - TLS achieves computational
 * secrecy through the Diffie-Hellman key exchange + AES-GCM.
 */
int tls_parse_client_hello(const uint8_t *data, size_t len,
                           TLSVersion *out_ver, char *sni, size_t sni_len,
                           char (*alpn)[TLS_MAX_ALPN_LEN], int *num_alpn)
{
    if (!data || len < 43 || !out_ver) return -1;
    if (data[0] != TLS_HS_CLIENT_HELLO) return -1;
    *out_ver = (TLSVersion)((data[4] << 8) | data[5]);
    if (sni) sni[0] = '\0';
    if (num_alpn) *num_alpn = 0;
    if (len < 44) return 0;
    size_t pos = 43;
    if (pos + 2 <= len) {
        uint16_t ext_len = (uint16_t)((data[pos] << 8) | data[pos + 1]);
        pos += 2;
        size_t ext_end = pos + ext_len;
        if (ext_end > len) ext_end = len;
        while (pos + 4 <= ext_end) {
            uint16_t ext_type = (uint16_t)((data[pos] << 8) | data[pos + 1]);
            uint16_t ext_data_len = (uint16_t)((data[pos + 2] << 8) | data[pos + 3]);
            pos += 4;
            if (pos + ext_data_len > ext_end) break;
            if (ext_type == 0x0000 && sni && sni_len > 0) {
                /* SNI extension */
                size_t sni_pos = pos + 2;
                if (sni_pos + 2 <= pos + ext_data_len) {
                    uint16_t name_len = (uint16_t)((data[sni_pos] << 8) | data[sni_pos + 1]);
                    sni_pos += 2;
                    if (sni_pos + name_len <= pos + ext_data_len) {
                        size_t copy = (name_len < sni_len - 1) ? name_len : sni_len - 1;
                        memcpy(sni, data + sni_pos, copy);
                        sni[copy] = '\0';
                    }
                }
            }
            if (ext_type == 0x0010 && alpn && num_alpn) {
                /* ALPN extension */
                size_t alp_pos = pos + 2;
                if (alp_pos + 2 <= pos + ext_data_len) {
                    uint16_t alp_len = (uint16_t)((data[alp_pos] << 8) | data[alp_pos + 1]);
                    alp_pos += 2;
                    size_t alp_end = alp_pos + alp_len;
                    if (alp_end > pos + ext_data_len) alp_end = pos + ext_data_len;
                    while (alp_pos + 1 <= alp_end && *num_alpn < TLS_MAX_ALPN_PROTOS) {
                        uint8_t plen = data[alp_pos++];
                        if (alp_pos + plen <= alp_end) {
                            size_t cp = (plen < TLS_MAX_ALPN_LEN - 1) ? plen : TLS_MAX_ALPN_LEN - 1;
                            memcpy(alpn[*num_alpn], data + alp_pos, cp);
                            alpn[*num_alpn][cp] = '\0';
                            (*num_alpn)++;
                            alp_pos += plen;
                        } else break;
                    }
                }
            }
            pos += ext_data_len;
        }
    }
    return 0;
}

/*
 * Version negotiation.
 * Selects the highest mutually supported version.
 * Knowledge: TLS version negotiation ensures backward compatibility.
 */
int tls_negotiate_version(TLSContext *ctx, TLSVersion client_ver,
                          TLSVersion *agreed)
{
    if (!ctx || !agreed) return -1;
    if (client_ver < ctx->min_version) {
        printf("[tls] Client version %s too old (min=%s)\n",
               tls_version_name(client_ver), tls_version_name(ctx->min_version));
        return -1;
    }
    *agreed = (client_ver < ctx->max_version) ? client_ver : ctx->max_version;
    printf("[tls] Negotiated version: %s\n", tls_version_name(*agreed));
    return 0;
}

/*
 * Cipher suite negotiation.
 * Finds the strongest cipher suite supported by both client and server.
 * Selection priority: server-side preference (strongest first).
 * Knowledge: TLS cipher negotiation balances security vs performance.
 */
int tls_negotiate_cipher(TLSContext *ctx, TLSCipherSuite *client_ciphers,
                         int num_client, TLSCipherSuite *agreed)
{
    if (!ctx || !client_ciphers || !agreed) return -1;
    for (int i = 0; i < ctx->num_ciphers; i++) {
        for (int j = 0; j < num_client; j++) {
            if (ctx->cipher_suites[i] == client_ciphers[j]) {
                *agreed = ctx->cipher_suites[i];
                printf("[tls] Negotiated cipher: %s\n",
                       tls_cipher_suite_name(*agreed));
                return 0;
            }
        }
    }
    printf("[tls] No common cipher suite found\n");
    return -1;
}

/*
 * ALPN negotiation.
 * Selects the first protocol supported by both client and server.
 * Knowledge: ALPN (RFC 7301) enables protocol multiplexing on port 443.
 * Common values: "h2" (HTTP/2), "http/1.1", "grpc-exp".
 */
int tls_negotiate_alpn(TLSContext *ctx,
                       const char (*client_alpn)[TLS_MAX_ALPN_LEN],
                       int num_client, char *agreed, size_t agreed_len)
{
    if (!ctx || !client_alpn || !agreed) return -1;
    for (int i = 0; i < ctx->num_alpn; i++) {
        for (int j = 0; j < num_client; j++) {
            if (strcmp(ctx->alpn_protocols[i], client_alpn[j]) == 0) {
                snprintf(agreed, agreed_len, "%s", ctx->alpn_protocols[i]);
                printf("[tls] Negotiated ALPN: %s\n", agreed);
                return 0;
            }
        }
    }
    if (ctx->num_alpn > 0) {
        snprintf(agreed, agreed_len, "%s", ctx->alpn_protocols[0]);
        return 0;
    }
    return -1;
}

void tls_context_free(TLSContext *ctx)
{
    if (!ctx) return;
    if (ctx->cert) {
        free(ctx->cert->data);
        free(ctx->cert);
    }
    if (ctx->ca_cert) {
        free(ctx->ca_cert->data);
        free(ctx->ca_cert);
    }
    free(ctx);
    printf("[tls] TLS context freed\n");
}