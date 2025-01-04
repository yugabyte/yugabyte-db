
/*
 * __API_SCHEMA__.collection() can be used to query collections, e.g.:
 * SELECT * FROM __API_SCHEMA__.collection('db','collection')
 *
 * While this seems slow, we use the planner hook to replace this function
 * directly with the table, so we usually do not call it directly.
 *
 * Output arguments need to match data tables exactly.
 */
CREATE OR REPLACE FUNCTION helio_api.collection(
    p_database_name text,
    p_collection_name text,
    OUT shard_key_value bigint,
    OUT object_id __CORE_SCHEMA__.bson,
    OUT document __CORE_SCHEMA__.bson,
    OUT creation_time timestamptz)
RETURNS SETOF record
LANGUAGE c
 STRICT
AS 'MODULE_PATHNAME', $function$command_api_collection$function$;
COMMENT ON FUNCTION helio_api.collection(text,text)
    IS 'query a Mongo collection';