/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/auth/scram256.c
 *
 * Implementation of SCRAM SHA 256 authentication
 *
 *-------------------------------------------------------------------------
 */

#include "postgres.h"
#include "common/scram-common.h"
#include "libpq/crypt.h"
#include "utils/builtins.h"
#include "io/helio_bson_core.h"
#include "common/base64.h" /* Postgres base64 encode / decode functions */
#include "common/hmac.h"   /* Postgres hmac functions */
#include "utils/varlena.h" /* Postgres variable-length builtIn types functions*/

/* Including Postgresql headers for test helper functions.*/
#include "common/saslprep.h" /* Postgres SASLprep normalization functions */

#undef ENABLE_GSS
#include "libpq/scram.h"

#define AUTH_DOUBLE_QUOTE_CHAR '"' /* DOUBLE QUOTE CHAR */
#define AUTH_EOS_CHAR '\0'; /* End of string CHAR */
#define AUTH_OK_KEY "ok" /* OK key inside result json */
#define AUTH_OK_KEY_LEN strlen(AUTH_OK_KEY) /* OK key length */
#define AUTH_ITER_KEY "iterations" /* 'iterations' key inside result json */
#define AUTH_ITER_KEY_LEN strlen(AUTH_ITER_KEY) /* Iterations key length */
#define AUTH_SALT_KEY "salt" /* 'salt' key inside result json */
#define AUTH_SALT_KEY_LEN strlen(AUTH_SALT_KEY) /* salt key length */
#define AUTH_SERV_SIGN_KEY "ServerSignature" /* Server signature key */
#define AUTH_SERV_SIGN_KEY_LEN strlen(AUTH_SERV_SIGN_KEY) /* Serv sign length */

/* For test helper functions */
#define AUTH_MSG_KEY "AuthMessage"
#define AUTH_MSG_KEY_LEN strlen(AUTH_MSG_KEY)
#define AUTH_CLIENT_PROOF_KEY "ClientProof"
#define AUTH_CLIENT_PROOF_KEY_LEN strlen(AUTH_CLIENT_PROOF_KEY)
#define AUTH_USER_NAME_KEY "UserName"
#define AUTH_USER_NAME_KEY_LEN strlen(AUTH_USER_NAME_KEY)

#define AUTH_ERR_PARSE_SHADOW_PASSWORD_FAILED -1
#define AUTH_ERR_SALTED_PASSWORD_GEN_FAILED -2


/*
 * Response to be send to the Compute gateway during the salt and iteration
 * count request while authenticating the client using SCRAM SHA 256
 */
typedef struct SaltIterationsReqResult
{
	/* Status of the request. 1 if ok; 0 otherwise */
	int ok;

	/* Iteration count retrieved from shadow password */
	int iterationCount;

	/* Retrieved salt from the shadow password */
	char *encodedSalt;
} SaltIterationsReqResult;

/*
 * Response to be send to the Compute gateway during SCRAM SHA 256
 * authentication request while authenticating the client
 */
typedef struct ScramAuthResult
{
	/* Status of the request. 1 if ok; 0 otherwise */
	int ok;

	/* Server signature string to be used by the compute gateway and then
	 * pass on to the mongo client */
	char *serverSignature;
} ScramAuthResult;

/*
 * Response to be send by the test helper function
 * (pgmongo_generate_auth_message_client_proof) that generates the Auth Message
 * and the Client Proof which is to be fed to
 * command_authenticate_with_scram_sha256 in the regression test suite.
 */
typedef struct ClientProofGeneratorResult
{
	/* Status of the request. 1 if ok; 0 otherwise */
	int ok;
	char *authMessage;
	char *encodedClientProof;
} ClientProofGeneratorResult;


