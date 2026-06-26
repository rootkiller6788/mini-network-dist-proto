#include "tls_handshake.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

int main(void)
{
    printf("=== mini-network-base: TLS 1.3 Handshake Demo ===\n\n");
    srand((unsigned int)time(NULL));

    printf("Creating TLS client context...\n");
    TLSContext *client = tls_context_create(true);
    if (!client) {
        fprintf(stderr, "Failed to create client context\n");
        return 1;
    }
    tls_print_state(client);
    printf("\n");

    printf("Creating TLS server context...\n");
    TLSContext *server = tls_context_create(false);
    if (!server) {
        fprintf(stderr, "Failed to create server context\n");
        return 1;
    }
    tls_print_state(server);
    printf("\n");

    printf("========================================\n");
    printf(" Step 1: Client -> ClientHello\n");
    printf("========================================\n\n");
    TLSClientHello ch;
    if (tls_client_hello(client, &ch) != 0) {
        fprintf(stderr, "ClientHello failed\n");
        return 1;
    }
    tls_print_client_hello(&ch);
    printf("\n");

    printf("========================================\n");
    printf(" Step 2: Server -> ServerHello\n");
    printf("========================================\n\n");
    TLSServerHello sh;
    if (tls_server_hello(server, &sh) != 0) {
        fprintf(stderr, "ServerHello failed\n");
        return 1;
    }
    tls_print_server_hello(&sh);
    printf("\n");

    printf("  [Key Exchange] ECDHE with x25519\n");
    printf("  [Shared Secret] Computed (simulated)\n");
    printf("  [Forward Secrecy] Ensured: compromise of long-term key\n");
    printf("                    does not compromise past session keys\n\n");

    printf("========================================\n");
    printf(" Step 3: Server -> EncryptedExtensions\n");
    printf("========================================\n\n");
    if (tls_encrypted_extensions(server) != 0) {
        fprintf(stderr, "EncryptedExtensions failed\n");
        return 1;
    }
    printf("  [Encrypted] ALPN, ServerName, etc. confirmed.\n\n");

    printf("========================================\n");
    printf(" Step 4: Server -> Certificate\n");
    printf("========================================\n\n");
    TLSCertificate cert;
    if (tls_certificate(server, &cert) != 0) {
        fprintf(stderr, "Certificate delivery failed\n");
        return 1;
    }
    printf("  [Certificate Chain]:\n");
    printf("    Subject: %.*s\n", (int)cert.subject_len, cert.subject);
    printf("    Issuer:  %.*s\n\n", (int)cert.issuer_len, cert.issuer);
    printf("  [client now verifies certificate chain]\n\n");

    printf("========================================\n");
    printf(" Step 5: Server -> CertificateVerify\n");
    printf("========================================\n\n");
    TLSCertificateVerify cv;
    if (tls_certificate_verify(server, &cv) != 0) {
        fprintf(stderr, "CertificateVerify failed\n");
        return 1;
    }
    printf("  [Signature] Scheme: rsa_pss_sha256\n");
    printf("  [Client verifies: OK]\n\n");

    printf("========================================\n");
    printf(" Step 6: Both -> Finished\n");
    printf("========================================\n\n");
    if (tls_finished(server) != 0) {
        fprintf(stderr, "Finished failed\n");
        return 1;
    }
    printf("  [Verify Data] Client finished & Server finished exchanged.\n");
    printf("  [Handshake] All previous messages integrity-checked.\n");
    tls_print_state(server);
    printf("\n");

    printf("========================================\n");
    printf(" Full TLS 1.3 Handshake Summary\n");
    printf("========================================\n\n");
    printf("  ClientHello          (Client  -> Server)\n");
    printf("  ServerHello          (Server  -> Client)\n");
    printf("  EncryptedExtensions   (Server  -> Client)\n");
    printf("  Certificate          (Server  -> Client)\n");
    printf("  CertificateVerify    (Server  -> Client)\n");
    printf("  Finished             (Both directions)\n\n");

    printf("  Key Exchange:  ECDHE (x25519)\n");
    printf("  Cipher Suite:  TLS_AES_128_GCM_SHA256\n");
    printf("  Forward Secrecy: Yes\n");
    printf("  0-RTT:         Not used in this session\n\n");

    printf("Now running full automated handshake...\n");
    TLSContext *auto_ctx = tls_context_create(true);
    if (tls_handshake(auto_ctx) == 0) {
        printf("\n[Automated handshake SUCCESS]\n");
        tls_print_state(auto_ctx);
    }
    printf("\n");

    printf("=== TLS 1.3 Handshake Demo Complete ===\n");

    tls_context_free(auto_ctx);
    tls_context_free(server);
    tls_context_free(client);

    return 0;
}
