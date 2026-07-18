CREATE OR REPLACE FUNCTION __API_SCHEMA_INTERNAL_V2__.bson_dollar_redact(__CORE_SCHEMA__.bson, __CORE_SCHEMA__.bson, text, __CORE_SCHEMA__.bson)
 RETURNS __CORE_SCHEMA__.bson
 LANGUAGE c
 IMMUTABLE PARALLEL SAFE STRICT
AS 'MODULE_PATHNAME', $function$bson_dollar_redact$function$;

CREATE OR REPLACE FUNCTION __API_SCHEMA_INTERNAL_V2__.bson_dollar_redact(document __CORE_SCHEMA__.bson, redactSpec __CORE_SCHEMA__.bson, redactSpecText text, variableSpec __CORE_SCHEMA__.bson, collationString text)
 RETURNS __CORE_SCHEMA__.bson
 LANGUAGE c
 IMMUTABLE PARALLEL SAFE CALLED ON NULL INPUT
AS 'MODULE_PATHNAME', $function$bson_dollar_redact$function$;