typedef struct ScramState
{
	char *userName;             /* User name or Role used for authentication */
	int iterations;             /* Iterations count for SCRAM authentication */
	char *encodedSalt;
	uint8 storedKey[SCRAM_KEY_LEN];  /* H(ClientSignature ^ ClientProof) */
	uint8 serverKey[SCRAM_KEY_LEN];  /* Used for Server Final Message */
	char *authMessage; /* Client First message bare + Server First message
	                    + Client Final message without Proof */
	char decodedClientProof[SCRAM_KEY_LEN];
} ScramState;

/* ------------------------------------------------------------------------- */


/* STATIC FUNCTION DECLARATIONS */

/* Get the shadow password from the Postgresql DB and parse it to get Salt,
 * Iteration count, StoredKey and ServerKey */
static bool ParseScramShadowPassword(ScramState *state);

/* Generate Auth Message for the test helper function.
 * Auth message = Client First message bare + Server First message
 *                 + Client Final message without Proof */
static int GenerateAuthMessageForTest(ScramState *state);


/* Generates the Salted password as:
 *   -- SaltedPassword = KeyDerive(password, salt, i) */
static int GenerateSaltedPasswordForTest(ScramState *scramState,
										 char *password,
										 uint8 *saltedPassword);


/* Verify the derived Client Proof against the storedKey in Shadow password */
static bool VerifyClientProof(ScramState *state);


/* Build the final server-side message */
static char * BuildServerFinalMessage(ScramState *state);


/* BuildResponseMsgForSaltRequest builds the response BSON for a SALT
 * request command */
static pgbson * BuildResponseMsgForSaltRequest(SaltIterationsReqResult *requestResult);


/* Build the response BSON for SCRAM SHA256 AUTH request */
static pgbson * BuildResponseMsgForAuthRequest(ScramAuthResult *authReqResult);


/* Build the response BSON for Auth Message and Client Proof generator test
 * helper function */
static pgbson * BuildResponseMsgForClientProofGeneratorForTest(
	ClientProofGeneratorResult *result);

/* wrappers around SCRAM functions to handle API changes across PostgreSQL version */
static bool ScramClientKey(const uint8 *saltedPassword, uint8 *clientKey);
static bool ScramServerKey(const uint8 *saltedPassword, uint8 *serverKey);
static bool ScramHash(const uint8 *clientKey, int keyLength, uint8 *clientStoredKey);
static bool ScramSaltedPassword(const char *password, const char *decodedSalt,
								int decodedLength, int iterations, uint8 *saltedPassword);


/* ------------------------------------------------------------------------- */

/*
 * FUNCTION DECLARATIONS FORMAT FOR HELIO EXTENSION FUNCTIONS
 */
PG_FUNCTION_INFO_V1(command_authenticate_with_scram_sha256);
PG_FUNCTION_INFO_V1(command_scram_sha256_get_salt_and_iterations);
PG_FUNCTION_INFO_V1(command_generate_auth_message_client_proof_for_test);
PG_FUNCTION_INFO_V1(command_generate_server_signature_for_test);


/*
 * This function provides the encoded salt and iteration count corresponding to
 * the given user name to the Compute gateway while authenticating the Mongo
 * client using SCRAM SHA 256
 * Input argument 1: User name. Type: text
 * Output: { "ok" : 1, "iterations" : int, "salt" : text }
 */
Datum
command_scram_sha256_get_salt_and_iterations(PG_FUNCTION_ARGS)
{
	ScramState scramState;
	SaltIterationsReqResult requestResult; /* Result of the salt request */

	/* Initializing the members of ScramState. This structure captures the SCRAM
	 * authentication details like username, Iterations count, SALT etc */
	memset(&scramState, 0, sizeof(ScramState));

	/* Initializing the members of SaltIterationsReqResult */
	memset(&requestResult, 0, sizeof(SaltIterationsReqResult));
	requestResult.encodedSalt = "";

	if (PG_ARGISNULL(0))
	{
		/* user name is passed as NULL */
		PG_RETURN_POINTER(BuildResponseMsgForSaltRequest(&requestResult));
	}

	/* Capture the User Name as char*. PLPGSQL sends it as TEXT */
	scramState.userName = text_to_cstring(PG_GETARG_TEXT_P(0));

	if (ParseScramShadowPassword(&scramState))
	{
		requestResult.ok = 1;
		requestResult.encodedSalt = scramState.encodedSalt;
		requestResult.iterationCount = scramState.iterations;
	}

	PG_RETURN_POINTER(BuildResponseMsgForSaltRequest(&requestResult));
}


