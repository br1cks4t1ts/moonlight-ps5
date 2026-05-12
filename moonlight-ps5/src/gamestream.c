#include "gamestream.h"
#include <mbedtls/entropy.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/pk.h>
#include <mbedtls/rsa.h>
#include <mbedtls/x509_crt.h>    /* also contains x509write_crt in mbedTLS 3.x */
#include <mbedtls/ssl.h>
#include <mbedtls/net_sockets.h>
#include <mbedtls/md.h>
#include <mbedtls/cipher.h>
#include <mbedtls/pem.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>

/* ── Static crypto state ──────────────────────────────────────────────────── */
static mbedtls_pk_context      s_client_key;
static mbedtls_x509_crt        s_client_cert;
static mbedtls_entropy_context s_entropy;
static mbedtls_ctr_drbg_context s_ctr_drbg;
static bool                    s_inited = false;

static char s_cert_pem[4096];
static char s_key_pem[4096];
static char s_unique_id[] = "0123456789ABCDEF";

/* ── Utility: hex encode/decode ──────────────────────────────────────────── */
static void bin2hex(const unsigned char *in, size_t len, char *out) {
    for (size_t i = 0; i < len; i++)
        snprintf(out + i * 2, 3, "%02x", in[i]);
}

static int hex2bin(const char *hex, unsigned char *out, size_t *out_len) {
    size_t hexlen = strlen(hex);
    if (hexlen % 2 != 0) return -1;
    *out_len = hexlen / 2;
    for (size_t i = 0; i < *out_len; i++) {
        unsigned int b;
        sscanf(hex + i * 2, "%02x", &b);
        out[i] = (unsigned char)b;
    }
    return 0;
}

/* ── Minimal XML field extractor ─────────────────────────────────────────── */
static int xml_find(const char *xml, const char *tag, char *out, size_t outsz) {
    char open[128], close[128];
    snprintf(open,  sizeof(open),  "<%s>",  tag);
    snprintf(close, sizeof(close), "</%s>", tag);

    const char *start = strstr(xml, open);
    if (!start) return -1;
    start += strlen(open);
    const char *end = strstr(start, close);
    if (!end) return -1;
    size_t len = (size_t)(end - start);
    if (len >= outsz) len = outsz - 1;
    memcpy(out, start, len);
    out[len] = '\0';
    return 0;
}

