SET search_path TO documentdb_api,documentdb_api_catalog,documentdb_core;

SET documentdb.next_collection_id TO 200;
SET documentdb.next_collection_index_id TO 200;

SELECT COUNT(documentdb_api.insert_one('exprdb', 'exprcoll', bson_build_document('_id'::text, i, 'a'::text, i, 'b'::text, i))) FROM generate_series(1, 1000) i;

SELECT documentdb_api_internal.create_indexes_non_concurrently('exprdb', '{ "createIndexes": "exprcoll", "indexes": [ { "key": { "a": 1 }, "name": "a_1", "enableOrderedIndex": true }] }', TRUE);
SELECT documentdb_api_internal.create_indexes_non_concurrently('exprdb', '{ "createIndexes": "exprcoll", "indexes": [ { "key": { "b": 1 }, "name": "b_1", "enableOrderedIndex": false }] }', TRUE);

-- no pushdown for arbitrary operators
EXPLAIN (COSTS OFF) SELECT document FROM bson_aggregation_find('exprdb', '{ "find": "exprcoll", "filter": { "$expr": { "$isArray": "$a" } } }');

-- pushdown does not work for a non-ordered index
EXPLAIN (COSTS OFF) SELECT document FROM bson_aggregation_find('exprdb', '{ "find": "exprcoll", "filter": { "$expr": { "$eq": [ "$b", 10 ] } } }');
EXPLAIN (COSTS OFF) SELECT document FROM bson_aggregation_find('exprdb', '{ "find": "exprcoll", "filter": { "$expr": { "$eq": [ 10, "$b" ] } } }');

EXPLAIN (COSTS OFF) SELECT document FROM bson_aggregation_find('exprdb', '{ "find": "exprcoll", "filter": { "$expr": { "$gt": [ "$b", 10 ] } } }');
EXPLAIN (COSTS OFF) SELECT document FROM bson_aggregation_find('exprdb', '{ "find": "exprcoll", "filter": { "$expr": { "$gt": [ 10, "$b" ] } } }');

-- simple pushdown of operators
EXPLAIN (COSTS OFF) SELECT document FROM bson_aggregation_find('exprdb', '{ "find": "exprcoll", "filter": { "$expr": { "$eq": [ "$a", 10 ] } } }');
EXPLAIN (COSTS OFF) SELECT document FROM bson_aggregation_find('exprdb', '{ "find": "exprcoll", "filter": { "$expr": { "$eq": [ 10, "$a" ] } } }');

EXPLAIN (COSTS OFF) SELECT document FROM bson_aggregation_find('exprdb', '{ "find": "exprcoll", "filter": { "$expr": { "$gt": [ "$a", 10 ] } } }');
EXPLAIN (COSTS OFF) SELECT document FROM bson_aggregation_find('exprdb', '{ "find": "exprcoll", "filter": { "$expr": { "$gt": [ 10, "$a" ] } } }');

EXPLAIN (COSTS OFF) SELECT document FROM bson_aggregation_find('exprdb', '{ "find": "exprcoll", "filter": { "$expr": { "$gte": [ "$a", 10 ] } } }');
EXPLAIN (COSTS OFF) SELECT document FROM bson_aggregation_find('exprdb', '{ "find": "exprcoll", "filter": { "$expr": { "$gte": [ 10, "$a" ] } } }');

EXPLAIN (COSTS OFF) SELECT document FROM bson_aggregation_find('exprdb', '{ "find": "exprcoll", "filter": { "$expr": { "$lt": [ "$a", 10 ] } } }');
EXPLAIN (COSTS OFF) SELECT document FROM bson_aggregation_find('exprdb', '{ "find": "exprcoll", "filter": { "$expr": { "$lt": [ 10, "$a" ] } } }');

