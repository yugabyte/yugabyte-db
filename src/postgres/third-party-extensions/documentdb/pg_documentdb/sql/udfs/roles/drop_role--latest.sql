/*
 * __API_SCHEMA_V2__.drop_role processes a dropRole command.
 */
CREATE OR REPLACE FUNCTION __API_SCHEMA_V2__.drop_role(
    p_spec __CORE_SCHEMA_V2__.bson)
 RETURNS __CORE_SCHEMA_V2__.bson
 LANGUAGE C
 VOLATILE
AS 'MODULE_PATHNAME', $function$command_drop_role$function$;
