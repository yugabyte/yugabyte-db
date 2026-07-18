-- compact database command implementation for the wire protocol
CREATE OR REPLACE FUNCTION __API_SCHEMA_V2__.compact(
    IN p_spec __CORE_SCHEMA_V2__.bson)
RETURNS __CORE_SCHEMA_V2__.bson
LANGUAGE C
VOLATILE PARALLEL UNSAFE STRICT
AS 'MODULE_PATHNAME', $function$command_compact$function$;
COMMENT ON FUNCTION __API_SCHEMA_V2__.compact(__CORE_SCHEMA_V2__.bson)
    IS 'Compact the collection data tables and indexes';


CREATE OR REPLACE FUNCTION __API_SCHEMA_INTERNAL_V2__.get_bloat_stats_worker(
    p_collection_id INT8)
RETURNS __CORE_SCHEMA_V2__.bson
LANGUAGE C
PARALLEL SAFE STRICT
AS 'MODULE_PATHNAME', $function$get_bloat_stats_worker$function$;
COMMENT ON FUNCTION __API_SCHEMA_INTERNAL_V2__.get_bloat_stats_worker(INT8)
    IS 'Gets bloat stats for a collection table in the worker';