EXPLAIN (COSTS OFF) SELECT document FROM bson_aggregation_find('exprdb', '{ "find": "exprcoll", "filter": { "$expr": { "$lte": [ "$a", 10 ] } } }');
EXPLAIN (COSTS OFF) SELECT document FROM bson_aggregation_find('exprdb', '{ "find": "exprcoll", "filter": { "$expr": { "$lte": [ 10, "$a" ] } } }');

-- partial pushdown if it's conjunction
EXPLAIN (COSTS OFF) SELECT document FROM bson_aggregation_find('exprdb', '{ "find": "exprcoll", "filter": { "$expr": { "$and": [ { "$lt": [ "$a", 10 ] }, { "$isArray": "$c" } ] } } }');
EXPLAIN (COSTS OFF) SELECT document FROM bson_aggregation_find('exprdb', '{ "find": "exprcoll", "filter": { "$expr": { "$and": [ { "$lt": [ "$a", 10 ] }, { "$gt": [ "$b", 6 ] } ] } } }');
EXPLAIN (COSTS OFF) SELECT document FROM bson_aggregation_find('exprdb', '{ "find": "exprcoll", "filter": { "$expr": { "$and": [ { "$lt": [ "$a", 10 ] }, { "$or": [ { "$eq": [ "$a", 5 ] }, { "$lt": [ "$a", 6 ] } ] } ] } } }');

-- full pushdown of supported conjunction
EXPLAIN (COSTS OFF) SELECT document FROM bson_aggregation_find('exprdb', '{ "find": "exprcoll", "filter": { "$expr": { "$and": [ { "$lt": [ "$a", 10 ] }, { "$gt": [ "$a", 5 ] } ] } } }');

-- pushdown of expressions that evaluate to constants
EXPLAIN (COSTS OFF) SELECT document FROM bson_aggregation_find('exprdb', '{ "find": "exprcoll", "filter": { "$expr": { "$lt": [ { "$add": [ 2, 3 ] }, "$a" ] } } }');

-- Support pushdown of expressions that are variables.
EXPLAIN (COSTS OFF) SELECT document FROM bson_aggregation_find('exprdb', '{ "find": "exprcoll", "filter": { "$expr": { "$lt": [ "$$myvar", "$a" ] } }, "let": { "myvar": 3 } }');

-- TODO: Support pushdown of expressions that evaluate to constants with variables.
EXPLAIN (COSTS OFF) SELECT document FROM bson_aggregation_find('exprdb', '{ "find": "exprcoll", "filter": { "$expr": { "$lt": [ { "$add": [ 2, "$$myvar" ] }, "$a" ] } }, "let": { "myvar": 3 } }');

set documentdb.enableExtendedExplainPlans to on;
SELECT documentdb_test_helpers.run_explain_and_trim($cmd$
    EXPLAIN (COSTS OFF, ANALYZE ON, SUMMARY OFF, TIMING OFF, BUFFERS OFF) SELECT document FROM bson_aggregation_find('exprdb', '{ "find": "exprcoll", "filter": { "$expr": { "$lt": [ "$$myvar", "$a" ] } }, "let": { "myvar": 3 } }') $cmd$);

-- now try with $lookup
SELECT COUNT(documentdb_api.insert_one('exprdb', 'exprcollright', bson_build_document('_id'::text, i, 'a'::text, i, 'b'::text, i))) FROM generate_series(1, 1000) i;
SELECT documentdb_api_internal.create_indexes_non_concurrently('exprdb', '{ "createIndexes": "exprcollright", "indexes": [ { "key": { "a": 1 }, "name": "a_1", "enableOrderedIndex": true }] }', TRUE);

-- standard lookup
SELECT documentdb_test_helpers.run_explain_and_trim($cmd$
    EXPLAIN (COSTS OFF, ANALYZE ON, SUMMARY OFF, TIMING OFF, BUFFERS OFF) SELECT document FROM bson_aggregation_pipeline('exprdb',
        '{ "aggregate": "exprcoll", "pipeline":[ { "$match": { "$expr": { "$gte": [ "$a", 10 ] } } }, { "$lookup": { "from": "exprcollright", "as": "res", "localField": "a", "foreignField": "a" } } ] }') $cmd$);

