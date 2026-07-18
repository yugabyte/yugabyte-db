SET search_path TO documentdb_api,documentdb_api_catalog,documentdb_api_internal,documentdb_core;

SET documentdb.next_collection_id TO 69500;
SET documentdb.next_collection_index_id TO 69500;

SELECT documentdb_api.drop_collection('hint_db', 'query_index_hints') IS NOT NULL;
SELECT documentdb_api.create_collection('hint_db', 'query_index_hints');

-- create various kinds of indexes.
SELECT documentdb_api_internal.create_indexes_non_concurrently('hint_db', '{ "createIndexes": "query_index_hints", "indexes": [ { "key": { "a": 1, "b": 1 }, "name": "a_b_1" }] }', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('hint_db', '{ "createIndexes": "query_index_hints", "indexes": [ { "key": { "a": 1, "c": 1 }, "name": "a_1_c_1" }] }', true);

SELECT documentdb_api.insert_one('hint_db', 'query_index_hints', '{ "_id": 1, "a": 1, "b": 1, "c": 1 }');

SELECT documentdb_test_helpers.run_explain_and_trim( $cmd$
    EXPLAIN (COSTS OFF, VERBOSE OFF, TIMING OFF, SUMMARY OFF, BUFFERS OFF, ANALYZE ON) SELECT document from bson_aggregation_find('hint_db', '{ "find": "query_index_hints", "sort": { "a": 1 }, "hint": { "a": 1, "c": 1 } }') $cmd$);
SELECT documentdb_test_helpers.run_explain_and_trim( $cmd$
    EXPLAIN (COSTS OFF, VERBOSE OFF, TIMING OFF, SUMMARY OFF, BUFFERS OFF, ANALYZE ON) SELECT document from bson_aggregation_find('hint_db', '{ "find": "query_index_hints", "sort": { "a": 1 }, "hint": { "a": 1, "b": 1 } }') $cmd$);

-- redo with ordered indexes off
SELECT documentdb_api.drop_collection('hint_db', 'query_index_hints') IS NOT NULL;
SELECT documentdb_api.create_collection('hint_db', 'query_index_hints');

-- create various kinds of indexes.
SELECT documentdb_api_internal.create_indexes_non_concurrently('hint_db', '{ "createIndexes": "query_index_hints", "indexes": [ { "key": { "a": 1, "b": 1 }, "name": "a_b_1", "enableOrderedIndex": false }] }', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('hint_db', '{ "createIndexes": "query_index_hints", "indexes": [ { "key": { "a": 1, "c": 1 }, "name": "a_1_c_1", "enableOrderedIndex": false }] }', true);

SELECT documentdb_api.insert_one('hint_db', 'query_index_hints', '{ "_id": 1, "a": 1, "b": 1, "c": 1 }');

SELECT documentdb_test_helpers.run_explain_and_trim( $cmd$
    EXPLAIN (COSTS OFF, VERBOSE OFF, TIMING OFF, SUMMARY OFF, BUFFERS OFF, ANALYZE ON) SELECT document from bson_aggregation_find('hint_db', '{ "find": "query_index_hints", "sort": { "a": 1 }, "hint": { "a": 1, "c": 1 } }') $cmd$);
SELECT documentdb_test_helpers.run_explain_and_trim( $cmd$
    EXPLAIN (COSTS OFF, VERBOSE OFF, TIMING OFF, SUMMARY OFF, BUFFERS OFF, ANALYZE ON) SELECT document from bson_aggregation_find('hint_db', '{ "find": "query_index_hints", "sort": { "a": 1 }, "hint": { "a": 1, "b": 1 } }') $cmd$);
