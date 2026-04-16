/*
 * __API_SCHEMA_V2__.connection_status processes a connectionStatus command.
 */
CREATE OR REPLACE FUNCTION __API_SCHEMA_V2__.connection_status(
    p_spec __CORE_SCHEMA_V2__.bson)
 RETURNS __CORE_SCHEMA_V2__.bson
 LANGUAGE C
 VOLATILE
AS 'MODULE_PATHNAME', $function$command_connection_status$function$;
