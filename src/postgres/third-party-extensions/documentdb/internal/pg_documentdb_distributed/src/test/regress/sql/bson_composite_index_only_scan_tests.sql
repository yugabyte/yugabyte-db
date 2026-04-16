SET citus.next_shard_id TO 690000;
SET documentdb.next_collection_id TO 69000;
SET documentdb.next_collection_index_id TO 69000;
SET search_path TO documentdb_api,documentdb_api_catalog,documentdb_api_internal,documentdb_core;

SET documentdb.enableExtendedExplainPlans to on;
SET documentdb.enableIndexOnlyScan to on;

-- we set this to make sure it doesn't pick index only scan on cases it shouldn't and when it should, costing won't affect test stability.
SET documentdb.forceIndexOnlyScanIfAvailable to on;


-- if documentdb_extended_rum exists, set alternate index handler
SELECT pg_catalog.set_config('documentdb.alternate_index_handler_name', 'extended_rum', false), extname FROM pg_extension WHERE extname = 'documentdb_extended_rum';

SELECT documentdb_api.drop_collection('idx_only_scan_db', 'idx_only_scan_coll') IS NOT NULL;
SELECT documentdb_api.create_collection('idx_only_scan_db', 'idx_only_scan_coll');

SELECT documentdb_api_internal.create_indexes_non_concurrently('idx_only_scan_db', '{ "createIndexes": "idx_only_scan_coll", "indexes": [ { "key": { "country": 1 }, "storageEngine": { "enableOrderedIndex": true }, "name": "country_1" }] }', true);

select documentdb_api.insert_one('idx_only_scan_db', 'idx_only_scan_coll', '{"_id": 1, "country": "USA", "provider": "AWS"}');
select documentdb_api.insert_one('idx_only_scan_db', 'idx_only_scan_coll', '{"_id": 2, "country": "USA", "provider": "Azure"}');
select documentdb_api.insert_one('idx_only_scan_db', 'idx_only_scan_coll', '{"_id": 3, "country": "Mexico", "provider": "GCP"}');
select documentdb_api.insert_one('idx_only_scan_db', 'idx_only_scan_coll', '{"_id": 4, "country": "India", "provider": "AWS"}');
select documentdb_api.insert_one('idx_only_scan_db', 'idx_only_scan_coll', '{"_id": 5, "country": "Brazil", "provider": "Azure"}');
select documentdb_api.insert_one('idx_only_scan_db', 'idx_only_scan_coll', '{"_id": 6, "country": "Brazil", "provider": "GCP"}');
select documentdb_api.insert_one('idx_only_scan_db', 'idx_only_scan_coll', '{"_id": 7, "country": "Mexico", "provider": "AWS"}');
select documentdb_api.insert_one('idx_only_scan_db', 'idx_only_scan_coll', '{"_id": 8, "country": "USA", "provider": "Azure"}');
select documentdb_api.insert_one('idx_only_scan_db', 'idx_only_scan_coll', '{"_id": 9, "country": "India", "provider": "GCP"}');
select documentdb_api.insert_one('idx_only_scan_db', 'idx_only_scan_coll', '{"_id": 10, "country": "Mexico", "provider": "AWS"}');
select documentdb_api.insert_one('idx_only_scan_db', 'idx_only_scan_coll', '{"_id": 11, "country": "USA", "provider": "Azure"}');
select documentdb_api.insert_one('idx_only_scan_db', 'idx_only_scan_coll', '{"_id": 12, "country": "Spain", "provider": "GCP"}');
select documentdb_api.insert_one('idx_only_scan_db', 'idx_only_scan_coll', '{"_id": 13, "country": "Italy", "provider": "AWS"}');
select documentdb_api.insert_one('idx_only_scan_db', 'idx_only_scan_coll', '{"_id": 14, "country": "France", "provider": "Azure"}');
select documentdb_api.insert_one('idx_only_scan_db', 'idx_only_scan_coll', '{"_id": 15, "country": "France", "provider": "GCP"}');
select documentdb_api.insert_one('idx_only_scan_db', 'idx_only_scan_coll', '{"_id": 16, "country": "Mexico", "provider": "AWS"}');

