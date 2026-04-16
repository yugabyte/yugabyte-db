SET citus.next_shard_id TO 2000000;
SET documentdb.next_collection_id TO 20000;
SET documentdb.next_collection_index_id TO 20000;

SET search_path TO documentdb_api_catalog, documentdb_core, documentdb_data, public;

-- make sure jobs are scheduled and disable it to avoid flakiness on the test as it could run on its schedule and delete documents before we run our commands in the test
select schedule, command, active from cron.job where jobname like '%ttl_task%';

select cron.unschedule(jobid) from cron.job where jobname like '%ttl_task%';

-- 1. Populate collection with a set of documents with different combination of $date fields --
SELECT documentdb_api.insert_one('db','ttlcoll', '{ "_id" : 0, "ttl" : { "$date": { "$numberLong": "-1000" } } }', NULL);
SELECT documentdb_api.insert_one('db','ttlcoll', '{ "_id" : 1, "ttl" : { "$date": { "$numberLong": "0" } } }', NULL);
SELECT documentdb_api.insert_one('db','ttlcoll', '{ "_id" : 2, "ttl" : { "$date": { "$numberLong": "100" } } }', NULL);
    -- Documents with date older than when the test was written
SELECT documentdb_api.insert_one('db','ttlcoll', '{ "_id" : 3, "ttl" : { "$date": { "$numberLong": "1657900030774" } } }', NULL);
    -- Documents with date way in future
SELECT documentdb_api.insert_one('db','ttlcoll', '{ "_id" : 4, "ttl" : { "$date": { "$numberLong": "2657899731608" } } }', NULL);
    -- Documents with date array
SELECT documentdb_api.insert_one('db','ttlcoll', '{ "_id" : 5, "ttl" : [{ "$date": { "$numberLong": "100" }}] }', NULL);
    -- Documents with date array, should be deleted based on min timestamp
SELECT documentdb_api.insert_one('db','ttlcoll', '{ "_id" : 6, "ttl" : [{ "$date": { "$numberLong": "100" }}, { "$date": { "$numberLong": "2657899731608" }}] }', NULL);
SELECT documentdb_api.insert_one('db','ttlcoll', '{ "_id" : 7, "ttl" : [true, { "$date": { "$numberLong": "100" }}, { "$date": { "$numberLong": "2657899731608" }}] }', NULL);
    -- Documents with non-date ttl field
SELECT documentdb_api.insert_one('db','ttlcoll', '{ "_id" : 8, "ttl" : true }', NULL);
    -- Documents with non-date ttl field
SELECT documentdb_api.insert_one('db','ttlcoll', '{ "_id" : 9, "ttl" : "would not expire" }', NULL);

-- 1. Create TTL Index --
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "ttlcoll", "indexes": [{"key": {"ttl": 1}, "name": "ttl_index", "v" : 1, "expireAfterSeconds": 5}]}', true);

-- 2. List All indexes --
SELECT bson_dollar_unwind(cursorpage, '$cursor.firstBatch') FROM documentdb_api.list_indexes_cursor_first_page('db','{ "listIndexes": "ttlcoll" }') ORDER BY 1;
SELECT * FROM documentdb_distributed_test_helpers.get_collection_indexes('db', 'ttlcoll') ORDER BY collection_id, index_id;

-- 3. Call ttl purge procedure with a batch size of 10
CALL documentdb_api_internal.delete_expired_rows(10);

-- 4.a. Check what documents are left after purging
SELECT shard_key_value, object_id, document  from documentdb_api.collection('db', 'ttlcoll') order by object_id;


-- 5. TTL indexes behaves like normal indexes that are used in queries
BEGIN;
set local enable_seqscan TO off;
SELECT documentdb_distributed_test_helpers.mask_plan_id_from_distributed_subplan($Q$
EXPLAIN(costs off) SELECT object_id FROM documentdb_data.documents_20000
		WHERE bson_dollar_eq(document, '{ "ttl" : { "$date" : { "$numberLong" : "100" } } }'::bson)
        LIMIT 100;
$Q$);
END;

