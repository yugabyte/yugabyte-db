CREATE OR REPLACE FUNCTION __CORE_SCHEMA__.bson_operator_selectivity(internal, oid, internal, integer)
 RETURNS double precision
 LANGUAGE c
 STABLE PARALLEL SAFE STRICT
AS 'MODULE_PATHNAME', $function$bson_operator_selectivity$function$;