/* ── HTTPS request using mbedTLS ─────────────────────────────────────────── */
static int https_get(const char *host, unsigned short port,
                     const char *path,
                     const char *clientCertPem, const char *clientKeyPem,
                     char *resp_body, size_t resp_sz) {
    mbedtls_net_context      net;
    mbedtls_ssl_context      ssl;
    mbedtls_ssl_config       conf;
    mbedtls_x509_crt         clicert;
    mbedtls_pk_context       clikey;
    int ret = -1;

    mbedtls_net_init(&net);
    mbedtls_ssl_init(&ssl);
    mbedtls_ssl_config_init(&conf);
    mbedtls_x509_crt_init(&clicert);
    mbedtls_pk_init(&clikey);

    char portstr[8];
    snprintf(portstr, sizeof(portstr), "%u", port);

    if (mbedtls_net_connect(&net, host, portstr, MBEDTLS_NET_PROTO_TCP) != 0)
        goto cleanup;

    if (mbedtls_ssl_config_defaults(&conf, MBEDTLS_SSL_IS_CLIENT,
                                    MBEDTLS_SSL_TRANSPORT_STREAM,
                                    MBEDTLS_SSL_PRESET_DEFAULT) != 0)
        goto cleanup;

    mbedtls_ssl_conf_authmode(&conf, MBEDTLS_SSL_VERIFY_NONE);
    mbedtls_ssl_conf_rng(&conf, mbedtls_ctr_drbg_random, &s_ctr_drbg);

    /* Load client cert + key for mutual TLS */
    if (clientCertPem && clientKeyPem) {
        if (mbedtls_x509_crt_parse(&clicert,
                (const unsigned char *)clientCertPem,
                strlen(clientCertPem) + 1) == 0 &&
            mbedtls_pk_parse_key(&clikey,
                (const unsigned char *)clientKeyPem,
                strlen(clientKeyPem) + 1, NULL, 0,
                mbedtls_ctr_drbg_random, &s_ctr_drbg) == 0) {
            mbedtls_ssl_conf_own_cert(&conf, &clicert, &clikey);
        }
    }

    if (mbedtls_ssl_setup(&ssl, &conf) != 0) goto cleanup;
    mbedtls_ssl_set_bio(&ssl, &net,
                        mbedtls_net_send, mbedtls_net_recv, NULL);

    while ((ret = mbedtls_ssl_handshake(&ssl)) != 0) {
        if (ret != MBEDTLS_ERR_SSL_WANT_READ &&
            ret != MBEDTLS_ERR_SSL_WANT_WRITE) {
            ret = -1;
            goto cleanup;
        }
    }

    {
        char req[2048];
        snprintf(req, sizeof(req),
                 "GET %s HTTP/1.0\r\n"
                 "Host: %s\r\n"
                 "Connection: close\r\n"
                 "\r\n", path, host);

        size_t reqlen = strlen(req);
        size_t written = 0;
        while (written < reqlen) {
            ret = mbedtls_ssl_write(&ssl,
                    (const unsigned char *)req + written,
                    reqlen - written);
            if (ret < 0) { ret = -1; goto cleanup; }
            written += (size_t)ret;
        }
    }

    {
        size_t total = 0;
        char buf[4096];
        while (1) {
            int n = mbedtls_ssl_read(&ssl,
                    (unsigned char *)buf, sizeof(buf) - 1);
            if (n == MBEDTLS_ERR_SSL_WANT_READ) continue;
            if (n <= 0) break;
            buf[n] = '\0';
            if (total + (size_t)n < resp_sz - 1) {
                memcpy(resp_body + total, buf, (size_t)n);
                total += (size_t)n;
            }
        }
        resp_body[total] = '\0';

        /* Skip HTTP headers */
        char *body = strstr(resp_body, "\r\n\r\n");
        if (body) {
            body += 4;
            memmove(resp_body, body, strlen(body) + 1);
        }
        ret = 0;
    }

cleanup:
    mbedtls_ssl_close_notify(&ssl);
    mbedtls_net_free(&net);
    mbedtls_ssl_free(&ssl);
    mbedtls_ssl_config_free(&conf);
    mbedtls_x509_crt_free(&clicert);
    mbedtls_pk_free(&clikey);
    return ret;
}

/* HTTP (plain, no TLS) for phase-1 pairing */
static int http_get(const char *host, unsigned short port, const char *path,
                    char *resp_body, size_t resp_sz) {
    mbedtls_net_context net;
    mbedtls_net_init(&net);
    char portstr[8];
    snprintf(portstr, sizeof(portstr), "%u", port);

    if (mbedtls_net_connect(&net, host, portstr, MBEDTLS_NET_PROTO_TCP) != 0) {
        mbedtls_net_free(&net);
        return -1;
    }

    char req[8192];
    snprintf(req, sizeof(req),
             "GET %s HTTP/1.0\r\nHost: %s\r\nConnection: close\r\n\r\n",
             path, host);
    mbedtls_net_send(&net, (const unsigned char *)req, strlen(req));

    size_t total = 0;
    char buf[4096];
    int n;
    while ((n = (int)mbedtls_net_recv(&net, (unsigned char *)buf,
                                       sizeof(buf) - 1)) > 0) {
        buf[n] = '\0';
        if (total + (size_t)n < resp_sz - 1) {
            memcpy(resp_body + total, buf, (size_t)n);
            total += (size_t)n;
        }
    }
    resp_body[total] = '\0';
    mbedtls_net_free(&net);

    char *body = strstr(resp_body, "\r\n\r\n");
    if (body) { body += 4; memmove(resp_body, body, strlen(body) + 1); }
    return 0;
}

