-- collMod database command implementation for the Mongo wire protocol
CREATE OR REPLACE FUNCTION __API_SCHEMA_V2__.coll_mod(
    IN p_database_name text,
    IN p_collection_name text, 
    IN p_spec __CORE_SCHEMA_V2__.bson)
RETURNS __CORE_SCHEMA_V2__.bson
LANGUAGE C
VOLATILE PARALLEL UNSAFE
AS 'MODULE_PATHNAME', $function$command_coll_mod$function$;
COMMENT ON FUNCTION __API_SCHEMA_V2__.coll_mod(text, text, __CORE_SCHEMA_V2__.bson)
    IS 'Updates the specification of collection';

