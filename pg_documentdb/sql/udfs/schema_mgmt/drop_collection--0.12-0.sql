/* db.collection.drop(writeConcern) */
DROP FUNCTION IF EXISTS __API_SCHEMA_V2__.drop_collection CASCADE;
CREATE OR REPLACE FUNCTION __API_SCHEMA_V2__.drop_collection(
    p_database_name text,
    p_collection_name text,
    p_write_concern __CORE_SCHEMA_V2__.bson default null,
    p_collection_uuid uuid default null,
    p_track_changes bool default true)
RETURNS bool
LANGUAGE C
AS 'MODULE_PATHNAME', $function$command_drop_collection$function$;
COMMENT ON FUNCTION __API_SCHEMA_V2__.drop_collection(text,text,__CORE_SCHEMA_V2__.bson,uuid,bool)
    IS 'drop a collection';
