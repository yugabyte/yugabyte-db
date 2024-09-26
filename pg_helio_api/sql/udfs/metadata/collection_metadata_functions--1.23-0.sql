
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

CREATE FUNCTION helio_api_internal.collection_update_trigger()
 RETURNS trigger
 LANGUAGE plpgsql
AS $function$
BEGIN
	PERFORM helio_api_internal.invalidate_collection_cache();
	RETURN NULL;
END;
$function$;

CREATE OR REPLACE FUNCTION helio_api_internal.invalidate_collection_cache()
 RETURNS void
 LANGUAGE c
AS 'MODULE_PATHNAME', $$command_invalidate_collection_cache$$;