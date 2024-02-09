
/* db.collection.shardCollection(key) */
DROP FUNCTION IF EXISTS __API_SCHEMA__.shard_collection CASCADE;
CREATE OR REPLACE FUNCTION __API_SCHEMA__.shard_collection(
    p_database_name text,
    p_collection_name text,
    p_shard_key __CORE_SCHEMA__.bson,
    p_is_reshard bool default true)
 RETURNS void
 LANGUAGE C
   STRICT
 AS 'MODULE_PATHNAME', $$command_shard_collection$$;
 COMMENT ON FUNCTION __API_SCHEMA__.shard_collection(text, text,__CORE_SCHEMA__.bson,bool)
    IS 'mongo wire protocol command to shard or reshard a mongo collection';