/*
 * __API_SCHEMA_V2__.users_info processes a Mongo wire protocol usersInfo command.
 */
CREATE OR REPLACE FUNCTION __API_SCHEMA_V2__.users_info(
    p_spec __CORE_SCHEMA_V2__.bson)
 RETURNS __CORE_SCHEMA_V2__.bson
 LANGUAGE C
 VOLATILE
AS 'MODULE_PATHNAME', __CONCAT_NAME_FUNCTION__($$, __EXTENSION_OBJECT_PREFIX_V2__, _extension_get_users$$);
