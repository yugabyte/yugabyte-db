CREATE OR REPLACE FUNCTION __API_SCHEMA_INTERNAL_V2__.bson_dollar_selectivity(internal, oid, internal, integer)
 RETURNS double precision
 LANGUAGE c
 STABLE PARALLEL SAFE STRICT
AS 'MODULE_PATHNAME', $function$bson_dollar_selectivity$function$;