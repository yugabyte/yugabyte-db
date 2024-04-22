CREATE OR REPLACE FUNCTION helio_api_internal.bson_dollar_selectivity(internal, oid, internal, integer)
 RETURNS double precision
 LANGUAGE c
 STABLE PARALLEL SAFE STRICT
AS 'MODULE_PATHNAME', $function$bson_dollar_selectivity$function$;