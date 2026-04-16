CREATE OR REPLACE FUNCTION __API_SCHEMA_V2__.db_stats(
    IN p_database_name text,
    IN p_scale float8 DEFAULT 1,
    IN p_freeStorage bool DEFAULT false)
RETURNS __CORE_SCHEMA__.bson
LANGUAGE C
AS 'MODULE_PATHNAME', $function$command_db_stats$function$;
COMMENT ON FUNCTION __API_SCHEMA_V2__.db_stats(text, float8, bool)
    IS 'Returns storage statistics for a given database';


CREATE OR REPLACE FUNCTION __API_SCHEMA_INTERNAL_V2__.db_stats_worker(
    IN p_collection_ids bigint[])
RETURNS __CORE_SCHEMA__.bson
LANGUAGE C
AS 'MODULE_PATHNAME', $function$command_db_stats_worker$function$;