-- 6. Explain of the SQL query that is used to delete documents
BEGIN;
SET LOCAL enable_seqscan TO OFF;
SELECT documentdb_distributed_test_helpers.mask_plan_id_from_distributed_subplan($Q$
EXPLAIN(costs off) DELETE FROM documentdb_data.documents_20000_2000000
    WHERE ctid IN
    (
        SELECT ctid FROM documentdb_data.documents_20000_2000000
        WHERE bson_dollar_lt(document, '{ "ttl" : { "$date" : { "$numberLong" : "100" } } }'::bson)
        AND shard_key_value = 20000
        LIMIT 100
    )
$Q$);
END;

-- 7.a. Query to select all the shards corresponding to a ttl index that needs to be considered for purging
-- ttlcoll is an unsharded collection

SELECT
    idx.collection_id,
    idx.index_id,
    (index_spec).index_key as key,
    (index_spec).index_pfe as pfe,
    -- trunc(extract(epoch FROM now()) * 1000, 0)::int8 as currentDateTime, -- removed to reduce test flakiness
    (index_spec).index_expire_after_seconds as expiry,
    coll.shard_key,
    dist.shardid
FROM documentdb_api_catalog.collection_indexes as idx, documentdb_api_catalog.collections as coll, pg_dist_shard as dist
WHERE index_is_valid AND (index_spec).index_expire_after_seconds >= 0
AND idx.collection_id = coll.collection_id 
AND dist.logicalrelid = ('documentdb_data.documents_' || coll.collection_id)::regclass
AND (dist.shardid = get_shard_id_for_distribution_column(logicalrelid, coll.collection_id) OR (coll.shard_key IS NOT NULL))
AND coll.collection_id >= 20000 AND coll.collection_id < 21000 -- added to reduce test flakiness
ORDER BY shardid ASC; -- added to remove reduce flakiness

-- 8. Shard collection
SELECT documentdb_api.shard_collection('db','ttlcoll', '{"ttl":"hashed"}', false);

-- 9. Add more records with previous deleted as well as new ids
SELECT documentdb_api.insert_one('db','ttlcoll', '{ "_id" : 1, "ttl" : { "$date": { "$numberLong": "0" } } }', NULL);
SELECT documentdb_api.insert_one('db','ttlcoll', '{ "_id" : 2, "ttl" : { "$date": { "$numberLong": "-1000" } } }', NULL);
SELECT documentdb_api.insert_one('db','ttlcoll', '{ "_id" : 100, "ttl" : { "$date": { "$numberLong": "1657900030774" } } }', NULL);
SELECT documentdb_api.insert_one('db','ttlcoll', '{ "_id" : 200, "ttl" : { "$date": { "$numberLong": "-1000" } } }', NULL);

-- 9.a. Query to select all the shards corresponding to a ttl index that needs to be considered for purging
-- ttlcoll is an unsharded collection

SELECT
    idx.collection_id,
    idx.index_id,
    (index_spec).index_key as key,
    (index_spec).index_pfe as pfe,
    -- trunc(extract(epoch FROM now()) * 1000, 0)::int8 as currentDateTime, -- removed to reduce test flakiness
    (index_spec).index_expire_after_seconds as expiry,
    coll.shard_key,
    dist.shardid
FROM documentdb_api_catalog.collection_indexes as idx, documentdb_api_catalog.collections as coll, pg_dist_shard as dist
WHERE index_is_valid AND (index_spec).index_expire_after_seconds >= 0
AND idx.collection_id = coll.collection_id 
AND dist.logicalrelid = ('documentdb_data.documents_' || coll.collection_id)::regclass
AND (dist.shardid = get_shard_id_for_distribution_column(logicalrelid, coll.collection_id) OR (coll.shard_key IS NOT NULL))
AND coll.collection_id >= 20000 AND coll.collection_id < 21000 -- added to reduce test flakiness
ORDER BY shardid ASC; -- added to reduce test flakiness

-- 10.b. Call ttl task procedure with a batch size of 0 --
BEGIN;
Set citus.log_remote_commands to on; -- Will print Citus rewrites of the queries
Set citus.log_local_commands to on; -- Will print the local queries 
set local documentdb.SingleTTLTaskTimeBudget to 1;
CALL documentdb_api_internal.delete_expired_rows(0); -- To test the sql query, it won't delete any data
Set citus.log_remote_commands to off;
Set citus.log_local_commands to off;
END;

