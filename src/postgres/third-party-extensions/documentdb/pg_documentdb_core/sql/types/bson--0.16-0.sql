CREATE OR REPLACE FUNCTION __CORE_SCHEMA__.bson_typanalyze(internal)
 RETURNS boolean
 LANGUAGE c
 STABLE PARALLEL SAFE STRICT
AS 'MODULE_PATHNAME', $function$bson_typanalyze$function$;

ALTER TYPE __CORE_SCHEMA__.bson SET ( ANALYZE = __CORE_SCHEMA__.bson_typanalyze );