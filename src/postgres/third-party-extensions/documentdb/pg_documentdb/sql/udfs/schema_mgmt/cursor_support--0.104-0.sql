CREATE OR REPLACE FUNCTION __API_SCHEMA_INTERNAL_V2__.cursor_directory_cleanup(expiry_time_seconds int8 DEFAULT NULL)
RETURNS void
LANGUAGE c
VOLATILE
AS 'MODULE_PATHNAME', $function$cursor_directory_cleanup$function$;