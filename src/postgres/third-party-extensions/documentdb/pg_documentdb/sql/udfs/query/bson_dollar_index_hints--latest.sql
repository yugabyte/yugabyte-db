
CREATE OR REPLACE FUNCTION __API_SCHEMA_INTERNAL_V2__.bson_dollar_index_hint(document __CORE_SCHEMA__.bson, index_name text, key_document __CORE_SCHEMA__.bson, is_sparse boolean)
 RETURNS bool
 LANGUAGE c
 IMMUTABLE PARALLEL SAFE STRICT
AS 'MODULE_PATHNAME', $function$bson_dollar_index_hint$function$;
