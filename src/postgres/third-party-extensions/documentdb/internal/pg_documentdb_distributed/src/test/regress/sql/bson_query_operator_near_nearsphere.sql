SET citus.next_shard_id TO 523000;
SET documentdb.next_collection_id TO 5230;
SET documentdb.next_collection_index_id TO 5230;

SET search_path to documentdb_api_catalog;

SELECT documentdb_api.insert_one('db','near_sphere','{ "_id": 1, "a": { "b": [ 0, 0]} }', NULL);
SELECT documentdb_api.insert_one('db','near_sphere','{ "_id": 2, "a": { "b": [ 1.1, 1.1]} }', NULL);
SELECT documentdb_api.insert_one('db','near_sphere','{ "_id": 3, "a": { "b": [ 2.29, 2.29]} }', NULL);
SELECT documentdb_api.insert_one('db','near_sphere','{ "_id": 4, "a": { "b": [ 3.31, 3.31]} }', NULL);
SELECT documentdb_api.insert_one('db','near_sphere','{ "_id": 5, "a": { "b": [ 4.42, 4.42]} }', NULL);
SELECT documentdb_api.insert_one('db','near_sphere','{ "_id": 6, "a": { "b": [ 5.5, 5.5]} }', NULL);
SELECT documentdb_api.insert_one('db','near_sphere','{ "_id": 7, "a": { "b": [ 6.66, 6.66]} }', NULL);
SELECT documentdb_api.insert_one('db','near_sphere','{ "_id": 8, "a": { "b": [ 7.74, 7.74]} }', NULL);
SELECT documentdb_api.insert_one('db','near_sphere','{ "_id": 9, "a": { "b": [ 8.81, 8.81]} }', NULL);
SELECT documentdb_api.insert_one('db','near_sphere','{ "_id": 10, "a": { "geo": {"type": "Point", "coordinates": [35.3, 35.4]}} }', NULL);
SELECT documentdb_api.insert_one('db','near_sphere','{ "_id": 11, "a": { "geo": {"type": "LineString", "coordinates": [[35.36, 35.42], [32.3, 30]]}} }', NULL);
SELECT documentdb_api.insert_one('db','near_sphere','{ "_id": 12, "a": { "geo": {"type": "Polygon", "coordinates": [[[35.73, 35.74], [38.6, 35.3], [38.7, 39.2], [35.73, 35.74]]]}} }', NULL);
SELECT documentdb_api.insert_one('db','near_sphere','{ "_id": 13, "a": { "geo": {"type": "MultiPoint", "coordinates": [[35.43, 35.44], [32.3, 30.3]]}} }', NULL);
SELECT documentdb_api.insert_one('db','near_sphere','{ "_id": 14, "a": { "geo": {"type": "MultiLineString", "coordinates": [[[35.83, 35.84], [32.3, 30.6]]]}} }', NULL);
SELECT documentdb_api.insert_one('db','near_sphere','{ "_id": 15, "a": { "geo": {"type": "MultiPolygon", "coordinates": [[[[35.312, 35.441], [38.644, 35.3231], [38.71, 39.32], [35.312, 35.441]]]]}} }', NULL);

-- Test near and nearsphere with index and runtime flags

-- Should fail
SELECT document FROM bson_aggregation_find('db', '{ "find": "near_sphere", "filter": { "a.b": { "$near": [0, 0]}}}');
SELECT document FROM bson_aggregation_find('db', '{ "find": "near_sphere", "filter": { "a.b": { "$nearSphere": [0, 0]}}}');

BEGIN;
-- Should pass
SET LOCAL documentdb.enable_force_push_geonear_index to off;
SELECT document FROM bson_aggregation_find('db', '{ "find": "near_sphere", "filter": { "a.b": { "$near": [0, 0]}}}');
SELECT document FROM bson_aggregation_find('db', '{ "find": "near_sphere", "filter": { "a.b": { "$nearSphere": [0, 0]}}}');
ROLLBACK;

-- $near and $nearSphere enforce index usage
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "near_sphere", "indexes": [{"key": {"a.b": "2d"}, "name": "my_2d_ab_idx" }, {"key": {"a.b": "2dsphere"}, "name": "my_2ds_ab_idx" }, {"key": {"a.geo": "2dsphere"}, "name": "my_2ds_ageo_idx" }]}', true);
SELECT documentdb_distributed_test_helpers.drop_primary_key('db','near_sphere');
SELECT * FROM documentdb_distributed_test_helpers.get_collection_indexes('db', 'near_sphere') ORDER BY collection_id, index_id;

