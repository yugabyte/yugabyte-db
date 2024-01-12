
CREATE OR REPLACE FUNCTION __API_SCHEMA_INTERNAL__.__EXTENSION_OBJECT__(_get_next_collection_id)()
 RETURNS bigint
 LANGUAGE c
 STRICT
AS 'MODULE_PATHNAME', $function$command_get_next_collection_id$function$;
COMMENT ON FUNCTION __API_SCHEMA_INTERNAL__.__EXTENSION_OBJECT__(_get_next_collection_id)()
    IS 'get next unique collection id to be used in __API_CATALOG_SCHEMA__.collections';

CREATE OR REPLACE FUNCTION __API_SCHEMA_INTERNAL__.ensure_valid_db_coll(text, text)
 RETURNS bool
 LANGUAGE c
 STRICT
AS 'MODULE_PATHNAME', $function$command_ensure_valid_db_coll$function$;
