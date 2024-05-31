CREATE OR REPLACE FUNCTION __API_CATALOG_SCHEMA__.bson_dollar_project_geonear(__CORE_SCHEMA__.bson, __CORE_SCHEMA__.bson)
 RETURNS __CORE_SCHEMA__.bson
 LANGUAGE c
 IMMUTABLE PARALLEL SAFE STRICT
AS 'MODULE_PATHNAME', $function$bson_dollar_project_geonear$function$;

CREATE OR REPLACE FUNCTION __API_CATALOG_SCHEMA__.bson_geonear_distance(__CORE_SCHEMA__.bson, __CORE_SCHEMA__.bson)
 RETURNS float8
 LANGUAGE c
 IMMUTABLE PARALLEL SAFE STRICT
AS 'MODULE_PATHNAME', $function$bson_geonear_distance$function$;

CREATE OR REPLACE FUNCTION helio_api_internal.bson_geonear_within_range(__CORE_SCHEMA__.bson, __CORE_SCHEMA__.bson)
 RETURNS bool
 LANGUAGE c
 IMMUTABLE PARALLEL SAFE STRICT
 SUPPORT __API_CATALOG_SCHEMA__.dollar_support
AS 'MODULE_PATHNAME', $function$bson_geonear_within_range$function$;
