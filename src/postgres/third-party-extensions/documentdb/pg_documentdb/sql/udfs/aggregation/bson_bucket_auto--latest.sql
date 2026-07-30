CREATE OR REPLACE FUNCTION __API_SCHEMA_INTERNAL_V2__.bson_dollar_bucket_auto(document __CORE_SCHEMA__.bson, spec __CORE_SCHEMA__.bson)
 RETURNS __CORE_SCHEMA__.bson
 LANGUAGE c
 STABLE
 WINDOW
AS 'MODULE_PATHNAME', $function$bson_dollar_bucket_auto$function$;
