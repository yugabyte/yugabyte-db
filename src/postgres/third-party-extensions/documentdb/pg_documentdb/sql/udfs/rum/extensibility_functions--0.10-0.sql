-- Note that the functions here have the prefix 'gin' even though they may be used on the RUM index.
-- This is because the RUM docs also use the same constants/functions as GIN extensibility for all existing functionality.
-- Consequently we reuse the gin prefix for functionality shared with GIN.

CREATE OR REPLACE FUNCTION __API_CATALOG_SCHEMA__.gin_bson_compare(bytea, bytea)
 RETURNS bool
 LANGUAGE c
 IMMUTABLE PARALLEL SAFE STRICT
AS 'MODULE_PATHNAME', $function$gin_bson_compare$function$;

CREATE OR REPLACE FUNCTION __API_CATALOG_SCHEMA__.gin_bson_extract_query(__CORE_SCHEMA__.bson, internal, int2, internal, internal, internal, internal)
 RETURNS internal
 LANGUAGE c
 PARALLEL SAFE STABLE
AS 'MODULE_PATHNAME', $function$gin_bson_extract_query$function$;

CREATE OR REPLACE FUNCTION __API_CATALOG_SCHEMA__.gin_bson_compare_partial(bytea, bytea, int2, internal)
 RETURNS int4
 LANGUAGE c
 IMMUTABLE PARALLEL SAFE STRICT
 AS 'MODULE_PATHNAME', $function$gin_bson_compare_partial$function$;

CREATE OR REPLACE FUNCTION __API_CATALOG_SCHEMA__.gin_bson_consistent(internal, smallint, anyelement, integer, internal, internal)
 RETURNS boolean
 LANGUAGE c
 IMMUTABLE PARALLEL SAFE STRICT
AS 'MODULE_PATHNAME', $function$gin_bson_consistent$function$;