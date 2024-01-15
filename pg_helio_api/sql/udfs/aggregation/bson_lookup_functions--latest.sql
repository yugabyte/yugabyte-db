
CREATE OR REPLACE FUNCTION __API_CATALOG_SCHEMA__.bson_dollar_lookup_extract_filter_expression(__CORE_SCHEMA__.bson, __CORE_SCHEMA__.bson)
 RETURNS SETOF __CORE_SCHEMA__.bson
 LANGUAGE c
 IMMUTABLE PARALLEL SAFE STRICT ROWS 100
AS 'MODULE_PATHNAME', $function$bson_dollar_lookup_extract_filter_expression$function$;


CREATE OR REPLACE FUNCTION __API_CATALOG_SCHEMA__.bson_lookup_unwind(__CORE_SCHEMA__.bson, text)
 RETURNS SETOF __CORE_SCHEMA__.bson
 LANGUAGE c
 IMMUTABLE PARALLEL SAFE STRICT ROWS 100
AS 'MODULE_PATHNAME', $function$bson_lookup_unwind$function$;