/* Persistent identity so the server remembers us across launches */
#define CERT_PATH "/system_ex/app/MLPS00001/client_cert.pem"
#define KEY_PATH  "/system_ex/app/MLPS00001/client_key.pem"

static int load_pem_file(const char *path, char *buf, size_t bufsz) {
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    size_t n = fread(buf, 1, bufsz - 1, f);
    fclose(f);
    if (n == 0) return -1;
    buf[n] = '\0';
    return 0;
}

static void save_pem_file(const char *path, const char *buf) {
    FILE *f = fopen(path, "wb");
    if (!f) { fprintf(stderr, "gs_init: cannot save %s\n", path); return; }
    fwrite(buf, 1, strlen(buf), f);
    fclose(f);
}

/* ── Init: load or generate RSA key + self-signed cert ──────────────────── */
int gs_init(const char *address) {
    (void)address;
    if (s_inited) return 0;

    mbedtls_pk_init(&s_client_key);
    mbedtls_x509_crt_init(&s_client_cert);
    mbedtls_entropy_init(&s_entropy);
    mbedtls_ctr_drbg_init(&s_ctr_drbg);

    const char *pers = "moonlight-ps5";
    if (mbedtls_ctr_drbg_seed(&s_ctr_drbg, mbedtls_entropy_func,
                               &s_entropy,
                               (const unsigned char *)pers,
                               strlen(pers)) != 0) {
        fprintf(stderr, "gs_init: ctr_drbg_seed failed\n");
        return -1;
    }

    /* Try loading a previously saved identity first */
    if (load_pem_file(KEY_PATH,  s_key_pem,  sizeof(s_key_pem))  == 0 &&
        load_pem_file(CERT_PATH, s_cert_pem, sizeof(s_cert_pem)) == 0) {
        if (mbedtls_pk_parse_key(&s_client_key,
                (const unsigned char *)s_key_pem, strlen(s_key_pem) + 1,
                NULL, 0, mbedtls_ctr_drbg_random, &s_ctr_drbg) == 0 &&
            mbedtls_x509_crt_parse(&s_client_cert,
                (const unsigned char *)s_cert_pem, strlen(s_cert_pem) + 1) == 0) {
            printf("gs_init: loaded saved client identity\n");
            s_inited = true;
            return 0;
        }
        fprintf(stderr, "gs_init: saved identity corrupt, regenerating\n");
        mbedtls_pk_free(&s_client_key);
        mbedtls_x509_crt_free(&s_client_cert);
        mbedtls_pk_init(&s_client_key);
        mbedtls_x509_crt_init(&s_client_cert);
    }

    /* Generate RSA-2048 key */
    if (mbedtls_pk_setup(&s_client_key,
            mbedtls_pk_info_from_type(MBEDTLS_PK_RSA)) != 0 ||
        mbedtls_rsa_gen_key(mbedtls_pk_rsa(s_client_key), mbedtls_ctr_drbg_random,
                            &s_ctr_drbg, 2048, 65537) != 0) {
        fprintf(stderr, "gs_init: rsa_gen_key failed\n");
        return -1;
    }

    /* Write self-signed cert */
    mbedtls_x509write_cert crt;
    mbedtls_x509write_crt_init(&crt);
    mbedtls_x509write_crt_set_version(&crt, MBEDTLS_X509_CRT_VERSION_3);
    mbedtls_x509write_crt_set_md_alg(&crt, MBEDTLS_MD_SHA256);
    mbedtls_x509write_crt_set_subject_key(&crt, &s_client_key);
    mbedtls_x509write_crt_set_issuer_key(&crt, &s_client_key);
    mbedtls_x509write_crt_set_subject_name(&crt, "CN=NVIDIA GameStream Client");
    mbedtls_x509write_crt_set_issuer_name(&crt,  "CN=NVIDIA GameStream Client");

    mbedtls_mpi serial_mpi;
    mbedtls_mpi_init(&serial_mpi);
    mbedtls_mpi_lset(&serial_mpi, 1);
    mbedtls_x509write_crt_set_serial(&crt, &serial_mpi);
    mbedtls_mpi_free(&serial_mpi);

    mbedtls_x509write_crt_set_validity(&crt, "20230101000000", "20400101000000");

    static unsigned char s_cert_der_buf[4096];
    int der_len = mbedtls_x509write_crt_der(&crt, s_cert_der_buf,
                                             sizeof(s_cert_der_buf),
                                             mbedtls_ctr_drbg_random, &s_ctr_drbg);
    mbedtls_x509write_crt_free(&crt);
    if (der_len <= 0) {
        fprintf(stderr, "gs_init: x509write_crt_der failed: %d\n", der_len);
        return -1;
    }
    const unsigned char *der_start = s_cert_der_buf + sizeof(s_cert_der_buf)
                                     - (size_t)der_len;

    if (mbedtls_x509_crt_parse_der(&s_client_cert, der_start, (size_t)der_len) != 0) {
        fprintf(stderr, "gs_init: x509_crt_parse_der failed\n");
        return -1;
    }

    if (mbedtls_pk_write_key_pem(&s_client_key,
            (unsigned char *)s_key_pem, sizeof(s_key_pem)) != 0) {
        return -1;
    }
    size_t pem_len;
    mbedtls_pem_write_buffer("-----BEGIN CERTIFICATE-----\n",
                              "-----END CERTIFICATE-----\n",
                              der_start, (size_t)der_len,
                              (unsigned char *)s_cert_pem, sizeof(s_cert_pem),
                              &pem_len);

    save_pem_file(KEY_PATH,  s_key_pem);
    save_pem_file(CERT_PATH, s_cert_pem);
    printf("gs_init: client cert generated and saved (%zu bytes)\n", pem_len);
    s_inited = true;
    return 0;
}

