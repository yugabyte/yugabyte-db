DROP FUNCTION IF EXISTS __API_SCHEMA_INTERNAL_V2__.delete_expired_rows_for_index(
    IN collection_id bigint,
    IN index_id bigint,
    IN index_key __CORE_SCHEMA__.bson,
    IN index_pfe __CORE_SCHEMA__.bson,
    IN current_datetime bigint,
    IN index_expiry int,
    IN ttl_batch_size int,
    IN shard_id bigint);


/*
 * Procedure to delete expired rows specified by ttl indexes. The procedure goes over
 * all the valid TTL indexes and deletes a small batch of expired rows from one of the 
 * shards of the corresponding collection. As longs as we delete some data from one of
 * the shards of the collection, we exit out of the function to make forward progress and
 * to keep delete deletion prcess short, so that it does not hold locks for a long period.
 *
 * Note since we go over all the shards for the collection, the solution works for all sharded
 * and unsharded collection.
 * 
 * The method takes the maximum number of documents than can be deleted in a batch as a parameter.
 * A value of -1 (default) instructs the caller to use the default batch size avilable via GUC.
 * Ability of pass the batch size parameter is useful for testing.
 *
 * IMP: This procedure is designed only to be called from pg_cron ttl purger task.
 */
CREATE OR REPLACE PROCEDURE __API_SCHEMA_INTERNAL_V2__.delete_expired_rows(IN p_batch_size int default -1)
    LANGUAGE c
AS 'MODULE_PATHNAME', $procedure$delete_expired_rows$procedure$;
COMMENT ON PROCEDURE __API_SCHEMA_INTERNAL_V2__.delete_expired_rows(int)
    IS 'Drops expired rows from applicable collections based on TTL indexes.';