-- lookup with let
SELECT documentdb_test_helpers.run_explain_and_trim($cmd$
    EXPLAIN (COSTS OFF, ANALYZE ON, SUMMARY OFF, TIMING OFF, BUFFERS OFF) SELECT document FROM bson_aggregation_pipeline('exprdb',
        '{ "aggregate": "exprcoll", "pipeline":[ { "$match": { "$expr": { "$gte": [ "$a", 10 ] } } }, { "$lookup": { "from": "exprcollright", "as": "res", "let": { "myvar": "$a" }, "pipeline": [ { "$match": { "$expr": { "$eq": [ "$a", "$$myvar" ] } } } ] } } ] }') $cmd$);

-- composite with partial $expr to the index
SELECT documentdb_api_internal.create_indexes_non_concurrently('exprdb', '{ "createIndexes": "exprcoll", "indexes": [ { "key": { "a": 1, "b": 1, "c": 1, "d": 1 }, "name": "abcd_1", "enableOrderedIndex": true }] }', TRUE);

EXPLAIN (COSTS OFF) SELECT document FROM bson_aggregation_find('exprdb',
    '{ "find": "exprcoll", "filter": { "a": 5, "b": 5, "c": 5, "$expr": { "$and": [ { "$eq": [ "$f", "$g" ] }, { "$gt": [ "$d", 5 ] } ] } } }');


-- after adding a row with arrays, can no longer push down (multikey expr not supported)
SELECT documentdb_api.insert_one('exprdb', 'exprcoll', '{ "_id": "array", "a": [ 1, 2, 3 ] }');

-- cannot push down any of these
set documentdb.forceDisableSeqScan to on;
EXPLAIN (COSTS OFF) SELECT document FROM bson_aggregation_find('exprdb', '{ "find": "exprcoll", "filter": { "$expr": { "$eq": [ "$a", 10 ] } } }');
EXPLAIN (COSTS OFF) SELECT document FROM bson_aggregation_find('exprdb', '{ "find": "exprcoll", "filter": { "$expr": { "$eq": [ 10, "$a" ] } } }');

EXPLAIN (COSTS OFF) SELECT document FROM bson_aggregation_find('exprdb', '{ "find": "exprcoll", "filter": { "$expr": { "$gt": [ "$a", 10 ] } } }');
EXPLAIN (COSTS OFF) SELECT document FROM bson_aggregation_find('exprdb', '{ "find": "exprcoll", "filter": { "$expr": { "$gt": [ 10, "$a" ] } } }');

EXPLAIN (COSTS OFF) SELECT document FROM bson_aggregation_find('exprdb', '{ "find": "exprcoll", "filter": { "$expr": { "$gte": [ "$a", 10 ] } } }');
EXPLAIN (COSTS OFF) SELECT document FROM bson_aggregation_find('exprdb', '{ "find": "exprcoll", "filter": { "$expr": { "$gte": [ 10, "$a" ] } } }');

EXPLAIN (COSTS OFF) SELECT document FROM bson_aggregation_find('exprdb', '{ "find": "exprcoll", "filter": { "$expr": { "$lt": [ "$a", 10 ] } } }');
EXPLAIN (COSTS OFF) SELECT document FROM bson_aggregation_find('exprdb', '{ "find": "exprcoll", "filter": { "$expr": { "$lt": [ 10, "$a" ] } } }');

EXPLAIN (COSTS OFF) SELECT document FROM bson_aggregation_find('exprdb', '{ "find": "exprcoll", "filter": { "$expr": { "$lte": [ "$a", 10 ] } } }');
EXPLAIN (COSTS OFF) SELECT document FROM bson_aggregation_find('exprdb', '{ "find": "exprcoll", "filter": { "$expr": { "$lte": [ 10, "$a" ] } } }');