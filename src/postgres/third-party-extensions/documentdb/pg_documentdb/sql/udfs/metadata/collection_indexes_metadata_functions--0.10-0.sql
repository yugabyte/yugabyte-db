CREATE OR REPLACE FUNCTION __API_SCHEMA_INTERNAL__.__EXTENSION_OBJECT__(_get_next_collection_index_id)()
 RETURNS integer
 LANGUAGE c
 STRICT
AS 'MODULE_PATHNAME', $function$command_get_next_collection_index_id$function$;
COMMENT ON FUNCTION __API_SCHEMA_INTERNAL__.__EXTENSION_OBJECT__(_get_next_collection_index_id)()
    IS 'get next unique index id to be used in __API_CATALOG_SCHEMA__.collection_indexes';