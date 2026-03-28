/*
 * __API_SCHEMA_V2__.create_role processes a createRole command.
 */
CREATE OR REPLACE FUNCTION __API_SCHEMA_V2__.create_role(
    p_spec __CORE_SCHEMA_V2__.bson)
 RETURNS __CORE_SCHEMA_V2__.bson
 LANGUAGE C
 VOLATILE
AS 'MODULE_PATHNAME', $function$command_create_role$function$;
