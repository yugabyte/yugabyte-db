SET search_path TO documentdb_core,documentdb_api,documentdb_api_catalog,documentdb_api_internal;
SET citus.next_shard_id TO 630000;
SET documentdb.next_collection_id TO 6300;
SET documentdb.next_collection_index_id TO 6300;

-- Utility function to add multiple documents to a collection.
CREATE OR REPLACE FUNCTION insert_docs(p_db TEXT, p_coll TEXT, p_num INT, p_start INT default 0)
RETURNS void
AS $$
DECLARE
    num INTEGER := p_start;
    docText bson;
BEGIN
    WHILE num < p_num + p_start LOOP
        docText :=  CONCAT('{ "a" : ', num, '}');
        PERFORM documentdb_api.insert_one(p_db, p_coll, docText::documentdb_core.bson, NULL);
        num := num + 1;
    END LOOP;
END;
$$
LANGUAGE plpgsql;


SELECT documentdb_api.drop_database('db1');

-- Non existing database should return zero values
SELECT documentdb_api.db_stats('db1');
SELECT documentdb_api.db_stats('db1', 1);
SELECT documentdb_api.db_stats('db1', 1024);

--=============== Tests for "collections" & "objects" count ===============+=--

-- Create a Collection
SELECT documentdb_api.create_collection('db1', 'col1');

-- db_stats with one empty collection
SELECT documentdb_api.db_stats('db1');
SELECT documentdb_api.db_stats('db1', 1);
SELECT documentdb_api.db_stats('db1', 1024);

-- Add one doc
SELECT documentdb_api.insert_one('db1','col1',' { "a" : 100 }', NULL);

-- The AutoVacuum might still be napping so count in stats might still be 0,
-- In this test we cannot wait till nap time is over, so we manually trigger the ANALYZE
ANALYZE;

-- db_stats with single collection and single document
SELECT documentdb_api.db_stats('db1');

-- Insert few docs in the collection
SELECT insert_docs('db1', 'col1', 20, 1);

-- In this test we cannot wait till Autovaccum nap time is over, so we manually trigger the ANALYZE
ANALYZE;

-- "objects" should be 21, "collections" should be 1, and fsStorageSize > fsUsedSize
SELECT documentdb_api.db_stats('db1');

-- Create 4 more Collections
SELECT documentdb_api.create_collection('db1', 'col2');
SELECT documentdb_api.create_collection('db1', 'col3');
SELECT documentdb_api.create_collection('db1', 'col4');
SELECT documentdb_api.create_collection('db1', 'col5');

-- "collections" and "indexes" count should increase to 5
SELECT documentdb_api.db_stats('db1');

-- Add one doc to each new collection
SELECT documentdb_api.insert_one('db1','col2',' { "a" : 100 }', NULL);
SELECT documentdb_api.insert_one('db1','col3',' { "a" : 100 }', NULL);
SELECT documentdb_api.insert_one('db1','col4',' { "a" : 100 }', NULL);
SELECT documentdb_api.insert_one('db1','col5',' { "a" : 100 }', NULL);

-- In this test we cannot wait till Autovaccum nap time is over, so we manually trigger the ANALYZE
ANALYZE;

-- "objects" count should increase to 25
SELECT documentdb_api.db_stats('db1');

-- Insert 20 more docs in each new collection
SELECT insert_docs('db1', 'col2', 20, 1);
SELECT insert_docs('db1', 'col3', 20, 1);
SELECT insert_docs('db1', 'col4', 20, 1);
SELECT insert_docs('db1', 'col5', 20, 1);

-- In this test we cannot wait till Autovaccum nap time is over, so we manually trigger the ANALYZE
ANALYZE;

-- "objects" count should increase to 105
SELECT documentdb_api.db_stats('db1');

-- Delete 1 document
SELECT documentdb_api.delete('db1', '{"delete":"col1", "deletes":[{"q":{"a":{"$gte": 100}},"limit":0}]}');

-- In this test we cannot wait till Autovaccum nap time is over, so we manually trigger the ANALYZE
ANALYZE;

-- "objects" count should reduce to 104
SELECT documentdb_api.db_stats('db1');

-- Delete 1 document from each remaining collections
SELECT documentdb_api.delete('db1', '{"delete":"col2", "deletes":[{"q":{"a":{"$gte": 100}},"limit":0}]}');
SELECT documentdb_api.delete('db1', '{"delete":"col3", "deletes":[{"q":{"a":{"$gte": 100}},"limit":0}]}');
SELECT documentdb_api.delete('db1', '{"delete":"col4", "deletes":[{"q":{"a":{"$gte": 100}},"limit":0}]}');
SELECT documentdb_api.delete('db1', '{"delete":"col5", "deletes":[{"q":{"a":{"$gte": 100}},"limit":0}]}');

-- In this test we cannot wait till Autovaccum nap time is over, so we manually trigger the ANALYZE
ANALYZE;

-- "objects" count should reduce to 100
SELECT documentdb_api.db_stats('db1');

-- Now shard all collections
SELECT documentdb_api.shard_collection('db1','col1', '{"a":"hashed"}', false);
SELECT documentdb_api.shard_collection('db1','col2', '{"a":"hashed"}', false);
SELECT documentdb_api.shard_collection('db1','col3', '{"a":"hashed"}', false);
SELECT documentdb_api.shard_collection('db1','col4', '{"a":"hashed"}', false);
SELECT documentdb_api.shard_collection('db1','col5', '{"a":"hashed"}', false);

-- "objects" count should remain 100
SELECT documentdb_api.db_stats('db1');

--===================== Test for "indexes", "indexSize" =====================--

