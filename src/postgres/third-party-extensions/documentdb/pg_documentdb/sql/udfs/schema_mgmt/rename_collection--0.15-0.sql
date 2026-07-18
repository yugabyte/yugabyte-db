/* API that renames a collection given a database and a collection to the specified target name. */
DROP FUNCTION IF EXISTS __API_SCHEMA_V2__.rename_collection;
CREATE OR REPLACE FUNCTION __API_SCHEMA_V2__.rename_collection(
    p_database_name text,
    p_collection_name text,
    p_target_name text,
    p_drop_target bool default false)
RETURNS void
LANGUAGE c
VOLATILE PARALLEL UNSAFE
AS 'MODULE_PATHNAME', $function$command_rename_collection$function$;
COMMENT ON FUNCTION __API_SCHEMA_V2__.rename_collection(text, text, text, bool)
    IS 'rename a collection';