ANALYZE documentdb_data.documents_69001;

set enable_seqscan to off;
set enable_bitmapscan to off;

-- test index only scan
EXPLAIN (ANALYZE ON, COSTS OFF, VERBOSE ON, TIMING OFF, SUMMARY OFF) SELECT document FROM bson_aggregation_pipeline('idx_only_scan_db', '{ "aggregate" : "idx_only_scan_coll", "pipeline" : [{ "$match" : {"country": {"$gte": "Brazil"}} }, { "$group" : { "_id" : "1", "n" : { "$sum" : 1 } } }]}');
SELECT document FROM bson_aggregation_pipeline('idx_only_scan_db', '{ "aggregate" : "idx_only_scan_coll", "pipeline" : [{ "$match" : {"country": {"$gte": "Brazil"}} }, { "$group" : { "_id" : "1", "n" : { "$sum" : 1 } } }]}');

EXPLAIN (ANALYZE ON, COSTS OFF, VERBOSE ON, TIMING OFF, SUMMARY OFF) SELECT document FROM bson_aggregation_pipeline('idx_only_scan_db', '{ "aggregate" : "idx_only_scan_coll", "pipeline" : [{ "$match" : {"country": {"$lt": "Mexico"}} }, { "$group" : { "_id" : "1", "n" : { "$sum" : 1 } } }]}');
SELECT document FROM bson_aggregation_pipeline('idx_only_scan_db', '{ "aggregate" : "idx_only_scan_coll", "pipeline" : [{ "$match" : {"country": {"$lt": "Mexico"}} }, { "$group" : { "_id" : "1", "n" : { "$sum" : 1 } } }]}');

EXPLAIN (ANALYZE ON, COSTS OFF, VERBOSE ON, TIMING OFF, SUMMARY OFF) SELECT document FROM bson_aggregation_count('idx_only_scan_db', '{"count": "idx_only_scan_coll", "query": {"country": {"$eq": "USA"}}}');
SELECT document FROM bson_aggregation_count('idx_only_scan_db', '{"count": "idx_only_scan_coll", "query": {"country": {"$eq": "USA"}}}');

-- now run VACUUM should see the heap blocks go down
VACUUM (FREEZE ON) documentdb_data.documents_69001;

EXPLAIN (ANALYZE ON, COSTS OFF, VERBOSE ON, TIMING OFF, SUMMARY OFF) SELECT document FROM bson_aggregation_count('idx_only_scan_db', '{"count": "idx_only_scan_coll", "query": {"country": {"$eq": "USA"}}}');

-- now update a document to change the country
SELECT documentdb_api.update('idx_only_scan_db', '{"update": "decimal128", "updates":[{"q": {"_id": 8},"u":{"$set":{"country": "Italy"}},"multi":false}]}');

EXPLAIN (ANALYZE ON, COSTS OFF, VERBOSE ON, TIMING OFF, SUMMARY OFF) SELECT document FROM bson_aggregation_count('idx_only_scan_db', '{"count": "idx_only_scan_coll", "query": {"country": {"$eq": "USA"}}}');
SELECT document FROM bson_aggregation_count('idx_only_scan_db', '{"count": "idx_only_scan_coll", "query": {"country": {"$eq": "USA"}}}');

EXPLAIN (ANALYZE ON, COSTS OFF, VERBOSE ON, TIMING OFF, SUMMARY OFF) SELECT document FROM bson_aggregation_count('idx_only_scan_db', '{"count": "idx_only_scan_coll", "query": {"country": {"$in": ["USA", "Italy"]}}}');
SELECT document FROM bson_aggregation_count('idx_only_scan_db', '{"count": "idx_only_scan_coll", "query": {"country": {"$in": ["USA", "Italy"]}}}');

