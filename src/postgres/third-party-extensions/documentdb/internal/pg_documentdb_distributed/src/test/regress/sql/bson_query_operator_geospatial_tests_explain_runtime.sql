SET search_path TO documentdb_api_catalog, postgis_public;
SET citus.next_shard_id TO 167500;
SET documentdb.next_collection_id TO 16750;
SET documentdb.next_collection_index_id TO 16750;

SELECT documentdb_api.drop_collection('db', 'geoquerytest') IS NOT NULL;
SELECT documentdb_api.create_collection('db', 'geoquerytest') IS NOT NULL;

-- avoid plans that use the primary key index
SELECT documentdb_distributed_test_helpers.drop_primary_key('db','geoquerytest');

BEGIN;
set local enable_seqscan TO off;
\i sql/bson_query_operator_geospatial_explain_core.sql
COMMIT;

ANALYZE;

BEGIN;
set local enable_seqscan TO off;
-- Testing $center and $centerSphere with infinite radius
EXPLAIN (ANALYZE ON, COSTS OFF, TIMING OFF, SUMMARY OFF, BUFFERS OFF) SELECT document FROM documentdb_api.collection('db', 'geoquerytest') WHERE document @@ '{"a.b": {"$geoWithin": {"$center": [[5, 10], {"$numberDecimal": "Infinity"}]}}}';
EXPLAIN (ANALYZE ON, COSTS OFF, TIMING OFF, SUMMARY OFF, BUFFERS OFF) SELECT document FROM documentdb_api.collection('db', 'geoquerytest') WHERE document @@ '{"geo.loc": {"$geoWithin": {"$centerSphere": [[5, 10], {"$numberDecimal": "Infinity"}]}}}';

-- Testing $center and $centerSphere with non-infinite radius
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