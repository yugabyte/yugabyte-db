/* db.collection.drop(writeConcern) */
DROP FUNCTION IF EXISTS helio_api.drop_collection CASCADE;
CREATE OR REPLACE FUNCTION helio_api.drop_collection(
    p_database_name text,
    p_collection_name text,
    p_write_concern helio_core.bson default null,
    p_collection_uuid uuid default null,
    p_track_changes bool default true)
RETURNS bool
LANGUAGE C
AS 'MODULE_PATHNAME', $function$command_drop_collection$function$;
COMMENT ON FUNCTION helio_api.drop_collection(text,text,helio_core.bson,uuid,bool)
    IS 'drop a collection';