-- match with count
EXPLAIN (ANALYZE ON, COSTS OFF, VERBOSE ON, TIMING OFF, SUMMARY OFF) SELECT document FROM bson_aggregation_pipeline('idx_only_scan_db', '{ "aggregate" : "idx_only_scan_coll", "pipeline" : [{ "$match" : {"country": {"$lt": "Mexico"}} }, { "$count": "count" }]}');
SELECT document FROM bson_aggregation_pipeline('idx_only_scan_db', '{ "aggregate" : "idx_only_scan_coll", "pipeline" : [{ "$match" : {"country": {"$lt": "Mexico"}} }, { "$count": "count" }]}');

-- range queries should also use index only scan
EXPLAIN (ANALYZE ON, COSTS OFF, VERBOSE ON, TIMING OFF, SUMMARY OFF) SELECT document FROM bson_aggregation_pipeline('idx_only_scan_db', '{ "aggregate" : "idx_only_scan_coll", "pipeline" : [{ "$match" : {"country": {"$gt": "Brazil"}, "country": {"$lt": "Mexico"}} }, { "$count": "count" }]}');
SELECT document FROM bson_aggregation_pipeline('idx_only_scan_db', '{ "aggregate" : "idx_only_scan_coll", "pipeline" : [{ "$match" : {"country": {"$gt": "Brazil"}, "country": {"$lt": "Mexico"}} }, { "$count": "count" }]}');

EXPLAIN (ANALYZE ON, COSTS OFF, VERBOSE ON, TIMING OFF, SUMMARY OFF) SELECT document FROM bson_aggregation_pipeline('idx_only_scan_db', '{ "aggregate" : "idx_only_scan_coll", "pipeline" : [{"$match": { "country": {"$gt": "Brazil"}, "country": {"$lt": "Mexico"} }}, { "$limit": 10 }, { "$count": "count" }]}');
EXPLAIN (ANALYZE ON, COSTS OFF, VERBOSE ON, TIMING OFF, SUMMARY OFF) SELECT document FROM bson_aggregation_pipeline('idx_only_scan_db', '{ "aggregate" : "idx_only_scan_coll", "pipeline" : [{"$match": { "country": {"$gt": "Brazil"}, "country": {"$lt": "Mexico"} }}, { "$limit": 10 }, { "$group": { "_id": 1, "c": { "$sum": 1 } } }]}');

-- No filters and not sharded should use _id_ index
EXPLAIN (ANALYZE ON, COSTS OFF, VERBOSE ON, TIMING OFF, SUMMARY OFF) SELECT document FROM bson_aggregation_pipeline('idx_only_scan_db', '{ "aggregate" : "idx_only_scan_coll", "pipeline" : [{"$match": {}}, { "$count": "count" }]}');
EXPLAIN (ANALYZE ON, COSTS OFF, VERBOSE ON, TIMING OFF, SUMMARY OFF) SELECT document FROM bson_aggregation_pipeline('idx_only_scan_db', '{ "aggregate" : "idx_only_scan_coll", "pipeline" : [{"$match": { "_id": { "$gt": 3, "$lt": 8 }}}, { "$count": "count" }]}');

-- count with match + limit uses index only scan 
EXPLAIN (ANALYZE ON, COSTS OFF, VERBOSE ON, TIMING OFF, SUMMARY OFF) SELECT document FROM bson_aggregation_pipeline('idx_only_scan_db', '{ "aggregate" : "idx_only_scan_coll", "pipeline" : [{"$match": { "_id": { "$gt": 3, "$lt": 8 }}}, { "$limit": 10 }, { "$count": "count" }]}');
EXPLAIN (ANALYZE ON, COSTS OFF, VERBOSE ON, TIMING OFF, SUMMARY OFF) SELECT document FROM bson_aggregation_pipeline('idx_only_scan_db', '{ "aggregate" : "idx_only_scan_coll", "pipeline" : [{"$match": { "_id": { "$gt": 3, "$lt": 8 }}}, { "$limit": 10 }, { "$group": { "_id": 1, "c": { "$sum": 1 } } }]}');

