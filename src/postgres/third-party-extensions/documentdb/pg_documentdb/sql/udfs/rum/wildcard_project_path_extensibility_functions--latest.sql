-- Note that the functions here have the prefix 'gin' even though they may be used on the RUM index.
-- This is because the RUM docs also use the same constants/functions as GIN extensibility for all existing functionality.
-- Consequently we reuse the gin prefix for functionality shared with GIN.

CREATE OR REPLACE FUNCTION __API_CATALOG_SCHEMA__.gin_bson_wildcard_project_extract_value(__CORE_SCHEMA__.bson, internal)
 RETURNS internal
 LANGUAGE c
 IMMUTABLE PARALLEL SAFE STRICT
AS 'MODULE_PATHNAME', $function$gin_bson_wildcard_project_extract_value$function$;

CREATE OR REPLACE FUNCTION __API_CATALOG_SCHEMA__.gin_bson_wildcard_project_options(internal)
 RETURNS void
 LANGUAGE c
 IMMUTABLE PARALLEL SAFE STRICT
AS 'MODULE_PATHNAME', $function$gin_bson_wildcard_project_options$function$;
