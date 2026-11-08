/* PS5Term owned SSH profile. GPL-3.0-or-later.
 * Portable C crypto and optional upstream SFTP; no payload, shell, filesystem,
 * forwarding or SCP/SFTP implementation is imported from ps5sshd. */
#ifndef PS5TERM_OWNED_SSH_SETTINGS_H
#define PS5TERM_OWNED_SSH_SETTINGS_H
#define SINGLE_THREADED
#define WOLFCRYPT_ONLY
#define WOLFSSL_WOLFSSH
#define WOLFSSH_USER_IO
#define NO_WOLFSSH_CLIENT
#define WOLFSSH_SHELL
#define WOLFSSH_MAX_SESSION_CHANNELS 4
#if defined(PS5TERM_OWNED_SFTP)
#define WOLFSSH_SFTP
#define WOLFSSH_MAX_SFTP_HANDLES 4
#define WOLFSSH_MAX_SFTP_RW 32768
#define WOLFSSH_MAX_SFTP_RECV 32768
#define WOLFSSH_MAX_SFTP_NAME 65536
/* Matches the native shell's path limit; 256 broke deep console paths. */
#define WOLFSSH_MAX_FILENAME 1024
#define SIZEOF_OFF_T 8
#else
#define NO_FILESYSTEM
#define NO_WOLFSSH_DIR
#endif
#define WOLFSSL_BASE64_ENCODE
#define WOLFSSL_ASN_TEMPLATE
#define WOLFSSL_SP_MATH_ALL
#define SP_WORD_SIZE 64
#if !defined(__SIZEOF_INT128__)
#error Owned SSH host profile requires a compiler with 128-bit integer support
#endif
#define HAVE___UINT128_T
#define WOLFSSL_HAVE_SP_ECC
#define WOLFSSL_HAVE_SP_DH
#define WOLFSSL_SP_384
#define HAVE_ECC
#define HAVE_ECC384
#define ECC_TIMING_RESISTANT
#define ECC_SHAMIR
#define TFM_TIMING_RESISTANT
#define HAVE_ED25519
#define WOLFSSL_ED25519_STREAMING_VERIFY
#define HAVE_CURVE25519
/* Standard hybrid KEX used by OpenSSH 9.9+. Portable C only, with existing
 * classical algorithms retained for clients that do not implement ML-KEM. */
#define WOLFSSL_HAVE_MLKEM
#define WOLFSSL_NO_ML_KEM_512
#define WOLFSSL_NO_ML_KEM_1024
#define WOLFSSH_NO_NISTP256_MLKEM768_SHA256
#define WOLFSSH_NO_NISTP384_MLKEM1024_SHA384
#define WOLFSSL_SHA3
#define WOLFSSL_SHAKE128
#define WOLFSSL_SHAKE256
#define HAVE_HASHDRBG
/* Host profile only: use OS entropy explicitly, never a deterministic fallback.
 * The PS5 service must supply and validate its own native entropy adapter. */
#define CUSTOM_RAND_GENERATE_SEED ps5term_host_seed
#ifdef __cplusplus
extern "C" {
#endif
int ps5term_host_seed(unsigned char *, unsigned int);
#ifdef __cplusplus
}
#endif
#define HAVE_AESGCM
#define WOLFSSL_AES_COUNTER
#define WOLFSSL_SHA384
#define WOLFSSL_SHA512
#define WOLFSSL_IGNORE_FILE_WARN
#define HAVE_STRINGS_H
#define WC_NO_HARDEN_WARNING
#define NO_WOLFSSL_DIR
#define NO_RSA
#define NO_DSA
#define NO_DES3
#define NO_MD4
#define NO_MD5
#define NO_RC4
#define NO_PSK
#define NO_PKCS12
#define NO_PWDBASED
#define WC_NO_ASYNC_THREADING
#define DEFAULT_WINDOW_SZ (64 * 1024)
#define WINDOW_SZ_UPPER_BOUND (256 * 1024)
#endif
