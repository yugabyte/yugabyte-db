/*
 * __API_SCHEMA_V2__.roles_info processes a rolesInfo command.
 */
CREATE OR REPLACE FUNCTION __API_SCHEMA_V2__.roles_info(
    p_spec __CORE_SCHEMA_V2__.bson)
 RETURNS __CORE_SCHEMA_V2__.bson
 LANGUAGE C
 VOLATILE
AS 'MODULE_PATHNAME', $function$command_roles_info$function$;