/*
 * command_authenticate_with_scram_sha256 authenticates the provided user name
 * with the scramed password from postgresql.
 * Input argument 1: User name. Type: text
 * Input argument 2: Auth message. Type: Text
 * Input argument 3: Client proof. Type: text
 * Output: {ok: 1, ServerSignature: text}
 */
Datum
command_authenticate_with_scram_sha256(PG_FUNCTION_ARGS)
{
	char *proof; /* Client proof as received from the compute gateway */
	char *clientProof;  /* Decoded Client Proof */
	int clientProofLen;
	ScramState scramState;
	ScramAuthResult authResult; /* Authentication result */

	/* Initializing the members of ScramState. This structure holds the SCRAM
	 * authentication details like username, auth message, decoded client
	 * proof etc */
	memset(&scramState, 0, sizeof(ScramState));

	/* Initializing the members of ScramAuthResult */
	memset(&authResult, 0, sizeof(ScramAuthResult));
	authResult.serverSignature = "";

	/* User Name */
	if (PG_ARGISNULL(0))
	{
		/* To indicate as NOT AUTHORIZED when user name is passed as NULL */
		PG_RETURN_POINTER(BuildResponseMsgForAuthRequest(&authResult));
	}

	/* Auth Message */
	if (PG_ARGISNULL(1))
	{
		/* To indicate as NOT AUTHORIZED when AUTH key is NULL */
		PG_RETURN_POINTER(BuildResponseMsgForAuthRequest(&authResult));
	}

	/* Client proof */
	if (PG_ARGISNULL(2))
	{
		/* To indicate as NOT AUTHORIZED when AUTH key is NULL */
		PG_RETURN_POINTER(BuildResponseMsgForAuthRequest(&authResult));
	}

	/* Capture the User Name as char*. PLPGSQL sends it as TEXT */
	scramState.userName = text_to_cstring(PG_GETARG_TEXT_P(0));

	/*
	 * ClientFirstMessageBare + "," + ServerFirstMessage + ","
	 * + clientFinalMessageWithoutProof;
	 */
	scramState.authMessage = text_to_cstring(PG_GETARG_TEXT_P(1));

	/* Client proof given by compute gateway which is used for
	 * deriving the Store Key and need to validate that against the stored
	 * key contained in the shadow password from Postgresql */
	proof = text_to_cstring(PG_GETARG_TEXT_P(2));

	/* Base64 decode length: the length of the string if it were to be decoded
	 * with base64, based on the length given by caller.  This is useful to
	 * estimate how large a buffer allocation needs to be done before doing
	 * the actual decoding.*/
	clientProofLen = pg_b64_dec_len(strlen(proof));
	clientProof = palloc(clientProofLen);

	/* Decode the Client Proof sent by the compute gateway. This is currently
	 * in Base64*/
	if (pg_b64_decode(proof, strlen(proof), clientProof,
					  clientProofLen) != SCRAM_KEY_LEN)
	{
		ereport(LOG, (errmsg("Malformed SCRAM message.")));
		PG_RETURN_POINTER(BuildResponseMsgForAuthRequest(&authResult));
	}

	/* Copy decoded Client Proof */
	memcpy(scramState.decodedClientProof, clientProof, SCRAM_KEY_LEN);
	pfree(clientProof);

	if (!ParseScramShadowPassword(&scramState))
	{
		ereport(LOG, (errmsg("Parsing SCRAM shadow password failed.")));
		PG_RETURN_POINTER(BuildResponseMsgForAuthRequest(&authResult));
	}

	/* Validate the shadow password from the compute gateway against the
	 * stored one in pg_authid table */
	if (!VerifyClientProof(&scramState))
	{
		PG_RETURN_POINTER(BuildResponseMsgForAuthRequest(&authResult));
	}

	/* Once the Client proof is validated, we prepare the Server
	 * signature for the client to authenticate the Server */
	authResult.serverSignature = BuildServerFinalMessage(&scramState);
	authResult.ok = (authResult.serverSignature != NULL) ? 1 : 0;

	PG_RETURN_POINTER(BuildResponseMsgForAuthRequest(&authResult));
}


