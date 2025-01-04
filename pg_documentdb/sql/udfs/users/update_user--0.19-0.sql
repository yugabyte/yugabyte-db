/*
 * helio_api.update_user processes a Mongo wire protocol updateUser command.
 */
CREATE OR REPLACE FUNCTION helio_api.update_user(
    p_spec helio_core.bson)
 RETURNS helio_core.bson
 LANGUAGE C
 VOLATILE
AS 'MODULE_PATHNAME', $$helio_extension_update_user$$;
