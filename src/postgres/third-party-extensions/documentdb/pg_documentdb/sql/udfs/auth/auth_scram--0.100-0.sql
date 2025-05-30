/* Authenticate using SCRAM SHA256 with postgresql db for a specific user name */
CREATE OR REPLACE FUNCTION __API_SCHEMA_INTERNAL_V2__.authenticate_with_scram_sha256(
    p_user_name text, p_auth_msg text, p_client_proof text)
 RETURNS __CORE_SCHEMA_V2__.bson
 LANGUAGE C
PARALLEL SAFE STABLE
AS 'MODULE_PATHNAME', $$command_authenticate_with_scram_sha256$$;
COMMENT ON FUNCTION __API_SCHEMA_INTERNAL_V2__.authenticate_with_scram_sha256(text, text, text)
    IS 'Used to authenticate the user with Postgresql DB using SCRAM SHA-256';

/*
 * scram_sha256_get_salt_and_iterations() gets SALT and Iteration
 * count for the given user from the Postgresql DB
 */
CREATE OR REPLACE FUNCTION __API_SCHEMA_INTERNAL_V2__.scram_sha256_get_salt_and_iterations(
    p_user_name text)
 RETURNS __CORE_SCHEMA_V2__.bson
 LANGUAGE C
PARALLEL SAFE STABLE
AS 'MODULE_PATHNAME', $$command_scram_sha256_get_salt_and_iterations$$;
COMMENT ON FUNCTION __API_SCHEMA_INTERNAL_V2__.scram_sha256_get_salt_and_iterations(text)
    IS 'Gets SALT and Iteration count for the given user from the backend';   