const char *gs_client_cert_pem(void) { return s_cert_pem; }
const char *gs_client_key_pem(void)  { return s_key_pem;  }

/* ── Load server info ────────────────────────────────────────────────────── */
int gs_load_server_info(GS_SERVER *server) {
    char url[1024], resp[65536];
    snprintf(url, sizeof(url), "/serverinfo?uniqueid=%s", s_unique_id);

    server->httpPort  = server->httpPort  ? server->httpPort  : 47989;
    server->httpsPort = server->httpsPort ? server->httpsPort : 47984;

    /* HTTP first — get version, ports, codec (wolf returns paired=0 here always) */
    if (http_get(server->address, server->httpPort, url, resp, sizeof(resp)) != 0) {
        fprintf(stderr, "gs_load_server_info: HTTP request failed\n");
        return -1;
    }
    xml_find(resp, "appversion", server->appVersion, sizeof(server->appVersion));
    xml_find(resp, "GfeVersion", server->gfeVersion, sizeof(server->gfeVersion));

    char httpsPortStr[16] = {0};
    if (xml_find(resp, "HttpsPort", httpsPortStr, sizeof(httpsPortStr)) == 0)
        server->httpsPort = (unsigned short)atoi(httpsPortStr);

    char codecStr[32] = {0};
    if (xml_find(resp, "ServerCodecModeSupport", codecStr, sizeof(codecStr)) == 0)
        server->serverInfo.serverCodecModeSupport = atoi(codecStr);

    /* HTTPS with client cert — wolf uses this to identify us and return true paired status */
    server->paired = false;
    if (s_cert_pem[0] && https_get(server->address, server->httpsPort, url,
                                   s_cert_pem, s_key_pem, resp, sizeof(resp)) == 0) {
        char paired[16] = {0};
        xml_find(resp, "PairStatus", paired, sizeof(paired));
        server->paired = (strcmp(paired, "1") == 0);
    }

    server->serverInfo.address              = server->address;
    server->serverInfo.serverInfoAppVersion = server->appVersion;
    server->serverInfo.serverInfoGfeVersion = server->gfeVersion;

    printf("gs: server %s appver=%s gfe=%s paired=%d codecMode=%d\n",
           server->address, server->appVersion, server->gfeVersion,
           server->paired, server->serverInfo.serverCodecModeSupport);
    return 0;
}