-- Create one more index
SELECT documentdb_api_internal.create_indexes_non_concurrently('db1', documentdb_distributed_test_helpers.generate_create_index_arg('col1', 'index_a_1', '{"a": 1}'), true);

-- "indexes" count should increase to 6, "indexSize" should increase
SELECT documentdb_api.db_stats('db1');

-- Create one more index in each remaining collections
SELECT documentdb_api_internal.create_indexes_non_concurrently('db1', documentdb_distributed_test_helpers.generate_create_index_arg('col2', 'index_a_1', '{"a": 1}'), true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('db1', documentdb_distributed_test_helpers.generate_create_index_arg('col3', 'index_a_1', '{"a": 1}'), true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('db1', documentdb_distributed_test_helpers.generate_create_index_arg('col4', 'index_a_1', '{"a": 1}'), true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('db1', documentdb_distributed_test_helpers.generate_create_index_arg('col5', 'index_a_1', '{"a": 1}'), true);

-- "indexes" count should increase to 10, "indexSize" should increase
SELECT documentdb_api.db_stats('db1');

-- Drop one index
CALL documentdb_api.drop_indexes('db1', '{"dropIndexes": "col1", "index": "index_a_1"}');

-- "indexes" count should reduce to 9
SELECT documentdb_api.db_stats('db1');

-- Drop one index from each remaining collections
CALL documentdb_api.drop_indexes('db1', '{"dropIndexes": "col2", "index": "index_a_1"}');
CALL documentdb_api.drop_indexes('db1', '{"dropIndexes": "col3", "index": "index_a_1"}');
CALL documentdb_api.drop_indexes('db1', '{"dropIndexes": "col4", "index": "index_a_1"}');
CALL documentdb_api.drop_indexes('db1', '{"dropIndexes": "col5", "index": "index_a_1"}');

-- "indexes" count should be back to 5 (one default _id index in each collection), "indexSize" should decrease
SELECT documentdb_api.db_stats('db1');

--===================== Test with Views =====================================--

-- create a view on a collection
SELECT documentdb_api.create_collection_view('db1', '{ "create": "col1_view1", "viewOn": "col1" }');

-- "views" should be 1
SELECT documentdb_api.db_stats('db1');

-- create one view on each remaining collection
SELECT documentdb_api.create_collection_view('db1', '{ "create": "col2_view1", "viewOn": "col2" }');
SELECT documentdb_api.create_collection_view('db1', '{ "create": "col3_view1", "viewOn": "col3" }');
SELECT documentdb_api.create_collection_view('db1', '{ "create": "col4_view1", "viewOn": "col4" }');
SELECT documentdb_api.create_collection_view('db1', '{ "create": "col5_view1", "viewOn": "col5" }');

-- "views" should be 5
SELECT documentdb_api.db_stats('db1');

-- Drop one collection (despite a view on it)
SELECT documentdb_api.drop_collection('db1', 'col5');

-- In this test we cannot wait till Autovaccum nap time is over, so we manually trigger the ANALYZE
ANALYZE;

-- "collections" should be 4, and "objects" will reduce
SELECT documentdb_api.db_stats('db1');

-- Drop one view
SELECT documentdb_api.drop_collection('db1', 'col5_view1');

-- "views" should be 4
SELECT documentdb_api.db_stats('db1');

-- Drop all remaining collections
SELECT documentdb_api.drop_collection('db1', 'col1');
SELECT documentdb_api.drop_collection('db1', 'col2');
SELECT documentdb_api.drop_collection('db1', 'col3');
SELECT documentdb_api.drop_collection('db1', 'col4');

-- Only "views" and fs stats should be available, rest all should be zero values.
SELECT documentdb_api.db_stats('db1');

--===================== Test with another database =============================--

-- Make sure this new database does not exist
SELECT documentdb_api.drop_database('db2');

-- Add one document
SELECT documentdb_api.insert_one('db2','col1',' { "a" : 100 }', NULL);

-- In this test we cannot wait till Autovaccum nap time is over, so we manually trigger the ANALYZE
ANALYZE;

-- various stats should be available
SELECT documentdb_api.db_stats('db2');


--===================== Test for "scale" Values =============================--

SELECT documentdb_api.db_stats('db2', 1);
SELECT documentdb_api.db_stats('db2', 2);
SELECT documentdb_api.db_stats('db2', 2.5);
SELECT documentdb_api.db_stats('db2', 2.99);
SELECT documentdb_api.db_stats('db2', 100);
SELECT documentdb_api.db_stats('db2', 1024.99);
SELECT documentdb_api.db_stats('db2', 2147483647);      -- INT_MAX
SELECT documentdb_api.db_stats('db2', 2147483647000);   -- More than INT_MAX

--===================== ERROR Cases =============================--

SELECT documentdb_api.db_stats('db2', 0);
SELECT documentdb_api.db_stats('db2', 0.99);
SELECT documentdb_api.db_stats('db2', -0.2);
SELECT documentdb_api.db_stats('db2', -2);
SELECT documentdb_api.db_stats('db2', -2147483648);      -- INT_MIN
SELECT documentdb_api.db_stats('db2', -2147483647000);   -- Less than INT_MIN

--======================== Clean Up =============================--

SET client_min_messages TO WARNING;

-- Clean up
SELECT documentdb_api.drop_database('db1');

-- Should return Zero values for non-existing collection (except of fs stats)
SELECT documentdb_api.db_stats('db1');

-- Clean up
SELECT documentdb_api.drop_database('db2');

-- Should return Zero values for non-existing collection (except of fs stats)
SELECT documentdb_api.db_stats('db2');

SET client_min_messages TO DEFAULT;
