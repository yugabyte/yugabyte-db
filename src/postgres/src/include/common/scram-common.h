/*-------------------------------------------------------------------------
 *
 * scram-common.h
 *		Declarations for helper functions used for SCRAM authentication
 *
 * Portions Copyright (c) 1996-2022, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * src/include/common/scram-common.h
 *
 *-------------------------------------------------------------------------
 */
#ifndef SCRAM_COMMON_H
#define SCRAM_COMMON_H

#include "common/cryptohash.h"
#include "common/sha2.h"

/* Name of SCRAM mechanisms per IANA */
#define SCRAM_SHA_256_NAME "SCRAM-SHA-256"
#define SCRAM_SHA_256_PLUS_NAME "SCRAM-SHA-256-PLUS"	/* with channel binding */

/* Length of SCRAM keys (client and server) */
#define SCRAM_KEY_LEN				PG_SHA256_DIGEST_LENGTH

/* length of HMAC */
#define SHA256_HMAC_B				PG_SHA256_BLOCK_LENGTH

/*
 * Size of random nonce generated in the authentication exchange.  This
 * is in "raw" number of bytes, the actual nonces sent over the wire are
 * encoded using only ASCII-printable characters.
 */
#define SCRAM_RAW_NONCE_LEN			18

/*
 * Length of salt when generating new secrets, in bytes.  (It will be stored
 * and sent over the wire encoded in Base64.)  16 bytes is what the example in
 * RFC 7677 uses.
 */
#define SCRAM_DEFAULT_SALT_LEN		16

/*
 * Default number of iterations when generating secret.  Should be at least
 * 4096 per RFC 7677.
 */
#define SCRAM_DEFAULT_ITERATIONS	4096

extern int	scram_SaltedPassword(const char *password, const char *salt,
								 int saltlen, int iterations, uint8 *result,
								 const char **errstr);
extern int	scram_H(const uint8 *str, int len, uint8 *result,
					const char **errstr);
extern int	scram_ClientKey(const uint8 *salted_password, uint8 *result,
							const char **errstr);
extern int	scram_ServerKey(const uint8 *salted_password, uint8 *result,
							const char **errstr);

extern char *scram_build_secret(const char *salt, int saltlen, int iterations,
								const char *password, const char **errstr);

#endif							/* SCRAM_COMMON_H */