-- this will need index scan
EXPLAIN (ANALYZE ON, COSTS OFF, VERBOSE ON, TIMING OFF, SUMMARY OFF) SELECT document FROM bson_aggregation_pipeline('idx_only_scan_db', '{ "aggregate" : "idx_only_scan_coll", "pipeline" : [{"$match": { "_id": { "$gt": 3, "$lt": 8 }}}, { "$limit": 10 }, { "$group": { "_id": 1, "c": { "$max": "$_id" } } }]}');

-- now test with compound index
SELECT documentdb_api_internal.create_indexes_non_concurrently('idx_only_scan_db', '{ "createIndexes": "idx_only_scan_coll", "indexes": [ { "key": { "country": 1, "provider": 1 }, "storageEngine": { "enableOrderedIndex": true }, "name": "country_provider_1" }] }', true);

EXPLAIN (ANALYZE ON, COSTS OFF, VERBOSE ON, TIMING OFF, SUMMARY OFF) SELECT document FROM bson_aggregation_pipeline('idx_only_scan_db', '{ "aggregate" : "idx_only_scan_coll", "pipeline" : [{ "$match" : {"country": {"$eq": "Mexico"}, "provider": {"$eq": "AWS"}} }, { "$count": "count" }]}');
SELECT document FROM bson_aggregation_pipeline('idx_only_scan_db', '{ "aggregate" : "idx_only_scan_coll", "pipeline" : [{ "$match" : {"country": {"$eq": "Mexico"}, "provider": {"$eq": "AWS"}} }, { "$count": "count" }]}');

EXPLAIN (ANALYZE ON, COSTS OFF, VERBOSE ON, TIMING OFF, SUMMARY OFF) SELECT document FROM bson_aggregation_pipeline('idx_only_scan_db', '{ "aggregate" : "idx_only_scan_coll", "pipeline" : [{ "$match" : {"country": {"$eq": "Mexico"}, "provider": {"$eq": "GCP"}} }, { "$count": "count" }]}');
SELECT document FROM bson_aggregation_pipeline('idx_only_scan_db', '{ "aggregate" : "idx_only_scan_coll", "pipeline" : [{ "$match" : {"country": {"$eq": "Mexico"}, "provider": {"$eq": "GCP"}} }, { "$count": "count" }]}');

-- if the filter doesn't match the first field in the index, shouldn't use the compound index and not index only scan
EXPLAIN (ANALYZE ON, COSTS OFF, VERBOSE ON, TIMING OFF, SUMMARY OFF) SELECT document FROM bson_aggregation_pipeline('idx_only_scan_db', '{ "aggregate" : "idx_only_scan_coll", "pipeline" : [{ "$match" : {"provider": {"$eq": "AWS"}} }, { "$count": "count" }]}');

-- if we project something out it shouldn't do index only scan
EXPLAIN (ANALYZE ON, COSTS OFF, VERBOSE ON, TIMING OFF, SUMMARY OFF) SELECT document FROM bson_aggregation_pipeline('idx_only_scan_db', '{ "aggregate" : "idx_only_scan_coll", "pipeline" : [{ "$match" : {"country": {"$gte": "Mexico"}} }, { "$group" : { "_id" : "$country", "n" : { "$sum" : 1 } } }]}');
EXPLAIN (ANALYZE ON, COSTS OFF, VERBOSE ON, TIMING OFF, SUMMARY OFF) SELECT document FROM bson_aggregation_pipeline('idx_only_scan_db', '{ "aggregate" : "idx_only_scan_coll", "pipeline" : [{ "$match" : {"country": {"$eq": "Mexico"}} }]}');

