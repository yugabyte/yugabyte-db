/*
 * __API_SCHEMA_V2__.update_user processes a wire protocol updateUser command.
 */
CREATE OR REPLACE FUNCTION __API_SCHEMA_V2__.update_user(
    p_spec __CORE_SCHEMA_V2__.bson)
 RETURNS __CORE_SCHEMA_V2__.bson
 LANGUAGE C
 VOLATILE
AS 'MODULE_PATHNAME', __CONCAT_NAME_FUNCTION__($$, __EXTENSION_OBJECT_PREFIX_V2__, _extension_update_user$$);