/*
 * Test helper function.
 * Generates Auth message and Client Proof which are needed as input parameters
 * command_authenticate_with_scram_sha256()
 * Input argument 1: User name. Type: text
 * Input argument 2: Password. Type: text
 * Output: {ok: 1, AuthMessage: <text>, ClientProof: <text>}
 */
Datum
command_generate_auth_message_client_proof_for_test(PG_FUNCTION_ARGS)
{
	int i;
	int encodedLen;
	uint8 clientKey[SCRAM_KEY_LEN];
	uint8 saltedPassword[SCRAM_KEY_LEN];
	uint8 clientSignature[SCRAM_KEY_LEN];
	pg_hmac_ctx *ctx;
	ScramState scramState;
	ClientProofGeneratorResult result;

	memset(&result, 0, sizeof(result));
	result.authMessage = "";
	result.encodedClientProof = "";

	/* User Name */
	if (PG_ARGISNULL(0))
	{
		PG_RETURN_POINTER(BuildResponseMsgForClientProofGeneratorForTest(&result));
	}

	/* Password */
	if (PG_ARGISNULL(1))
	{
		PG_RETURN_POINTER(BuildResponseMsgForClientProofGeneratorForTest(&result));
	}

	/* Capture the User Name as char*. PLPGSQL sends it as TEXT */
	scramState.userName = text_to_cstring(PG_GETARG_TEXT_P(0));

	if (GenerateSaltedPasswordForTest(&scramState,
									  text_to_cstring(PG_GETARG_TEXT_P(1)),
									  (uint8 *) saltedPassword) < 0)
	{
		PG_RETURN_POINTER(BuildResponseMsgForClientProofGeneratorForTest(&result));
	}

	/* ClientKey = HMAC(saltedPassword, "Client Key") */
	if (!ScramClientKey(saltedPassword, clientKey))
	{
		PG_RETURN_POINTER(BuildResponseMsgForClientProofGeneratorForTest(&result));
	}

	/* Genereate Auth Message */
	if (GenerateAuthMessageForTest(&scramState) < 0)
	{
		ereport(LOG, (errmsg("Auth message generation failed.")));
		PG_RETURN_POINTER(BuildResponseMsgForClientProofGeneratorForTest(&result));
	}

	/* ClientSignature = HMAC(StoredKey, AuthMessage) */
	ctx = pg_hmac_create(PG_SHA256);

	/* calculate ClientSignature */
	if (pg_hmac_init(ctx, scramState.storedKey, SCRAM_KEY_LEN) < 0 ||
		pg_hmac_update(ctx,
					   (uint8 *) scramState.authMessage,
					   strlen(scramState.authMessage)) < 0 ||
		pg_hmac_final(ctx, clientSignature, sizeof(clientSignature)) < 0)
	{
		ereport(LOG, (errmsg("HMAC(StoredKey, AuthMessage) failed.")));
		pg_hmac_free(ctx);
		PG_RETURN_POINTER(BuildResponseMsgForClientProofGeneratorForTest(&result));
	}

	pg_hmac_free(ctx);

	/* ClientProof = ClientKey ^ ClientSignature */
	for (i = 0; i < SCRAM_KEY_LEN; i++)
	{
		scramState.decodedClientProof[i] = clientKey[i] ^ clientSignature[i];
	}

	/* Encode Client Proof */
	encodedLen = pg_b64_enc_len(SCRAM_KEY_LEN);
	result.encodedClientProof = palloc0(encodedLen + 1);

	encodedLen = pg_b64_encode((const char *) scramState.decodedClientProof,
							   SCRAM_KEY_LEN, result.encodedClientProof,
							   encodedLen);

	if (encodedLen < 0)
	{
		encodedLen = 0;
	}
	else
	{
		result.ok = 1;
		result.authMessage = scramState.authMessage;
		ereport(DEBUG1, (errmsg("Auth Message sent is [%s].",
								result.authMessage)));
	}

	result.encodedClientProof[encodedLen] = AUTH_EOS_CHAR;

	PG_RETURN_POINTER(BuildResponseMsgForClientProofGeneratorForTest(&result));
}


