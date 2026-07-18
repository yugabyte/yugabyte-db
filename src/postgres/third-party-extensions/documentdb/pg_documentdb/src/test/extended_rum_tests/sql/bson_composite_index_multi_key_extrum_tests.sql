SET search_path TO documentdb_api,documentdb_core,documentdb_api_catalog;

SET documentdb.next_collection_id TO 1000;
SET documentdb.next_collection_index_id TO 1000;

set documentdb.enableCompositeReducedCorrelatedTerms to on;
set documentdb.enableUniqueCompositeReducedCorrelatedTerms to on;

-- now test query path pushdown
SELECT documentdb_api_internal.create_indexes_non_concurrently('mkey_db', '{ "createIndexes": "mkey_coll", "indexes": [ { "key": { "a.b": 1, "a.c": 1 }, "name": "a_b_c_1", "enableOrderedIndex": 1 } ] }');

-- multikey path on "a" - query on a.c can't be pushed
SELECT documentdb_api.insert_one('mkey_db', 'mkey_coll', '{ "_id": 1, "a": [ { "b": 1, "c": 1 }, { "b": 2, "c": 2 }] }');

set documentdb.enableExtendedExplainPlans to on;
SELECT documentdb_test_helpers.run_explain_and_trim( $cmd$
    EXPLAIN (COSTS OFF, ANALYZE ON, SUMMARY OFF, TIMING OFF, BUFFERS OFF) SELECT document FROM bson_aggregation_find('mkey_db', '{ "find": "mkey_coll", "filter": { "a.b": { "$gt": 0 }, "a.c": 2 }}') $cmd$);

SELECT documentdb_test_helpers.run_explain_and_trim( $cmd$
    EXPLAIN (COSTS OFF, ANALYZE ON, SUMMARY OFF, TIMING OFF, BUFFERS OFF) SELECT document FROM bson_aggregation_find('mkey_db', '{ "find": "mkey_coll", "filter": { "a.b": 1, "a.c": 2 }}') $cmd$);

-- order by doesn't work on secondary path (but works on the first one)
SELECT documentdb_test_helpers.run_explain_and_trim($cmd$
    EXPLAIN (COSTS OFF, ANALYZE ON, SUMMARY OFF, TIMING OFF, BUFFERS OFF) SELECT document FROM bson_aggregation_find('mkey_db', '{ "find": "mkey_coll", "filter": { "a.b": { "$exists": true } }, "sort": { "a.b": 1, "a.c": 1 }, "hint": "a_b_c_1" }') $cmd$);

SELECT documentdb_test_helpers.run_explain_and_trim( $cmd$
    EXPLAIN (COSTS OFF, ANALYZE ON, SUMMARY OFF, TIMING OFF, BUFFERS OFF) SELECT document FROM bson_aggregation_find('mkey_db', '{ "find": "mkey_coll", "filter": { "a.b": { "$exists": true } }, "sort": { "a.b": -1 }, "hint": "a_b_c_1" }') $cmd$);

-- multikey on a.b - query on a.c can be pushed
TRUNCATE documentdb_data.documents_801;

SELECT documentdb_api.insert_one('mkey_db', 'mkey_coll', '{ "_id": 1, "a": { "b": [ 1, 2 ], "c": 2 } }');
SELECT documentdb_test_helpers.run_explain_and_trim( $cmd$
    EXPLAIN (COSTS OFF, ANALYZE ON, SUMMARY OFF, TIMING OFF, BUFFERS OFF) SELECT document FROM bson_aggregation_find('mkey_db', '{ "find": "mkey_coll", "filter": { "a.b": { "$gt": 0 }, "a.c": 2 }}') $cmd$);

SELECT documentdb_test_helpers.run_explain_and_trim( $cmd$
    EXPLAIN (COSTS OFF, ANALYZE ON, SUMMARY OFF, TIMING OFF, BUFFERS OFF) SELECT document FROM bson_aggregation_find('mkey_db', '{ "find": "mkey_coll", "filter": { "a.b": 1, "a.c": 2 }}') $cmd$);

-- same behavior for order by.
SELECT documentdb_test_helpers.run_explain_and_trim($cmd$
    EXPLAIN (COSTS OFF, ANALYZE ON, SUMMARY OFF, TIMING OFF, BUFFERS OFF) SELECT document FROM bson_aggregation_find('mkey_db', '{ "find": "mkey_coll", "filter": { "a.b": { "$exists": true } }, "sort": { "a.b": 1, "a.c": 1 }, "hint": "a_b_c_1" }') $cmd$);

SELECT documentdb_test_helpers.run_explain_and_trim( $cmd$
    EXPLAIN (COSTS OFF, ANALYZE ON, SUMMARY OFF, TIMING OFF, BUFFERS OFF) SELECT document FROM bson_aggregation_find('mkey_db', '{ "find": "mkey_coll", "filter": { "a.b": { "$exists": true } }, "sort": { "a.b": -1 }, "hint": "a_b_c_1" }') $cmd$);
