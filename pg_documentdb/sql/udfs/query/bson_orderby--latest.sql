
CREATE OR REPLACE FUNCTION __API_CATALOG_SCHEMA__.bson_orderby(__CORE_SCHEMA__.bson, __CORE_SCHEMA__.bson)
 RETURNS __CORE_SCHEMA__.bson
 LANGUAGE c
 IMMUTABLE PARALLEL SAFE STRICT
AS 'MODULE_PATHNAME', $function$command_bson_orderby$function$;


-- ORDER BY operator function for $setWindowFields
CREATE OR REPLACE FUNCTION helio_api_internal.bson_orderby_partition(__CORE_SCHEMA__.bson, __CORE_SCHEMA__.bson, boolean)
 RETURNS __CORE_SCHEMA__.bson
 LANGUAGE c
 IMMUTABLE PARALLEL SAFE STRICT
AS 'MODULE_PATHNAME', $function$bson_orderby_partition$function$;
