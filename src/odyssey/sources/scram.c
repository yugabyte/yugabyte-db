
/*
 * Odyssey.
 *
 * Scalable PostgreSQL connection pooler.
 */

#include <kiwi.h>
#include <machinarium.h>
#include <odyssey.h>

int od_scram_parse_verifier(od_scram_state_t *scram_state, char *verifier)
{
	char *value = NULL;
	char *scheme = NULL;
	char *iterations_raw = NULL;
	char *salt_raw = NULL;
	char *stored_key_raw = NULL;
	char *server_key_raw = NULL;
	char *salt = NULL;
	char *stored_key = NULL;
	char *server_key = NULL;

	value = strdup(verifier);
	if (value == NULL)
		return -1;

	scheme = strtok(value, "$");
	if (scheme == NULL)
		goto error;

	iterations_raw = strtok(NULL, ":");
	if (iterations_raw == NULL)
		goto error;

	salt_raw = strtok(NULL, "$");
	if (salt_raw == NULL)
		goto error;

	stored_key_raw = strtok(NULL, ":");
	if (stored_key_raw == NULL)
		goto error;

	server_key_raw = strtok(NULL, "");
	if (server_key_raw == NULL)
		goto error;

	if (strcmp(scheme, "SCRAM-SHA-256") != 0)
		goto error;

	char *end;
	int iterations = strtol(iterations_raw, &end, 10);
	if (*end != '\0' || iterations < 1)
		goto error;

	scram_state->iterations = iterations;

	int salt_raw_len = strlen(salt_raw);
	int salt_dst_len = pg_b64_dec_len(salt_raw_len);
	salt = malloc(salt_dst_len);
	if (salt == NULL)
		goto error;

	int salt_len =
		od_b64_decode(salt_raw, salt_raw_len, salt, salt_dst_len);
	free(salt);

	if (salt_len < 0)
		goto error;

	scram_state->salt = strdup(salt_raw);

	if (scram_state->salt == NULL)
		goto error;

	int stored_key_raw_len = strlen(stored_key_raw);
	int stored_key_dst_len = pg_b64_dec_len(stored_key_raw_len);
	stored_key = malloc(stored_key_dst_len);
	if (stored_key == NULL)
		goto error;

	int stored_key_len = od_b64_decode(stored_key_raw, stored_key_raw_len,
					   stored_key, stored_key_dst_len);
	if (stored_key_len != SCRAM_KEY_LEN)
		goto error;

	memcpy(scram_state->stored_key, stored_key, SCRAM_KEY_LEN);

	int server_key_raw_len = strlen(server_key_raw);
	int server_key_dst_len = pg_b64_dec_len(server_key_raw_len);
	server_key = malloc(server_key_dst_len);
	if (server_key == NULL)
		goto error;

	int server_key_len = od_b64_decode(server_key_raw, server_key_raw_len,
					   server_key, server_key_dst_len);
	if (server_key_len != SCRAM_KEY_LEN)
		goto error;

	memcpy(scram_state->server_key, server_key, SCRAM_KEY_LEN);

	free(stored_key);
	free(server_key);
	free(value);

	return 0;

error:

	free(stored_key);
	free(server_key);
	free(value);
	scram_state->salt = NULL;

	return -1;
}

int od_scram_init_from_plain_password(od_scram_state_t *scram_state,
				      char *plain_password)
{
	char *prep_password = NULL;

	pg_saslprep_rc rc = pg_saslprep(plain_password, &prep_password);
	if (rc == SASLPREP_OOM)
		goto error;

	const char *password;
	if (rc == SASLPREP_SUCCESS)
		password = prep_password;
	else
		password = plain_password;

	char salt[SCRAM_DEFAULT_SALT_LEN];
	RAND_bytes((uint8_t *)salt, sizeof(salt));

	scram_state->iterations = SCRAM_DEFAULT_ITERATIONS;

	int salt_dst_len = pg_b64_enc_len(sizeof(salt)) + 1;
	scram_state->salt = malloc(salt_dst_len);
	if (!scram_state->salt)
		goto error;

	int base64_salt_len = od_b64_encode(salt, sizeof(salt),
					    scram_state->salt, salt_dst_len);
	scram_state->salt[base64_salt_len] = '\0';
	const char *errstr = NULL;

	uint8_t salted_password[SCRAM_KEY_LEN];
	od_scram_SaltedPassword(password, salt, sizeof(salt),
				scram_state->iterations, salted_password,
				&errstr);
	od_scram_ClientKey(salted_password, scram_state->stored_key, &errstr);
	od_scram_H(scram_state->stored_key, SCRAM_KEY_LEN,
		   scram_state->stored_key, &errstr);
	od_scram_ServerKey(salted_password, scram_state->server_key, &errstr);

	if (prep_password)
		free(prep_password);

	return 0;

error:

	if (prep_password)
		free(prep_password);

	return -1;
}

