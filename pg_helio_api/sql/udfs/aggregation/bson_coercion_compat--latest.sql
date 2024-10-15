CREATE OR REPLACE FUNCTION helio_api_internal.helio_core_bson_to_bson(helio_core.bson)
 RETURNS __CORE_SCHEMA__.bson
 LANGUAGE c
 IMMUTABLE PARALLEL SAFE STRICT
AS 'MODULE_PATHNAME', $function$helio_core_bson_to_bson$function$;