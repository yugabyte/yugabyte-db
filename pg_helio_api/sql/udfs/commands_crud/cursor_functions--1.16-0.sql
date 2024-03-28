DROP FUNCTION IF EXISTS helio_api.current_cursor_state(helio_core.bson);

DROP FUNCTION IF EXISTS helio_api.cursor_state(helio_core.bson, helio_core.bson);

-- This function is STABLE (Not Volatile) as within 1 transaction snapshot
-- The value given the "document" does not change.
-- However it's not guaranteed to be the same across transaction snapshots.
CREATE OR REPLACE FUNCTION helio_api_internal.current_cursor_state(helio_core.bson)
 RETURNS helio_core.bson
 LANGUAGE c
 STABLE
AS 'MODULE_PATHNAME', $function$command_current_cursor_state$function$;

CREATE OR REPLACE FUNCTION helio_api_internal.cursor_state(helio_core.bson, helio_core.bson)
 RETURNS bool
 LANGUAGE c
 IMMUTABLE STRICT
AS 'MODULE_PATHNAME', $function$command_cursor_state$function$;