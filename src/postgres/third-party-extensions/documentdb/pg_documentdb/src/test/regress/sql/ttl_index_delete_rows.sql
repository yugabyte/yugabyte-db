SET search_path TO documentdb_core, documentdb_api, documentdb_api_catalog, public;
SET documentdb.next_collection_id TO 781000;
SET documentdb.next_collection_index_id TO 781000;

-- make sure ttl job is scheduled and disable it to avoid flakiness on the test as it could run on its schedule and delete documents before we run our commands in the test
select schedule, command, active from cron.job where jobname like '%ttl_task%';

select cron.unschedule(jobid) from cron.job where jobname like '%ttl_task%';

-- 1. Populate collection with a set of documents with different combination of $date fields --
SELECT documentdb_api.insert_one('ttl_tests','coll1', '{ "_id" : 0, "ttl" : { "$date": { "$numberLong": "-1000" } } }', NULL);
SELECT documentdb_api.insert_one('ttl_tests','coll1', '{ "_id" : 1, "ttl" : { "$date": { "$numberLong": "0" } } }', NULL);
SELECT documentdb_api.insert_one('ttl_tests','coll1', '{ "_id" : 2, "ttl" : { "$date": { "$numberLong": "100" } } }', NULL);
    -- Documents with date older than when the test was written
SELECT documentdb_api.insert_one('ttl_tests','coll1', '{ "_id" : 3, "ttl" : { "$date": { "$numberLong": "1657900030774" } } }', NULL);
    -- Documents with date way in future
SELECT documentdb_api.insert_one('ttl_tests','coll1', '{ "_id" : 4, "ttl" : { "$date": { "$numberLong": "2657899731608" } } }', NULL);
    -- Documents with date array
SELECT documentdb_api.insert_one('ttl_tests','coll1', '{ "_id" : 5, "ttl" : [{ "$date": { "$numberLong": "100" }}] }', NULL);
    -- Documents with date array, should be deleted based on min timestamp
SELECT documentdb_api.insert_one('ttl_tests','coll2', '{ "_id" : 6, "ttl" : [{ "$date": { "$numberLong": "100" }}, { "$date": { "$numberLong": "2657899731608" }}] }', NULL);
SELECT documentdb_api.insert_one('ttl_tests','coll1', '{ "_id" : 6, "other_field" : [{ "$date": { "$numberLong": "100" }}, { "$date": { "$numberLong": "2657899731608" }}] }', NULL);
SELECT documentdb_api.insert_one('ttl_tests','coll2', '{ "_id" : 7, "ttl" : [true, { "$date": { "$numberLong": "100" }}, { "$date": { "$numberLong": "2657899731608" }}] }', NULL);
SELECT documentdb_api.insert_one('ttl_tests','coll1', '{ "_id" : 7, "other_field" : [true, { "$date": { "$numberLong": "100" }}, { "$date": { "$numberLong": "2657899731608" }}] }', NULL);
    -- Documents with non-date ttl field
SELECT documentdb_api.insert_one('ttl_tests','coll2', '{ "_id" : 8, "ttl" : true }', NULL);
SELECT documentdb_api.insert_one('ttl_tests','coll1', '{ "_id" : 8, "other_field" : true }', NULL);
    -- Documents with non-date ttl field
SELECT documentdb_api.insert_one('ttl_tests','coll2', '{ "_id" : 9, "ttl" : "would not expire" }', NULL);
SELECT documentdb_api.insert_one('ttl_tests','coll1', '{ "_id" : 9, "other_field" : "would not expire" }', NULL);