machine_msg_t *
od_scram_create_client_first_message(od_scram_state_t *scram_state)
{
	uint8_t nonce[SCRAM_RAW_NONCE_LEN];
	RAND_bytes(nonce, SCRAM_RAW_NONCE_LEN);

	int client_nonce_dst_len = pg_b64_enc_len(SCRAM_RAW_NONCE_LEN) + 1;
	scram_state->client_nonce = malloc(client_nonce_dst_len);
	if (scram_state->client_nonce == NULL)
		return NULL;

	int base64_nonce_len =
		od_b64_encode((char *)nonce, SCRAM_RAW_NONCE_LEN,
			      scram_state->client_nonce, client_nonce_dst_len);
	scram_state->client_nonce[base64_nonce_len] = '\0';

	size_t result_len = strlen("n,,n=,r=") + base64_nonce_len;
	char *result = malloc(result_len + 1);
	if (result == NULL)
		goto error;

	od_snprintf(result, result_len + 1, "n,,n=,r=%s",
		    scram_state->client_nonce);

	scram_state->client_first_message = strdup(result + 3);
	if (scram_state->client_first_message == NULL)
		goto error;

	machine_msg_t *msg = kiwi_fe_write_authentication_sasl_initial(
		NULL, "SCRAM-SHA-256", result, result_len);
	if (msg == NULL)
		goto error;

	free(result);
	return msg;

error:

	free(result);
	free(scram_state->client_nonce);
	free(scram_state->client_first_message);

	return NULL;
}

int read_server_first_message(od_scram_state_t *scram_state, char *auth_data,
			      size_t auth_data_size, char **server_nonce_ptr,
			      size_t *server_nonce_size_ptr, char **salt_ptr,
			      int *iterations_ptr)
{
	scram_state->server_first_message =
		od_strdup_from_buf(auth_data, auth_data_size);
	if (scram_state->server_first_message == NULL)
		return -1;

	char *server_nonce;
	size_t server_nonce_size;
	char *salt = NULL;
	if (read_attribute_buf(&auth_data, &auth_data_size, 'r', &server_nonce,
			       &server_nonce_size))
		goto error;

	char *client_nonce = scram_state->client_nonce;
	size_t client_nonce_len = strlen(client_nonce);
	if (server_nonce_size < client_nonce_len ||
	    memcmp(server_nonce, client_nonce, client_nonce_len) != 0)
		goto error;

	char *base64_salt;
	size_t base64_salt_size;
	if (read_attribute_buf(&auth_data, &auth_data_size, 's', &base64_salt,
			       &base64_salt_size))
		goto error;

	int salt_dst_len = pg_b64_dec_len(base64_salt_size) + 1;
	salt = malloc(salt_dst_len);
	if (salt == NULL)
		goto error;

	int salt_len = od_b64_decode(base64_salt, base64_salt_size, salt,
				     salt_dst_len);
	if (salt_len < 0)
		goto error;

	salt[salt_len] = '\0';

	char *iterations_raw;
	size_t iterations_raw_size;
	if (read_attribute_buf(&auth_data, &auth_data_size, 'i',
			       &iterations_raw, &iterations_raw_size))
		goto error;

	char *end;
	int iterations =
		od_memtol(iterations_raw, iterations_raw_size, &end, 10);
	if (end != iterations_raw + iterations_raw_size || auth_data_size ||
	    iterations < 1)
		goto error;

	*server_nonce_ptr = server_nonce;
	*server_nonce_size_ptr = server_nonce_size;
	*salt_ptr = salt;
	*iterations_ptr = iterations;

	return 0;

error:

	free(scram_state->server_first_message);
	free(salt);
	return -1;
}