-- 10.a.
CALL documentdb_api_internal.delete_expired_rows(10);

-- 11.a. Check what documents are left after purging
SELECT shard_key_value, object_id, document  from documentdb_api.collection('db', 'ttlcoll') order by object_id;

-- 12. Explain of the SQL query that is used to delete documents after sharding
BEGIN;
SET LOCAL enable_seqscan TO OFF;
SELECT documentdb_distributed_test_helpers.mask_plan_id_from_distributed_subplan($Q$
EXPLAIN(costs off) DELETE FROM documentdb_data.documents_20000_2000016
    WHERE ctid IN
    (
        SELECT ctid FROM documentdb_data.documents_20000_2000016
        WHERE bson_dollar_lt(document, '{ "ttl" : { "$date" : { "$numberLong" : "100" } } }'::bson)
        LIMIT 100
    )
$Q$);
END;


-- 13. TTL index can be created
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "ttlcoll", "indexes": [{"key": {"ttl1": 1}, "name": "ttl_index1", "expireAfterSeconds": 100, "sparse" : true}]}', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "ttlcoll", "indexes": [{"key": {"ttl2": 1}, "name": "ttl_index2", "expireAfterSeconds": 100, "unique" : true}]}', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "ttlcoll", "indexes": [{"key": {"ttl3": 1}, "name": "ttl_index3", "expireAfterSeconds": 100, "sparse" : true, "unique" : true}]}', true);
SELECT bson_dollar_unwind(cursorpage, '$cursor.firstBatch') FROM documentdb_api.list_indexes_cursor_first_page('db','{ "listIndexes": "ttlcoll" }') ORDER BY 1;

-- 14. TTL index creation restrictions
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "ttlcoll", "indexes": [{"key": {"ttl": 1}, "name": "ttl_index2", "expireAfterSeconds": -1}]}', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "ttlcoll", "indexes": [{"key": {"ttl": 1}, "name": "ttl_index2", "expireAfterSeconds": "stringNotAllowed"}]}', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "ttlcoll", "indexes": [{"key": {"ttl": 1}, "name": "ttl_index2", "expireAfterSeconds": true}]}', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "ttlcoll", "indexes": [{"key": {"ttl": 1}, "name": "ttl_index2", "expireAfterSeconds": 707992037530324174}]}', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "ttlcoll", "indexes": [{"key": {"ttl": 1}, "name": "ttl_index2", "expireAfterSeconds": 100, "v" : 1}]}', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "ttlcoll", "indexes": [{"key": {"_id": 1}, "name": "ttl_idx", "expireAfterSeconds": 100}]}', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "ttlcoll", "indexes": [{"key": {"_id": 1, "_id" : 1}, "name": "ttl_idx", "expireAfterSeconds": 100}]}', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "ttlcoll", "indexes": [{"key": {"_id": 1, "non_id" : 1}, "name": "ttl_idx", "expireAfterSeconds": 100}]}', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "ttlcoll", "indexes": [{"key": {"non_id1": 1, "non_id2" : 1}, "name": "ttl_idx", "expireAfterSeconds": 100}]}', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "ttlcoll", "indexes": [{"key": {"non_id1.$**": 1}, "name": "ttl_idx", "expireAfterSeconds": 100}]}', true);

-- 15. Unsupported ttl index scenarios
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "ttlcoll", "indexes": [{"key": {"ttl4": "hashed"}, "name": "ttl_index4", "expireAfterSeconds": 100}]}', true);

