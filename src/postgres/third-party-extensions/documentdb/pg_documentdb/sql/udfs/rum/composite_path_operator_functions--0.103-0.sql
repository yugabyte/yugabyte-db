
CREATE OR REPLACE FUNCTION __API_SCHEMA_INTERNAL_V2__.gin_bson_composite_path_extract_value(__CORE_SCHEMA__.bson, internal)
 RETURNS internal
 LANGUAGE c
 IMMUTABLE PARALLEL SAFE STRICT
AS 'MODULE_PATHNAME', $function$gin_bson_composite_path_extract_value$function$;

CREATE OR REPLACE FUNCTION __API_SCHEMA_INTERNAL_V2__.gin_bson_composite_path_extract_query(__CORE_SCHEMA__.bson, internal, int2, internal, internal, internal, internal)
 RETURNS internal
 LANGUAGE c
 PARALLEL SAFE STABLE
AS 'MODULE_PATHNAME', $function$gin_bson_composite_path_extract_query$function$;

CREATE OR REPLACE FUNCTION __API_SCHEMA_INTERNAL_V2__.gin_bson_composite_path_consistent(internal, smallint, anyelement, integer, internal, internal)
 RETURNS boolean
 LANGUAGE c
 IMMUTABLE PARALLEL SAFE STRICT
AS 'MODULE_PATHNAME', $function$gin_bson_composite_path_consistent$function$;

CREATE OR REPLACE FUNCTION __API_SCHEMA_INTERNAL_V2__.gin_bson_composite_path_compare_partial(bytea, bytea, int2, internal)
 RETURNS int4
 LANGUAGE c
 IMMUTABLE PARALLEL SAFE STRICT
 AS 'MODULE_PATHNAME', $function$gin_bson_composite_path_compare_partial$function$;

CREATE OR REPLACE FUNCTION __API_SCHEMA_INTERNAL_V2__.gin_bson_composite_path_options(internal)
 RETURNS void
 LANGUAGE c
 IMMUTABLE PARALLEL SAFE STRICT
AS 'MODULE_PATHNAME', $function$gin_bson_composite_path_options$function$;