static int calculate_client_proof(od_scram_state_t *scram_state,
				  const char *password, const char *salt,
				  int iterations,
				  const char *client_final_message,
				  uint8_t *client_proof)
{
	char *prepared_password = NULL;
	pg_saslprep_rc rc = pg_saslprep(password, &prepared_password);

	if (rc == SASLPREP_OOM)
		return -1;

	if (rc != SASLPREP_SUCCESS)
		prepared_password = strdup(password);

	if (prepared_password == NULL)
		return -1;

	scram_state->salted_password = malloc(SCRAM_KEY_LEN);
	if (scram_state->salted_password == NULL)
		goto error;

	od_scram_ctx_t *ctx = od_scram_HMAC_create();
	const char *errstr = NULL;

	od_scram_SaltedPassword(prepared_password, salt, strlen(salt),
				iterations, scram_state->salted_password,
				&errstr);

	uint8_t client_key[SCRAM_KEY_LEN];
	od_scram_ClientKey(scram_state->salted_password, client_key, &errstr);

	uint8_t stored_key[SCRAM_KEY_LEN];
	od_scram_H(client_key, SCRAM_KEY_LEN, stored_key, &errstr);

	od_scram_HMAC_init(ctx, stored_key, SCRAM_KEY_LEN);

	od_scram_HMAC_update(ctx, scram_state->client_first_message,
			     strlen(scram_state->client_first_message));
	od_scram_HMAC_update(ctx, ",", 1);
	od_scram_HMAC_update(ctx, scram_state->server_first_message,
			     strlen(scram_state->server_first_message));
	od_scram_HMAC_update(ctx, ",", 1);
	od_scram_HMAC_update(ctx, client_final_message,
			     strlen(client_final_message));

	uint8_t client_signature[SCRAM_KEY_LEN];
	od_scram_HMAC_final(client_signature, ctx);

	for (int i = 0; i < SCRAM_KEY_LEN; i++)
		client_proof[i] = client_key[i] ^ client_signature[i];

	od_scram_HMAC_free(ctx);

	free(prepared_password);
	return 0;

error:

	free(prepared_password);
	return -1;
}

static char *calculate_server_signature(od_scram_state_t *scram_state)
{
	od_scram_ctx_t *ctx = od_scram_HMAC_create();

	od_scram_HMAC_init(ctx, scram_state->server_key, SCRAM_KEY_LEN);
	od_scram_HMAC_update(ctx, scram_state->client_first_message,
			     strlen(scram_state->client_first_message));
	od_scram_HMAC_update(ctx, ",", 1);
	od_scram_HMAC_update(ctx, scram_state->server_first_message,
			     strlen(scram_state->server_first_message));
	od_scram_HMAC_update(ctx, ",", 1);
	od_scram_HMAC_update(ctx, scram_state->client_final_message,
			     strlen(scram_state->client_final_message));

	uint8_t server_signature[SCRAM_KEY_LEN];
	od_scram_HMAC_final(server_signature, ctx);
	od_scram_HMAC_free(ctx);

	int base64_signature_dst_len = pg_b64_enc_len(SCRAM_KEY_LEN) + 1;
	char *base64_signature = malloc(base64_signature_dst_len);
	if (base64_signature == NULL)
		return NULL;

	int base64_signature_len =
		od_b64_encode((char *)server_signature, SCRAM_KEY_LEN,
			      base64_signature, base64_signature_dst_len);
	base64_signature[base64_signature_len] = '\0';

	return base64_signature;
}

