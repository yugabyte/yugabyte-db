-- killOp database administration command implementation
CREATE OR REPLACE FUNCTION __API_SCHEMA_V2__.kill_op(p_command_spec __CORE_SCHEMA_V2__.bson)
RETURNS __CORE_SCHEMA_V2__.bson
 LANGUAGE c
 VOLATILE STRICT
AS 'MODULE_PATHNAME', $function$command_kill_op$function$;