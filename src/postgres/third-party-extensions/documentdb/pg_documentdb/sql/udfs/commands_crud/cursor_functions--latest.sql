DROP FUNCTION IF EXISTS __API_SCHEMA_V2__.current_cursor_state(__CORE_SCHEMA_V2__.bson);

DROP FUNCTION IF EXISTS __API_SCHEMA_V2__.cursor_state(__CORE_SCHEMA_V2__.bson, __CORE_SCHEMA_V2__.bson);

-- This function is STABLE (Not Volatile) as within 1 transaction snapshot
-- The value given the "document" does not change.
-- However it's not guaranteed to be the same across transaction snapshots.
CREATE OR REPLACE FUNCTION __API_SCHEMA_INTERNAL_V2__.current_cursor_state(__CORE_SCHEMA_V2__.bson)
 RETURNS __CORE_SCHEMA_V2__.bson
 LANGUAGE c
 STABLE
AS 'MODULE_PATHNAME', $function$command_current_cursor_state$function$;

CREATE OR REPLACE FUNCTION __API_SCHEMA_INTERNAL_V2__.cursor_state(__CORE_SCHEMA_V2__.bson, __CORE_SCHEMA_V2__.bson)
 RETURNS bool
 LANGUAGE c
 IMMUTABLE STRICT
AS 'MODULE_PATHNAME', $function$command_cursor_state$function$;