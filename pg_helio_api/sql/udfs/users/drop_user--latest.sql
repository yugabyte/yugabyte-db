/*
 * helio_api.drop_user processes a Mongo wire protocol dropUser command.
 */
CREATE OR REPLACE FUNCTION helio_api.drop_user(
    p_spec helio_core.bson)
 RETURNS helio_core.bson
 LANGUAGE C
 VOLATILE
AS 'MODULE_PATHNAME', $$helio_extension_drop_user$$;