-- validations
-- $near/$nearSphere tests
SELECT document FROM bson_aggregation_find('db', '{ "find": "near_sphere", "filter": { "a.b": { "$near": [0] } }}');
SELECT document FROM bson_aggregation_find('db', '{ "find": "near_sphere", "filter": { "a.b": { "$near": {"$geometry": {"type": "Point", "coordinates": [0]}}}}}');
SELECT document FROM bson_aggregation_find('db', '{ "find": "near_sphere", "filter": { "a.b": { "$near": {"$geometry": {"type": "Point", "coordinates": [0, 0]}, "$hello": 1}}}}');
SELECT document FROM bson_aggregation_find('db', '{ "find": "near_sphere", "filter": { "a.b": { "$near": {"$geometry": {"type": "Point"}}}}}');
SELECT document FROM bson_aggregation_find('db', '{ "find": "near_sphere", "filter": { "a.b": { "$near": {"$geometry": {"tide": "Point", "coordintes": [1,1]}}}}}');
SELECT document FROM bson_aggregation_find('db', '{ "find": "near_sphere", "filter": { "a.b": { "$near": {}}}}');
SELECT document FROM bson_aggregation_find('db', '{ "find": "near_sphere", "filter": { "a.b": { "$near": []}}}');
SELECT document FROM bson_aggregation_find('db', '{ "find": "near_sphere", "filter": { "a.b": { "$near": {"tide": "Point", "coordintes": [1,1]}}}}');
SELECT document FROM bson_aggregation_find('db', '{ "find": "near_sphere", "filter": { "a.b": { "$near": {"x": 1, "coordinates": [1,1]}}}}');
SELECT document FROM bson_aggregation_find('db', '{ "find": "near_sphere", "filter": { "a.b": { "$near": {"x": "a", "y": 1}}}}');

SELECT document FROM bson_aggregation_find('db', '{ "find": "near_sphere", "filter": { "a.b": { "$nearSphere": [0] } }}');
SELECT document FROM bson_aggregation_find('db', '{ "find": "near_sphere", "filter": { "a.b": { "$nearSphere": {"$geometry": {"type": "Point", "coordinates": [0]}}}}}');
SELECT document FROM bson_aggregation_find('db', '{ "find": "near_sphere", "filter": { "a.b": { "$nearSphere": {"$geometry": {"type": "Point", "coordinates": [0, 0]}, "$hello": 1}}}}');
SELECT document FROM bson_aggregation_find('db', '{ "find": "near_sphere", "filter": { "a.b": { "$nearSphere": {"$geometry": {"type": "Point"}}}}}');
SELECT document FROM bson_aggregation_find('db', '{ "find": "near_sphere", "filter": { "a.b": { "$nearSphere": {"$geometry": {"tide": "Point", "coordintes": [1,1]}}}}}');
SELECT document FROM bson_aggregation_find('db', '{ "find": "near_sphere", "filter": { "a.b": { "$nearSphere": {}}}}');
SELECT document FROM bson_aggregation_find('db', '{ "find": "near_sphere", "filter": { "a.b": { "$nearSphere": []}}}');
SELECT document FROM bson_aggregation_find('db', '{ "find": "near_sphere", "filter": { "a.b": { "$nearSphere": {"tide": "Point", "coordintes": [1,1]}}}}');
SELECT document FROM bson_aggregation_find('db', '{ "find": "near_sphere", "filter": { "a.b": { "$nearSphere": {"x": 1, "coordinates": [1,1]}}}}');
SELECT document FROM bson_aggregation_find('db', '{ "find": "near_sphere", "filter": { "a.b": { "$nearSphere": {"x": "a", "y": 1}}}}');

SELECT document FROM bson_aggregation_find('db', '{ "find": "near_sphere", "filter": { "a.b": { "$near": {"coordinates": [1,1]}}, "b": { "$nearSphere": {"coordinates": [1,1]}}}}');
SELECT document FROM bson_aggregation_find('db', '{ "find": "near_sphere", "filter": { "a.b": { "$nearSphere": {"coordinates": [1,1]}}, "b": { "$nearSphere": {"coordinates": [1,1]}}}}');
SELECT document FROM bson_aggregation_find('db', '{ "find": "near_sphere", "filter": { "a.b": { "$near": {"coordinates": [1,1]}}, "b": { "$near": {"coordinates": [1,1]}}}}');

SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "near_sphere", "pipeline": [ { "$sort": { "a.b": 1 }}, { "$match": { "a.b": { "$near": {"coordinates": [1,1]}}}} ], "cursor": {} }');

-- $near/$nearSphere with invalid distance argument
SELECT document FROM bson_aggregation_find('db', '{ "find": "near_sphere", "filter": { "a.b": { "$near": [1, 1], "$minDistance": -1}}}');
SELECT document FROM bson_aggregation_find('db', '{ "find": "near_sphere", "filter": { "a.b": { "$near": [1, 1], "$minDistance": {"$numberDouble": "NaN"}}}}');
SELECT document FROM bson_aggregation_find('db', '{ "find": "near_sphere", "filter": { "a.b": { "$near": {"$geometry": {"coordinates": [1, 1]}, "$minDistance": {"$numberDouble": "Infinity"}}}}}');
SELECT document FROM bson_aggregation_find('db', '{ "find": "near_sphere", "filter": { "a.b": { "$near": {"$geometry": {"coordinates": [1, 1]}, "$maxDistance": {"$numberDouble": "Infinity"}}}}}');
SELECT document FROM bson_aggregation_find('db', '{ "find": "near_sphere", "filter": { "a.b": { "$near": [1, 1], "$maxDistance": {"$numberDouble": "-Infinity"}}}}');
SELECT document FROM bson_aggregation_find('db', '{ "find": "near_sphere", "filter": { "a.b": { "$near": [1, 1], "$maxDistance": {"$numberDouble": "-NaN"}}}}');

