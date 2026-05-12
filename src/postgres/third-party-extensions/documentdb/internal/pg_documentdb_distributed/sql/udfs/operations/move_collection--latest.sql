
CREATE OR REPLACE FUNCTION __API_DISTRIBUTED_SCHEMA__.move_collection(
    move_collection_spec __CORE_SCHEMA_V2__.bson)
 RETURNS __CORE_SCHEMA_V2__.bson
 LANGUAGE C
AS 'MODULE_PATHNAME', $$documentdb_command_move_collection$$;