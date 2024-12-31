CREATE OR REPLACE FUNCTION helio_core.bson_typanalyze(internal)
 RETURNS boolean
 LANGUAGE c
 STABLE PARALLEL SAFE STRICT
AS 'MODULE_PATHNAME', $function$bson_typanalyze$function$;

ALTER TYPE bson SET ( ANALYZE = helio_core.bson_typanalyze );