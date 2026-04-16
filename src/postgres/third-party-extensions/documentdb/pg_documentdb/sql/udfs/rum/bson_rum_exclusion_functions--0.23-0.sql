CREATE OR REPLACE FUNCTION __API_SCHEMA_INTERNAL_V2__.bson_unique_exclusion_index_equal(
    __API_CATALOG_SCHEMA__.shard_key_and_document, __API_CATALOG_SCHEMA__.shard_key_and_document)
 RETURNS boolean
 LANGUAGE C
 IMMUTABLE PARALLEL SAFE STRICT
AS 'MODULE_PATHNAME', $function$bson_unique_exclusion_index_equal$function$;

CREATE FUNCTION __API_SCHEMA_INTERNAL_V2__.gin_bson_exclusion_extract_value(__API_CATALOG_SCHEMA__.shard_key_and_document, internal)
 RETURNS internal
 LANGUAGE C STRICT IMMUTABLE
AS 'MODULE_PATHNAME', $function$gin_bson_exclusion_extract_value$function$;

CREATE OR REPLACE FUNCTION __API_SCHEMA_INTERNAL_V2__.gin_bson_exclusion_options(internal)
 RETURNS void
 LANGUAGE c
 IMMUTABLE PARALLEL SAFE STRICT
AS 'MODULE_PATHNAME', $function$gin_bson_exclusion_options$function$;

CREATE OR REPLACE FUNCTION __API_SCHEMA_INTERNAL_V2__.gin_bson_exclusion_extract_query(__API_CATALOG_SCHEMA__.shard_key_and_document, internal, int2, internal, internal, internal, internal)
 RETURNS void
 LANGUAGE c
 IMMUTABLE PARALLEL SAFE STRICT
AS 'MODULE_PATHNAME', $function$gin_bson_exclusion_extract_query$function$;

CREATE OR REPLACE FUNCTION __API_SCHEMA_INTERNAL_V2__.gin_bson_exclusion_consistent(internal, smallint, anyelement, integer, internal, internal)
 RETURNS boolean
 LANGUAGE c
 IMMUTABLE PARALLEL SAFE STRICT
AS 'MODULE_PATHNAME', $function$gin_bson_exclusion_consistent$function$;