/* ── Pairing ─────────────────────────────────────────────────────────────── */
static void aes_cbc_decrypt(const unsigned char *key, size_t keybits,
                             const unsigned char *in, size_t inlen,
                             unsigned char *out) {
    mbedtls_cipher_context_t ctx;
    mbedtls_cipher_init(&ctx);
    mbedtls_cipher_setup(&ctx, mbedtls_cipher_info_from_values(
        MBEDTLS_CIPHER_ID_AES, (int)keybits, MBEDTLS_MODE_ECB));
    mbedtls_cipher_setkey(&ctx, key, (int)keybits, MBEDTLS_DECRYPT);
    size_t olen;
    for (size_t i = 0; i < inlen; i += 16) {
        mbedtls_cipher_update(&ctx, in + i, 16, out + i, &olen);
    }
    mbedtls_cipher_free(&ctx);
}

static void aes_cbc_encrypt(const unsigned char *key, size_t keybits,
                             const unsigned char *in, size_t inlen,
                             unsigned char *out) {
    mbedtls_cipher_context_t ctx;
    mbedtls_cipher_init(&ctx);
    mbedtls_cipher_setup(&ctx, mbedtls_cipher_info_from_values(
        MBEDTLS_CIPHER_ID_AES, (int)keybits, MBEDTLS_MODE_ECB));
    mbedtls_cipher_setkey(&ctx, key, (int)keybits, MBEDTLS_ENCRYPT);
    size_t olen;
    for (size_t i = 0; i < inlen; i += 16) {
        mbedtls_cipher_update(&ctx, in + i, 16, out + i, &olen);
    }
    mbedtls_cipher_free(&ctx);
}

