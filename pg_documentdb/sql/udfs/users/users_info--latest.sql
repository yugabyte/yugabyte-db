/*
 * helio_api.users_info processes a Mongo wire protocol usersInfo command.
 */
CREATE OR REPLACE FUNCTION helio_api.users_info(
    p_spec helio_core.bson)
 RETURNS helio_core.bson
 LANGUAGE C
 VOLATILE
AS 'MODULE_PATHNAME', $$helio_extension_get_users$$;