-- 16. Behavioral difference with Native Sharded Mongo
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "ttlcoll", "indexes": [{"key": {"ttlnew": 1}, "name": "ttl_new_index1", "sparse" : true, "expireAfterSeconds" : 10}]}', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "ttlcoll", "indexes": [{"key": {"ttlnew": 1}, "name": "ttl_new_index2", "sparse" : false, "expireAfterSeconds" : 10}]}', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "ttlcoll", "indexes": [{"key": {"ttlnew": 1}, "name": "ttl_new_index3", "expireAfterSeconds": 100, "sparse" : true, "unique" : true}]}', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "ttlcoll", "indexes": [{"key": {"ttlnew": "hashed"}, "name": "ttl_new_index4", "expireAfterSeconds": 100}]}', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "ttlcoll", "indexes": [{"key": {"ttlnew2": 5}, "name": "ttl_new_indexj", "sparse" : true, "expireAfterSeconds" : 10}]}', true);
SELECT bson_dollar_unwind(cursorpage, '$cursor.firstBatch') FROM documentdb_api.list_indexes_cursor_first_page('db','{ "listIndexes": "ttlcoll" }') ORDER BY 1;
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "ttlcoll", "indexes": [{"key": {"ttlnew3": "hashed"}, "name": "ttl_new_indexk", "unique" : true, "expireAfterSeconds" : 10}]}', true);


-- 17. Partial filter expresson tests

SELECT documentdb_api_internal.create_indexes_non_concurrently(
  'db',
  '{
     "createIndexes": "ttlcoll2",
     "indexes": [
       {
         "key": {"ttl": 1},
         "name": "ttl_pfe_index",
         "expireAfterSeconds" : 5,
         "partialFilterExpression":
         {
           "$and": [
             {"b": 55},
             {"a": {"$exists": true}},
             {"c": {"$exists": 1}}
            ]
         }
       }
     ]
   }',
   true
);

SELECT bson_dollar_unwind(cursorpage, '$cursor.firstBatch') FROM documentdb_api.list_indexes_cursor_first_page('db','{ "listIndexes": "ttlcoll2" }') ORDER BY 1;
SELECT * FROM documentdb_distributed_test_helpers.get_collection_indexes('db', 'ttlcoll2') ORDER BY collection_id, index_id;


SELECT documentdb_api.insert_one('db','ttlcoll2', '{ "_id" : 0, "b": 55, "a" : 1, "c": 1, "ttl" : { "$date": { "$numberLong": "-1000" } } }', NULL);
SELECT documentdb_api.insert_one('db','ttlcoll2', '{ "_id" : 1, "b": 56, "a" : 1, "c": 1, "ttl" : { "$date": { "$numberLong": "0" } } }', NULL);
SELECT documentdb_api.insert_one('db','ttlcoll2', '{ "_id" : 2, "b": 56, "a" : 1, "c": 1, "ttl" : { "$date": { "$numberLong": "100" } } }', NULL);
SELECT documentdb_api.insert_one('db','ttlcoll2', '{ "_id" : 3, "b": 55, "a" : 1, "c": 1, "ttl" : { "$date": { "$numberLong": "1657900030774" } } }', NULL);
SELECT documentdb_api.insert_one('db','ttlcoll2', '{ "_id" : 4, "b": 55, "a" : 1, "c": 1, "ttl" : { "$date": { "$numberLong": "2657899731608" } } }', NULL);
SELECT documentdb_api.insert_one('db','ttlcoll2', '{ "_id" : 5, "b": 55, "a" : 1, "c": 1, "ttl" : [{ "$date": { "$numberLong": "100" }}] }', NULL);
SELECT documentdb_api.insert_one('db','ttlcoll2', '{ "_id" : 6, "b": 55, "a" : 1, "d": 1, "ttl" : [{ "$date": { "$numberLong": "100" }}, { "$date": { "$numberLong": "2657899731608" }}] }', NULL);
SELECT documentdb_api.insert_one('db','ttlcoll2', '{ "_id" : 7, "b": 55, "a" : 1, "c": 1, "ttl" : [true, { "$date": { "$numberLong": "100" }}, { "$date": { "$numberLong": "2657899731608" }}] }', NULL);
SELECT documentdb_api.insert_one('db','ttlcoll2', '{ "_id" : 8, "b": 55, "a" : 1, "c": 1, "ttl" : true }', NULL);
SELECT documentdb_api.insert_one('db','ttlcoll2', '{ "_id" : 9, "b": 55, "a" : 1, "c": 1, "ttl" : "would not expire" }', NULL);

CALL documentdb_api_internal.delete_expired_rows(10);
SELECT shard_key_value, object_id, document  from documentdb_api.collection('db', 'ttlcoll2') order by object_id;

-- 18. Large TTL (expire after INT_MAX seconds aka 68 years)