-- negation, elemMatch, type and size queries should not use index only scan
EXPLAIN (ANALYZE ON, COSTS OFF, VERBOSE ON, TIMING OFF, SUMMARY OFF) SELECT document FROM bson_aggregation_pipeline('idx_only_scan_db', '{ "aggregate" : "idx_only_scan_coll", "pipeline" : [{ "$match" : {"country": {"$ne": "Mexico"}} }, { "$count": "count" }]}');
EXPLAIN (ANALYZE ON, COSTS OFF, VERBOSE ON, TIMING OFF, SUMMARY OFF) SELECT document FROM bson_aggregation_pipeline('idx_only_scan_db', '{ "aggregate" : "idx_only_scan_coll", "pipeline" : [{ "$match" : {"country": {"$type": "string"}} }, { "$count": "count" }]}');
EXPLAIN (ANALYZE ON, COSTS OFF, VERBOSE ON, TIMING OFF, SUMMARY OFF) SELECT document FROM bson_aggregation_pipeline('idx_only_scan_db', '{ "aggregate" : "idx_only_scan_coll", "pipeline" : [{ "$match" : {"country": {"$size": 2}} }, { "$count": "count" }]}');
EXPLAIN (ANALYZE ON, COSTS OFF, VERBOSE ON, TIMING OFF, SUMMARY OFF) SELECT document FROM bson_aggregation_pipeline('idx_only_scan_db', '{ "aggregate" : "idx_only_scan_coll", "pipeline" : [{ "$match" : {"country": {"$elemMatch": {"$eq": "Mexico"}}} }, { "$count": "count" }]}');

-- if we turn the GUC off by it shouldn't use index only scan
set documentdb.enableIndexOnlyScan to off;
EXPLAIN (ANALYZE ON, COSTS OFF, VERBOSE ON, TIMING OFF, SUMMARY OFF) SELECT document FROM bson_aggregation_pipeline('idx_only_scan_db', '{ "aggregate" : "idx_only_scan_coll", "pipeline" : [{ "$match" : {"country": {"$lt": "Mexico"}} }, { "$count": "count" }]}');

set documentdb.enableIndexOnlyScan to on;
set documentdb.forceIndexOnlyScanIfAvailable to on;

-- if we insert a multi-key value, it shouldn't use index only scan
SELECT documentdb_api.insert_one('idx_only_scan_db', 'idx_only_scan_coll', '{"_id": 17, "country": "Mexico", "provider": ["AWS", "GCP"]}');
EXPLAIN (ANALYZE ON, COSTS OFF, VERBOSE ON, TIMING OFF, SUMMARY OFF) SELECT document FROM bson_aggregation_pipeline('idx_only_scan_db', '{ "aggregate" : "idx_only_scan_coll", "pipeline" : [{ "$match" : {"country": {"$eq": "Mexico"}, "provider": {"$eq": ["AWS", "GCP"]}} }, { "$count": "count" }]}');
CALL documentdb_api.drop_indexes('idx_only_scan_db', '{ "dropIndexes": "idx_only_scan_coll", "index": "country_provider_1" }');

-- now insert a truncated term, should not use index only scan
SELECT documentdb_api.insert_one('idx_only_scan_db', 'idx_only_scan_coll', FORMAT('{ "_id": 18, "country": { "key": "%s", "provider": "%s" } }', repeat('a', 10000), repeat('a', 10000))::bson);
EXPLAIN (ANALYZE ON, COSTS OFF, VERBOSE ON, TIMING OFF, SUMMARY OFF) SELECT document FROM bson_aggregation_pipeline('idx_only_scan_db', '{ "aggregate" : "idx_only_scan_coll", "pipeline" : [{ "$match" : {"country": {"$eq": "Mexico"}} }, { "$count": "count" }]}');

-- if we delete it and vacuum it should use index only scan again
SELECT documentdb_api.delete('idx_only_scan_db', '{ "delete": "idx_only_scan_coll", "deletes": [ {"q": {"_id": {"$eq": 18} }, "limit": 0} ]}');

