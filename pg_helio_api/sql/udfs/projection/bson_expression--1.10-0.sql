
CREATE OR REPLACE FUNCTION __API_CATALOG_SCHEMA__.bson_expression_get(document __CORE_SCHEMA__.bson, expressionSpec __CORE_SCHEMA__.bson, isNullOnEmpty bool default false)
 RETURNS __CORE_SCHEMA__.bson
 LANGUAGE c
 IMMUTABLE PARALLEL SAFE STRICT
AS 'MODULE_PATHNAME', $function$bson_expression_get$function$;

CREATE OR REPLACE FUNCTION __API_CATALOG_SCHEMA__.bson_expression_map(document __CORE_SCHEMA__.bson, sourceArrayName text, expressionSpec __CORE_SCHEMA__.bson, isNullOnEmpty bool default false)
 RETURNS __CORE_SCHEMA__.bson
 LANGUAGE c
 IMMUTABLE PARALLEL SAFE STRICT
AS 'MODULE_PATHNAME', $function$bson_expression_map$function$;