SELECT document FROM bson_aggregation_find('db', '{ "find": "near_sphere", "filter": { "a.b": { "$nearSphere": [1, 1], "$minDistance": -1}}}');
SELECT document FROM bson_aggregation_find('db', '{ "find": "near_sphere", "filter": { "a.b": { "$nearSphere": [1, 1], "$minDistance": {"$numberDouble": "NaN"}}}}');
SELECT document FROM bson_aggregation_find('db', '{ "find": "near_sphere", "filter": { "a.b": { "$nearSphere": {"$geometry": {"coordinates": [1, 1]}, "$minDistance": {"$numberDouble": "Infinity"}}}}}');
SELECT document FROM bson_aggregation_find('db', '{ "find": "near_sphere", "filter": { "a.b": { "$nearSphere": {"$geometry": {"coordinates": [1, 1]}, "$maxDistance": {"$numberDouble": "Infinity"}}}}}');
SELECT document FROM bson_aggregation_find('db', '{ "find": "near_sphere", "filter": { "a.b": { "$nearSphere": [1, 1], "$maxDistance": {"$numberDouble": "-Infinity"}}}}');
SELECT document FROM bson_aggregation_find('db', '{ "find": "near_sphere", "filter": { "a.b": { "$nearSphere": [1, 1], "$maxDistance": {"$numberDouble": "-NaN"}}}}');

-- invalid places to call $near/$nearSphere

SELECT document FROM documentdb_api.collection('db','near_sphere') WHERE document @@ '{ "a.b": { "$elemMatch" : {"$near": [1,1] }}}';
SELECT document FROM documentdb_api.collection('db','near_sphere') WHERE document @@ '{ "$or": [{"a.b": {"$near": [1,1] }},{"t": 1}]}';
SELECT document FROM bson_aggregation_find('db', '{ "find": "near_sphere", "filter": {}, "projection": { "p" : {"$elemMatch": {"geo": {"$near": [1,1]}}}}}');
SELECT documentdb_api.update('db', '{"update":"near_sphere", "updates":[{"q":{},"u":{"$set": {"a.$[elem].b": 1}},"multi":false,"upsert":true, "arrayFilters": [{"elem.geo": {"$near": [1, 1]}}]}]}');
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "near_sphere", "indexes": [{"key": {"t": 1}, "name": "my_invalid_idx", "partialFilterExpression": {"geo": {"$near": [1,1]}} }]}', true);


BEGIN;
-- $near operator

-- Test with legacy coordinate pair
SELECT document FROM bson_aggregation_find('db', '{ "find": "near_sphere", "filter": { "a.b": { "$near": [0, 0]}}}');
SELECT document FROM bson_aggregation_find('db', '{ "find": "near_sphere", "filter": { "a.b": { "$near": [0, 0], "$minDistance": 6}}}');
SELECT document FROM bson_aggregation_find('db', '{ "find": "near_sphere", "filter": { "a.b": { "$near": [0, 0], "$maxDistance": 6}}}');
SELECT document FROM bson_aggregation_find('db', '{ "find": "near_sphere", "filter": { "a.b": { "$near": [0, 0], "$minDistance": 5, "$maxDistance": 8}}}');
SELECT document FROM bson_aggregation_find('db', '{ "find": "near_sphere", "filter": { "a.b": { "$near": [0, 0, 6]}}}');
SELECT document FROM bson_aggregation_find('db', '{ "find": "near_sphere", "filter": { "a.b": { "$near": {"x": 0, "y": 0}}}}');
SELECT document FROM bson_aggregation_find('db', '{ "find": "near_sphere", "filter": { "a.b": { "$near": {"x": 0, "y": 0, "z": 6}}}}');


-- Test with GeoJson point
SELECT document FROM bson_aggregation_find('db', '{ "find": "near_sphere", "filter": { "a.b": { "$near": {"type": "Point", "coordinates": [0, 0]}}}}');
SELECT document FROM bson_aggregation_find('db', '{ "find": "near_sphere", "filter": { "a.b": { "$near": {"type": "Point", "coordinates": [0, 0]}, "$minDistance": 700000}}}');
SELECT document FROM bson_aggregation_find('db', '{ "find": "near_sphere", "filter": { "a.b": { "$near": {"type": "Point", "coordinates": [0, 0]}, "$maxDistance": 1000000}}}');
SELECT document FROM bson_aggregation_find('db', '{ "find": "near_sphere", "filter": { "a.b": { "$near": {"type": "Point", "coordinates": [0, 0]}, "$minDistance": 500000, "$maxDistance": 1500000}}}');

-- Test with GeoJson point with $geometry
SELECT document FROM bson_aggregation_find('db', '{ "find": "near_sphere", "filter": { "a.b": { "$near": {"$geometry": {"type": "Point", "coordinates": [0, 0]}}}}}');
SELECT document FROM bson_aggregation_find('db', '{ "find": "near_sphere", "filter": { "a.b": { "$near": {"$geometry": {"type": "Point", "coordinates": [0, 0]}, "$minDistance": 700000}}}}');
SELECT document FROM bson_aggregation_find('db', '{ "find": "near_sphere", "filter": { "a.b": { "$near": {"$geometry": {"type": "Point", "coordinates": [0, 0]}, "$maxDistance": 1000000}}}}');
SELECT document FROM bson_aggregation_find('db', '{ "find": "near_sphere", "filter": { "a.b": { "$near": {"$geometry": {"type": "Point", "coordinates": [0, 0]}, "$minDistance": 500000, "$maxDistance": 1500000}}}}');