VACUUM (FREEZE ON) documentdb_data.documents_69001;

EXPLAIN (ANALYZE ON, COSTS OFF, VERBOSE ON, TIMING OFF, SUMMARY OFF) SELECT document FROM bson_aggregation_pipeline('idx_only_scan_db', '{ "aggregate" : "idx_only_scan_coll", "pipeline" : [{ "$match" : {"country": {"$eq": "Mexico"}} }, { "$count": "count" }]}');

-- test _id index only scans
EXPLAIN (ANALYZE ON, COSTS OFF, VERBOSE ON, TIMING OFF, SUMMARY OFF) SELECT document FROM bson_aggregation_pipeline('idx_only_scan_db', '{ "aggregate" : "idx_only_scan_coll", "pipeline" : [{ "$match" : {"_id": {"$gt": 5 }} }, { "$count": "count" }]}');
EXPLAIN (ANALYZE ON, COSTS OFF, VERBOSE ON, TIMING OFF, SUMMARY OFF) SELECT document FROM bson_aggregation_pipeline('idx_only_scan_db', '{ "aggregate" : "idx_only_scan_coll", "pipeline" : [{ "$match" : {"_id": {"$gt": 5, "$lt": 8 }} }, { "$count": "count" }]}');

EXPLAIN (ANALYZE ON, COSTS OFF, VERBOSE ON, TIMING OFF, SUMMARY OFF) SELECT document FROM bson_aggregation_pipeline('idx_only_scan_db', '{ "aggregate" : "idx_only_scan_coll", "pipeline" : [{ "$match" : {"_id": {"$gt": 5, "$lt": 8 }, "country": { "$lt": { "$maxKey": 1 }}} }, { "$count": "count" }]}');

-- count with legacy count aggregates
set documentdb.enableNewCountAggregates to off;
EXPLAIN (ANALYZE ON, COSTS OFF, VERBOSE ON, TIMING OFF, SUMMARY OFF) SELECT document FROM bson_aggregation_pipeline('idx_only_scan_db', '{ "aggregate" : "idx_only_scan_coll", "pipeline" : [{ "$match" : {"country": {"$eq": "Mexico"}} }, { "$count": "count" }]}');
EXPLAIN (ANALYZE ON, COSTS OFF, VERBOSE ON, TIMING OFF, SUMMARY OFF) SELECT document FROM bson_aggregation_pipeline('idx_only_scan_db', '{ "aggregate" : "idx_only_scan_coll", "pipeline" : [{ "$match" : {"country": {"$gte": "Brazil"}} }, { "$group" : { "_id" : "1", "n" : { "$sum" : 1 } } }]}');
EXPLAIN (ANALYZE ON, COSTS OFF, VERBOSE ON, TIMING OFF, SUMMARY OFF) SELECT document FROM bson_aggregation_pipeline('idx_only_scan_db', '{ "aggregate" : "idx_only_scan_coll", "pipeline" : [{ "$match" : {"country": {"$gte": "Brazil"}} }, { "$group" : { "_id" : null, "n" : { "$sum" : 1 } } }]}');

set documentdb.enableNewCountAggregates to on;

-- TODO support sharded collections, currently we don't because of the shard_key_value filter

-- SELECT documentdb_api.shard_collection('idx_only_scan_db', 'idx_only_scan_coll', '{ "country": "hashed" }', FALSE);
-- EXPLAIN (COSTS OFF, VERBOSE ON, TIMING OFF, SUMMARY OFF) SELECT document FROM bson_aggregation_pipeline('idx_only_scan_db', '{ "aggregate" : "idx_only_scan_coll", "pipeline" : [{ "$match" : {"country": {"$gte": "Brazil"}} }, { "$group" : { "_id" : "1", "n" : { "$sum" : 1 } } }]}');
-- SELECT document FROM bson_aggregation_pipeline('idx_only_scan_db', '{ "aggregate" : "idx_only_scan_coll", "pipeline" : [{ "$match" : {"country": {"$gte": "Brazil"}} }, { "$group" : { "_id" : "1", "n" : { "$sum" : 1 } } }]}');

