-- drop the public schema function and move it to helio_api_internal
DROP FUNCTION IF EXISTS helio_api_catalog.bson_dollar_inverse_match;

CREATE OR REPLACE FUNCTION helio_api_internal.bson_dollar_inverse_match(
    document helio_core.bson,
    spec helio_core.bson)
 RETURNS bool
 LANGUAGE c
 IMMUTABLE PARALLEL SAFE STRICT
AS 'MODULE_PATHNAME', $function$bson_dollar_inverse_match$function$;
