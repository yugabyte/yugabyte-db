
CREATE OR REPLACE FUNCTION __API_SCHEMA_V2__.coll_stats(
    IN p_database_name text,
    IN p_collection_name text,
    IN p_scale float8 DEFAULT 1)
RETURNS __CORE_SCHEMA_V2__.bson
LANGUAGE C
AS 'MODULE_PATHNAME', $function$command_coll_stats$function$;
COMMENT ON FUNCTION __API_SCHEMA_V2__.coll_stats(text, text, float8)
    IS 'Returns variety of storage statistics for a given collection';


CREATE OR REPLACE FUNCTION __API_SCHEMA_INTERNAL_V2__.coll_stats_worker(
    IN p_database_name text,
    IN p_collection_name text,
    IN p_scale float8 DEFAULT 1)
RETURNS __CORE_SCHEMA_V2__.bson
LANGUAGE C
AS 'MODULE_PATHNAME', $function$command_coll_stats_worker$function$;


CREATE OR REPLACE FUNCTION __API_SCHEMA_INTERNAL_V2__.coll_stats_aggregation(
    IN p_database_name text,
    IN p_collection_name text,
    IN p_collStatsSpec __CORE_SCHEMA_V2__.bson)
RETURNS __CORE_SCHEMA_V2__.bson
LANGUAGE C STABLE PARALLEL SAFE STRICT
AS 'MODULE_PATHNAME', $function$command_coll_stats_aggregation$function$;