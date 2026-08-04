

CREATE OR REPLACE FUNCTION __API_SCHEMA_INTERNAL_V2__.index_stats_aggregation(
    IN p_database_name text,
    IN p_collection_name text)
RETURNS SETOF __CORE_SCHEMA__.bson
LANGUAGE C STABLE PARALLEL SAFE STRICT ROWS 100
AS 'MODULE_PATHNAME', $function$command_index_stats_aggregation$function$;


CREATE OR REPLACE FUNCTION __API_SCHEMA_INTERNAL_V2__.index_stats_worker(
    IN p_database_name text,
    IN p_collection_name text)
RETURNS __CORE_SCHEMA__.bson
LANGUAGE C STABLE PARALLEL SAFE STRICT
AS 'MODULE_PATHNAME', $function$command_index_stats_worker$function$;