-- EXPLAIN (COSTS OFF, VERBOSE ON, TIMING OFF, SUMMARY OFF) SELECT document FROM bson_aggregation_pipeline('idx_only_scan_db', '{ "aggregate" : "idx_only_scan_coll", "pipeline" : [{ "$match" : {"country": {"$in": ["Mexico", "USA"]}} }, { "$group" : { "_id" : "1", "n" : { "$sum" : 1 } } }]}');
-- SELECT document FROM bson_aggregation_pipeline('idx_only_scan_db', '{ "aggregate" : "idx_only_scan_coll", "pipeline" : [{ "$match" : {"country": {"$in": ["Mexico", "USA"]}} }, { "$group" : { "_id" : "1", "n" : { "$sum" : 1 } } }]}');

SELECT COUNT(documentdb_api.insert_one('idx_only_scan_db', 'idx_only_scan_sharded', bson_build_document('_id'::text, i, 'shardKey'::text, i % 10, 'value'::text, i))) FROM generate_series(1, 1000) i;
SELECT documentdb_api.shard_collection('{ "shardCollection": "idx_only_scan_db.idx_only_scan_sharded", "key": { "shardKey": "hashed" } }');

SELECT documentdb_api_internal.create_indexes_non_concurrently('idx_only_scan_db', '{ "createIndexes": "idx_only_scan_sharded", "indexes": [ { "key": { "shardKey": 1 }, "name": "idx1", "enableOrderedIndex": true }]}', TRUE);

VACUUM (ANALYZE ON, FREEZE ON) documentdb_data.documents_69002;

-- should never pick an index only scan (shard key needs to be re-evaluated)
BEGIN;
set local enable_seqscan to off;
set local documentdb.forceIndexOnlyScanIfAvailable to on;
EXPLAIN (ANALYZE ON, COSTS OFF, VERBOSE ON, TIMING OFF, SUMMARY OFF)
    SELECT document FROM bson_aggregation_pipeline('idx_only_scan_db', '{ "aggregate" : "idx_only_scan_sharded", "pipeline" : [{ "$match" : {"shardKey": {"$eq": 5 } } }, { "$count": "c" } ]}');

ROLLBACK;

-- test index only scan is not enabled for wildcard indexes
set documentdb.enableCompositeWildcardIndex to on;
SELECT documentdb_api.insert_one('idx_only_scan_db', 'compwildcard2', '{ "a": { "b": 1, "c": 5 }}');
SELECT documentdb_api.insert_one('idx_only_scan_db', 'compwildcard2', '{ "a": { "b": 1, "c": 6 }}');
SELECT documentdb_api.insert_one('idx_only_scan_db', 'compwildcard2', '{ "a": { "b": 2, "c": 6 }}');
SELECT documentdb_api.insert_one('idx_only_scan_db', 'compwildcard2', '{ "a": { "b": 2, "c": 7 }}');
SELECT documentdb_api.insert_one('idx_only_scan_db', 'compwildcard2', '{ "a": { "b": 3, "c": 7 }}');
SELECT documentdb_api_internal.create_indexes_non_concurrently('idx_only_scan_db', '{ "createIndexes": "compwildcard2", "indexes": [ { "key": { "$**": 1 }, "name": "$**_1", "enableOrderedIndex": true }]}', TRUE);

set documentdb.forceIndexOnlyScanIfAvailable to on;
EXPLAIN (ANALYZE ON, COSTS OFF, VERBOSE ON, TIMING OFF, SUMMARY OFF)
    SELECT document FROM bson_aggregation_count('idx_only_scan_db', '{ "count" : "compwildcard2", "query" : { "a.b": { "$gt": 2 } } }');