/*
 * Test helper function to generate Server Signature as:
 *      -- SaltedPassword = KeyDerive(password, salt, i)
 *      -- ServerKey = HMAC(SaltedPassword, "Server Key")
 *      -- ServerSignature = HMAC(ServerKey, AuthMessage)
 * Input argument 1: User name. Type: text
 * Input argument 2: Password. Type: text
 * Input argument 3: AuthMessage. Type: text
 * Output: {ok: 1, ServerSignature: text}
 */
Datum
command_generate_server_signature_for_test(PG_FUNCTION_ARGS)
{
	uint8 saltedPassword[SCRAM_KEY_LEN];
	ScramState scramState;
	ScramAuthResult result;

	memset(&result, 0, sizeof(result));
	result.serverSignature = "";

	/* User Name */
	if (PG_ARGISNULL(0) || PG_ARGISNULL(1) || PG_ARGISNULL(2))
	{
		PG_RETURN_POINTER(BuildResponseMsgForAuthRequest(&result));
	}

	/* Capture the User Name as char*. PLPGSQL sends it as TEXT */
	scramState.userName = text_to_cstring(PG_GETARG_TEXT_P(0));
	char *password = text_to_cstring(PG_GETARG_TEXT_P(1));
	scramState.authMessage = text_to_cstring(PG_GETARG_TEXT_P(2));

	ereport(DEBUG1, (errmsg("Auth Message received is [%s].",
							scramState.authMessage)));

	if (GenerateSaltedPasswordForTest(&scramState, password, saltedPassword) < 0)
	{
		PG_RETURN_POINTER(BuildResponseMsgForAuthRequest(&result));
	}

	/* ServerKey = HMAC(SaltedPassword, "Server Key") */
	if (!ScramServerKey(saltedPassword, scramState.serverKey))
	{
		PG_RETURN_POINTER(BuildResponseMsgForAuthRequest(&result));
	}

	result.serverSignature = BuildServerFinalMessage(&scramState);

	PG_RETURN_POINTER(BuildResponseMsgForAuthRequest(&result));
}


/* Get the shadow password from the Postgresql DB and parse it to get Salt,
 * Iteration count, StoredKey and ServerKey */
static bool
ParseScramShadowPassword(ScramState *scramState)
{
#if (PG_VERSION_NUM >= 150000)
	const char *logDetail = NULL;
#else
	char *logDetail = NULL;
#endif

	char *shadowPass;   /* Shadow password for the specific user in PG */

	/* Look up the user's password. */
	shadowPass = get_role_password((const char *) scramState->userName,
								   &logDetail);
	if (shadowPass == NULL)
	{
		/* Given user name does not exist or password is empty for that
		 * user name */
		ereport(LOG, (errmsg("No shadow password for user name: [%s].",
							 scramState->userName)));
		return false;
	}

	/* Parse the shadow password, get Stored and encoded salt */
	return parse_scram_secret((const char *) shadowPass,
							  &(scramState->iterations),
							  &(scramState->encodedSalt),
							  scramState->storedKey,
							  scramState->serverKey);
}


