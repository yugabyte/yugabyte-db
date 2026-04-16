-- drop the public schema function and move it to __API_SCHEMA_INTERNAL_V2__
DROP FUNCTION IF EXISTS __API_CATALOG_SCHEMA_V2__.bson_dollar_inverse_match;

CREATE OR REPLACE FUNCTION __API_SCHEMA_INTERNAL_V2__.bson_dollar_inverse_match(
    document __CORE_SCHEMA_V2__.bson,
    spec __CORE_SCHEMA_V2__.bson)
 RETURNS bool
 LANGUAGE c
 IMMUTABLE PARALLEL SAFE STRICT
AS 'MODULE_PATHNAME', $function$bson_dollar_inverse_match$function$;