-- Test order of documents
SELECT document FROM bson_aggregation_find('db', '{ "find": "near_sphere", "filter": { "a.b": { "$near": [5, 6]}}}');
SELECT document FROM bson_aggregation_find('db', '{ "find": "near_sphere", "filter": { "a.b": { "$near": {"coordinates": [5, 6]}}}}');

-- $nearSphere operator

-- Test with legacy coordinate pair
SELECT document FROM bson_aggregation_find('db', '{ "find": "near_sphere", "filter": { "a.b": { "$nearSphere": [0, 0]}}}');
SELECT document FROM bson_aggregation_find('db', '{ "find": "near_sphere", "filter": { "a.b": { "$nearSphere": [0, 0], "$minDistance": 0.1}}}');
SELECT document FROM bson_aggregation_find('db', '{ "find": "near_sphere", "filter": { "a.b": { "$nearSphere": [0, 0], "$maxDistance": 0.15}}}');
SELECT document FROM bson_aggregation_find('db', '{ "find": "near_sphere", "filter": { "a.b": { "$nearSphere": [0, 0], "$minDistance": 0.16, "$maxDistance": 0.2}}}');
SELECT document FROM bson_aggregation_find('db', '{ "find": "near_sphere", "filter": { "a.b": { "$nearSphere": [0, 0, 0.15]}}}');
SELECT document FROM bson_aggregation_find('db', '{ "find": "near_sphere", "filter": { "a.b": { "$nearSphere": {"x": 0, "y": 0}}}}');
SELECT document FROM bson_aggregation_find('db', '{ "find": "near_sphere", "filter": { "a.b": { "$nearSphere": {"x": 0, "y": 0, "z": 0.15}}}}');

-- Test with GeoJson point
SELECT document FROM bson_aggregation_find('db', '{ "find": "near_sphere", "filter": { "a.b": { "$nearSphere": {"type": "Point", "coordinates": [0, 0]}}}}');
SELECT document FROM bson_aggregation_find('db', '{ "find": "near_sphere", "filter": { "a.b": { "$nearSphere": {"type": "Point", "coordinates": [0, 0]}, "$minDistance": 700000}}}');
SELECT document FROM bson_aggregation_find('db', '{ "find": "near_sphere", "filter": { "a.b": { "$nearSphere": {"type": "Point", "coordinates": [0, 0]}, "$maxDistance": 1000000}}}');
SELECT document FROM bson_aggregation_find('db', '{ "find": "near_sphere", "filter": { "a.b": { "$nearSphere": {"type": "Point", "coordinates": [0, 0]}, "$minDistance": 500000, "$maxDistance": 1500000}}}');

-- Test with GeoJson point with $geometry
SELECT document FROM bson_aggregation_find('db', '{ "find": "near_sphere", "filter": { "a.b": { "$nearSphere": {"$geometry": {"type": "Point", "coordinates": [0, 0]}}}}}');
SELECT document FROM bson_aggregation_find('db', '{ "find": "near_sphere", "filter": { "a.b": { "$nearSphere": {"$geometry": {"type": "Point", "coordinates": [0, 0]}, "$minDistance": 700000}}}}');
SELECT document FROM bson_aggregation_find('db', '{ "find": "near_sphere", "filter": { "a.b": { "$nearSphere": {"$geometry": {"type": "Point", "coordinates": [0, 0]}, "$maxDistance": 1000000}}}}');
SELECT document FROM bson_aggregation_find('db', '{ "find": "near_sphere", "filter": { "a.b": { "$nearSphere": {"$geometry": {"type": "Point", "coordinates": [0, 0]}, "$minDistance": 500000, "$maxDistance": 1500000}}}}');

-- Test order of documents
SELECT document FROM bson_aggregation_find('db', '{ "find": "near_sphere", "filter": { "a.b": { "$nearSphere": [5, 6]}}}');
SELECT document FROM bson_aggregation_find('db', '{ "find": "near_sphere", "filter": { "a.b": { "$nearSphere": {"coordinates": [5, 6]}}}}');

-- Test with GeoJson objects in documents

-- Test with GeoJson point
SELECT document FROM bson_aggregation_find('db', '{ "find": "near_sphere", "filter": { "a.geo": { "$nearSphere": {"type": "Point", "coordinates": [0, 0]}}}}');
SELECT document FROM bson_aggregation_find('db', '{ "find": "near_sphere", "filter": { "a.geo": { "$nearSphere": {"type": "Point", "coordinates": [0, 0]}, "$minDistance": 5000000}}}');
SELECT document FROM bson_aggregation_find('db', '{ "find": "near_sphere", "filter": { "a.geo": { "$nearSphere": {"type": "Point", "coordinates": [0, 0]}, "$maxDistance": 5000000}}}');
SELECT document FROM bson_aggregation_find('db', '{ "find": "near_sphere", "filter": { "a.geo": { "$nearSphere": {"type": "Point", "coordinates": [0, 0]}, "$minDistance": 4500000, "$maxDistance": 5000000}}}');

