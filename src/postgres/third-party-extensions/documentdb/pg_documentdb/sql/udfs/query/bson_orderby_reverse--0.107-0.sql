
CREATE OR REPLACE FUNCTION __API_SCHEMA_INTERNAL_V2__.bson_orderby_reverse(__CORE_SCHEMA__.bson, __CORE_SCHEMA__.bson)
 RETURNS __CORE_SCHEMA__.bson
 LANGUAGE c
 IMMUTABLE PARALLEL SAFE STRICT
AS 'MODULE_PATHNAME', $function$command_bson_orderby_reverse$function$;
