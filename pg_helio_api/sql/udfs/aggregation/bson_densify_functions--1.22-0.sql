CREATE OR REPLACE FUNCTION helio_api_internal.bson_densify_range(__CORE_SCHEMA__.bson, __CORE_SCHEMA__.bson)
 RETURNS __CORE_SCHEMA__.bson
 LANGUAGE c
 WINDOW
 STABLE
AS 'MODULE_PATHNAME', $function$bson_densify_range$function$;

CREATE OR REPLACE FUNCTION helio_api_internal.bson_densify_partition(__CORE_SCHEMA__.bson, __CORE_SCHEMA__.bson)
 RETURNS __CORE_SCHEMA__.bson
 LANGUAGE c
 WINDOW
 STABLE
AS 'MODULE_PATHNAME', $function$bson_densify_partition$function$;

CREATE OR REPLACE FUNCTION helio_api_internal.bson_densify_full(__CORE_SCHEMA__.bson, __CORE_SCHEMA__.bson)
 RETURNS __CORE_SCHEMA__.bson
 LANGUAGE c
 WINDOW
 STABLE
AS 'MODULE_PATHNAME', $function$bson_densify_full$function$;

CREATE OR REPLACE FUNCTION helio_api_internal.densify_partition_by_fields(__CORE_SCHEMA__.bson, __CORE_SCHEMA__.bson)
 RETURNS __CORE_SCHEMA__.bson
 LANGUAGE c
 STABLE
AS 'MODULE_PATHNAME', $function$densify_partition_by_fields$function$;