SELECT documentdb_api_internal.create_indexes_non_concurrently('ttl_tests', '{"createIndexes": "coll1", "indexes": [{"key": {"ttl": 1}, "name": "ttl_index", "v" : 1, "expireAfterSeconds": 5}]}', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('ttl_tests', '{"createIndexes": "coll2", "indexes": [{"key": {"ttl": 1}, "name": "ttl_index", "v" : 1, "expireAfterSeconds": 10}]}', true);

SELECT document FROM documentdb_api.collection('ttl_tests', 'coll1');
SELECT document FROM documentdb_api.collection('ttl_tests', 'coll2');

-- should not delete any documents because batch size is 0
CALL documentdb_api_internal.delete_expired_rows(0);

SELECT document FROM documentdb_api.collection('ttl_tests', 'coll1');
SELECT document FROM documentdb_api.collection('ttl_tests', 'coll2');

-- should delete all expired rows, but only ttl field since we haven't created an index for other_field
CALL documentdb_api_internal.delete_expired_rows();
SELECT document FROM documentdb_api.collection('ttl_tests', 'coll1');
SELECT document FROM documentdb_api.collection('ttl_tests', 'coll2');

SELECT documentdb_api_internal.create_indexes_non_concurrently('ttl_tests', '{"createIndexes": "coll1", "indexes": [{"key": {"other_field": 1}, "name": "ttl_index_other_field", "v" : 1, "expireAfterSeconds": 10}]}', true);

-- should now delete other_field entries
CALL documentdb_api_internal.delete_expired_rows();
SELECT document FROM documentdb_api.collection('ttl_tests', 'coll1');

-- insert more data and create one more ttl index
SELECT documentdb_api.insert_one('ttl_tests','coll2', '{ "_id" : 10, "new_field" : [{ "$date": { "$numberLong": "100" }}, { "$date": { "$numberLong": "2657899731608" }}] }', NULL);
SELECT documentdb_api.insert_one('ttl_tests','coll2', '{ "_id" : 11, "new_field" : true }', NULL);

SELECT documentdb_api_internal.create_indexes_non_concurrently('ttl_tests', '{"createIndexes": "coll2", "indexes": [{"key": {"new_field": 1}, "name": "ttl_index_new_field", "v" : 1, "expireAfterSeconds": 10}]}', true);

SELECT documentdb_api.insert_one('ttl_tests','coll1', '{ "_id" : 11, "ttl" : { "$date": { "$numberLong": "100" }} }', NULL);
SELECT documentdb_api.insert_one('ttl_tests','coll1', '{ "_id" : 12, "other_field" : [{ "$date": { "$numberLong": "100" }}, { "$date": { "$numberLong": "2657899731608" }}] }', NULL);
SELECT documentdb_api.insert_one('ttl_tests','coll1', '{ "_id" : 13, "ttl" : true, "other_field": {"$date": {"$numberLong": "-10" }} }', NULL);

SELECT document FROM documentdb_api.collection('ttl_tests', 'coll1');
SELECT document FROM documentdb_api.collection('ttl_tests', 'coll2');

CALL documentdb_api_internal.delete_expired_rows();

SELECT document FROM documentdb_api.collection('ttl_tests', 'coll1');
SELECT document FROM documentdb_api.collection('ttl_tests', 'coll2');

SELECT drop_collection('ttl_tests', 'coll1');
SELECT drop_collection('ttl_tests', 'coll2');

-- test with partial filter expression
SELECT documentdb_api.insert_one('ttl_tests','coll1', '{ "_id" : 1, "ttl" : { "$date": { "$numberLong": "-1000" } }, "a": 1, "b":55 }');

-- should not be pruned since it doesn't match the partial filter expression
SELECT documentdb_api.insert_one('ttl_tests','coll1', '{ "_id" : 2, "ttl" : { "$date": { "$numberLong": "-1000" } }, "a": 1, "b":54 }');
SELECT documentdb_api.insert_one('ttl_tests','coll1', '{ "_id" : 3, "ttl" : { "$date": { "$numberLong": "-1000" } }, "b":55 }');

SELECT documentdb_api_internal.create_indexes_non_concurrently(
  'ttl_tests',
  '{
     "createIndexes": "coll1",
     "indexes": [
       {
         "key": {"ttl": 1},
         "name": "ttl_pfe_index",
         "expireAfterSeconds" : 5,
         "partialFilterExpression":
         {
           "$and": [
             {"b": 55},
             {"a": {"$exists": true}}
            ]
         }
       }
     ]
   }',
   true
);

CALL documentdb_api_internal.delete_expired_rows();

SELECT document FROM documentdb_api.collection('ttl_tests', 'coll1');

SELECT drop_collection('ttl_tests', 'coll1');