machine_msg_t *
od_scram_create_client_final_message(od_scram_state_t *scram_state,
				     char *password, char *auth_data,
				     size_t auth_data_size)
{
	char *server_nonce;
	size_t server_nonce_size;
	char *salt;
	int iterations;

	int rc = read_server_first_message(scram_state, auth_data,
					   auth_data_size, &server_nonce,
					   &server_nonce_size, &salt,
					   &iterations);
	if (rc == -1)
		return NULL;

#define SCRAM_FINAL_MAX_SIZE 512
	char result[SCRAM_FINAL_MAX_SIZE];

	char attributes[] = "c=biws,r=";
	memcpy(result, attributes, sizeof(attributes) - 1);
	memcpy(result + sizeof(attributes) - 1, server_nonce,
	       server_nonce_size);
	result[sizeof(attributes) + server_nonce_size - 1] = '\0';

	scram_state->client_final_message = strdup(result);
	if (scram_state->client_final_message == NULL)
		return NULL;

	uint8_t client_proof[SCRAM_KEY_LEN];
	rc = calculate_client_proof(scram_state, password, salt, iterations,
				    result, client_proof);
	if (rc == -1)
		goto error;

	size_t size = 0;
	while (result[size] != '\0' && size < 508) {
		size++;
	}

	result[size++] = ',';
	result[size++] = 'p';
	result[size++] = '=';

	size += od_b64_encode((char *)client_proof, SCRAM_KEY_LEN,
			      result + size, SCRAM_FINAL_MAX_SIZE - size);
#undef SCRAM_FINAL_MAX_SIZE
	result[size] = '\0';

	machine_msg_t *msg =
		kiwi_fe_write_authentication_scram_final(NULL, result, size);
	if (msg == NULL)
		goto error;

	return msg;

error:

	free(scram_state->client_final_message);
	return NULL;
}

int read_server_final_message(char *auth_data, size_t auth_data_size,
			      char *server_signature)
{
	if (!auth_data_size || *auth_data == 'e')
		return -1;

	char *signature;
	size_t signature_size;
	if (read_attribute_buf(&auth_data, &auth_data_size, 'v', &signature,
			       &signature_size) ||
	    auth_data_size)
		return -1;

	int decoded_signature_len = pg_b64_dec_len(signature_size);
	char *decoded_signature = malloc(decoded_signature_len);
	if (decoded_signature == NULL)
		return -1;

	decoded_signature_len =
		od_b64_decode(signature, signature_size, decoded_signature,
			      decoded_signature_len);
	if (decoded_signature_len != SCRAM_KEY_LEN)
		goto error;

	memcpy(server_signature, decoded_signature, SCRAM_KEY_LEN);

	free(decoded_signature);
	return 0;

error:

	free(decoded_signature);
	return -1;
}

od_retcode_t od_scram_verify_server_signature(od_scram_state_t *scram_state,
					      char *auth_data,
					      size_t auth_data_size)
{
	char server_signature[SHA256_DIGEST_LENGTH];

	od_retcode_t rc = read_server_final_message(auth_data, auth_data_size,
						    server_signature);
	if (rc == NOT_OK_RESPONSE)
		return NOT_OK_RESPONSE;

	od_scram_ctx_t *ctx = od_scram_HMAC_create();

	const char *errstr = NULL;
	uint8_t server_key[SCRAM_KEY_LEN];
	od_scram_ServerKey(scram_state->salted_password, server_key, &errstr);
	od_scram_HMAC_init(ctx, server_key, SCRAM_KEY_LEN);

	od_scram_HMAC_update(ctx, scram_state->client_first_message,
			     strlen(scram_state->client_first_message));
	od_scram_HMAC_update(ctx, ",", 1);
	od_scram_HMAC_update(ctx, scram_state->server_first_message,
			     strlen(scram_state->server_first_message));
	od_scram_HMAC_update(ctx, ",", 1);
	od_scram_HMAC_update(ctx, scram_state->client_final_message,
			     strlen(scram_state->client_final_message));

	uint8_t expected_server_signature[SHA256_DIGEST_LENGTH];
	od_scram_HMAC_final(expected_server_signature, ctx);

	od_scram_HMAC_free(ctx);

	if (memcmp(expected_server_signature, server_signature,
		   SHA256_DIGEST_LENGTH) != 0)
		return NOT_OK_RESPONSE;

	return OK_RESPONSE;
}

