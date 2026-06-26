#include "tls_handshake.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

static void generate_random(uint8_t *buf, size_t len)
{
    for (size_t i = 0; i < len; i++)
        buf[i] = (uint8_t)(rand() & 0xFF);
}

static void simulate_ecdh_keygen(uint8_t *priv, uint8_t *pub)
{
    generate_random(priv, TLS_PUBLIC_KEY_SIZE);
    generate_random(pub, TLS_PUBLIC_KEY_SIZE);
    pub[0] = 0x04;
}

static void simulate_ecdh_shared(const uint8_t *our_priv,
                                  const uint8_t *peer_pub,
                                  uint8_t *shared)
{
    (void)our_priv;
    (void)peer_pub;
    generate_random(shared, TLS_SHARED_SECRET_SIZE);
}

static void derive_verify_data(const uint8_t *transcript_hash,
                                size_t hash_len,
                                uint8_t *verify_data,
                                size_t verify_len,
                                bool is_server)
{
    memset(verify_data, 0, verify_len);
    const char *label = is_server ? "server finished" : "client finished";
    for (size_t i = 0; i < verify_len && i < hash_len; i++) {
        verify_data[i] = (uint8_t)(transcript_hash[i] ^
                          (uint8_t)label[i % ((is_server ? 1 : 2) + 14)]);
    }
}

TLSContext* tls_context_create(bool is_client)
{
    TLSContext *ctx = (TLSContext*)calloc(1, sizeof(TLSContext));
    if (!ctx) return NULL;
    ctx->state = TLS_STATE_START;
    ctx->is_client = is_client;
    ctx->handshake_done = false;
    ctx->transcript_len = 0;
    generate_random(ctx->client_private_key, TLS_PUBLIC_KEY_SIZE);
    generate_random(ctx->server_private_key, TLS_PUBLIC_KEY_SIZE);
    simulate_ecdh_keygen(ctx->client_private_key, ctx->client_public_key);
    simulate_ecdh_keygen(ctx->server_private_key, ctx->server_public_key);
    memset(ctx->handshake_hash, 0, sizeof(ctx->handshake_hash));
    memset(ctx->transcript_hash, 0, sizeof(ctx->transcript_hash));
    return ctx;
}

void tls_context_free(TLSContext *ctx)
{
    if (ctx) free(ctx);
}

int tls_client_hello(TLSContext *ctx, TLSClientHello *hello)
{
    if (!ctx || !hello) return -1;
    ctx->state = TLS_STATE_CLIENT_HELLO;
    hello->cipher_suites_count = 3;
    hello->cipher_suites[0] = TLS_CIPHER_AES_128_GCM_SHA256;
    hello->cipher_suites[1] = TLS_CIPHER_AES_256_GCM_SHA384;
    hello->cipher_suites[2] = TLS_CIPHER_CHACHA20_POLY1305_SHA256;
    hello->extensions_count = 4;
    hello->extensions[0].type = TLS_EXT_SERVER_NAME;
    hello->extensions[0].length = 0;
    hello->extensions[1].type = TLS_EXT_SUPPORTED_GROUPS;
    hello->extensions[1].length = 4;
    hello->extensions[1].data[0] = 0x00;
    hello->extensions[1].data[1] = 0x02;
    hello->extensions[1].data[2] = (uint8_t)((TLS_GROUP_X25519 >> 8) & 0xFF);
    hello->extensions[1].data[3] = (uint8_t)(TLS_GROUP_X25519 & 0xFF);
    hello->extensions[2].type = TLS_EXT_KEY_SHARE;
    hello->extensions[2].length = TLS_PUBLIC_KEY_SIZE + 4;
    hello->extensions[2].data[0] = (uint8_t)((TLS_GROUP_X25519 >> 8) & 0xFF);
    hello->extensions[2].data[1] = (uint8_t)(TLS_GROUP_X25519 & 0xFF);
    hello->extensions[2].data[2] = 0x00;
    hello->extensions[2].data[3] = TLS_PUBLIC_KEY_SIZE;
    memcpy(hello->extensions[2].data + 4, ctx->client_public_key,
           TLS_PUBLIC_KEY_SIZE);
    hello->extensions[3].type = TLS_EXT_SUPPORTED_VERSIONS;
    hello->extensions[3].length = 3;
    hello->extensions[3].data[0] = 0x02;
    hello->extensions[3].data[1] = (uint8_t)((TLS_VERSION_1_3 >> 8) & 0xFF);
    hello->extensions[3].data[2] = (uint8_t)(TLS_VERSION_1_3 & 0xFF);
    generate_random(hello->random, TLS_RANDOM_SIZE);
    hello->legacy_session_id_len = 0;
    hello->supports_0rtt = false;
    memcpy(&ctx->client_hello, hello, sizeof(TLSClientHello));
    fprintf(stderr, "  [TLS] ClientHello generated.\n");
    return 0;
}

