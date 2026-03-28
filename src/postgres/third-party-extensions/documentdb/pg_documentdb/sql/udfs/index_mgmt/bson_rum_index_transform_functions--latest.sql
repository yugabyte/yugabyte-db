CREATE FUNCTION __API_SCHEMA_INTERNAL__.bson_index_transform(bytea, bytea, int2, internal)
 RETURNS bytea
 LANGUAGE C IMMUTABLE PARALLEL SAFE
AS 'MODULE_PATHNAME', $function$gin_bson_composite_index_term_transform$function$;