int od_scram_read_client_first_message(od_scram_state_t *scram_state,
				       char *auth_data, size_t auth_data_size)
{
	if (!auth_data_size)
		return -6;
	switch (*auth_data) {
	case 'n': // client without channel binding
	case 'y': // client with    channel binding
		auth_data++;
		auth_data_size--;
		break;

	case 'p': // todo: client requires channel binding
		return -3;

	default:
		return -2;
	}

	if (!auth_data_size || *auth_data != ',')
		return -2;

	auth_data++;
	auth_data_size--;

	if (!auth_data_size || *auth_data == 'a' ||
	    *auth_data != ',') // todo: authorization identity
		return -4;

	auth_data++;
	auth_data_size--;

	if (!auth_data_size || *auth_data == 'm') // todo: mandatory extensions
		return -5;

	char *client_first_message = malloc(auth_data_size + 1);
	if (client_first_message == NULL)
		return -1;
	memcpy(client_first_message, auth_data, auth_data_size);
	client_first_message[auth_data_size] = '\0';

	if (read_attribute_buf(&auth_data, &auth_data_size, 'n', NULL, NULL))
		goto error;

	char *client_nonce;
	size_t client_nonce_size;
	if (read_attribute_buf(&auth_data, &auth_data_size, 'r', &client_nonce,
			       &client_nonce_size))
		goto error;

	for (size_t i = 0; i < client_nonce_size; i++) {
		char c = client_nonce[i];
		if (c < 0x21 || c > 0x7E || c == ',')
			goto error;
	}

	{
		char *t = malloc(client_nonce_size + 1);
		if (t == NULL)
			goto error;
		memcpy(t, client_nonce, client_nonce_size);
		t[client_nonce_size] = '\0';
		client_nonce = t;
	}

	while (auth_data_size)
		if (read_any_attribute_buf(&auth_data, &auth_data_size, NULL,
					   NULL, NULL) == -1)
			goto error_free_client_nonce;

	scram_state->client_first_message = client_first_message;
	scram_state->client_nonce = client_nonce;

	return 0;

error_free_client_nonce:

	free(client_nonce);

error:

	free(client_first_message);

	return -1;
}

int od_scram_read_client_final_message(od_scram_state_t *scram_state,
				       char *auth_data, size_t auth_data_size,
				       char **final_nonce_ptr,
				       size_t *final_nonce_size_ptr,
				       char **proof_ptr)
{
	const char *input_start = auth_data;
	char *proof_start;
	char *base64_proof;
	size_t base64_proof_size;
	char *proof = NULL;

	char *auth_data_copy = od_strdup_from_buf(auth_data, auth_data_size);
	if (auth_data_copy == NULL)
		goto error;

	char *channel_binding;
	size_t channel_binding_size;
	if (read_attribute_buf(&auth_data, &auth_data_size, 'c',
			       &channel_binding, &channel_binding_size))
		goto error;

	if (channel_binding_size != 4 ||
	    memcmp(channel_binding, "biws", channel_binding_size) !=
		    0) // todo channel binding
		goto error;

	char *client_final_nonce;
	size_t client_final_nonce_size;
	if (read_attribute_buf(&auth_data, &auth_data_size, 'r',
			       &client_final_nonce, &client_final_nonce_size))
		goto error;

	char attribute;
	do {
		proof_start = auth_data - 1;
		if (read_any_attribute_buf(&auth_data, &auth_data_size,
					   &attribute, &base64_proof,
					   &base64_proof_size))
			goto error;
	} while (attribute != 'p');

	int proof_size = pg_b64_dec_len(base64_proof_size);
	proof = malloc(proof_size);
	if (proof == NULL)
		goto error;

	int proof_len = od_b64_decode(base64_proof, base64_proof_size, proof,
				      proof_size);
	if (proof_len < 0)
		goto error;

	if (auth_data_size)
		goto error;

	scram_state->client_final_message =
		malloc(proof_start - input_start + 1);
	if (!scram_state->client_final_message)
		goto error;

	memcpy(scram_state->client_final_message, auth_data_copy,
	       proof_start - input_start);
	free(auth_data_copy);
	scram_state->client_final_message[proof_start - input_start] = '\0';

	*final_nonce_ptr = client_final_nonce;
	*final_nonce_size_ptr = client_final_nonce_size;
	*proof_ptr = proof;

	return 0;

error:

	free(proof);
	free(auth_data_copy);

	return -1;
}

