/* Add functions for the currentOp aggregation stage */
/* Spec contains { allUsers: <boolean>, idleConnections: <boolean>, idleCursors: <boolean>, idleSessions: <boolean>, localOps: <boolean> } */
DROP FUNCTION IF EXISTS __API_SCHEMA_INTERNAL_V2__.current_op_aggregation;
CREATE OR REPLACE FUNCTION __API_SCHEMA_INTERNAL_V2__.current_op_aggregation(p_spec __CORE_SCHEMA_V2__.bson, OUT document __CORE_SCHEMA_V2__.bson)
RETURNS SETOF __CORE_SCHEMA_V2__.bson
 LANGUAGE c
 VOLATILE STRICT ROWS 100
AS 'MODULE_PATHNAME', $function$command_current_op_aggregation$function$;

DROP FUNCTION IF EXISTS __API_SCHEMA_INTERNAL_V2__.current_op_worker;
CREATE OR REPLACE FUNCTION __API_SCHEMA_INTERNAL_V2__.current_op_worker(p_spec __CORE_SCHEMA_V2__.bson, OUT document __CORE_SCHEMA_V2__.bson)
RETURNS __CORE_SCHEMA_V2__.bson
 LANGUAGE c
 VOLATILE STRICT
AS 'MODULE_PATHNAME', $function$command_current_op_worker$function$;

DROP FUNCTION IF EXISTS __API_SCHEMA_V2__.current_op_command;
CREATE OR REPLACE FUNCTION __API_SCHEMA_V2__.current_op_command(p_spec __CORE_SCHEMA_V2__.bson, OUT document __CORE_SCHEMA_V2__.bson)
RETURNS __CORE_SCHEMA_V2__.bson
LANGUAGE c
VOLATILE STRICT
AS 'MODULE_PATHNAME', $function$command_current_op$function$;