-- Test with GeoJson point with $geometry
SELECT document FROM bson_aggregation_find('db', '{ "find": "near_sphere", "filter": { "a.geo": { "$nearSphere": {"$geometry": {"type": "Point", "coordinates": [0, 0]}}}}}');
SELECT document FROM bson_aggregation_find('db', '{ "find": "near_sphere", "filter": { "a.geo": { "$nearSphere": {"$geometry": {"type": "Point", "coordinates": [0, 0]}, "$minDistance": 5000000}}}}');
SELECT document FROM bson_aggregation_find('db', '{ "find": "near_sphere", "filter": { "a.geo": { "$nearSphere": {"$geometry": {"type": "Point", "coordinates": [0, 0]}, "$maxDistance": 5000000}}}}');
SELECT document FROM bson_aggregation_find('db', '{ "find": "near_sphere", "filter": { "a.geo": { "$nearSphere": {"$geometry": {"type": "Point", "coordinates": [0, 0]}, "$minDistance": 4500000, "$maxDistance": 5000000}}}}');

-- Test order of documents
SELECT document FROM bson_aggregation_find('db', '{ "find": "near_sphere", "filter": { "a.geo": { "$nearSphere": { "type": "Point", "coordinates": [5, 6] }}}}');
SELECT document FROM bson_aggregation_find('db', '{ "find": "near_sphere", "filter": { "a.geo": { "$nearSphere": {"coordinates": [5, 6]}}}}');

-- Verify that @@ operator handles the near / nearSphere operators
SELECT document FROM documentdb_api.collection('db', 'near_sphere') WHERE document @@ '{ "a.b": { "$near": [5, 6]}}';
SELECT document FROM documentdb_api.collection('db', 'near_sphere') WHERE document @@ '{ "a.b": { "$near": [5, 6]}}';
SELECT document FROM documentdb_api.collection('db', 'near_sphere') WHERE document @@ '{ "a.geo": { "$nearSphere": [5, 6]}}';
SELECT document FROM documentdb_api.collection('db', 'near_sphere') WHERE document @@ '{ "a.geo": { "$nearSphere": {"coordinates": [5, 6]}}}';

-- Explain tests to ensure index pushdown
EXPLAIN (VERBOSE ON, COSTS OFF) SELECT document FROM bson_aggregation_find('db', '{ "find": "near_sphere", "filter": { "a.b": { "$near": [0, 0]}}}');
EXPLAIN (VERBOSE ON, COSTS OFF) SELECT document FROM bson_aggregation_find('db', '{ "find": "near_sphere", "filter": { "a.b": { "$near": [0, 0], "$minDistance": 5, "$maxDistance": 8}}}');
EXPLAIN (VERBOSE ON, COSTS OFF) SELECT document FROM bson_aggregation_find('db', '{ "find": "near_sphere", "filter": { "a.b": { "$near": {"type": "Point", "coordinates": [0, 0]}}}}');
EXPLAIN (VERBOSE ON, COSTS OFF) SELECT document FROM bson_aggregation_find('db', '{ "find": "near_sphere", "filter": { "a.b": { "$near": {"type": "Point", "coordinates": [0, 0]}, "$minDistance": 500000, "$maxDistance": 1500000}}}');
EXPLAIN (VERBOSE ON, COSTS OFF) SELECT document FROM bson_aggregation_find('db', '{ "find": "near_sphere", "filter": { "a.b": { "$nearSphere": [0, 0]}}}');
EXPLAIN (VERBOSE ON, COSTS OFF) SELECT document FROM bson_aggregation_find('db', '{ "find": "near_sphere", "filter": { "a.b": { "$nearSphere": [0, 0], "$minDistance": 0.8, "$maxDistance": 1}}}');
EXPLAIN (VERBOSE ON, COSTS OFF) SELECT document FROM bson_aggregation_find('db', '{ "find": "near_sphere", "filter": { "a.geo": { "$nearSphere": {"type": "Point", "coordinates": [0, 0]}}}}');
EXPLAIN (VERBOSE ON, COSTS OFF) SELECT document FROM bson_aggregation_find('db', '{ "find": "near_sphere", "filter": { "a.b": { "$nearSphere": {"$geometry": {"type": "Point", "coordinates": [0, 0]}, "$minDistance": 500000, "$maxDistance": 1500000}}}}');


ROLLBACK;

SELECT document FROM bson_aggregation_find('db', '{ "find": "near_sphere", "filter": { "a.geo": { "$nearSphere": [0, 0]}}}');


-- check update
BEGIN;
SELECT documentdb_api.update('db', '{"update":"near_sphere", "updates":[{"q":{"a.b": {"$near": [1,1]}} ,"u":{"$set": {"t": 1}},"multi":false,"upsert":false}]}');
SELECT documentdb_api.update('db', '{"update":"near_sphere", "updates":[{"q":{"a.b": {"$near": [1.1,1.1]}} ,"u":{"$set": {"t": 1}},"multi":false,"upsert":false}]}');
SELECT documentdb_api.update('db', '{"update":"near_sphere", "updates":[{"q":{"a.b": {"$near": [1,1]}} ,"u":{"$set": {"t": 1}},"multi":true,"upsert":false}]}');
ROLLBACK;

-- Shard the collection
SELECT documentdb_api.shard_collection('db', 'near_sphere', '{"_id": "hashed"}', false);