machine_msg_t *
od_scram_create_server_first_message(od_scram_state_t *scram_state)
{
	char *result;

	uint8_t nonce[SCRAM_RAW_NONCE_LEN + 1];
	RAND_bytes(nonce, SCRAM_RAW_NONCE_LEN);

	int server_nonce_len = pg_b64_enc_len(SCRAM_RAW_NONCE_LEN) + 1;

	scram_state->server_nonce = malloc(server_nonce_len);
	if (scram_state->server_nonce == NULL)
		goto error;

	int base64_nonce_len =
		od_b64_encode((char *)nonce, SCRAM_RAW_NONCE_LEN,
			      scram_state->server_nonce, server_nonce_len);
	scram_state->server_nonce[base64_nonce_len] = '\0';

	size_t size = 12 + strlen(scram_state->client_nonce) +
		      strlen(scram_state->server_nonce) +
		      strlen(scram_state->salt);

	result = malloc(size + 1);

	if (!result)
		goto error;

	snprintf(result, size + 1, "r=%s%s,s=%s,i=%u",
		 scram_state->client_nonce, scram_state->server_nonce,
		 scram_state->salt, scram_state->iterations);

	scram_state->server_first_message = result;

	return kiwi_be_write_authentication_sasl_continue(NULL, result, size);

error:

	free(scram_state->server_nonce);
	scram_state->server_nonce = NULL;
	free(scram_state->server_first_message);
	scram_state->server_first_message = NULL;

	return NULL;
}

od_retcode_t od_scram_verify_final_nonce(od_scram_state_t *scram_state,
					 char *final_nonce,
					 size_t final_nonce_size)
{
	size_t client_nonce_len = strlen(scram_state->client_nonce);
	size_t server_nonce_len = strlen(scram_state->server_nonce);

	if (final_nonce_size != client_nonce_len + server_nonce_len)
		return NOT_OK_RESPONSE;

	if (memcmp(final_nonce, scram_state->client_nonce, client_nonce_len) !=
	    0)
		return NOT_OK_RESPONSE;

	if (memcmp(final_nonce + client_nonce_len, scram_state->server_nonce,
		   server_nonce_len) != 0)
		return NOT_OK_RESPONSE;

	return OK_RESPONSE;
}

od_retcode_t od_scram_verify_client_proof(od_scram_state_t *scram_state,
					  char *client_proof)
{
	uint8_t client_signature[SCRAM_KEY_LEN];
	uint8_t client_key[SCRAM_KEY_LEN];
	uint8_t client_stored_key[SCRAM_KEY_LEN];

	od_scram_ctx_t *ctx = od_scram_HMAC_create();
	const char *errstr = NULL;

	od_scram_HMAC_init(ctx, scram_state->stored_key, SCRAM_KEY_LEN);
	od_scram_HMAC_update(ctx, scram_state->client_first_message,
			     strlen(scram_state->client_first_message));
	od_scram_HMAC_update(ctx, ",", 1);
	od_scram_HMAC_update(ctx, scram_state->server_first_message,
			     strlen(scram_state->server_first_message));
	od_scram_HMAC_update(ctx, ",", 1);
	od_scram_HMAC_update(ctx, scram_state->client_final_message,
			     strlen(scram_state->client_final_message));
	od_scram_HMAC_final(client_signature, ctx);

	for (int i = 0; i < SCRAM_KEY_LEN; i++)
		client_key[i] = client_proof[i] ^ client_signature[i];

	od_scram_H(client_key, SCRAM_KEY_LEN, client_stored_key, &errstr);
	od_scram_HMAC_free(ctx);

	if (memcmp(client_stored_key, scram_state->stored_key, SCRAM_KEY_LEN) !=
	    0)
		return NOT_OK_RESPONSE;

	return OK_RESPONSE;
}

machine_msg_t *
od_scram_create_server_final_message(od_scram_state_t *scram_state)
{
	char *signature = calculate_server_signature(scram_state);
	if (signature == NULL)
		return NULL;

	size_t size = strlen("v=") + strlen(signature);
	char *result = malloc(size + 1);
	if (result == NULL)
		goto error;

	snprintf(result, size + 1, "v=%s", signature);

	free(signature);

	machine_msg_t *msg =
		kiwi_be_write_authentication_sasl_final(NULL, result, size);

	if (msg == NULL)
		goto error;
	free(result);

	return msg;

error:

	free(result);

	return NULL;
}