SELECT documentdb_api_internal.create_indexes_non_concurrently(
  'db',
  '{
     "createIndexes": "ttlcoll3",
     "indexes": [
       {
         "key": {"ttl": 1},
         "name": "ttl_large_expireAfterSeconds",
         "expireAfterSeconds" :  2147483647
       }
     ]
   }',
   true
);

SELECT bson_dollar_unwind(cursorpage, '$cursor.firstBatch') FROM documentdb_api.list_indexes_cursor_first_page('db','{ "listIndexes": "ttlcoll3" }') ORDER BY 1;
SELECT * FROM documentdb_distributed_test_helpers.get_collection_indexes('db', 'ttlcoll3') ORDER BY collection_id, index_id;

  -- Timestamp: -623051866000 ( 4/4/1950 more than 68 years from 4/4/2024). So, with the ttl index index `ttl_large_expireAfterSeconds`, _id : [1, 6, 7] should be deleted.

SELECT documentdb_api.insert_one('db','ttlcoll3', '{ "_id" : 0, "b": 55, "a" : 1, "c": 1, "ttl" : { "$date": { "$numberLong": "-623051866000" } } }', NULL);
SELECT documentdb_api.insert_one('db','ttlcoll3', '{ "_id" : 1, "b": 56, "a" : 1, "c": 1, "ttl" : { "$date": { "$numberLong": "1657900030774" } } }', NULL);
SELECT documentdb_api.insert_one('db','ttlcoll3', '{ "_id" : 2, "b": 56, "a" : 1, "c": 1, "ttl" : { "$date": { "$numberLong": "1712253575000" } } }', NULL);
SELECT documentdb_api.insert_one('db','ttlcoll3', '{ "_id" : 3, "b": 55, "a" : 1, "c": 1, "ttl" : { "$date": { "$numberLong": "4867927028000" } } }', NULL);
SELECT documentdb_api.insert_one('db','ttlcoll3', '{ "_id" : 4, "b": 55, "a" : 1, "c": 1, "ttl" : { "$date": { "$numberLong": "2657899731608" } } }', NULL);
SELECT documentdb_api.insert_one('db','ttlcoll3', '{ "_id" : 5, "b": 55, "a" : 1, "c": 1, "ttl" : [{ "$date": { "$numberLong": "1697900030774" }}] }', NULL);
SELECT documentdb_api.insert_one('db','ttlcoll3', '{ "_id" : 6, "b": 55, "a" : 1, "d": 1, "ttl" : [{ "$date": { "$numberLong": "-623051866000" }}, { "$date": { "$numberLong": "2657899731608" }}] }', NULL);
SELECT documentdb_api.insert_one('db','ttlcoll3', '{ "_id" : 7, "b": 55, "a" : 1, "c": 1, "ttl" : [true, { "$date": { "$numberLong": "-623051866000" }}, { "$date": { "$numberLong": "2657899731608" }}] }', NULL);
SELECT documentdb_api.insert_one('db','ttlcoll3', '{ "_id" : 8, "b": 55, "a" : 1, "c": 1, "ttl" : true }', NULL);
SELECT documentdb_api.insert_one('db','ttlcoll3', '{ "_id" : 9, "b": 55, "a" : 1, "c": 1, "ttl" : "would not expire" }', NULL);

CALL documentdb_api_internal.delete_expired_rows(10);
SELECT shard_key_value, object_id, document  from documentdb_api.collection('db', 'ttlcoll3') order by object_id;

-- 19 Float TTL
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "ttlcoll1", "indexes": [{"key": {"ttlnew": 1}, "name": "ttl_new_index5", "sparse" : true, "expireAfterSeconds" : {"$numberDouble":"12345.12345"}}]}', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "ttlcoll1", "indexes": [{"key": {"ttlnew": 1}, "name": "ttl_new_index6", "sparse" : false, "expireAfterSeconds" : {"$numberDouble":"12345.12345"}}]}', true);
SELECT bson_dollar_unwind(cursorpage, '$cursor.firstBatch') FROM documentdb_api.list_indexes_cursor_first_page('db','{ "listIndexes": "ttlcoll1" }') ORDER BY 1;
