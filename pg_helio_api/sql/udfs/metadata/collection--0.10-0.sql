
/*
 * __API_SCHEMA__.collection() can be used to query collections, e.g.:
 * SELECT * FROM __API_SCHEMA__.collection('db','collection')
 *
 * While this seems slow, we use the planner hook to replace this function
 * directly with the table, so we usually do not call it directly.
 *
 * Output arguments need to match data tables exactly.
 */
CREATE OR REPLACE FUNCTION __API_SCHEMA__.collection(
    p_database_name text,
    p_collection_name text,
    OUT document __CORE_SCHEMA__.bson)
RETURNS SETOF __CORE_SCHEMA__.bson
LANGUAGE c
 STRICT
 SUPPORT __API_SCHEMA_INTERNAL__.aggregation_support
AS 'MODULE_PATHNAME', $function$command_api_collection$function$;
COMMENT ON FUNCTION __API_SCHEMA__.collection(text,text)
    IS 'query a Mongo collection';