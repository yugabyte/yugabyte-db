
CREATE OR REPLACE FUNCTION __API_CATALOG_SCHEMA__.bson_dollar_unwind(__CORE_SCHEMA__.bson, text)
 RETURNS SETOF __CORE_SCHEMA__.bson
 LANGUAGE c
 IMMUTABLE PARALLEL SAFE STRICT ROWS 100
AS 'MODULE_PATHNAME', $function$bson_dollar_unwind$function$;

CREATE OR REPLACE FUNCTION __API_CATALOG_SCHEMA__.bson_dollar_unwind(__CORE_SCHEMA__.bson, __CORE_SCHEMA__.bson)
 RETURNS SETOF __CORE_SCHEMA__.bson
 LANGUAGE c
 IMMUTABLE PARALLEL SAFE STRICT ROWS 100
AS 'MODULE_PATHNAME', $function$bson_dollar_unwind_with_options$function$;

CREATE OR REPLACE FUNCTION __API_CATALOG_SCHEMA__.bson_distinct_unwind(__CORE_SCHEMA__.bson, text)
 RETURNS SETOF __CORE_SCHEMA__.bson
 LANGUAGE c
 IMMUTABLE PARALLEL SAFE STRICT ROWS 100
AS 'MODULE_PATHNAME', $function$bson_distinct_unwind$function$;

CREATE OR REPLACE FUNCTION __API_CATALOG_SCHEMA__.bson_build_distinct_response(__CORE_SCHEMA__.bson[])
 RETURNS __CORE_SCHEMA__.bson
 LANGUAGE c
 IMMUTABLE PARALLEL SAFE
AS 'MODULE_PATHNAME', $function$bson_build_distinct_response$function$;
