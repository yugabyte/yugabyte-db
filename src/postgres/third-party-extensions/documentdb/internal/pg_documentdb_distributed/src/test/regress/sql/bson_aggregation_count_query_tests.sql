SET search_path TO documentdb_core,documentdb_api,documentdb_api_catalog,documentdb_api_internal;
SET citus.next_shard_id TO 58000000;
SET documentdb.next_collection_id TO 58000;
SET documentdb.next_collection_index_id TO 58000;

SELECT COUNT(documentdb_api.insert_one('countdb', 'countcoll', bson_build_document('_id'::text, i, 'value'::text, i))) FROM generate_series(1, 200) i;

SELECT documentdb_api_internal.create_indexes_non_concurrently('countdb', '{ "createIndexes": "countcoll", "indexes": [ { "key": { "a": 1 }, "name": "a_1" }] }', TRUE);

ANALYZE documentdb_data.documents_58001;

SELECT document FROM bson_aggregation_count('countdb', '{ "count": "countcoll" }');
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM bson_aggregation_count('countdb', '{ "count": "countcoll" }');

SELECT document FROM bson_aggregation_count('countdb', '{ "count": "countcoll", "query": {} }');
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM bson_aggregation_count('countdb', '{ "count": "countcoll", "query": {} }');

SELECT document FROM bson_aggregation_count('countdb', '{ "count": "countcoll", "query": {}, "limit": 1000 }');
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM bson_aggregation_count('countdb', '{ "count": "countcoll", "query": {}, "limit": 1000 }');

SELECT document FROM bson_aggregation_count('countdb', '{ "count": "countcoll", "query": {}, "limit": 100 }');
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM bson_aggregation_count('countdb', '{ "count": "countcoll", "query": {}, "limit": 100 }');

SELECT document FROM bson_aggregation_count('countdb', '{ "count": "countcoll", "query": { "$alwaysTrue": 1 } }');
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM bson_aggregation_count('countdb', '{ "count": "countcoll", "query": { "$alwaysTrue": 1 } }');

SELECT document FROM bson_aggregation_count('countdb', '{ "count": "countcoll", "query": { "$alwaysFalse": 1 } }');
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM bson_aggregation_count('countdb', '{ "count": "countcoll", "query": { "$alwaysFalse": 1 } }');

BEGIN;
set local client_min_messages to DEBUG1;
set local documentdb.forceRunDiagnosticCommandInline to on;

-- here we only query count metadata
SELECT document FROM bson_aggregation_count('countdb', '{ "count": "countcoll", "query": {} }');

-- here we get storage & index sizes
SELECT documentdb_api.coll_stats('countdb', 'countcoll');

-- test it out for collstats aggregation too
SELECT document FROM bson_aggregation_pipeline('countdb', '{ "aggregate": "countcoll", "pipeline": [ { "$collStats": { "count": {} }} ] }');

SELECT document FROM bson_aggregation_pipeline('countdb', '{ "aggregate": "countcoll", "pipeline": [ { "$collStats": { "storageStats": {} }} ] }');

ROLLBACK;

set documentdb.enableNewCountAggregates to off;
SELECT document FROM bson_aggregation_count('countdb', '{ "count": "countcoll", "query": {"value": {"$gt": 20}, "value": {"$lt": 150} } }');
EXPLAIN (ANALYZE ON, COSTS OFF, VERBOSE ON, TIMING OFF, SUMMARY OFF) SELECT document FROM bson_aggregation_count('countdb', '{ "count": "countcoll", "query": {"value": {"$gt": 20}, "value": {"$lt": 150} } }');

set documentdb.enableNewCountAggregates to on;
SELECT document FROM bson_aggregation_count('countdb', '{ "count": "countcoll", "query": {"value": {"$gt": 20}, "value": {"$lt": 150} } }');
EXPLAIN (ANALYZE ON, COSTS OFF, VERBOSE ON, TIMING OFF, SUMMARY OFF) SELECT document FROM bson_aggregation_count('countdb', '{ "count": "countcoll", "query": {"value": {"$gt": 20}, "value": {"$lt": 150} } }');

-- test with sharded collection, should be pushed to the workers
SELECT documentdb_api.shard_collection('countdb', 'countcoll', '{ "_id": "hashed" }', false);

EXPLAIN (COSTS OFF, VERBOSE ON, TIMING OFF, SUMMARY OFF) SELECT document FROM bson_aggregation_count('countdb', '{ "count": "countcoll", "query": {"value": {"$gt": 20}, "value": {"$lt": 150} } }');
EXPLAIN (COSTS OFF, VERBOSE ON, TIMING OFF, SUMMARY OFF) SELECT document FROM bson_aggregation_pipeline('countdb', '{ "aggregate": "countcoll", "pipeline": [{"$match": {"value": {"$gt": 20}, "value": {"$lt": 150} }}, {"$count": "c"}] }');
EXPLAIN (COSTS OFF, VERBOSE ON, TIMING OFF, SUMMARY OFF) SELECT document FROM bson_aggregation_pipeline('countdb', '{ "aggregate": "countcoll", "pipeline": [{"$match": {"value": {"$gt": 20}, "value": {"$lt": 150} }}, {"$group": {"_id": null, "count": {"$sum": 1}}}] }');

-- Test $sum with empty object, null, and non-numeric values (should all result in 0)

-- $sum: {}
EXPLAIN (COSTS OFF, VERBOSE ON, TIMING OFF, SUMMARY OFF)
    SELECT document FROM bson_aggregation_pipeline('countdb', '{ "aggregate" : "countcoll", "pipeline" : [{ "$match" : { "value" : { "$gt" : 50 } } }, { "$group" : { "_id" : { "$literal" : "sumEmpty" }, "cnt" : { "$sum" : {} } } }] }');
SELECT document FROM bson_aggregation_pipeline('countdb', '{ "aggregate" : "countcoll", "pipeline" : [{ "$match" : { "value" : { "$gt" : 50 } } }, { "$group" : { "_id" : { "$literal" : "sumEmpty" }, "cnt" : { "$sum" : {} } } }] }');

-- $sum: null
EXPLAIN (COSTS OFF, VERBOSE ON, TIMING OFF, SUMMARY OFF)
    SELECT document FROM bson_aggregation_pipeline('countdb', '{ "aggregate" : "countcoll", "pipeline" : [{ "$match" : { "value" : { "$gt" : 50 } } }, { "$group" : { "_id" : { "$literal" : "sumNull" }, "cnt" : { "$sum" : null } } }] }');
SELECT document FROM bson_aggregation_pipeline('countdb', '{ "aggregate" : "countcoll", "pipeline" : [{ "$match" : { "value" : { "$gt" : 50 } } }, { "$group" : { "_id" : { "$literal" : "sumNull" }, "cnt" : { "$sum" : null } } }] }');

-- $sum: <constant non-numeric>
EXPLAIN (COSTS OFF, VERBOSE ON, TIMING OFF, SUMMARY OFF)
    SELECT document FROM bson_aggregation_pipeline('countdb', '{ "aggregate" : "countcoll", "pipeline" : [{ "$match" : { "value" : { "$gt" : 50 } } }, { "$group" : { "_id" : { "$literal" : "sumUtf8" }, "cnt" : { "$sum" : "constant-non-numeric" } } }] }');
SELECT document FROM bson_aggregation_pipeline('countdb', '{ "aggregate" : "countcoll", "pipeline" : [{ "$match" : { "value" : { "$gt" : 50 } } }, { "$group" : { "_id" : { "$literal" : "sumUtf8" }, "cnt" : { "$sum" : "constant-non-numeric" } } }] }');
