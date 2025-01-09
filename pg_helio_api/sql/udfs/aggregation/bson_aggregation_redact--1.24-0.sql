CREATE OR REPLACE FUNCTION helio_api_internal.bson_dollar_redact(__CORE_SCHEMA__.bson, __CORE_SCHEMA__.bson, text, __CORE_SCHEMA__.bson)
 RETURNS __CORE_SCHEMA__.bson
 LANGUAGE c
 IMMUTABLE PARALLEL SAFE STRICT
AS 'MODULE_PATHNAME', $function$bson_dollar_redact$function$;
