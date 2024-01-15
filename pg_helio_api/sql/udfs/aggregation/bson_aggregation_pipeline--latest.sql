DROP FUNCTION IF EXISTS __API_CATALOG_SCHEMA__.bson_aggregation_pipeline;

-- This function is the Mongo aggregation pipeline function.
-- It is a wrapper function that passes the aggregation pipeline from the GW to the backend
-- such that the planner phase can replace the parameterized value in the planning phase.
CREATE OR REPLACE FUNCTION __API_CATALOG_SCHEMA__.bson_aggregation_pipeline(
    databaseName text,
    aggregationPipeline __CORE_SCHEMA__.bson,
    OUT document __CORE_SCHEMA__.bson)
 RETURNS SETOF __CORE_SCHEMA__.bson
 LANGUAGE c
 IMMUTABLE PARALLEL SAFE STRICT
 SUPPORT __API_SCHEMA_INTERNAL__.aggregation_support
AS 'MODULE_PATHNAME', $function$command_bson_aggregation_pipeline$function$;
