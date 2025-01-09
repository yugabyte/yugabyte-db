CREATE OR REPLACE FUNCTION __API_SCHEMA_INTERNAL_V2__.bson_densify_range(__CORE_SCHEMA__.bson, __CORE_SCHEMA__.bson)
 RETURNS __CORE_SCHEMA__.bson
 LANGUAGE c
 WINDOW
 STABLE
AS 'MODULE_PATHNAME', $function$bson_densify_range$function$;

CREATE OR REPLACE FUNCTION __API_SCHEMA_INTERNAL_V2__.bson_densify_partition(__CORE_SCHEMA__.bson, __CORE_SCHEMA__.bson)
 RETURNS __CORE_SCHEMA__.bson
 LANGUAGE c
 WINDOW
 STABLE
AS 'MODULE_PATHNAME', $function$bson_densify_partition$function$;

CREATE OR REPLACE FUNCTION __API_SCHEMA_INTERNAL_V2__.bson_densify_full(__CORE_SCHEMA__.bson, __CORE_SCHEMA__.bson)
 RETURNS __CORE_SCHEMA__.bson
 LANGUAGE c
 WINDOW
 STABLE
AS 'MODULE_PATHNAME', $function$bson_densify_full$function$;