BEGIN;
SET LOCAL enable_seqscan to off;
set local citus.enable_local_execution TO OFF;
SET LOCAL seq_page_cost TO 9999999;

-- $near operator

-- Test with legacy coordinate pair
SELECT document FROM bson_aggregation_find('db', '{ "find": "near_sphere", "filter": { "a.b": { "$near": [0, 0]}}}');
SELECT document FROM bson_aggregation_find('db', '{ "find": "near_sphere", "filter": { "a.b": { "$near": [0, 0], "$minDistance": 6}}}');
SELECT document FROM bson_aggregation_find('db', '{ "find": "near_sphere", "filter": { "a.b": { "$near": [0, 0], "$maxDistance": 6}}}');
SELECT document FROM bson_aggregation_find('db', '{ "find": "near_sphere", "filter": { "a.b": { "$near": [0, 0], "$minDistance": 5, "$maxDistance": 8}}}');
SELECT document FROM bson_aggregation_find('db', '{ "find": "near_sphere", "filter": { "a.b": { "$near": {"x": 0, "y": 0}}}}');
SELECT document FROM bson_aggregation_find('db', '{ "find": "near_sphere", "filter": { "a.b": { "$near": {"x": 0, "y": 0, "z": 6}}}}');

-- Test with GeoJson point
SELECT document FROM bson_aggregation_find('db', '{ "find": "near_sphere", "filter": { "a.b": { "$near": {"type": "Point", "coordinates": [0, 0]}}}}');
SELECT document FROM bson_aggregation_find('db', '{ "find": "near_sphere", "filter": { "a.b": { "$near": {"type": "Point", "coordinates": [0, 0]}, "$minDistance": 700000}}}');
SELECT document FROM bson_aggregation_find('db', '{ "find": "near_sphere", "filter": { "a.b": { "$near": {"type": "Point", "coordinates": [0, 0]}, "$maxDistance": 1000000}}}');
SELECT document FROM bson_aggregation_find('db', '{ "find": "near_sphere", "filter": { "a.b": { "$near": {"type": "Point", "coordinates": [0, 0]}, "$minDistance": 500000, "$maxDistance": 1500000}}}');

-- Test with GeoJson point with $geometry
SELECT document FROM bson_aggregation_find('db', '{ "find": "near_sphere", "filter": { "a.b": { "$near": {"$geometry": {"type": "Point", "coordinates": [0, 0]}}}}}');
SELECT document FROM bson_aggregation_find('db', '{ "find": "near_sphere", "filter": { "a.b": { "$near": {"$geometry": {"type": "Point", "coordinates": [0, 0]}, "$minDistance": 700000}}}}');
SELECT document FROM bson_aggregation_find('db', '{ "find": "near_sphere", "filter": { "a.b": { "$near": {"$geometry": {"type": "Point", "coordinates": [0, 0]}, "$maxDistance": 1000000}}}}');
SELECT document FROM bson_aggregation_find('db', '{ "find": "near_sphere", "filter": { "a.b": { "$near": {"$geometry": {"type": "Point", "coordinates": [0, 0]}, "$minDistance": 500000, "$maxDistance": 1500000}}}}');

-- Test order of documents
SELECT document FROM bson_aggregation_find('db', '{ "find": "near_sphere", "filter": { "a.b": { "$near": [5, 6]}}}');
SELECT document FROM bson_aggregation_find('db', '{ "find": "near_sphere", "filter": { "a.b": { "$near": {"coordinates": [5, 6]}}}}');

-- $nearSphere operator

-- Test with legacy coordinate pair
SELECT document FROM bson_aggregation_find('db', '{ "find": "near_sphere", "filter": { "a.b": { "$nearSphere": [0, 0]}}}');
SELECT document FROM bson_aggregation_find('db', '{ "find": "near_sphere", "filter": { "a.b": { "$nearSphere": [0, 0], "$minDistance": 0.1}}}');
SELECT document FROM bson_aggregation_find('db', '{ "find": "near_sphere", "filter": { "a.b": { "$nearSphere": [0, 0], "$maxDistance": 0.15}}}');
SELECT document FROM bson_aggregation_find('db', '{ "find": "near_sphere", "filter": { "a.b": { "$nearSphere": [0, 0], "$minDistance": 0.16, "$maxDistance": 0.2}}}');
SELECT document FROM bson_aggregation_find('db', '{ "find": "near_sphere", "filter": { "a.b": { "$nearSphere": {"x": 0, "y": 0}}}}');
SELECT document FROM bson_aggregation_find('db', '{ "find": "near_sphere", "filter": { "a.b": { "$nearSphere": {"x": 0, "y": 0, "z": 0.15}}}}');


-- Test with GeoJson point
SELECT document FROM bson_aggregation_find('db', '{ "find": "near_sphere", "filter": { "a.b": { "$nearSphere": {"type": "Point", "coordinates": [0, 0]}}}}');
SELECT document FROM bson_aggregation_find('db', '{ "find": "near_sphere", "filter": { "a.b": { "$nearSphere": {"type": "Point", "coordinates": [0, 0]}, "$minDistance": 700000}}}');
SELECT document FROM bson_aggregation_find('db', '{ "find": "near_sphere", "filter": { "a.b": { "$nearSphere": {"type": "Point", "coordinates": [0, 0]}, "$maxDistance": 1000000}}}');
SELECT document FROM bson_aggregation_find('db', '{ "find": "near_sphere", "filter": { "a.b": { "$nearSphere": {"type": "Point", "coordinates": [0, 0]}, "$minDistance": 500000, "$maxDistance": 1500000}}}');

