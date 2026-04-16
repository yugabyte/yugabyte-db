SET search_path TO documentdb_api_catalog, postgis_public;
SET citus.next_shard_id TO 167400;
SET documentdb.next_collection_id TO 16740;
SET documentdb.next_collection_index_id TO 16740;

SELECT documentdb_api.drop_collection('db', 'geoquerytest') IS NOT NULL;
SELECT documentdb_api.create_collection('db', 'geoquerytest') IS NOT NULL;

SELECT documentdb_distributed_test_helpers.drop_primary_key('db','geoquerytest');

-- 2d and 2d with pfe
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "geoquerytest", "indexes": [{"key": {"a.b": "2d"}, "name": "my_2d_ab_idx" }]}', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "geoquerytest", "indexes": [{"key": {"d": "2d"}, "name": "my_2d_idx_pfe", "partialFilterExpression" : {"e": { "$gte": 5} } }]}', true);

-- 2dsphere and 2dsphere with pfe
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "geoquerytest", "indexes": [{"key": {"geo.loc": "2dsphere"}, "name": "my_2ds_geoloc_idx" }]}', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "geoquerytest", "indexes": [{"key": {"largeGeo": "2dsphere"}, "name": "my_2ds_largegeo_idx" }]}', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "geoquerytest", "indexes": [{"key": {"d": "2dsphere"}, "name": "my_2ds_idx_pfe", "partialFilterExpression" : {"e": { "$gte": 5} } }]}', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "geoquerytest", "indexes": [{"key": {"geoA": "2dsphere", "geoB": "2dsphere"}, "name": "my_2dsgeo_a_or_b_idx" }]}', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "geoquerytest", "indexes": [{"key": {"geo1": "2dsphere", "geo2": "2dsphere"}, "name": "my_2ds_geo1_2_pfe_idx", "partialFilterExpression": { "region": { "$eq": "USA" } } }]}', true); -- Multi 2dsphere index with pfe

-- Also create a regular index on the same path, to validate that geo operators are never pushed to regular index
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "geoquerytest", "indexes": [{"key": {"a.b": 1}, "name": "my_regular_a_idx" }]}', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "geoquerytest", "indexes": [{"key": {"geo.loc": 1}, "name": "my_regular_geo.loc_idx" }]}', true);

BEGIN;
set local enable_seqscan TO off;
\i sql/bson_query_operator_geospatial_explain_core.sql
COMMIT;

-- few items for $center recheck condition
SELECT documentdb_api.insert_one('db','geoquerytest','{ "_id" : 401, "a" : { "b": [70, 70] } }', NULL);
SELECT documentdb_api.insert_one('db','geoquerytest','{ "_id" : 402, "a" : { "b": [80, 80] } }', NULL);
SELECT documentdb_api.insert_one('db','geoquerytest','{ "_id" : 403, "a" : { "b": [90, 90] } }', NULL);

ANALYZE;

BEGIN;
set local enable_seqscan TO off;
-- Testing $center and $centerSphere with infinite radius - gives no recheck condition
EXPLAIN (ANALYZE ON, COSTS OFF, TIMING OFF, SUMMARY OFF, BUFFERS OFF) SELECT document FROM documentdb_api.collection('db', 'geoquerytest') WHERE document @@ '{"a.b": {"$geoWithin": {"$center": [[5, 10], {"$numberDecimal": "Infinity"}]}}}';
EXPLAIN (ANALYZE ON, COSTS OFF, TIMING OFF, SUMMARY OFF, BUFFERS OFF) SELECT document FROM documentdb_api.collection('db', 'geoquerytest') WHERE document @@ '{"geo.loc": {"$geoWithin": {"$centerSphere": [[5, 10], {"$numberDecimal": "Infinity"}]}}}';

