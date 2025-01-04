CREATE OR REPLACE FUNCTION __EXTENSION_OBJECT__(rumhandler)(internal)
 RETURNS index_am_handler
 LANGUAGE C
AS 'MODULE_PATHNAME', $function$extensionrumhandler$function$;
