
CREATE OR REPLACE FUNCTION helio_api.coll_stats(
    IN p_database_name text,
    IN p_collection_name text,
    IN p_scale float8 DEFAULT 1)
RETURNS helio_core.bson
LANGUAGE C
AS 'MODULE_PATHNAME', $function$command_coll_stats$function$;
COMMENT ON FUNCTION helio_api.coll_stats(text, text, float8)
    IS 'Returns variety of storage statistics for a given collection';


CREATE OR REPLACE FUNCTION helio_api_internal.coll_stats_worker(
    IN p_database_name text,
    IN p_collection_name text,
    IN p_scale float8 DEFAULT 1)
RETURNS helio_core.bson
LANGUAGE C
AS 'MODULE_PATHNAME', $function$command_coll_stats_worker$function$;


CREATE OR REPLACE FUNCTION helio_api_internal.coll_stats_aggregation(
    IN p_database_name text,
    IN p_collection_name text,
    IN p_collStatsSpec helio_core.bson)
RETURNS helio_core.bson
LANGUAGE C STABLE PARALLEL SAFE STRICT
AS 'MODULE_PATHNAME', $function$command_coll_stats_aggregation$function$;