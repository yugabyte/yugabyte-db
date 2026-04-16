#ifndef ODYSSEY_SCRAM_H
#define ODYSSEY_SCRAM_H

/*
 * Odyssey.
 *
 * Scalable PostgreSQL connection pooler.
 */

#if PG_VERSION_NUM >= 130000
#define od_b64_encode(src, src_len, dst, dst_len) \
	pg_b64_encode(src, src_len, dst, dst_len);
#define od_b64_decode(src, src_len, dst, dst_len) \
	pg_b64_decode(src, src_len, dst, dst_len);
#else
#define od_b64_encode(src, src_len, dst, dst_len) \
	pg_b64_encode(src, src_len, dst);
#define od_b64_decode(src, src_len, dst, dst_len) \
	pg_b64_decode(src, src_len, dst);
#endif

#if PG_VERSION_NUM < 140000
typedef scram_HMAC_ctx od_scram_ctx_t;

#define od_scram_HMAC_init scram_HMAC_init
#define od_scram_HMAC_create() malloc(sizeof(od_scram_ctx_t))
#define od_scram_HMAC_update scram_HMAC_update
#define od_scram_HMAC_final scram_HMAC_final
#define od_scram_HMAC_free(ctx) free(ctx)

#else

struct pg_hmac_ctx {
	pg_cryptohash_ctx *hash;
	pg_cryptohash_type type;
	int block_size;
	int digest_size;

	/*
	 * Use the largest block size among supported options.  This wastes some
	 * memory but simplifies the allocation logic.
	 */
	uint8 k_ipad[PG_SHA512_BLOCK_LENGTH];
	uint8 k_opad[PG_SHA512_BLOCK_LENGTH];
};

typedef struct pg_hmac_ctx od_scram_ctx_t;

#define od_scram_HMAC_init pg_hmac_init
#define od_scram_HMAC_create() pg_hmac_create(PG_SHA256)
#define od_scram_HMAC_update(ctx, str, slen) \
	pg_hmac_update(ctx, (const uint8_t *)str, slen)
#define od_scram_HMAC_final(dest, ctx) pg_hmac_final(ctx, dest, sizeof(dest))
#define od_scram_HMAC_free pg_hmac_free

#endif

#if PG_VERSION_NUM < 150000

#define od_scram_H(input, len, result, errstr) scram_H(input, len, result)
#define od_scram_ServerKey(salted_password, result, errstr) \
	scram_ServerKey(salted_password, result)
#define od_scram_SaltedPassword(password, salt, saltlen, iterations, result, \
				errstr)                                      \
	scram_SaltedPassword(password, salt, saltlen, iterations, result)
#define od_scram_ClientKey(salted_password, result, errstr) \
	scram_ClientKey(salted_password, result)

#else

#define od_scram_H(input, len, result, errstr) \
	scram_H(input, len, result, errstr)
#define od_scram_ServerKey(salted_password, result, errstr) \
	scram_ServerKey(salted_password, result, errstr)
#define od_scram_SaltedPassword(password, salt, saltlen, iterations, result, \
				errstr)                                      \
	scram_SaltedPassword(password, salt, saltlen, iterations, result,    \
			     errstr)
#define od_scram_ClientKey(salted_password, result, errstr) \
	scram_ClientKey(salted_password, result, errstr)

#endif

typedef struct od_scram_state od_scram_state_t;

struct od_scram_state {
	char *client_nonce;
	char *client_first_message;
	char *client_final_message;

	char *server_nonce;
	char *server_first_message;

	uint8_t *salted_password;
	int iterations;
	char *salt;

	uint8_t stored_key[32];
	uint8_t server_key[32];
};

static inline void od_scram_state_init(od_scram_state_t *state)
{
	memset(state, 0, sizeof(*state));
}

static inline void od_scram_state_free(od_scram_state_t *state)
{
	free(state->client_nonce);
	free(state->client_first_message);
	free(state->client_final_message);
	if (state->server_nonce)
		free(state->server_nonce);
	if (state->server_first_message)
		free(state->server_first_message);
	free(state->salted_password);
	free(state->salt);

	memset(state, 0, sizeof(*state));
}

machine_msg_t *
od_scram_create_client_first_message(od_scram_state_t *scram_state);

machine_msg_t *
od_scram_create_client_final_message(od_scram_state_t *scram_state,
				     char *password, char *auth_data,
				     size_t auth_data_size);

machine_msg_t *
od_scram_create_server_first_message(od_scram_state_t *scram_state);

machine_msg_t *
od_scram_create_server_final_message(od_scram_state_t *scram_state);

int od_scram_verify_server_signature(od_scram_state_t *scram_state,
				     char *auth_data, size_t auth_data_size);

int od_scram_verify_final_nonce(od_scram_state_t *scram_state,
				char *final_nonce, size_t final_nonce_size);

int od_scram_verify_client_proof(od_scram_state_t *scram_state,
				 char *client_proof);

int od_scram_parse_verifier(od_scram_state_t *scram_state, char *verifier);

int od_scram_init_from_plain_password(od_scram_state_t *scram_state,
				      char *plain_password);

int od_scram_read_client_first_message(od_scram_state_t *scram_state,
				       char *auth_data, size_t auth_data_size);

int od_scram_read_client_final_message(od_scram_state_t *scram_state,
				       char *auth_data, size_t auth_data_size,
				       char **final_nonce_ptr,
				       size_t *final_nonce_size_ptr,
				       char **proof_ptr);

#endif /* ODYSSEY_SCRAM_H */
