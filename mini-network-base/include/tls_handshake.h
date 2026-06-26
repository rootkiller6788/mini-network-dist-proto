#ifndef TLS_HANDSHAKE_H
#define TLS_HANDSHAKE_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#define TLS_VERSION_1_3         0x0304
#define TLS_VERSION_1_2         0x0303
#define TLS_RANDOM_SIZE         32
#define TLS_MAX_CIPHER_SUITES   16
#define TLS_MAX_EXTENSIONS      8
#define TLS_HANDSHAKE_HEADER_SIZE 4

#define TLS_HANDSHAKE_CLIENT_HELLO        0x01
#define TLS_HANDSHAKE_SERVER_HELLO        0x02
#define TLS_HANDSHAKE_ENCRYPTED_EXTENSIONS 0x08
#define TLS_HANDSHAKE_CERTIFICATE          0x0B
#define TLS_HANDSHAKE_CERTIFICATE_VERIFY   0x0F
#define TLS_HANDSHAKE_FINISHED             0x14

#define TLS_EXT_SERVER_NAME            0x0000
#define TLS_EXT_SUPPORTED_GROUPS       0x000A
#define TLS_EXT_KEY_SHARE              0x0033
#define TLS_EXT_SUPPORTED_VERSIONS     0x002B
#define TLS_EXT_SIGNATURE_ALGORITHMS   0x000D
#define TLS_EXT_PSK_KEY_EXCHANGE_MODES 0x002D
#define TLS_EXT_ALPN                   0x0010

#define TLS_CIPHER_AES_128_GCM_SHA256       0x1301
#define TLS_CIPHER_AES_256_GCM_SHA384       0x1302
#define TLS_CIPHER_CHACHA20_POLY1305_SHA256 0x1303

#define TLS_GROUP_X25519    0x001D
#define TLS_GROUP_SECP256R1 0x0017
#define TLS_GROUP_SECP384R1 0x0018
#define TLS_GROUP_SECP521R1 0x0019

#define TLS_FINISHED_SIZE 32
#define TLS_MAX_CERT_CHAIN_LEN 4096
#define TLS_MAX_HANDSHAKE_MSG  16384
#define TLS_PUBLIC_KEY_SIZE    32
#define TLS_SHARED_SECRET_SIZE 32

typedef enum {
    TLS_STATE_START          = 0,
    TLS_STATE_CLIENT_HELLO   = 1,
    TLS_STATE_SERVER_HELLO   = 2,
    TLS_STATE_ENCRYPTED_EXT  = 3,
    TLS_STATE_CERTIFICATE    = 4,
    TLS_STATE_CERT_VERIFY    = 5,
    TLS_STATE_FINISHED       = 6,
    TLS_STATE_HANDSHAKE_DONE = 7
} TLSState;

typedef struct {
    uint16_t type;
    uint16_t length;
    uint8_t  data[1024];
} TLSExtension;

typedef struct {
    uint8_t  random[TLS_RANDOM_SIZE];
    uint16_t cipher_suites[TLS_MAX_CIPHER_SUITES];
    size_t   cipher_suites_count;
    TLSExtension extensions[TLS_MAX_EXTENSIONS];
    size_t   extensions_count;
    bool     supports_0rtt;
    uint8_t  legacy_session_id[32];
    size_t   legacy_session_id_len;
} TLSClientHello;

typedef struct {
    uint8_t  random[TLS_RANDOM_SIZE];
    uint16_t cipher_suite;
    uint16_t key_share_group;
    uint8_t  key_share[TLS_PUBLIC_KEY_SIZE];
    uint8_t  legacy_session_id_echo[32];
    size_t   legacy_session_id_echo_len;
} TLSServerHello;

typedef struct {
    uint8_t  cert_chain[TLS_MAX_CERT_CHAIN_LEN];
    size_t   cert_chain_len;
    uint8_t  signature[256];
    size_t   signature_len;
    uint8_t  issuer[256];
    size_t   issuer_len;
    uint8_t  subject[256];
    size_t   subject_len;
} TLSCertificate;

typedef struct {
    uint8_t  verify_data[32];
    size_t   verify_data_len;
    uint8_t  signature_scheme[2];
} TLSCertificateVerify;

typedef struct {
    uint8_t  verify_data[TLS_FINISHED_SIZE];
    uint8_t  client_verify_data[TLS_FINISHED_SIZE];
    uint8_t  server_verify_data[TLS_FINISHED_SIZE];
    bool     client_finished;
    bool     server_finished;
} TLSFinished;

typedef struct {
    TLSState             state;
    TLSClientHello       client_hello;
    TLSServerHello       server_hello;
    TLSCertificate       certificate;
    TLSCertificateVerify cert_verify;
    TLSFinished          finished;
    uint8_t              client_private_key[TLS_PUBLIC_KEY_SIZE];
    uint8_t              client_public_key[TLS_PUBLIC_KEY_SIZE];
    uint8_t              server_private_key[TLS_PUBLIC_KEY_SIZE];
    uint8_t              server_public_key[TLS_PUBLIC_KEY_SIZE];
    uint8_t              shared_secret[TLS_SHARED_SECRET_SIZE];
    uint8_t              handshake_hash[64];
    bool                 is_client;
    bool                 handshake_done;
    uint8_t              transcript_hash[64];
    size_t               transcript_len;
} TLSContext;

TLSContext* tls_context_create(bool is_client);
void        tls_context_free(TLSContext *ctx);

int         tls_client_hello(TLSContext *ctx, TLSClientHello *hello);
int         tls_server_hello(TLSContext *ctx, TLSServerHello *hello);
int         tls_encrypted_extensions(TLSContext *ctx);
int         tls_certificate(TLSContext *ctx, TLSCertificate *cert);
int         tls_certificate_verify(TLSContext *ctx, TLSCertificateVerify *cv);
int         tls_finished(TLSContext *ctx);
int         tls_handshake(TLSContext *ctx);

void        tls_print_client_hello(TLSClientHello *hello);
void        tls_print_server_hello(TLSServerHello *hello);
void        tls_print_state(TLSContext *ctx);
const char* tls_state_name(TLSState state);
const char* tls_cipher_name(uint16_t cipher);
const char* tls_group_name(uint16_t group);

#endif
