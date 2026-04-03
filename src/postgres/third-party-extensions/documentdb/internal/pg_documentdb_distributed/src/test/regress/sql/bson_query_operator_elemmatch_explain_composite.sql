SET search_path TO documentdb_core,documentdb_api,documentdb_api_catalog,documentdb_api_internal;
SET citus.next_shard_id TO 1064000;
SET documentdb.next_collection_id TO 10640;
SET documentdb.next_collection_index_id TO 10640;

set enable_seqscan TO on;
set documentdb.forceUseIndexIfAvailable to on;
set documentdb.forceDisableSeqScan to off;

SELECT documentdb_api.drop_collection('comp_elmdb', 'cmp_elemmatch_ops') IS NOT NULL;
SELECT documentdb_api.create_collection('comp_elmdb', 'cmp_elemmatch_ops') IS NOT NULL;

SELECT documentdb_api_internal.create_indexes_non_concurrently('comp_elmdb',
    '{ "createIndexes": "cmp_elemmatch_ops", "indexes": [ { "key": { "price": 1 }, "name": "price_1", "enableCompositeTerm": true }, { "key": { "brands": 1 }, "name": "brands_1", "enableCompositeTerm": true } ] }', TRUE);
SELECT documentdb_api_internal.create_indexes_non_concurrently('comp_elmdb',
    '{ "createIndexes": "cmp_elemmatch_ops", "indexes": [ { "key": { "brands.name": 1 }, "name": "brands.name_1", "enableCompositeTerm": true }, { "key": { "brands.rating": 1 }, "name": "brands.rating_1", "enableCompositeTerm": true } ] }', TRUE);

SELECT documentdb_api.insert_one('comp_elmdb', 'cmp_elemmatch_ops', '{ "_id": 1, "price": [ 120, 150, 100 ] }');
SELECT documentdb_api.insert_one('comp_elmdb', 'cmp_elemmatch_ops', '{ "_id": 2, "price": [ 110, 140, 160 ] }');

-- pushes to the price index
set documentdb.enableExtendedExplainPlans to on;
EXPLAIN (COSTS OFF, ANALYZE ON, TIMING OFF, SUMMARY OFF) SELECT document FROM bson_aggregation_find('comp_elmdb',
    '{ "find": "cmp_elemmatch_ops", "filter": { "price": { "$elemMatch": { "$gt": 120, "$lt": 150 } } } }');

-- without the GUC becomes a disjoint index filter
set documentdb.useNewElemMatchIndexOperatorOnPushdown to off;
EXPLAIN (COSTS OFF, ANALYZE ON, TIMING OFF, SUMMARY OFF) SELECT document FROM bson_aggregation_find('comp_elmdb',
    '{ "find": "cmp_elemmatch_ops", "filter": { "price": { "$elemMatch": { "$gt": 120, "$lt": 150 } } } }');
reset documentdb.useNewElemMatchIndexOperatorOnPushdown;

EXPLAIN (COSTS OFF, ANALYZE ON, TIMING OFF, SUMMARY OFF) SELECT document FROM bson_aggregation_find('comp_elmdb',
    '{ "find": "cmp_elemmatch_ops", "filter": { "price": { "$elemMatch": { "$in": [ 120, 140 ], "$gt": 121 } } } }');

EXPLAIN (COSTS OFF, ANALYZE ON, TIMING OFF, SUMMARY OFF) SELECT document FROM bson_aggregation_find('comp_elmdb',
    '{ "find": "cmp_elemmatch_ops", "filter": { "price": { "$elemMatch": { "$type": "number" } } } }');

EXPLAIN (COSTS OFF, ANALYZE ON, TIMING OFF, SUMMARY OFF) SELECT document FROM bson_aggregation_find('comp_elmdb',
    '{ "find": "cmp_elemmatch_ops", "filter": { "price": { "$elemMatch": { "$ne": 160 } } } }');

EXPLAIN (COSTS OFF, ANALYZE ON, TIMING OFF, SUMMARY OFF) SELECT document FROM bson_aggregation_find('comp_elmdb',
    '{ "find": "cmp_elemmatch_ops", "filter": { "price": { "$elemMatch": { "$nin": [ 160, 110, 140] } } } }');

-- now test some with nested objects
SELECT documentdb_api.insert_one('comp_elmdb', 'cmp_elemmatch_ops', '{ "_id": 3, "brands": [ { "name" : "alpha", "rating" : 5 }, { "name" : "beta", "rating" : 3 } ] }');
SELECT documentdb_api.insert_one('comp_elmdb', 'cmp_elemmatch_ops', '{ "_id": 4, "brands": [ { "name" : "alpha", "rating" : 4 }, { "name" : "beta", "rating" : 2 } ] }');
SELECT documentdb_api.insert_one('comp_elmdb', 'cmp_elemmatch_ops', '{ "_id": 5, "brands": [ { "name" : "alpha", "rating" : 2 }, { "name" : "beta", "rating" : 4 } ] }');

EXPLAIN (COSTS OFF, ANALYZE ON, TIMING OFF, SUMMARY OFF) SELECT document FROM bson_aggregation_find('comp_elmdb',
    '{ "find": "cmp_elemmatch_ops", "filter": { "brands": { "$elemMatch": { "name": "alpha", "rating": 2 } } } }');

EXPLAIN (COSTS OFF, ANALYZE ON, TIMING OFF, SUMMARY OFF) SELECT document FROM bson_aggregation_find('comp_elmdb',
    '{ "find": "cmp_elemmatch_ops", "filter": { "brands": { "$elemMatch": { "name": "alpha" } } } }');

-- test elemMatch behavior when confronted with multiple arrays
SELECT documentdb_api.insert_one('comp_elmdb', 'cmp_elemmatch_ops', '{ "_id": 6, "brands": [ { "name": [ "gurci", "dolte" ], "rating": 5 } ]}');

-- this technically matches the doc 6 above and the elemMatches don't get joined.
EXPLAIN (COSTS OFF, ANALYZE ON, TIMING OFF, SUMMARY OFF) SELECT document FROM bson_aggregation_find('comp_elmdb',
    '{ "find": "cmp_elemmatch_ops", "filter": { "brands": { "$elemMatch": { "name": { "$gt": "gabba", "$lt": "ergo" } } } } }');

-- this can now join the elemMatch filters
EXPLAIN (COSTS OFF, ANALYZE ON, TIMING OFF, SUMMARY OFF) SELECT document FROM bson_aggregation_find('comp_elmdb',
    '{ "find": "cmp_elemmatch_ops", "filter": { "brands.name": { "$elemMatch": { "$gt": "gabba", "$lt": "ergo" } } } }');


-- disjoint filter handling for elemMatch and non elemMatch: this matches a document since these are matching different elements of the array.
EXPLAIN (COSTS OFF, ANALYZE ON, TIMING OFF, SUMMARY OFF) SELECT document FROM bson_aggregation_find('comp_elmdb',
    '{ "find": "cmp_elemmatch_ops", "filter": { "price": { "$eq": 110, "$elemMatch": { "$gt": 155, "$lt": 165 } } } }');
