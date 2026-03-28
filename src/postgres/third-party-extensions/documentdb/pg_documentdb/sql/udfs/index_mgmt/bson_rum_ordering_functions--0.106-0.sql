
CREATE FUNCTION __API_SCHEMA_INTERNAL__.bson_rum_composite_ordering(bytea, __CORE_SCHEMA__.bson, int2, internal)
 RETURNS __CORE_SCHEMA__.bson
 LANGUAGE C
 IMMUTABLE PARALLEL SAFE
AS 'MODULE_PATHNAME', $function$gin_bson_composite_ordering_transform$function$;