/*
 * Test helper function.
 * Generate Auth Message for command_generate_auth_message_client_proof_for_test
 * This is a test helper function that helps in forming the input parameter to
 * the function: command_authenticate_with_scram_sha256() for the tests added
 * pg_helio_api regress suite.
 *
 * Auth message = Client First message bare + Server First message
 *                 + Client Final message without Proof
 * Sample:
 *  "n=myuser,r=Ppo+ljw4yimF3BMws2XNX/NWPRm2bYwu,
 *   r=Ppo+ljw4yimF3BMws2XNX/NWPRm2bYwu`1,
 *   s=TWljcm9zb2Z0LkF6dXJlLkNvc21vcy5Nb25nby5Db3JlLkNvbW1vbi5Cc29uLkNvc21vc0Jzb25WYWx1ZQ==,
 *   i=4096,c=biws,
 *   r=Ppo+ljw4yimF3BMws2XNX/NWPRm2bYwu`1"
 */
static int
GenerateAuthMessageForTest(ScramState *scramState)
{
	scramState->authMessage = palloc0(1024);
	char *buf = scramState->authMessage;

	strcat(buf, "n=");
	strcat(buf, scramState->userName);

	/* Keeping the Client nonce as a fixed sample string here rather than
	 * generating it dynamically. Same for the Combined nonce (the second 'r=').
	 * For the unit testing of command_authenticate_with_scram_sha256()
	 * this should be fine. Real testing with dynamically generated nonce(s)
	 * will happen in the E2E testing that includes the Compute gateway and
	 * the mongo client. */
	strcat(buf,
		   ",r=Ppo+ljw4yimF3BMws2XNX/NWPRm2bYwu,r=Ppo+ljw4yimF3BMws2XNX/NWPRm2bYwu`1,s=");
	strcat(buf, scramState->encodedSalt);
	strcat(buf, ",i=");
	sprintf(buf + strlen(buf), "%d,c=biws,r=Ppo+ljw4yimF3BMws2XNX/NWPRm2bYwu`1",
			scramState->iterations);
	return 0;
}


/* Test helper function.
 * Generates the Salted password as:
 *   -- SaltedPassword = KeyDerive(password, salt, i)
 */
static int
GenerateSaltedPasswordForTest(ScramState *scramState,
							  char *password,
							  uint8 *saltedPassword)
{
	int encodedLen;
	int decodedLen;
	char *decodedSalt;
	char *prepPassword;
	pg_saslprep_rc returnCode;

	if (!ParseScramShadowPassword(scramState))
	{
		ereport(LOG, (errmsg("Parsing SCRAM shadow password failed..")));
		return AUTH_ERR_PARSE_SHADOW_PASSWORD_FAILED;
	}

	encodedLen = strlen(scramState->encodedSalt);
	decodedLen = pg_b64_dec_len(encodedLen);
	decodedSalt = palloc(decodedLen);
	decodedLen = pg_b64_decode(scramState->encodedSalt, encodedLen,
							   decodedSalt, decodedLen);

	/*
	 * Normalize the password with SASLprep.  If that doesn't work, because
	 * the password isn't valid UTF-8 or contains prohibited characters, just
	 * proceed with the original password.
	 */
	returnCode = pg_saslprep(password, &prepPassword);
	if (returnCode == SASLPREP_SUCCESS)
	{
		password = prepPassword;
	}

	if (!ScramSaltedPassword(password, decodedSalt, decodedLen,
							 scramState->iterations, saltedPassword))
	{
		return AUTH_ERR_SALTED_PASSWORD_GEN_FAILED;
	}

	return 0;
}


/*
 * Verify the client proof contained in the last message received from
 * client in an exchange.  Returns true if the verification is a success,
 * or false for a failure.
 */
