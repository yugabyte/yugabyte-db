
CREATE OR REPLACE FUNCTION helio_api.binary_version()
RETURNS text
LANGUAGE C
IMMUTABLE PARALLEL SAFE STRICT
AS 'MODULE_PATHNAME', $function$get_helio_api_binary_version$function$;

CREATE OR REPLACE FUNCTION helio_api.binary_extended_version()
RETURNS text
LANGUAGE C
IMMUTABLE PARALLEL SAFE STRICT
AS 'MODULE_PATHNAME', $function$get_helio_api_extended_binary_version$function$;