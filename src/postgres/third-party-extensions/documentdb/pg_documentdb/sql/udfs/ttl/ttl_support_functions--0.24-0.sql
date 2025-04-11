/*
 * This function deletes a batch of documents from a collection with an active ttl index.
 *
 * Param 1: Collection Id
 * Param 2: Index Id
 * Param 2: Index Key
 * Param 3: Current date time in milliseconds since epoch
 * Param 4: Partial filter expression
 * Param 5: IndexExpiryAfterSeconds field specified in the index
 * Param 6: Batch size
 * Param 7: Shard Id
*/
CREATE OR REPLACE FUNCTION __API_SCHEMA_INTERNAL_V2__.delete_expired_rows_for_index(
    IN collection_id bigint,
    IN index_id bigint,
    IN index_key __CORE_SCHEMA__.bson,
    IN index_pfe __CORE_SCHEMA__.bson,
    IN current_datetime bigint,
    IN index_expiry int,
    IN ttl_batch_size int,
    IN shard_id bigint)
 RETURNS bigint
 LANGUAGE C
VOLATILE PARALLEL UNSAFE CALLED ON NULL INPUT
AS 'MODULE_PATHNAME', $function$delete_expired_rows_for_index$function$;


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
AS $procedure$
DECLARE
    collection_row record;
    expired_rows_deleted bigint;
    start_time TIMESTAMP;
    end_time TIMESTAMP;
    config_value TEXT;
    elapsed_time INTERVAL;
    single_ttl_task_time_budget INTERVAL;
    exception_message TEXT;
    exception_detail TEXT;
BEGIN

    config_value := current_setting(__SINGLE_QUOTED_STRING__(__API_GUC_PREFIX__) || '.SingleTTLTaskTimeBudget');
    single_ttl_task_time_budget := (config_value::INTEGER * '1 millisecond'::INTERVAL);
    start_time := clock_timestamp();

    SET LOCAL search_path TO __API_DATA_SCHEMA__;
    -- This loop goes over all the shards of a collection holding the ttl index and starts deleting data.
    FOR collection_row IN
        SELECT
            idx.collection_id,
            idx.index_id,
            (index_spec).index_key as key,
            (index_spec).index_pfe as pfe,
            trunc(extract(epoch FROM now()) * 1000, 0)::int8 as currentDateTime,
            (index_spec).index_expire_after_seconds as expiry,
            coll.shard_key,
            dist.shardid
        FROM __API_CATALOG_SCHEMA__.collection_indexes as idx, __API_CATALOG_SCHEMA__.collections as coll,
            pg_dist_shard as dist,
            pg_dist_placement as placement,
            pg_dist_local_group as lg
        WHERE index_is_valid AND (index_spec).index_expire_after_seconds >= 0
        AND idx.collection_id = coll.collection_id 
        AND dist.logicalrelid = ('documents_' || coll.collection_id)::regclass
        AND placement.shardid = dist.shardid
        AND placement.groupid = lg.groupid -- filter shards that live on this worker node 
        AND (dist.shardid = get_shard_id_for_distribution_column(logicalrelid, coll.collection_id) OR (coll.shard_key IS NOT NULL))
    LOOP

        expired_rows_deleted:= __API_SCHEMA_INTERNAL_V2__.delete_expired_rows_for_index(
            collection_row.collection_id,
            collection_row.index_id,
            collection_row.key,
            collection_row.pfe,
            collection_row.currentDateTime,
            collection_row.expiry,
            p_batch_size,
            collection_row.shardid);

        end_time := clock_timestamp();
        elapsed_time := end_time - start_time;

        IF current_setting(__SINGLE_QUOTED_STRING__(__API_GUC_PREFIX__) || '.logTTLProgressActivity')::BOOLEAN IS TRUE THEN
            RAISE LOG 'TTL task: deleted_documents=%, collection_id=%, index_id=%, shard_id=%, elapsed_time=%ms, budgeted_time=%ms',
            expired_rows_deleted,
            collection_row.collection_id,
            collection_row.index_id,
            collection_row.shardid,
            elapsed_time,
            single_ttl_task_time_budget;
        END IF;

        -- As long as we could delete data from any shards of a collection with ttl index
        -- we keep going until the time budget allocated for a single invocation of a 
        -- ttl task exceeds.
        IF  elapsed_time > single_ttl_task_time_budget THEN
            RETURN;
        END IF;

        COMMIT;

    END LOOP;
END;
$procedure$
LANGUAGE plpgsql;