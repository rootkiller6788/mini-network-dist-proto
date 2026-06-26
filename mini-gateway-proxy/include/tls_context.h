#ifndef TLS_CONTEXT_H
#define TLS_CONTEXT_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#define TLS_MAX_CERT_SIZE      8192
#define TLS_MAX_SNI_LEN        256
#define TLS_MAX_ALPN_PROTOS    8
#define TLS_MAX_ALPN_LEN       64
#define TLS_CIPHER_SUITE_COUNT 16
#define TLS_MAX_CIPHER_LEN     64
#define TLS_RECORD_MAX         16384

typedef enum {
    TLS_1_0 = 0x0301,
    TLS_1_1 = 0x0302,
    TLS_1_2 = 0x0303,
    TLS_1_3 = 0x0304
} TLSVersion;

typedef enum {
    TLS_CIPHER_RSA_AES128_SHA     = 0x002F,
    TLS_CIPHER_RSA_AES256_SHA     = 0x0035,
    TLS_CIPHER_ECDHE_RSA_AES128   = 0xC013,
    TLS_CIPHER_ECDHE_RSA_AES256   = 0xC014,
    TLS_CIPHER_ECDHE_ECDSA_AES128 = 0xC009,
    TLS_CIPHER_ECDHE_ECDSA_AES256 = 0xC00A,
    TLS_CIPHER_AES128_GCM_SHA256  = 0x009C,
    TLS_CIPHER_AES256_GCM_SHA384  = 0x009D,
    TLS_CIPHER_CHACHA20_POLY1305  = 0xCCA8
} TLSCipherSuite;

typedef enum {
    TLS_HS_HELLO_REQUEST          = 0,
    TLS_HS_CLIENT_HELLO           = 1,
    TLS_HS_SERVER_HELLO           = 2,
    TLS_HS_CERTIFICATE            = 11,
    TLS_HS_SERVER_KEY_EXCHANGE    = 12,
    TLS_HS_CERTIFICATE_REQUEST    = 13,
    TLS_HS_SERVER_HELLO_DONE      = 14,
    TLS_HS_CERTIFICATE_VERIFY     = 15,
    TLS_HS_CLIENT_KEY_EXCHANGE    = 16,
    TLS_HS_FINISHED               = 20
} TLSHandshakeType;

typedef struct {
    uint8_t *data;
    size_t   len;
    char     common_name[256];
    char     subject_alt_names[4][256];
    int      num_sans;
    bool     verified;
    time_t   not_before;
    time_t   not_after;
} TLSCertificate;

typedef struct {
    TLSVersion       min_version;
    TLSVersion       max_version;
    TLSCipherSuite   cipher_suites[TLS_CIPHER_SUITE_COUNT];
    int              num_ciphers;
    char             sni_hostname[TLS_MAX_SNI_LEN];
    char             alpn_protocols[TLS_MAX_ALPN_PROTOS][TLS_MAX_ALPN_LEN];
    int              num_alpn;
    TLSCertificate   *cert;
    TLSCertificate   *ca_cert;
    bool             verify_client;
    bool             session_resumption;
} TLSContext;

TLSContext*     tls_context_init(TLSVersion min_ver, TLSVersion max_ver);
int             tls_set_certificate(TLSContext *ctx, const uint8_t *der_data,
                                    size_t der_len);
int             tls_set_ca_certificate(TLSContext *ctx, const uint8_t *der_data,
                                       size_t der_len);
int             tls_add_cipher_suite(TLSContext *ctx, TLSCipherSuite cs);
int             tls_set_sni(TLSContext *ctx, const char *hostname);
int             tls_add_alpn(TLSContext *ctx, const char *protocol);
int             tls_validate_certificate(TLSCertificate *cert,
                                         const char *expected_cn);
int             tls_validate_cert_chain(TLSCertificate *leaf,
                                        TLSCertificate *ca);
int             tls_check_cert_expiry(TLSCertificate *cert);
const char*     tls_cipher_suite_name(TLSCipherSuite cs);
const char*     tls_version_name(TLSVersion ver);
bool            tls_is_version_supported(TLSVersion ver);
int             tls_parse_client_hello(const uint8_t *data, size_t len,
                                       TLSVersion *out_ver, char *sni,
                                       size_t sni_len,
                                       char (*alpn)[TLS_MAX_ALPN_LEN],
                                       int *num_alpn);
int             tls_negotiate_version(TLSContext *ctx, TLSVersion client_ver,
                                      TLSVersion *agreed);
int             tls_negotiate_cipher(TLSContext *ctx, TLSCipherSuite *client_ciphers,
                                     int num_client, TLSCipherSuite *agreed);
int             tls_negotiate_alpn(TLSContext *ctx, const char (*client_alpn)[TLS_MAX_ALPN_LEN],
                                   int num_client, char *agreed, size_t agreed_len);
void            tls_context_free(TLSContext *ctx);

#endif