int gs_pair(GS_SERVER *server, const char *pin) {
    char url[4096], resp[65536];
    char paired_val[16];

    /* --- Phase 1: getservercert (HTTP) --- */
    unsigned char salt[16];
    mbedtls_ctr_drbg_random(&s_ctr_drbg, salt, sizeof(salt));
    char salt_hex[33]; bin2hex(salt, 16, salt_hex);

    /* clientcert = hex(PEM string) — this is what all Moonlight clients send */
    size_t cert_pem_len = strlen(s_cert_pem);
    char cert_pem_hex[8192]; bin2hex((const unsigned char *)s_cert_pem, cert_pem_len, cert_pem_hex);
    printf("gs_pair: cert_pem_len=%zu\n", cert_pem_len);
    fflush(stdout);

    snprintf(url, sizeof(url),
             "/pair?uniqueid=%s&devicename=PS5-Moonlight&updateState=1"
             "&phrase=getservercert&salt=%s&clientcert=%s",
             s_unique_id, salt_hex, cert_pem_hex);
    if (http_get(server->address, server->httpPort, url, resp, sizeof(resp)) != 0) {
        printf("gs_pair phase1: request failed\n"); return -1;
    }
    memset(paired_val, 0, sizeof(paired_val));
    xml_find(resp, "paired", paired_val, sizeof(paired_val));
    if (strcmp(paired_val, "1") != 0) {
        printf("gs_pair phase1: server rejected pair (paired=%s)\n", paired_val);
        return -1;
    }
    char server_cert_hex[8192] = {0};
    xml_find(resp, "plaincert", server_cert_hex, sizeof(server_cert_hex));

    /* Parse server cert (hex-encoded DER in <plaincert>) */
    unsigned char server_cert_der[4096];
    size_t server_cert_len = 0;
    hex2bin(server_cert_hex, server_cert_der, &server_cert_len);
    mbedtls_x509_crt srv_cert;
    mbedtls_x509_crt_init(&srv_cert);
    mbedtls_x509_crt_parse_der(&srv_cert, server_cert_der, server_cert_len);

    /* AES key = SHA256(salt || pin)[0:16] */
    size_t pin_len = strlen(pin);
    unsigned char salt_pin[32];
    memcpy(salt_pin, salt, 16);
    memcpy(salt_pin + 16, pin, pin_len);
    unsigned char aes_key_full[32];
    mbedtls_md(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256),
               salt_pin, 16 + pin_len, aes_key_full);
    unsigned char aes_key[16];
    memcpy(aes_key, aes_key_full, 16);

    /* --- Phase 2: clientchallenge (HTTP) --- */
    unsigned char random_challenge[16];
    mbedtls_ctr_drbg_random(&s_ctr_drbg, random_challenge, sizeof(random_challenge));
    unsigned char enc_challenge[16];
    aes_cbc_encrypt(aes_key, 128, random_challenge, 16, enc_challenge);
    char challenge_hex[33]; bin2hex(enc_challenge, 16, challenge_hex);

    snprintf(url, sizeof(url),
             "/pair?uniqueid=%s&devicename=PS5-Moonlight&updateState=1"
             "&clientchallenge=%s", s_unique_id, challenge_hex);
    if (http_get(server->address, server->httpPort, url, resp, sizeof(resp)) != 0) {
        printf("gs_pair phase2: request failed\n");
        mbedtls_x509_crt_free(&srv_cert); return -1;
    }
    memset(paired_val, 0, sizeof(paired_val));
    xml_find(resp, "paired", paired_val, sizeof(paired_val));
    if (strcmp(paired_val, "1") != 0) {
        printf("gs_pair phase2: rejected\n");
        mbedtls_x509_crt_free(&srv_cert); return -1;
    }
    char challengeresp_hex[1024] = {0};
    xml_find(resp, "challengeresponse", challengeresp_hex, sizeof(challengeresp_hex));

    /* Decrypt server challenge response:
     * dec = SHA256_hash(32) || server_challenge(16) */
    unsigned char challengeresp_enc[64] = {0};
    size_t challengeresp_len = 0;
    hex2bin(challengeresp_hex, challengeresp_enc, &challengeresp_len);
    unsigned char challengeresp[64] = {0};
    aes_cbc_decrypt(aes_key, 128, challengeresp_enc, challengeresp_len, challengeresp);

    /* server_challenge is at offset 32 (after the 32-byte hash) */
    unsigned char server_challenge[16];
    memcpy(server_challenge, challengeresp + 32, 16);

    /* --- Phase 3: serverchallengeresp (HTTP) --- */
    /* New 16-byte client secret (NOT the phase2 challenge) */
    unsigned char client_secret[16];
    mbedtls_ctr_drbg_random(&s_ctr_drbg, client_secret, sizeof(client_secret));

    /* Use the EXISTING signature bytes from our X.509 cert (not a new RSA sign) */
    const unsigned char *our_cert_sig     = s_client_cert.MBEDTLS_PRIVATE(sig).p;
    size_t               our_cert_sig_len = s_client_cert.MBEDTLS_PRIVATE(sig).len;
    printf("gs_pair phase3: cert_sig_len=%zu\n", our_cert_sig_len);

    /* challengeRespHash = SHA256(server_challenge || our_cert_sig || client_secret) */
    unsigned char hash_input[16 + 512 + 16];
    size_t hi_len = 0;
    memcpy(hash_input + hi_len, server_challenge,  16);             hi_len += 16;
    memcpy(hash_input + hi_len, our_cert_sig, our_cert_sig_len);   hi_len += our_cert_sig_len;
    memcpy(hash_input + hi_len, client_secret,     16);             hi_len += 16;
    unsigned char challenge_resp_hash[32];
    mbedtls_md(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256),
               hash_input, hi_len, challenge_resp_hash);

    /* Encrypt 32 bytes (2 AES blocks) */
    unsigned char enc_resp[32];
    aes_cbc_encrypt(aes_key, 128, challenge_resp_hash, 32, enc_resp);
    char enc_resp_hex[65]; bin2hex(enc_resp, 32, enc_resp_hex);

    snprintf(url, sizeof(url),
             "/pair?uniqueid=%s&devicename=PS5-Moonlight&updateState=1"
             "&serverchallengeresp=%s", s_unique_id, enc_resp_hex);
    if (http_get(server->address, server->httpPort, url, resp, sizeof(resp)) != 0) {
        printf("gs_pair phase3: request failed\n");
        mbedtls_x509_crt_free(&srv_cert); return -1;
    }
    memset(paired_val, 0, sizeof(paired_val));
    xml_find(resp, "paired", paired_val, sizeof(paired_val));
    if (strcmp(paired_val, "1") != 0) {
        printf("gs_pair phase3: rejected\n");
        mbedtls_x509_crt_free(&srv_cert); return -1;
    }
    char pairing_secret_hex[1024] = {0};
    xml_find(resp, "pairingsecret", pairing_secret_hex, sizeof(pairing_secret_hex));

    /* --- Phase 4: clientpairingsecret (HTTP) --- */
    /* pairingsecret = serverSecret(16) || serverSig(256) — hex encoded, not AES */
    /* Client sends: client_secret(16) || SHA256withRSA_sign(client_secret) */
    unsigned char ps_hash[32];
    mbedtls_md(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256),
               client_secret, 16, ps_hash);
    unsigned char client_ps_sig[512];
    size_t client_ps_sig_len = 0;
    mbedtls_pk_sign(&s_client_key, MBEDTLS_MD_SHA256,
                    ps_hash, 32,
                    client_ps_sig, sizeof(client_ps_sig), &client_ps_sig_len,
                    mbedtls_ctr_drbg_random, &s_ctr_drbg);

    unsigned char client_pairing_secret[16 + 512];
    memcpy(client_pairing_secret, client_secret, 16);
    memcpy(client_pairing_secret + 16, client_ps_sig, client_ps_sig_len);
    char client_pairing_secret_hex[2048];
    bin2hex(client_pairing_secret, 16 + client_ps_sig_len, client_pairing_secret_hex);

    snprintf(url, sizeof(url),
             "/pair?uniqueid=%s&devicename=PS5-Moonlight&updateState=1"
             "&clientpairingsecret=%s", s_unique_id, client_pairing_secret_hex);
    if (http_get(server->address, server->httpPort, url, resp, sizeof(resp)) != 0) {
        printf("gs_pair phase4: request failed\n");
        mbedtls_x509_crt_free(&srv_cert); return -1;
    }
    memset(paired_val, 0, sizeof(paired_val));
    xml_find(resp, "paired", paired_val, sizeof(paired_val));
    if (strcmp(paired_val, "1") != 0) {
        printf("gs_pair phase4: rejected\n");
        mbedtls_x509_crt_free(&srv_cert); return -1;
    }

    /* --- Phase 5: pairchallenge (HTTPS) --- */
    snprintf(url, sizeof(url),
             "/pair?uniqueid=%s&devicename=PS5-Moonlight&updateState=1"
             "&phrase=pairchallenge", s_unique_id);
    if (https_get(server->address, server->httpsPort, url,
                  s_cert_pem, s_key_pem, resp, sizeof(resp)) != 0) {
        printf("gs_pair phase5: request failed\n");
        mbedtls_x509_crt_free(&srv_cert); return -1;
    }
    memset(paired_val, 0, sizeof(paired_val));
    xml_find(resp, "paired", paired_val, sizeof(paired_val));
    if (strcmp(paired_val, "1") != 0) {
        printf("gs_pair phase5: rejected\n");
        mbedtls_x509_crt_free(&srv_cert); return -1;
    }

    strncpy(server->serverCertHex, server_cert_hex, sizeof(server->serverCertHex) - 1);
    mbedtls_x509_crt_free(&srv_cert);
    printf("gs_pair: paired successfully!\n");
    server->paired = true;
    return 0;
}

