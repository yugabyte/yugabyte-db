
CREATE OR REPLACE FUNCTION __API_SCHEMA_INTERNAL_V2__.bson_orderby_compare_sort_support(internal)
RETURNS void
 LANGUAGE c
 IMMUTABLE PARALLEL SAFE STRICT
AS 'MODULE_PATHNAME', $function$bson_orderby_compare_sort_support$function$;