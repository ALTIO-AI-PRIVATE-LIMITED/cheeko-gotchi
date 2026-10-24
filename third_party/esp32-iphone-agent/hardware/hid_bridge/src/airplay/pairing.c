/*
 * AirPlay legacy pairing (pair-setup / pair-verify), ported from UxPlay lib/pairing.c and
 * lib/crypto.c. Copyright (C) 2018 Juho Vähä-Herttua, (C) 2020 Jaslo Ziska,
 * (C) 2021-2023 F. Duncanh; ESP32 port (libsodium + mbedTLS instead of OpenSSL) 2026.
 *
 * This program is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software Foundation,
 * either version 3 of the License, or (at your option) any later version. See LICENSE-GPL.md.
 */
#include <string.h>
#include <sodium.h>
#include "mbedtls/sha512.h"
#include "mbedtls/aes.h"
#include "esp_random.h"
#include "ap_internal.h"

#define SALT_KEY "Pair-Verify-AES-Key"
#define SALT_IV "Pair-Verify-AES-IV"

uint8_t ap_pk[32];
uint8_t ap_sk[64];

bool ap_crypto_init(const uint8_t seed[32]) {
  if (sodium_init() < 0) return false;
  return crypto_sign_seed_keypair(ap_pk, ap_sk, seed) == 0;
}

void ap_sha512(const void *a, size_t an, const void *b, size_t bn, uint8_t out[64]) {
  mbedtls_sha512_context ctx;
  mbedtls_sha512_init(&ctx);
  mbedtls_sha512_starts(&ctx, 0);
  mbedtls_sha512_update(&ctx, a, an);
  if (bn) mbedtls_sha512_update(&ctx, b, bn);
  mbedtls_sha512_finish(&ctx, out);
  mbedtls_sha512_free(&ctx);
}

// AES-128-CTR over `len` bytes of a keystream derived from the shared secret, after
// skipping `skip` bytes (pair-verify step 2 skips the server's own signature block).
static void verify_ctr(const ap_pairing *p, size_t skip, uint8_t *buf, size_t len) {
  uint8_t h[64], key[16], iv[16], stream[16], waste[64] = {0};
  size_t off = 0;
  ap_sha512(SALT_KEY, strlen(SALT_KEY), p->secret, 32, h);
  memcpy(key, h, 16);
  ap_sha512(SALT_IV, strlen(SALT_IV), p->secret, 32, h);
  memcpy(iv, h, 16);
  mbedtls_aes_context aes;
  mbedtls_aes_init(&aes);
  mbedtls_aes_setkey_enc(&aes, key, 128);
  if (skip) mbedtls_aes_crypt_ctr(&aes, skip, &off, iv, stream, waste, waste);
  mbedtls_aes_crypt_ctr(&aes, len, &off, iv, stream, buf, buf);
  mbedtls_aes_free(&aes);
}

bool ap_pair_verify1(ap_pairing *p, const uint8_t their_ecdh[32], const uint8_t their_ed[32], uint8_t out[96]) {
  uint8_t msg[64];
  if (p->status == PAIR_FINISHED) return false;
  memcpy(p->their_pub, their_ecdh, 32);
  memcpy(p->their_ed, their_ed, 32);
  esp_fill_random(p->our_priv, 32);
  crypto_scalarmult_base(p->our_pub, p->our_priv);
  if (crypto_scalarmult(p->secret, p->our_priv, p->their_pub) != 0) return false;
  p->status = PAIR_HANDSHAKE;

  // sign both ECDH public keys (ours first), then encrypt the signature
  memcpy(msg, p->our_pub, 32);
  memcpy(msg + 32, p->their_pub, 32);
  memcpy(out, p->our_pub, 32);
  crypto_sign_detached(out + 32, NULL, msg, 64, ap_sk);
  verify_ctr(p, 0, out + 32, 64);
  return true;
}

bool ap_pair_verify2(ap_pairing *p, const uint8_t sig[64]) {
  uint8_t msg[64], s[64];
  if (p->status != PAIR_HANDSHAKE) return false;
  memcpy(s, sig, 64);
  verify_ctr(p, 64, s, 64);  // one "fake round" for the handshake signature, as in UxPlay
  memcpy(msg, p->their_pub, 32);
  memcpy(msg + 32, p->our_pub, 32);
  if (crypto_sign_verify_detached(s, msg, 64, p->their_ed) != 0) return false;
  p->status = PAIR_FINISHED;
  return true;
}
