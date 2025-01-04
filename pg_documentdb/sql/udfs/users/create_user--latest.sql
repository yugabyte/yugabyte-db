/*
 * helio_api.create_user processes a Mongo wire protocol createUser command.
 */
CREATE OR REPLACE FUNCTION helio_api.create_user(
    p_spec helio_core.bson)
 RETURNS helio_core.bson
 LANGUAGE C
 VOLATILE
AS 'MODULE_PATHNAME', $$helio_extension_create_user$$;
