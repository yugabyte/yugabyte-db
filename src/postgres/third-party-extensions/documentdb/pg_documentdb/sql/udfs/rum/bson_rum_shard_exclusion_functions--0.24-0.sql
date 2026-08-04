
CREATE OR REPLACE FUNCTION __API_SCHEMA_INTERNAL_V2__.generate_unique_shard_document(p_document __CORE_SCHEMA__.bson, p_shard_key_value bigint, p_unique_spec __CORE_SCHEMA__.bson, p_sparse boolean)
 RETURNS __CORE_SCHEMA__.bson
 LANGUAGE C STRICT IMMUTABLE PARALLEL SAFE
AS 'MODULE_PATHNAME', $function$generate_unique_shard_document$function$;

CREATE OR REPLACE FUNCTION __API_SCHEMA_INTERNAL_V2__.bson_unique_shard_path_equal(__CORE_SCHEMA__.bson, __CORE_SCHEMA__.bson)
 RETURNS boolean
 LANGUAGE C STRICT IMMUTABLE PARALLEL SAFE
AS 'MODULE_PATHNAME', $function$bson_unique_shard_path_equal$function$;

CREATE OR REPLACE FUNCTION __API_SCHEMA_INTERNAL_V2__.gin_bson_unique_shard_extract_value(__CORE_SCHEMA__.bson, internal)
 RETURNS internal
 LANGUAGE C STRICT IMMUTABLE
AS 'MODULE_PATHNAME', $function$gin_bson_unique_shard_extract_value$function$;

CREATE OR REPLACE FUNCTION __API_SCHEMA_INTERNAL_V2__.gin_bson_unique_shard_extract_query(__CORE_SCHEMA__.bson, internal, int2, internal, internal, internal, internal)
 RETURNS void
 LANGUAGE c
 IMMUTABLE PARALLEL SAFE STRICT
AS 'MODULE_PATHNAME', $function$gin_bson_unique_shard_extract_query$function$;

CREATE OR REPLACE FUNCTION __API_SCHEMA_INTERNAL_V2__.gin_bson_unique_shard_pre_consistent(internal,smallint,__CORE_SCHEMA__.bson,int,internal,internal,internal,internal)
 RETURNS void
 LANGUAGE c
 IMMUTABLE PARALLEL SAFE STRICT
AS 'MODULE_PATHNAME', $function$gin_bson_unique_shard_pre_consistent$function$;

CREATE OR REPLACE FUNCTION __API_SCHEMA_INTERNAL_V2__.gin_bson_unique_shard_consistent(internal, smallint, anyelement, integer, internal, internal)
 RETURNS boolean
 LANGUAGE c
 IMMUTABLE PARALLEL SAFE STRICT
AS 'MODULE_PATHNAME', $function$gin_bson_unique_shard_consistent$function$;

CREATE OR REPLACE FUNCTION __API_SCHEMA_INTERNAL_V2__.bson_unique_index_equal(__CORE_SCHEMA__.bson, __CORE_SCHEMA__.bson)
 RETURNS bool
 LANGUAGE C
 IMMUTABLE PARALLEL SAFE STRICT
AS 'MODULE_PATHNAME', $function$bson_unique_index_term_equal$function$;