/* get_shard_key_value returns the shard key value from the given document.
 * Returns the collection_id for non-sharded collections.
 */
CREATE OR REPLACE FUNCTION __API_SCHEMA_INTERNAL__.get_shard_key_value(
    p_shard_key __CORE_SCHEMA__.bson,
    p_collection_id bigint,
    p_document __CORE_SCHEMA__.bson)
 RETURNS bigint
 LANGUAGE c
 IMMUTABLE PARALLEL SAFE
AS 'MODULE_PATHNAME', $$command_get_shard_key_value$$;
COMMENT ON FUNCTION __API_SCHEMA_INTERNAL__.get_shard_key_value(__CORE_SCHEMA__.bson,bigint,__CORE_SCHEMA__.bson)
    IS 'return the shard key value for the given document';
