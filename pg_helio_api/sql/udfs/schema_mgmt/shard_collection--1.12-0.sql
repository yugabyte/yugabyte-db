
/* mongo wire protocol command to shard or reshard a collection */
DROP FUNCTION IF EXISTS helio_api.shard_collection CASCADE;
CREATE OR REPLACE FUNCTION helio_api.shard_collection(
    p_database_name text,
    p_collection_name text,
    p_shard_key helio_core.bson,
    p_is_reshard bool default true)
 RETURNS void
 LANGUAGE C
   STRICT
 AS 'MODULE_PATHNAME', $$command_shard_collection$$;
 COMMENT ON FUNCTION helio_api.shard_collection(text, text,helio_core.bson,bool)
    IS 'Top level command to shard or reshard a helio collection';