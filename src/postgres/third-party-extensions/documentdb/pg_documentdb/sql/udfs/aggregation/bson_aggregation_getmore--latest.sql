DROP FUNCTION IF EXISTS __API_CATALOG_SCHEMA__.bson_aggregation_getmore;

-- This function is the `aggregation get more` function.
-- It is a wrapper function that passes the get more from the GW to the backend
-- such that the planner phase can replace the parameterized value in the planning phase.
CREATE OR REPLACE FUNCTION __API_CATALOG_SCHEMA__.bson_aggregation_getmore(
    databaseName text,
    getMoreSpec __CORE_SCHEMA__.bson,
    continuationSpec __CORE_SCHEMA__.bson,
    OUT document __CORE_SCHEMA__.bson)
 RETURNS SETOF __CORE_SCHEMA__.bson
 LANGUAGE c
 IMMUTABLE PARALLEL SAFE STRICT
AS 'MODULE_PATHNAME', $function$command_bson_aggregation_getmore$function$;
