
DROP FUNCTION IF EXISTS __API_SCHEMA_INTERNAL_V2__.rum_bson_single_path_extract_tsvector;
CREATE OR REPLACE FUNCTION __API_SCHEMA_INTERNAL_V2__.rum_bson_single_path_extract_tsvector(__CORE_SCHEMA__.bson, internal, internal, internal, internal)
 RETURNS internal
 LANGUAGE c
 IMMUTABLE PARALLEL SAFE STRICT
AS 'MODULE_PATHNAME', $function$rum_bson_single_path_extract_tsvector$function$;

DROP FUNCTION IF EXISTS __API_SCHEMA_INTERNAL_V2__.rum_bson_text_path_options;
CREATE OR REPLACE FUNCTION __API_SCHEMA_INTERNAL_V2__.rum_bson_text_path_options(internal)
 RETURNS void
 LANGUAGE c
 IMMUTABLE PARALLEL SAFE STRICT
AS 'MODULE_PATHNAME', $function$rum_bson_text_path_options$function$;