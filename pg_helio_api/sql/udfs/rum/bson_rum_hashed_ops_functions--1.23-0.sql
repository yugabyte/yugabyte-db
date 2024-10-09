CREATE FUNCTION helio_api_internal.gin_bson_hashed_extract_value(__CORE_SCHEMA__.bson, internal)
 RETURNS internal
 LANGUAGE C STRICT IMMUTABLE
AS 'MODULE_PATHNAME', $function$gin_bson_hashed_extract_value$function$;

CREATE OR REPLACE FUNCTION helio_api_internal.gin_bson_hashed_options(internal)
 RETURNS void
 LANGUAGE c
 IMMUTABLE PARALLEL SAFE STRICT
AS 'MODULE_PATHNAME', $function$gin_bson_hashed_options$function$;

CREATE OR REPLACE FUNCTION helio_api_internal.gin_bson_hashed_extract_query(__CORE_SCHEMA__.bson, internal, int2, internal, internal, internal, internal)
 RETURNS void
 LANGUAGE c
 IMMUTABLE PARALLEL SAFE STRICT
AS 'MODULE_PATHNAME', $function$gin_bson_hashed_extract_query$function$;

CREATE OR REPLACE FUNCTION helio_api_internal.gin_bson_hashed_consistent(internal, smallint, anyelement, integer, internal, internal)
 RETURNS boolean
 LANGUAGE c
 IMMUTABLE PARALLEL SAFE STRICT
AS 'MODULE_PATHNAME', $function$gin_bson_hashed_consistent$function$;