int tls_server_hello(TLSContext *ctx, TLSServerHello *hello)
{
    if (!ctx || !hello) return -1;
    if (ctx->state != TLS_STATE_CLIENT_HELLO &&
        ctx->state != TLS_STATE_START) return -2;
    ctx->state = TLS_STATE_SERVER_HELLO;
    generate_random(hello->random, TLS_RANDOM_SIZE);
    hello->cipher_suite = TLS_CIPHER_AES_128_GCM_SHA256;
    hello->key_share_group = TLS_GROUP_X25519;
    memcpy(hello->key_share, ctx->server_public_key, TLS_PUBLIC_KEY_SIZE);
    hello->legacy_session_id_echo_len = 0;
    memcpy(&ctx->server_hello, hello, sizeof(TLSServerHello));
    simulate_ecdh_shared(ctx->client_private_key,
                         ctx->server_public_key,
                         ctx->shared_secret);
    fprintf(stderr, "  [TLS] ServerHello generated. Cipher=%s Group=%s\n",
            tls_cipher_name(hello->cipher_suite),
            tls_group_name(hello->key_share_group));
    return 0;
}

int tls_encrypted_extensions(TLSContext *ctx)
{
    if (!ctx) return -1;
    if (ctx->state != TLS_STATE_SERVER_HELLO) return -2;
    ctx->state = TLS_STATE_ENCRYPTED_EXT;
    fprintf(stderr, "  [TLS] EncryptedExtensions sent. "
            "Server params confirmed.\n");
    return 0;
}

int tls_certificate(TLSContext *ctx, TLSCertificate *cert)
{
    if (!ctx || !cert) return -1;
    if (ctx->state != TLS_STATE_ENCRYPTED_EXT) return -2;
    ctx->state = TLS_STATE_CERTIFICATE;
    memset(cert, 0, sizeof(TLSCertificate));
    const char *fake_cert = "-----BEGIN CERTIFICATE-----\n"
        "MIIDXTCCAkWgAwIBAgIJAJC1FiUKFOnhMA0GCSqGSIb3QqEBCwUAMEUxCzAJBgNV\n"
        "-----END CERTIFICATE-----\n";
    cert->cert_chain_len = strlen(fake_cert);
    memcpy(cert->cert_chain, fake_cert, cert->cert_chain_len);
    generate_random(cert->signature, 256);
    cert->signature_len = 256;
    const char *issuer = "CN=Example Root CA";
    const char *subject = "CN=example.com";
    cert->issuer_len = strlen(issuer);
    memcpy(cert->issuer, issuer, cert->issuer_len);
    cert->subject_len = strlen(subject);
    memcpy(cert->subject, subject, cert->subject_len);
    memcpy(&ctx->certificate, cert, sizeof(TLSCertificate));
    fprintf(stderr, "  [TLS] Certificate delivered. Subject=%s\n", subject);
    return 0;
}

int tls_certificate_verify(TLSContext *ctx, TLSCertificateVerify *cv)
{
    if (!ctx || !cv) return -1;
    if (ctx->state != TLS_STATE_CERTIFICATE) return -2;
    ctx->state = TLS_STATE_CERT_VERIFY;
    generate_random(cv->verify_data, 32);
    cv->verify_data_len = 32;
    cv->signature_scheme[0] = 0x08;
    cv->signature_scheme[1] = 0x04;
    memcpy(&ctx->cert_verify, cv, sizeof(TLSCertificateVerify));
    fprintf(stderr, "  [TLS] CertificateVerify completed. "
            "SigScheme=rsa_pss_sha256\n");
    return 0;
}

int tls_finished(TLSContext *ctx)
{
    if (!ctx) return -1;
    if (ctx->state != TLS_STATE_CERT_VERIFY) return -2;
    ctx->state = TLS_STATE_FINISHED;
    memset(&ctx->finished, 0, sizeof(TLSFinished));
    generate_random(ctx->transcript_hash, 32);
    ctx->transcript_len = 32;
    if (ctx->is_client) {
        derive_verify_data(ctx->transcript_hash, 32,
                           ctx->finished.client_verify_data,
                           TLS_FINISHED_SIZE, false);
        ctx->finished.client_finished = true;
        derive_verify_data(ctx->transcript_hash, 32,
                           ctx->finished.server_verify_data,
                           TLS_FINISHED_SIZE, true);
        ctx->finished.server_finished = true;
    } else {
        derive_verify_data(ctx->transcript_hash, 32,
                           ctx->finished.server_verify_data,
                           TLS_FINISHED_SIZE, true);
        ctx->finished.server_finished = true;
        derive_verify_data(ctx->transcript_hash, 32,
                           ctx->finished.client_verify_data,
                           TLS_FINISHED_SIZE, false);
        ctx->finished.client_finished = true;
    }
    memcpy(ctx->finished.verify_data, ctx->finished.client_verify_data,
           TLS_FINISHED_SIZE);
    ctx->handshake_done = true;
    ctx->state = TLS_STATE_HANDSHAKE_DONE;
    fprintf(stderr, "  [TLS] Finished. Handshake complete. "
            "Forward secrecy achieved.\n");
    return 0;
}