static bool
VerifyClientProof(ScramState *state)
{
	uint8 clientSignature[SCRAM_KEY_LEN];
	uint8 clientKey[SCRAM_KEY_LEN];
	uint8 clientStoredKey[SCRAM_KEY_LEN];
	pg_hmac_ctx *ctx = pg_hmac_create(PG_SHA256);
	int i;

	/* Calculate ClientSignature */
	if (pg_hmac_init(ctx, state->storedKey, SCRAM_KEY_LEN) < 0 ||
		pg_hmac_update(ctx,
					   (uint8 *) state->authMessage,
					   strlen(state->authMessage)) < 0 ||
		pg_hmac_final(ctx, clientSignature, sizeof(clientSignature)) < 0)
	{
		ereport(LOG, (errmsg("Client Signature derivation failed.")));
		return false;
	}

	pg_hmac_free(ctx);

	/* Extract the ClientKey that the client calculated from the proof */
	for (i = 0; i < SCRAM_KEY_LEN; i++)
	{
		clientKey[i] = state->decodedClientProof[i] ^ clientSignature[i];
	}

	/* Hash it one more time, and compare with StoredKey */
	if (!ScramHash(clientKey, SCRAM_KEY_LEN, clientStoredKey))
	{
		return false;
	}

	/* H(ClientSignature ^ ClientProof) = StoredKey */
	if (memcmp(clientStoredKey, state->storedKey, SCRAM_KEY_LEN) != 0)
	{
		ereport(LOG, (errmsg("Client proof verification failed.")));
		return false;
	}

	return true;
}


/*
 * Build the final server-side message of an exchange.
 */
static char *
BuildServerFinalMessage(ScramState *state)
{
	uint8 serverSignature[SCRAM_KEY_LEN];
	char *serverSignatureBase64 = "";
	int siglen;
	pg_hmac_ctx *ctx = pg_hmac_create(PG_SHA256);

	/* calculate ServerSignature */
	if (pg_hmac_init(ctx, state->serverKey, SCRAM_KEY_LEN) < 0 ||
		pg_hmac_update(ctx,
					   (uint8 *) state->authMessage,
					   strlen(state->authMessage)) < 0 ||
		pg_hmac_final(ctx, serverSignature, sizeof(serverSignature)) < 0)
	{
		return serverSignatureBase64;
	}

	pg_hmac_free(ctx);

	/* 3 bytes will be converted to 4 */
	siglen = pg_b64_enc_len(SCRAM_KEY_LEN);

	/* don't forget the zero-terminator */
	serverSignatureBase64 = palloc0(siglen + 1);
	siglen = pg_b64_encode((const char *) serverSignature,
						   SCRAM_KEY_LEN, serverSignatureBase64,
						   siglen);

	siglen = (siglen < 0) ? 0 : siglen;
	serverSignatureBase64[siglen] = AUTH_EOS_CHAR;
	return serverSignatureBase64;
}


/*
 * BuildResponseMsgForSaltRequest builds the response BSON for a SALT
 * request command
 */
static pgbson *
BuildResponseMsgForSaltRequest(SaltIterationsReqResult *requestResult)
{
	pgbson_writer resultWriter;

	PgbsonWriterInit(&resultWriter);

	PgbsonWriterAppendInt32(&resultWriter, AUTH_OK_KEY, AUTH_OK_KEY_LEN,
							requestResult->ok);
	PgbsonWriterAppendInt32(&resultWriter, AUTH_ITER_KEY, AUTH_ITER_KEY_LEN,
							requestResult->iterationCount);
	PgbsonWriterAppendUtf8(&resultWriter, AUTH_SALT_KEY, AUTH_SALT_KEY_LEN,
						   requestResult->encodedSalt);

	return PgbsonWriterGetPgbson(&resultWriter);
}


/* Build the response BSON for SCRAM SHA256 AUTH request */
static pgbson *
BuildResponseMsgForAuthRequest(ScramAuthResult *authReqResult)
{
	pgbson_writer resultWriter;

	PgbsonWriterInit(&resultWriter);

	PgbsonWriterAppendInt32(&resultWriter, AUTH_OK_KEY, AUTH_OK_KEY_LEN,
							authReqResult->ok);
	PgbsonWriterAppendUtf8(&resultWriter, AUTH_SERV_SIGN_KEY,
						   AUTH_SERV_SIGN_KEY_LEN,
						   authReqResult->serverSignature);

	return PgbsonWriterGetPgbson(&resultWriter);
}