-- Test with GeoJson point with $geometry
SELECT document FROM bson_aggregation_find('db', '{ "find": "near_sphere", "filter": { "a.b": { "$nearSphere": {"$geometry": {"type": "Point", "coordinates": [0, 0]}}}}}');
SELECT document FROM bson_aggregation_find('db', '{ "find": "near_sphere", "filter": { "a.b": { "$nearSphere": {"$geometry": {"type": "Point", "coordinates": [0, 0]}, "$minDistance": 700000}}}}');
SELECT document FROM bson_aggregation_find('db', '{ "find": "near_sphere", "filter": { "a.b": { "$nearSphere": {"$geometry": {"type": "Point", "coordinates": [0, 0]}, "$maxDistance": 1000000}}}}');
SELECT document FROM bson_aggregation_find('db', '{ "find": "near_sphere", "filter": { "a.b": { "$nearSphere": {"$geometry": {"type": "Point", "coordinates": [0, 0]}, "$minDistance": 500000, "$maxDistance": 1500000}}}}');

-- Test order of documents
SELECT document FROM bson_aggregation_find('db', '{ "find": "near_sphere", "filter": { "a.b": { "$nearSphere": [5, 6]}}}');
SELECT document FROM bson_aggregation_find('db', '{ "find": "near_sphere", "filter": { "a.b": { "$nearSphere": {"coordinates": [5, 6]}}}}');

-- Test with GeoJson objects in documents

-- Test with legacy coordinate pair
SELECT document FROM bson_aggregation_find('db', '{ "find": "near_sphere", "filter": { "a.geo": { "$nearSphere": [0, 0]}}}');
SELECT document FROM bson_aggregation_find('db', '{ "find": "near_sphere", "filter": { "a.geo": { "$nearSphere": [0, 0], "$minDistance": 0.8}}}');
SELECT document FROM bson_aggregation_find('db', '{ "find": "near_sphere", "filter": { "a.geo": { "$nearSphere": [0, 0], "$maxDistance": 0.8}}}');
SELECT document FROM bson_aggregation_find('db', '{ "find": "near_sphere", "filter": { "a.geo": { "$nearSphere": [0, 0], "$minDistance": 0.8, "$maxDistance": 1}}}');

-- Test with GeoJson point
SELECT document FROM bson_aggregation_find('db', '{ "find": "near_sphere", "filter": { "a.geo": { "$nearSphere": {"type": "Point", "coordinates": [0, 0]}}}}');
SELECT document FROM bson_aggregation_find('db', '{ "find": "near_sphere", "filter": { "a.geo": { "$nearSphere": {"type": "Point", "coordinates": [0, 0]}, "$minDistance": 5000000}}}');
SELECT document FROM bson_aggregation_find('db', '{ "find": "near_sphere", "filter": { "a.geo": { "$nearSphere": {"type": "Point", "coordinates": [0, 0]}, "$maxDistance": 5000000}}}');
SELECT document FROM bson_aggregation_find('db', '{ "find": "near_sphere", "filter": { "a.geo": { "$nearSphere": {"type": "Point", "coordinates": [0, 0]}, "$minDistance": 4500000, "$maxDistance": 5000000}}}');

-- Test with GeoJson point with $geometry
SELECT document FROM bson_aggregation_find('db', '{ "find": "near_sphere", "filter": { "a.geo": { "$nearSphere": {"$geometry": {"type": "Point", "coordinates": [0, 0]}}}}}');
SELECT document FROM bson_aggregation_find('db', '{ "find": "near_sphere", "filter": { "a.geo": { "$nearSphere": {"$geometry": {"type": "Point", "coordinates": [0, 0]}, "$minDistance": 5000000}}}}');
SELECT document FROM bson_aggregation_find('db', '{ "find": "near_sphere", "filter": { "a.geo": { "$nearSphere": {"$geometry": {"type": "Point", "coordinates": [0, 0]}, "$maxDistance": 5000000}}}}');
SELECT document FROM bson_aggregation_find('db', '{ "find": "near_sphere", "filter": { "a.geo": { "$nearSphere": {"$geometry": {"type": "Point", "coordinates": [0, 0]}, "$minDistance": 4500000, "$maxDistance": 5000000}}}}');

-- Test order of documents
SELECT document FROM bson_aggregation_find('db', '{ "find": "near_sphere", "filter": { "a.geo": { "$nearSphere": [5, 6]}}}');
SELECT document FROM bson_aggregation_find('db', '{ "find": "near_sphere", "filter": { "a.geo": { "$nearSphere": {"coordinates": [5, 6]}}}}');

