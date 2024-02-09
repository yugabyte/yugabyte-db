
/* db.createCollection(name) */
CREATE OR REPLACE FUNCTION __API_SCHEMA__.create_collection(p_database_name text, p_collection_name text)
RETURNS bool
SET search_path TO __API_CATALOG_SCHEMA__,__CORE_SCHEMA__, pg_catalog
LANGUAGE c
 STRICT
AS 'MODULE_PATHNAME', $function$command_create_collection_core$function$;
COMMENT ON FUNCTION __API_SCHEMA__.create_collection(p_database_name text, p_collection_name text)
    IS 'create a Mongo collection';