/* Build the response BSON for Auth Message and Client Proof generator test
 * helper function */
static pgbson *
BuildResponseMsgForClientProofGeneratorForTest(ClientProofGeneratorResult *result)
{
	pgbson_writer resultWriter;

	PgbsonWriterInit(&resultWriter);

	PgbsonWriterAppendInt32(&resultWriter, AUTH_OK_KEY, AUTH_OK_KEY_LEN,
							result->ok);
	PgbsonWriterAppendUtf8(&resultWriter, AUTH_MSG_KEY,
						   AUTH_MSG_KEY_LEN,
						   result->authMessage);
	PgbsonWriterAppendUtf8(&resultWriter, AUTH_CLIENT_PROOF_KEY,
						   AUTH_CLIENT_PROOF_KEY_LEN,
						   result->encodedClientProof);

	return PgbsonWriterGetPgbson(&resultWriter);
}


/*
 * ScramClientKey is a wrapper around scram_ClientKey that handles error reporting
 * for different postgres versions.
 */
static bool
ScramClientKey(const uint8 *saltedPassword, uint8 *clientKey)
{
#if (PG_VERSION_NUM >= 150000)
	const char *errorString = NULL;
	if (scram_ClientKey(saltedPassword, clientKey, &errorString) < 0)
	{
		ereport(LOG, (errmsg("Client Key derivation failed: %s", errorString)));
		return false;
	}
#else
	if (scram_ClientKey(saltedPassword, clientKey) < 0)
	{
		ereport(LOG, (errmsg("Client Key derivation failed.")));
		return false;
	}
#endif

	return true;
}


/*
 * ScramServerKey is a wrapper around scram_SeverKey that handles error reporting
 * for different postgres versions.
 */
static bool
ScramServerKey(const uint8 *saltedPassword, uint8 *serverKey)
{
#if (PG_VERSION_NUM >= 150000)
	const char *errorString = NULL;
	if (scram_ServerKey(saltedPassword, serverKey, &errorString) < 0)
	{
		ereport(LOG, (errmsg("Server Key derivation failed: %s", errorString)));
		return false;
	}
#else
	if (scram_ServerKey(saltedPassword, serverKey) < 0)
	{
		ereport(LOG, (errmsg("Server Key derivation failed.")));
		return false;
	}
#endif

	return true;
}


/*
 * ScramHash is a wrapper around scram_H that handles error reporting
 * for different postgres versions.
 */
static bool
ScramHash(const uint8 *clientKey, int keyLength, uint8 *clientStoredKey)
{
#if (PG_VERSION_NUM >= 150000)
	const char *errorString = NULL;
	if (scram_H(clientKey, keyLength, clientStoredKey, &errorString) < 0)
	{
		ereport(LOG, (errmsg("Hashing of client key failed: %s", errorString)));
		return false;
	}
#else
	if (scram_H(clientKey, keyLength, clientStoredKey) < 0)
	{
		ereport(LOG, (errmsg("Hashing of client key failed.")));
		return false;
	}
#endif

	return true;
}


/*
 * mScramSaltedPassword is a wrapper around scram_SaltedPassword that handles error
 * reporting for different postgres versions.
 */
static bool
ScramSaltedPassword(const char *password, const char *decodedSalt,
					int decodedLength, int iterations, uint8 *saltedPassword)
{
#if (PG_VERSION_NUM >= 150000)
	const char *errorString = NULL;
	if (scram_SaltedPassword(password, decodedSalt, decodedLength,
							 iterations, saltedPassword, &errorString) < 0)
	{
		ereport(LOG, (errmsg("get salted password failed: %s", errorString)));
		return false;
	}
#else
	if (scram_SaltedPassword(password, decodedSalt, decodedLength,
							 iterations, saltedPassword) < 0)
	{
		ereport(LOG, (errmsg("get salted password failed.")));
		return false;
	}
#endif

	return true;
}
