DROP FUNCTION IF EXISTS __API_CATALOG_SCHEMA_V2__.bson_dollar_lookup_extract_filter_expression;

CREATE OR REPLACE FUNCTION __API_SCHEMA_INTERNAL_V2__.bson_dollar_lookup_extract_filter_expression(__CORE_SCHEMA__.bson, __CORE_SCHEMA__.bson)
 RETURNS __CORE_SCHEMA__.bson
 LANGUAGE c
 IMMUTABLE PARALLEL SAFE STRICT
AS 'MODULE_PATHNAME', $function$bson_dollar_lookup_extract_filter_expression$function$;

CREATE OR REPLACE FUNCTION __API_CATALOG_SCHEMA__.bson_lookup_unwind(__CORE_SCHEMA__.bson, text)
 RETURNS SETOF __CORE_SCHEMA__.bson
 LANGUAGE c
 IMMUTABLE PARALLEL SAFE STRICT ROWS 100
AS 'MODULE_PATHNAME', $function$bson_lookup_unwind$function$;

CREATE OR REPLACE FUNCTION __API_SCHEMA_INTERNAL_V2__.bson_dollar_lookup_extract_filter_array(__CORE_SCHEMA__.bson, __CORE_SCHEMA__.bson)
 RETURNS __CORE_SCHEMA__.bson[]
 LANGUAGE c
 IMMUTABLE PARALLEL SAFE STRICT
AS 'MODULE_PATHNAME', $function$bson_dollar_lookup_extract_filter_array$function$;

CREATE OR REPLACE FUNCTION __API_SCHEMA_INTERNAL_V2__.bson_dollar_lookup_filter_support(internal)
 RETURNS internal
 LANGUAGE c
 IMMUTABLE PARALLEL SAFE STRICT
AS 'MODULE_PATHNAME', $$bson_dollar_lookup_filter_support$$;

CREATE OR REPLACE FUNCTION __API_SCHEMA_INTERNAL_V2__.bson_dollar_lookup_join_filter(__CORE_SCHEMA__.bson, __CORE_SCHEMA__.bson, text)
 RETURNS bool
 LANGUAGE c
 SUPPORT __API_SCHEMA_INTERNAL_V2__.bson_dollar_lookup_filter_support
 IMMUTABLE PARALLEL SAFE STRICT
AS 'MODULE_PATHNAME', $function$bson_dollar_lookup_join_filter$function$;

CREATE OR REPLACE FUNCTION __API_SCHEMA_INTERNAL_V2__.bson_dollar_lookup_project(__CORE_SCHEMA__.bson, __CORE_SCHEMA__.bson[], text)
 RETURNS SETOF __CORE_SCHEMA__.bson
 LANGUAGE c
 IMMUTABLE PARALLEL SAFE STRICT ROWS 100
AS 'MODULE_PATHNAME', $function$bson_dollar_lookup_project$function$;