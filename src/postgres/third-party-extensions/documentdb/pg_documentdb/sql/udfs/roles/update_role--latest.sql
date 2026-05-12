/*
 * __API_SCHEMA_V2__.update_role processes a updateRole command.
 */
CREATE OR REPLACE FUNCTION __API_SCHEMA_V2__.update_role(
    p_spec __CORE_SCHEMA_V2__.bson)
 RETURNS __CORE_SCHEMA_V2__.bson
 LANGUAGE C
 VOLATILE
AS 'MODULE_PATHNAME', $function$command_update_role$function$;