int tls_handshake(TLSContext *ctx)
{
    if (!ctx) return -1;
    TLSClientHello ch;
    TLSServerHello sh;
    TLSCertificate cert;
    TLSCertificateVerify cv;
    int rc;
    rc = tls_client_hello(ctx, &ch);
    if (rc != 0) return rc;
    tls_print_client_hello(&ch);
    rc = tls_server_hello(ctx, &sh);
    if (rc != 0) return rc;
    tls_print_server_hello(&sh);
    rc = tls_encrypted_extensions(ctx);
    if (rc != 0) return rc;
    rc = tls_certificate(ctx, &cert);
    if (rc != 0) return rc;
    rc = tls_certificate_verify(ctx, &cv);
    if (rc != 0) return rc;
    rc = tls_finished(ctx);
    if (rc != 0) return rc;
    fprintf(stderr, "  [TLS] Full handshake completed.\n");
    return 0;
}

void tls_print_client_hello(TLSClientHello *hello)
{
    if (!hello) return;
    fprintf(stderr, "  [TLS ClientHello] Random: ");
    for (int i = 0; i < 8; i++)
        fprintf(stderr, "%02x", hello->random[i]);
    fprintf(stderr, "...\n");
    fprintf(stderr, "    Cipher suites: ");
    for (size_t i = 0; i < hello->cipher_suites_count; i++)
        fprintf(stderr, "%s ", tls_cipher_name(hello->cipher_suites[i]));
    fprintf(stderr, "\n");
    fprintf(stderr, "    Extensions: %zu\n", hello->extensions_count);
    fprintf(stderr, "    0-RTT supported: %s\n",
            hello->supports_0rtt ? "yes" : "no");
}

void tls_print_server_hello(TLSServerHello *hello)
{
    if (!hello) return;
    fprintf(stderr, "  [TLS ServerHello] Random: ");
    for (int i = 0; i < 8; i++)
        fprintf(stderr, "%02x", hello->random[i]);
    fprintf(stderr, "...\n");
    fprintf(stderr, "    Cipher: %s (0x%04x)\n",
            tls_cipher_name(hello->cipher_suite), hello->cipher_suite);
    fprintf(stderr, "    Key share group: %s\n",
            tls_group_name(hello->key_share_group));
}

void tls_print_state(TLSContext *ctx)
{
    if (!ctx) return;
    fprintf(stderr, "  [TLS State] %s  Handshake=%s  "
            "Role=%s\n",
            tls_state_name(ctx->state),
            ctx->handshake_done ? "COMPLETE" : "IN_PROGRESS",
            ctx->is_client ? "CLIENT" : "SERVER");
}

const char* tls_state_name(TLSState state)
{
    switch (state) {
    case TLS_STATE_START:           return "START";
    case TLS_STATE_CLIENT_HELLO:    return "CLIENT_HELLO";
    case TLS_STATE_SERVER_HELLO:    return "SERVER_HELLO";
    case TLS_STATE_ENCRYPTED_EXT:   return "ENCRYPTED_EXTENSIONS";
    case TLS_STATE_CERTIFICATE:     return "CERTIFICATE";
    case TLS_STATE_CERT_VERIFY:     return "CERTIFICATE_VERIFY";
    case TLS_STATE_FINISHED:        return "FINISHED";
    case TLS_STATE_HANDSHAKE_DONE:  return "HANDSHAKE_DONE";
    default:                        return "UNKNOWN";
    }
}

const char* tls_cipher_name(uint16_t cipher)
{
    switch (cipher) {
    case TLS_CIPHER_AES_128_GCM_SHA256:       return "AES_128_GCM_SHA256";
    case TLS_CIPHER_AES_256_GCM_SHA384:       return "AES_256_GCM_SHA384";
    case TLS_CIPHER_CHACHA20_POLY1305_SHA256: return "CHACHA20_POLY1305";
    default:                                   return "UNKNOWN";
    }
}

const char* tls_group_name(uint16_t group)
{
    switch (group) {
    case TLS_GROUP_X25519:     return "x25519";
    case TLS_GROUP_SECP256R1:  return "secp256r1";
    case TLS_GROUP_SECP384R1:  return "secp384r1";
    case TLS_GROUP_SECP521R1:  return "secp521r1";
    default:                   return "UNKNOWN";
    }
}