/* ── App list (debug) ────────────────────────────────────────────────────── */
void gs_print_applist(GS_SERVER *server) {
    char url[512], resp[65536];
    snprintf(url, sizeof(url), "/applist?uniqueid=%s", s_unique_id);
    if (https_get(server->address, server->httpsPort, url,
                  s_cert_pem, s_key_pem, resp, sizeof(resp)) != 0) {
        printf("gs_applist: request failed\n"); return;
    }
    printf("gs_applist: %s\n", resp);
    fflush(stdout);
}

/* ── Launch app ──────────────────────────────────────────────────────────── */
int gs_start_app(GS_SERVER *server, PSTREAM_CONFIGURATION config, int appId) {
    char url[2048], resp[65536];

    /* Generate session key (rikey) */
    unsigned char rikey[16];
    mbedtls_ctr_drbg_random(&s_ctr_drbg, rikey, sizeof(rikey));
    char rikey_hex[33]; bin2hex(rikey, 16, rikey_hex);

    static int rikeyid = 0;
    rikeyid++;

    snprintf(url, sizeof(url),
             "/launch?uniqueid=%s&appid=%d"
             "&mode=%dx%dx%d&additionalStates=1&sops=0&rikey=%s&rikeyid=%d"
             "&localAudioPlayMode=0&surroundAudioInfo=196610"
             "&remoteControllersBitmap=15&gcmap=15",
             s_unique_id, appId,
             config->width, config->height, config->fps,
             rikey_hex, rikeyid);

    if (https_get(server->address, server->httpsPort, url,
                  s_cert_pem, s_key_pem, resp, sizeof(resp)) != 0) {
        fprintf(stderr, "gs_start_app: HTTPS request failed\n"); return -1;
    }
    printf("gs_start_app resp: %.512s\n", resp);
    fflush(stdout);

    memset(server->rtspSessionUrl, 0, sizeof(server->rtspSessionUrl));
    xml_find(resp, "sessionUrl0", server->rtspSessionUrl, sizeof(server->rtspSessionUrl));

    /* Populate SERVER_INFORMATION */
    server->serverInfo.address              = server->address;
    server->serverInfo.serverInfoAppVersion = server->appVersion;
    server->serverInfo.serverInfoGfeVersion = server->gfeVersion;
    server->serverInfo.rtspSessionUrl       = server->rtspSessionUrl[0]
                                              ? server->rtspSessionUrl : NULL;

    /* Set the rikey in config */
    memcpy(config->remoteInputAesKey, rikey, 16);
    config->remoteInputAesIv[0] = (char)(rikeyid >> 24);
    config->remoteInputAesIv[1] = (char)(rikeyid >> 16);
    config->remoteInputAesIv[2] = (char)(rikeyid >> 8);
    config->remoteInputAesIv[3] = (char)(rikeyid);

    printf("gs_start_app: launched appId=%d sessionUrl=%s\n", appId, server->rtspSessionUrl);
    return 0;
}

int gs_quit_app(GS_SERVER *server) {
    char url[512], resp[4096];
    snprintf(url, sizeof(url), "/cancel?uniqueid=%s", s_unique_id);
    https_get(server->address, server->httpsPort, url,
              s_cert_pem, s_key_pem, resp, sizeof(resp));
    return 0;
}