-- Testing $center and $centerSphere with non-infinite radius - gives rows removed by recheck
EXPLAIN (ANALYZE ON, COSTS OFF, TIMING OFF, SUMMARY OFF, BUFFERS OFF) SELECT document FROM documentdb_api.collection('db', 'geoquerytest') WHERE document @@ '{"a.b": {"$geoWithin": {"$center": [[0, 0], 100]}}}';
EXPLAIN (ANALYZE ON, COSTS OFF, TIMING OFF, SUMMARY OFF, BUFFERS OFF) SELECT document FROM documentdb_api.collection('db', 'geoquerytest') WHERE document @@ '{"geo.loc": {"$geoWithin": {"$centerSphere": [[0, 0], 1]}}}';
EXPLAIN (ANALYZE ON, COSTS OFF, TIMING OFF, SUMMARY OFF, BUFFERS OFF) SELECT document FROM documentdb_api.collection('db', 'geoquerytest') WHERE document @@ '{"largeGeo": {"$geoWithin": {"$centerSphere": [[-59.80246852929814, -2.3633072488322853], 2.768403272464979]}}}' ORDER BY object_id; -- no result
EXPLAIN (ANALYZE ON, COSTS OFF, TIMING OFF, SUMMARY OFF, BUFFERS OFF) SELECT document FROM documentdb_api.collection('db', 'geoquerytest') WHERE document @@ '{"largeGeo": {"$geoWithin": {"$centerSphere": [[-61.52266094410311, 17.79937981451866], 2.9592242752161573]}}}' ORDER BY object_id; -- big enough for linestring but not for polygon
EXPLAIN (ANALYZE ON, COSTS OFF, TIMING OFF, SUMMARY OFF, BUFFERS OFF) SELECT document FROM documentdb_api.collection('db', 'geoquerytest') WHERE document @@ '{"largeGeo": {"$geoWithin": {"$centerSphere": [[-61.52266094410311, 17.79937981451866], 3.15]}}}' ORDER BY object_id; -- radius > pi, both docs in result

ROLLBACK;

SELECT documentdb_api.delete('db', '{"delete":"geoquerytest", "deletes":[{"q":{}, "limit": 0}]}');

-- Again testing with shards
-- Shard the collection and run the tests
SELECT documentdb_api.shard_collection('db', 'geoquerytest', '{ "_id": "hashed" }', false);

BEGIN;
set local enable_seqscan TO off;
\i sql/bson_query_operator_geospatial_explain_core.sql
COMMIT;

-- few items for $center recheck condition
SELECT documentdb_api.insert_one('db','geoquerytest','{ "_id" : 401, "a" : { "b": [70, 70] } }', NULL);
SELECT documentdb_api.insert_one('db','geoquerytest','{ "_id" : 402, "a" : { "b": [80, 80] } }', NULL);
SELECT documentdb_api.insert_one('db','geoquerytest','{ "_id" : 403, "a" : { "b": [90, 90] } }', NULL);

ANALYZE;

BEGIN;
set local enable_seqscan TO off;

-- Not running analyze on sharded collections as EXPLAIN returns task from only 1 of the 8 shards so the result can be flaky
-- Testing $center and $centerSphere with infinite radius
EXPLAIN (COSTS OFF) SELECT document FROM documentdb_api.collection('db', 'geoquerytest') WHERE document @@ '{"a.b": {"$geoWithin": {"$center": [[5, 10], {"$numberDecimal": "Infinity"}]}}}';
EXPLAIN (COSTS OFF) SELECT document FROM documentdb_api.collection('db', 'geoquerytest') WHERE document @@ '{"geo.loc": {"$geoWithin": {"$centerSphere": [[5, 10], {"$numberDecimal": "Infinity"}]}}}';

-- Testing $center and $centerSphere with non-infinite radius
EXPLAIN (COSTS OFF) SELECT document FROM documentdb_api.collection('db', 'geoquerytest') WHERE document @@ '{"a.b": {"$geoWithin": {"$center": [[0, 0], 100]}}}';
EXPLAIN (COSTS OFF) SELECT document FROM documentdb_api.collection('db', 'geoquerytest') WHERE document @@ '{"geo.loc": {"$geoWithin": {"$centerSphere": [[0, 0], 1]}}}';
EXPLAIN (COSTS OFF) SELECT document FROM documentdb_api.collection('db', 'geoquerytest') WHERE document @@ '{"largeGeo": {"$geoWithin": {"$centerSphere": [[-59.80246852929814, -2.3633072488322853], 2.768403272464979]}}}' ORDER BY object_id; -- no result
EXPLAIN (COSTS OFF) SELECT document FROM documentdb_api.collection('db', 'geoquerytest') WHERE document @@ '{"largeGeo": {"$geoWithin": {"$centerSphere": [[-61.52266094410311, 17.79937981451866], 2.9592242752161573]}}}' ORDER BY object_id; -- big enough for linestring but not for polygon
EXPLAIN (COSTS OFF) SELECT document FROM documentdb_api.collection('db', 'geoquerytest') WHERE document @@ '{"largeGeo": {"$geoWithin": {"$centerSphere": [[-61.52266094410311, 17.79937981451866], 3.15]}}}' ORDER BY object_id; -- radius > pi, both docs in result
ROLLBACK;

SELECT documentdb_api.drop_collection('db', 'geoquerytest') IS NOT NULL;