-- Explain tests to ensure index pushdown
EXPLAIN (VERBOSE ON, COSTS OFF) SELECT document FROM bson_aggregation_find('db', '{ "find": "near_sphere", "filter": { "a.b": { "$near": [0, 0]}}}');
EXPLAIN (VERBOSE ON, COSTS OFF) SELECT document FROM bson_aggregation_find('db', '{ "find": "near_sphere", "filter": { "a.b": { "$near": [0, 0], "$minDistance": 5, "$maxDistance": 8}}}');
EXPLAIN (VERBOSE ON, COSTS OFF) SELECT document FROM bson_aggregation_find('db', '{ "find": "near_sphere", "filter": { "a.b": { "$near": {"type": "Point", "coordinates": [0, 0]}}}}');
EXPLAIN (VERBOSE ON, COSTS OFF) SELECT document FROM bson_aggregation_find('db', '{ "find": "near_sphere", "filter": { "a.b": { "$near": {"type": "Point", "coordinates": [0, 0]}, "$minDistance": 500000, "$maxDistance": 1500000}}}');
EXPLAIN (VERBOSE ON, COSTS OFF) SELECT document FROM bson_aggregation_find('db', '{ "find": "near_sphere", "filter": { "a.b": { "$nearSphere": [0, 0]}}}');
EXPLAIN (VERBOSE ON, COSTS OFF) SELECT document FROM bson_aggregation_find('db', '{ "find": "near_sphere", "filter": { "a.b": { "$nearSphere": [0, 0], "$minDistance": 0.8, "$maxDistance": 1}}}');
EXPLAIN (VERBOSE ON, COSTS OFF) SELECT document FROM bson_aggregation_find('db', '{ "find": "near_sphere", "filter": { "a.geo": { "$nearSphere": {"type": "Point", "coordinates": [0, 0]}}}}');
EXPLAIN (VERBOSE ON, COSTS OFF) SELECT document FROM bson_aggregation_find('db', '{ "find": "near_sphere", "filter": { "a.b": { "$nearSphere": {"$geometry": {"type": "Point", "coordinates": [0, 0]}, "$minDistance": 500000, "$maxDistance": 1500000}}}}');

ROLLBACK;

-- update with sharded
BEGIN;
SELECT documentdb_api.update('db', '{"update":"near_sphere", "updates":[{"q":{"a.b": {"$near": [1,1]}} ,"u":{"$set": {"t": 1}},"multi":true,"upsert":false}]}');
ROLLBACK;


-- Test near and nearsphere with different sorting features.
SELECT documentdb_api.insert_one('db','near_sphere_distinct','{ "_id": 1, "zone": 1, "loc": { "type": "Point", "coordinates": [-20, -20]} }', NULL);
SELECT documentdb_api.insert_one('db','near_sphere_distinct','{ "_id": 2, "zone": 2, "loc": { "type": "Point", "coordinates": [-10, -10]} }', NULL);
SELECT documentdb_api.insert_one('db','near_sphere_distinct','{ "_id": 3, "zone": 3, "loc": { "type": "Point", "coordinates": [0, 0]} }', NULL);
SELECT documentdb_api.insert_one('db','near_sphere_distinct','{ "_id": 4, "zone": 4, "loc": { "type": "Point", "coordinates": [10, 10]} }', NULL);
SELECT documentdb_api.insert_one('db','near_sphere_distinct','{ "_id": 5, "zone": 5, "loc": { "type": "Point", "coordinates": [20, 20]} }', NULL);
SELECT documentdb_api.insert_one('db','near_sphere_distinct','{ "_id": 6, "zone": 1, "loc": { "type": "Point", "coordinates": [-20, -20]} }', NULL);
SELECT documentdb_api.insert_one('db','near_sphere_distinct','{ "_id": 7, "zone": 2, "loc": { "type": "Point", "coordinates": [-10, -10]} }', NULL);
SELECT documentdb_api.insert_one('db','near_sphere_distinct','{ "_id": 8, "zone": 3, "loc": { "type": "Point", "coordinates": [0, 0]} }', NULL);
SELECT documentdb_api.insert_one('db','near_sphere_distinct','{ "_id": 9, "zone": 4, "loc": { "type": "Point", "coordinates": [10, 10]} }', NULL);
SELECT documentdb_api.insert_one('db','near_sphere_distinct','{ "_id": 10, "zone": 5, "loc": { "type": "Point", "coordinates": [20, 20]} }', NULL);
SELECT documentdb_api.insert_one('db','near_sphere_distinct','{ "_id": 11, "zone": 6, "loc": { "type": "Point", "coordinates": [30, 30]} }', NULL);

SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "near_sphere_distinct", "indexes": [{"key": {"loc.coordinates": "2d"}, "name": "my_2d_loccoord_idx" }]}', true);

SELECT document FROM bson_aggregation_distinct('db', '{ "distinct": "near_sphere_distinct", "key": "zone", "query": {"loc.coordinates": { "$near": [0, 0], "$maxDistance": 1}} }');

-- find and modify fails with geonear
-- With sort on findAndModify with near, sort takes precedence
SELECT documentdb_api.find_and_modify('db', '{"findAndModify": "near_sphere_distinct", "query": { "loc.coordinates": { "$near": [0, 0], "$maxDistance": 1} }, "sort": {"_id": -1}, "update": {"a": 10}, "fields": {"_id": 1}}');
-- If no sort clause then near decides the ordering
SELECT documentdb_api.find_and_modify('db', '{"findAndModify": "near_sphere_distinct", "query": { "loc.coordinates": { "$near": [30, 30]} }, "update": {"a": 10}, "fields": {"_id": 1}}');
