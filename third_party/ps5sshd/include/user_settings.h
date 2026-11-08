/* Shared wolfSSL/wolfCrypt + wolfSSH configuration for the PS5 build. */
#ifndef PS5SSHD_USER_SETTINGS_H
#define PS5SSHD_USER_SETTINGS_H

/* platform */
#define SINGLE_THREADED
#define WOLFSSL_NO_SOCK
#define NO_WRITEV
#define WOLFSSL_IGNORE_FILE_WARN
#define NO_WOLFSSL_DIR
#define WC_NO_HARDEN_WARNING

/* wolfCrypt only, tuned for wolfSSH */
#define WOLFSSL_WOLFSSH
#define WOLFCRYPT_ONLY
#define WOLFSSL_ASN_TEMPLATE
#define WOLFSSL_PUBLIC_MP
#define WOLFSSL_BASE64_ENCODE
#define WOLFSSL_KEY_GEN
#define HAVE_HASHDRBG
#define NO_ERROR_STRINGS

/* side channel hardening */
#define TFM_TIMING_RESISTANT
#define ECC_TIMING_RESISTANT
#define WC_RSA_BLINDING

/* math */
#define WOLFSSL_SP_MATH_ALL
#ifndef PS5SSHD_HOST
#define WOLFSSL_SP_X86_64
#define WOLFSSL_SP_X86_64_ASM
#define SP_WORD_SIZE 64
#endif
#define WOLFSSL_HAVE_SP_RSA
#define WOLFSSL_HAVE_SP_DH
#define WOLFSSL_HAVE_SP_ECC
#define WOLFSSL_SP_4096
#define WOLFSSL_SP_384
#define WOLFSSL_SP_521

/* public key */
#define WC_NO_RSA_OAEP
#define WC_RSA_PSS
#define HAVE_ECC
#define ECC_SHAMIR
#define HAVE_ECC384
#define HAVE_ECC521
#define HAVE_CURVE25519
#define HAVE_ED25519
#define WOLFSSL_ED25519_STREAMING_VERIFY
#define WOLFSSL_HAVE_MLKEM
#define WOLFSSL_WC_MLKEM

/* hashes */
#define WOLFSSL_SHA384
#define WOLFSSL_SHA512
#define WOLFSSL_SHA3
#define WOLFSSL_SHAKE128
#define WOLFSSL_SHAKE256

/* symmetric */
#define HAVE_AESGCM
#define WOLFSSL_AES_COUNTER
#ifndef PS5SSHD_HOST
#define WOLFSSL_X86_64_BUILD
#define WOLFSSL_AESNI
#define HAVE_INTEL_AVX1
#define HAVE_INTEL_AVX2
#endif

/* not needed */
#define NO_DSA
#define NO_DES3
#define NO_MD4
#define NO_MD5
#define NO_RC4
#define NO_PSK
#define NO_PKCS12
#define NO_PWDBASED
#define NO_OLD_TLS
#define WOLFSSL_NO_TLS12
#define NO_DES3_TLS_SUITES
#define WC_NO_ASYNC_THREADING

/* wolfSSH */
#define WOLFSSH_SFTP
#define WOLFSSH_SCP
#define WOLFSSH_TERM
#define WOLFSSH_SHELL
#define WOLFSSH_KEYGEN
#define WOLFSSH_MAX_SFTP_RW (256 * 1024)
#define DEFAULT_WINDOW_SZ (2 * 1024 * 1024)
#define WINDOW_SZ_UPPER_BOUND (16 * 1024 * 1024)
#define NO_WOLFSSH_CLIENT

#endif
