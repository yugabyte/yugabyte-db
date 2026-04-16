SET search_path TO documentdb_api_catalog;

SET citus.next_shard_id TO 498000;
SET documentdb.next_collection_id TO 4980;
SET documentdb.next_collection_index_id TO 4980;

SELECT documentdb_api.insert_one('db','agg_geonear','{ "_id": 1, "a": { "b": [ 0, 0]} }', NULL);
SELECT documentdb_api.insert_one('db','agg_geonear','{ "_id": 2, "a": { "b": [ 1.1, 1.1]} }', NULL);
SELECT documentdb_api.insert_one('db','agg_geonear','{ "_id": 3, "a": { "b": [ 2.29, 2.29]} }', NULL);
SELECT documentdb_api.insert_one('db','agg_geonear','{ "_id": 4, "a": { "b": [ 3.31, 3.31]} }', NULL);
SELECT documentdb_api.insert_one('db','agg_geonear','{ "_id": 5, "a": { "b": [ 4.42, 4.42]} }', NULL);
SELECT documentdb_api.insert_one('db','agg_geonear','{ "_id": 6, "a": { "b": [ 5.5, 5.5]} }', NULL);
SELECT documentdb_api.insert_one('db','agg_geonear','{ "_id": 7, "a": { "b": [ 6.66, 6.66]} }', NULL);
SELECT documentdb_api.insert_one('db','agg_geonear','{ "_id": 8, "a": { "b": [ 7.74, 7.74]} }', NULL);
SELECT documentdb_api.insert_one('db','agg_geonear','{ "_id": 9, "a": { "b": [ 8.81, 8.81]} }', NULL);
SELECT documentdb_api.insert_one('db','agg_geonear','{ "_id": 10, "a": { "geo": {"type": "Point", "coordinates": [35.3, 35.4]}} }', NULL);
SELECT documentdb_api.insert_one('db','agg_geonear','{ "_id": 11, "a": { "geo": {"type": "LineString", "coordinates": [[35.36, 35.42], [32.3, 30]]}} }', NULL);
SELECT documentdb_api.insert_one('db','agg_geonear','{ "_id": 12, "a": { "geo": {"type": "Polygon", "coordinates": [[[35.73, 35.74], [38.6, 35.3], [38.7, 39.2], [35.73, 35.74]]]}} }', NULL);
SELECT documentdb_api.insert_one('db','agg_geonear','{ "_id": 13, "a": { "geo": {"type": "MultiPoint", "coordinates": [[35.43, 35.44], [32.3, 30]]}} }', NULL);
SELECT documentdb_api.insert_one('db','agg_geonear','{ "_id": 14, "a": { "geo": {"type": "MultiLineString", "coordinates": [[[35.83, 35.84], [32.3, 30]]]}} }', NULL);
SELECT documentdb_api.insert_one('db','agg_geonear','{ "_id": 15, "a": { "geo": {"type": "MultiPolygon", "coordinates": [[[[35.312, 35.441], [38.644, 35.3231], [38.71, 39.32], [35.312, 35.441]]]]}} }', NULL);

-- Validations (more validations to follow)
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "agg_geonear", "pipeline": [ { "$match": { "query": {  } } }, { "$geoNear": { "near": [5, 6], "key": "a.b", "distanceField": "dist.calculated" } } ]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "agg_geonear", "pipeline": [ { "$geoNear": { "near": [5, 6], "distanceField": "dist.calculated" } } ]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "agg_geonear", "pipeline": [ { "$geoNear": {  } } ]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "agg_geonear", "pipeline": [ { "$geoNear": "value" } ]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "agg_geonear", "pipeline": [ { "$geoNear": { "near": true, "distanceField": "dist.calculated", "key": "a" } } ]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "agg_geonear", "pipeline": [ { "$geoNear": { "near": [0, 0], "distanceField": "dist.calculated", "key": "a", "query": { "$text": { "$search": "cat" } } } } ]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "agg_geonear", "pipeline": [ { "$geoNear": { "near": [0, 0], "distanceField": "dist.calculated", "key": "a", "query": { "$or": [{"$text": { "$search": "cat" }}, {"b": 2}] } } } ]}');        
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "agg_geonear", "pipeline": [ { "$geoNear": { "near": [0, 0], "distanceField": "dist.calculated", "key": "a", "query": { "a": {"$near": [1, 1]}} } } ]}');       
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "agg_geonear", "pipeline": [ { "$geoNear": { "near": [0, 0], "distanceField": "dist.calculated", "key": "a", "query": { "a": {"$nearSphere": {"coordinates": [1, 1]}}} } } ]}'); 
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "agg_geonear", "pipeline": [ { "$geoNear": { "near": [0, 0], "distanceField": "dist.calculated", "key": "a",  "minDistance": -1} } ]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "agg_geonear", "pipeline": [ { "$geoNear": { "near": [0, 0], "distanceField": "dist.calculated", "key": "a",  "minDistance": {"$numberDouble": "-Infinity"}} } ]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "agg_geonear", "pipeline": [ { "$geoNear": { "near": [0, 0], "distanceField": "dist.calculated", "key": "a",  "minDistance": {"$numberDouble": "NaN"}} } ]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "agg_geonear", "pipeline": [ { "$geoNear": { "near": [0, 0], "distanceField": "dist.calculated", "key": "a",  "minDistance": {"$numberDouble": "-NaN"}} } ]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "agg_geonear", "pipeline": [ { "$geoNear": { "near": [0, 0], "distanceField": "dist.calculated", "key": "a",  "maxDistance": {"$numberDecimal": "-Infinity"}} } ]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "agg_geonear", "pipeline": [ { "$geoNear": { "near": [0, 0], "distanceField": "dist.calculated", "key": "a",  "maxDistance": {"$numberDecimal": "NaN"}} } ]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "agg_geonear", "pipeline": [ { "$geoNear": { "near": [0, 0], "distanceField": "dist.calculated", "key": "a",  "maxDistance": {"$numberDecimal": "-NaN"}} } ]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "agg_geonear", "pipeline": [ { "$geoNear": { "near": [0, 0], "distanceField": "dist.calculated", "key": "a"} }, { "$match": { "a": { "$near": {"coordinates": [1,1]}}}} ]}');

-- Tests for verifying strict index usage
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "agg_geonear", "pipeline": [ { "$geoNear": { "near": [5, 6], "distanceField": "dist.calculated", "key": "a.b" } }] }');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "agg_geonear", "pipeline": [ { "$geoNear": { "near": { "type": "Point", "coordinates": [5, 6] }, "distanceField": "dist.calculated", "key": "a.b" } }] }');

SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "agg_geonear", "indexes": [{"key": {"a.b": "2d"}, "name": "my_2d_ab_idx" }, {"key": {"a.b": "2dsphere"}, "name": "my_2ds_ab_idx" }, {"key": {"a.geo": "2dsphere"}, "name": "my_2ds_ageo_idx" }, {"key": {"a.c": "2d"}, "name": "my_2d_ac_idx" }]}', true);
SELECT documentdb_distributed_test_helpers.drop_primary_key('db','agg_geonear');
SELECT * FROM documentdb_distributed_test_helpers.get_collection_indexes('db', 'agg_geonear') ORDER BY collection_id, index_id;
\d documentdb_data.documents_4980


-- If geo indexes are available on different paths then also geonear should fail
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "agg_geonear", "pipeline": [ { "$geoNear": { "near": { "type": "Point", "coordinates": [5, 6] }, "distanceField": "dist.calculated", "key": "not_available" } }] }');

BEGIN;
-- Find using $geoNear
set local citus.enable_local_execution TO OFF;
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "agg_geonear", "pipeline": [ { "$geoNear": { "near": [5, 6], "distanceField": "dist.calculated", "key": "a.b" } }, { "$addFields": { "dist.calculated": {"$round":[ { "$multiply": ["$dist.calculated", 100000] }] } } } ]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "agg_geonear", "pipeline": [ { "$geoNear": { "near": [5, 6], "distanceField": "dist.calculated", "key": "a.b", "includeLocs": "dist.location" } } , { "$addFields": { "dist.calculated": {"$round":[ { "$multiply": ["$dist.calculated", 100000] }] } } } ]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "agg_geonear", "pipeline": [ { "$geoNear": { "near": [5, 6], "distanceField": "dist.calculated", "spherical": true, "key": "a.b", "includeLocs": "dist.location" } } , { "$addFields": { "dist.calculated": {"$round":[ { "$multiply": ["$dist.calculated", 100000] }] } } } ]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "agg_geonear", "pipeline": [ { "$geoNear": { "near": [5, 6], "distanceField": "dist.calculated", "spherical": true, "key": "a.b", "includeLocs": "dist.location", "distanceMultiplier": 6378.1 } } , { "$addFields": { "dist.calculated": {"$round":[ { "$multiply": ["$dist.calculated", 100000] }] } } } ]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "agg_geonear", "pipeline": [ { "$geoNear": { "near": [5, 6], "distanceField": "dist.calculated", "spherical": false, "key": "a.b", "includeLocs": "dist.location" } } , { "$addFields": { "dist.calculated": {"$round":[ { "$multiply": ["$dist.calculated", 100000] }] } } } ]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "agg_geonear", "pipeline": [ { "$geoNear": { "near": { "type": "Point", "coordinates": [5, 6] }, "distanceField": "dist.calculated", "spherical": false, "key": "a.b", "includeLocs": "dist.location" } } , { "$addFields": { "dist.calculated": {"$round":[ { "$multiply": ["$dist.calculated", 100000] }] } } } ]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "agg_geonear", "pipeline": [ { "$geoNear": { "near": { "type": "Point", "coordinates": [5, 6] }, "distanceField": "dist.calculated", "spherical": false, "key": "a.b", "includeLocs": "dist.location", "distanceMultiplier": 0.001 } } , { "$addFields": { "dist.calculated": {"$round":[ { "$multiply": ["$dist.calculated", 100000] }] } } } ]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "agg_geonear", "pipeline": [ { "$geoNear": { "near": { "type": "Point", "coordinates": [5, 6] }, "distanceField": "dist.calculated", "spherical": true, "key": "a.b", "includeLocs": "dist.location" } } , { "$addFields": { "dist.calculated": {"$round":[ { "$multiply": ["$dist.calculated", 100000] }] } } } ]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "agg_geonear", "pipeline": [ { "$geoNear": { "near": { "type": "Point", "coordinates": [35, 35] }, "distanceField": "dist.calculated", "key": "a.geo"} } , { "$addFields": { "dist.calculated": {"$round":[ { "$multiply": ["$dist.calculated", 100000] }] } } } ]}');

--min/max distance test
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "agg_geonear", "pipeline": [ { "$geoNear": { "near": { "type": "Point", "coordinates": [5, 6] }, "distanceField": "dist.calculated", "key": "a.b", "includeLocs": "dist.location", "minDistance": 0, "maxDistance": 500000} } , { "$addFields": { "dist.calculated": {"$round":[ { "$multiply": ["$dist.calculated", 100000] }] } } } ]}'); -- Upto 500 kms
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "agg_geonear", "pipeline": [ { "$geoNear": { "near": [0, 0], "distanceField": "dist.calculated", "key": "a.b", "includeLocs": "dist.location", "minDistance": 0, "maxDistance": 2} } , { "$addFields": { "dist.calculated": {"$round":[ { "$multiply": ["$dist.calculated", 100000] }] } } } ]}'); -- Upto 2 cartesian distance
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "agg_geonear", "pipeline": [ { "$geoNear": { "near": [5, 6], "distanceField": "dist.calculated", "key": "a.b", "spherical": true, "includeLocs": "dist.location", "minDistance": 0, "maxDistance": 0.1} } , { "$addFields": { "dist.calculated": {"$round":[ { "$multiply": ["$dist.calculated", 100000] }] } } } ]}'); -- Upto 0.1 radians
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "agg_geonear", "pipeline": [ { "$geoNear": { "near": [0, 0, 2], "distanceField": "dist.calculated", "key": "a.b", "includeLocs": "dist.location"} } , { "$addFields": { "dist.calculated": {"$round":[ { "$multiply": ["$dist.calculated", 100000] }] } } } ]}'); -- Upto 2 cartesian distance
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "agg_geonear", "pipeline": [ { "$geoNear": { "near": [5, 6, 0.1], "distanceField": "dist.calculated", "key": "a.b", "spherical": true, "includeLocs": "dist.location"} } , { "$addFields": { "dist.calculated": {"$round":[ { "$multiply": ["$dist.calculated", 100000] }] } } } ]}'); -- Upto 0.1 radians
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "agg_geonear", "pipeline": [ { "$geoNear": { "maxDistance": 0.2, "near": [5, 6, 0.1], "distanceField": "dist.calculated", "key": "a.b", "spherical": true, "includeLocs": "dist.location"} } , { "$addFields": { "dist.calculated": {"$round":[ { "$multiply": ["$dist.calculated", 100000] }] } } } ]}'); -- Upto 0.2 radians
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "agg_geonear", "pipeline": [ { "$geoNear": { "near": [5, 6, 0.1], "maxDistance": 0.2, "distanceField": "dist.calculated", "key": "a.b", "spherical": true, "includeLocs": "dist.location"} } , { "$addFields": { "dist.calculated": {"$round":[ { "$multiply": ["$dist.calculated", 100000] }] } } } ]}'); -- Upto 0.2 radians


EXPLAIN (VERBOSE ON, COSTS OFF) SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "agg_geonear", "pipeline": [ { "$geoNear": { "near": { "type": "Point", "coordinates": [5, 6] }, "distanceField": "dist.calculated", "spherical": true, "key": "a.b", "includeLocs": "dist.location" } } ]}');
EXPLAIN (VERBOSE ON, COSTS OFF) SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "agg_geonear", "pipeline": [ { "$geoNear": { "near": [5, 6], "distanceField": "dist.calculated", "spherical": true, "key": "a.b", "includeLocs": "dist.location" } } ]}');
EXPLAIN (VERBOSE ON, COSTS OFF) SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "agg_geonear", "pipeline": [ { "$geoNear": { "near": { "type": "Point", "coordinates": [35, 35] }, "distanceField": "dist.calculated", "key": "a.geo"} } ]}');    

-- min / maxDistance Explains
EXPLAIN (VERBOSE ON, COSTS OFF) SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "agg_geonear", "pipeline": [ { "$geoNear": { "near": { "type": "Point", "coordinates": [5, 6] }, "distanceField": "dist.calculated", "key": "a.b", "includeLocs": "dist.location", "minDistance": 0, "maxDistance": 500000} } ]}'); -- Upto 500 kms
EXPLAIN (VERBOSE ON, COSTS OFF) SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "agg_geonear", "pipeline": [ { "$geoNear": { "near": [0, 0], "distanceField": "dist.calculated", "key": "a.b", "includeLocs": "dist.location", "minDistance": 0, "maxDistance": 2} } ]}'); -- Upto 2 cartesian distance
EXPLAIN (VERBOSE ON, COSTS OFF) SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "agg_geonear", "pipeline": [ { "$geoNear": { "near": [5, 6], "distanceField": "dist.calculated", "key": "a.b", "spherical": true, "includeLocs": "dist.location", "minDistance": 0, "maxDistance": 0.1} } ]}'); -- Upto 0.1 radians
ROLLBACK;

-- Shard the collection
SELECT documentdb_api.shard_collection('db', 'agg_geonear', '{"_id": "hashed"}', false);


BEGIN;
-- Find using $geoNear
set local citus.enable_local_execution TO OFF;
SELECT * FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "agg_geonear", "pipeline": [ { "$geoNear": { "near": [5, 6], "distanceField": "dist.calculated", "key": "a.b" } } , { "$addFields": { "dist.calculated": {"$round":[ { "$multiply": ["$dist.calculated", 100000] }] } } } ]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "agg_geonear", "pipeline": [ { "$geoNear": { "near": [5, 6], "distanceField": "dist.calculated", "key": "a.b", "includeLocs": "dist.location" } } , { "$addFields": { "dist.calculated": {"$round":[ { "$multiply": ["$dist.calculated", 100000] }] } } } ]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "agg_geonear", "pipeline": [ { "$geoNear": { "near": [5, 6], "distanceField": "dist.calculated", "spherical": true, "key": "a.b", "includeLocs": "dist.location" } } , { "$addFields": { "dist.calculated": {"$round":[ { "$multiply": ["$dist.calculated", 100000] }] } } } ]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "agg_geonear", "pipeline": [ { "$geoNear": { "near": [5, 6], "distanceField": "dist.calculated", "spherical": true, "key": "a.b", "includeLocs": "dist.location", "distanceMultiplier": 6378.1 } } , { "$addFields": { "dist.calculated": {"$round":[ { "$multiply": ["$dist.calculated", 100000] }] } } } ]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "agg_geonear", "pipeline": [ { "$geoNear": { "near": [5, 6], "distanceField": "dist.calculated", "spherical": false, "key": "a.b", "includeLocs": "dist.location" } } , { "$addFields": { "dist.calculated": {"$round":[ { "$multiply": ["$dist.calculated", 100000] }] } } } ]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "agg_geonear", "pipeline": [ { "$geoNear": { "near": { "type": "Point", "coordinates": [5, 6] }, "distanceField": "dist.calculated", "spherical": false, "key": "a.b", "includeLocs": "dist.location" } } , { "$addFields": { "dist.calculated": {"$round":[ { "$multiply": ["$dist.calculated", 100000] }] } } } ]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "agg_geonear", "pipeline": [ { "$geoNear": { "near": { "type": "Point", "coordinates": [5, 6] }, "distanceField": "dist.calculated", "spherical": false, "key": "a.b", "includeLocs": "dist.location", "distanceMultiplier": 0.001 } } , { "$addFields": { "dist.calculated": {"$round":[ { "$multiply": ["$dist.calculated", 100000] }] } } } ]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "agg_geonear", "pipeline": [ { "$geoNear": { "near": { "type": "Point", "coordinates": [5, 6] }, "distanceField": "dist.calculated", "spherical": true, "key": "a.b", "includeLocs": "dist.location" } } , { "$addFields": { "dist.calculated": {"$round":[ { "$multiply": ["$dist.calculated", 100000] }] } } } ]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "agg_geonear", "pipeline": [ { "$geoNear": { "near": { "type": "Point", "coordinates": [35, 35] }, "distanceField": "dist.calculated", "key": "a.geo"} } , { "$addFields": { "dist.calculated": {"$round":[ { "$multiply": ["$dist.calculated", 100000] }] } } } ]}');

--min/max distance test
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "agg_geonear", "pipeline": [ { "$geoNear": { "near": { "type": "Point", "coordinates": [5, 6] }, "distanceField": "dist.calculated", "key": "a.b", "includeLocs": "dist.location", "minDistance": 0, "maxDistance": 500000} } , { "$addFields": { "dist.calculated": {"$round":[ { "$multiply": ["$dist.calculated", 100000] }] } } } ]}'); -- Upto 500 kms
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "agg_geonear", "pipeline": [ { "$geoNear": { "near": [0, 0], "distanceField": "dist.calculated", "key": "a.b", "includeLocs": "dist.location", "minDistance": 0, "maxDistance": 2} } , { "$addFields": { "dist.calculated": {"$round":[ { "$multiply": ["$dist.calculated", 100000] }] } } } ]}'); -- Upto 2 cartesian distance
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "agg_geonear", "pipeline": [ { "$geoNear": { "near": [5, 6], "distanceField": "dist.calculated", "key": "a.b", "spherical": true, "includeLocs": "dist.location", "minDistance": 0, "maxDistance": 0.1} } , { "$addFields": { "dist.calculated": {"$round":[ { "$multiply": ["$dist.calculated", 100000] }] } } } ]}'); -- Upto 0.1 radians
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "agg_geonear", "pipeline": [ { "$geoNear": { "near": [0, 0, 2], "distanceField": "dist.calculated", "key": "a.b", "includeLocs": "dist.location"} } , { "$addFields": { "dist.calculated": {"$round":[ { "$multiply": ["$dist.calculated", 100000] }] } } } ]}'); -- Upto 2 cartesian distance
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "agg_geonear", "pipeline": [ { "$geoNear": { "near": [5, 6, 0.1], "distanceField": "dist.calculated", "key": "a.b", "spherical": true, "includeLocs": "dist.location"} } , { "$addFields": { "dist.calculated": {"$round":[ { "$multiply": ["$dist.calculated", 100000] }] } } } ]}'); -- Upto 0.1 radians
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "agg_geonear", "pipeline": [ { "$geoNear": { "maxDistance": 0.2, "near": [5, 6, 0.1], "distanceField": "dist.calculated", "key": "a.b", "spherical": true, "includeLocs": "dist.location"} } , { "$addFields": { "dist.calculated": {"$round":[ { "$multiply": ["$dist.calculated", 100000] }] } } } ]}'); -- Upto 0.2 radians
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "agg_geonear", "pipeline": [ { "$geoNear": { "near": [5, 6, 0.1], "maxDistance": 0.2, "distanceField": "dist.calculated", "key": "a.b", "spherical": true, "includeLocs": "dist.location"} } , { "$addFields": { "dist.calculated": {"$round":[ { "$multiply": ["$dist.calculated", 100000] }] } } } ]}'); -- Upto 0.2 radians


-- As of version 1.16 ORDER BY clauses are not pushed to shards it can pick either 2d or 2dsphere index for use in shards and sorts the data in coordinator.
EXPLAIN (VERBOSE ON, COSTS OFF) SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "agg_geonear", "pipeline": [ { "$geoNear": { "near": { "type": "Point", "coordinates": [5, 6] }, "distanceField": "dist.calculated", "spherical": true, "key": "a.b", "includeLocs": "dist.location" } } ]}');
EXPLAIN (VERBOSE ON, COSTS OFF) SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "agg_geonear", "pipeline": [ { "$geoNear": { "near": [5, 6], "distanceField": "dist.calculated", "spherical": true, "key": "a.b", "includeLocs": "dist.location" } } ]}');
EXPLAIN (VERBOSE ON, COSTS OFF) SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "agg_geonear", "pipeline": [ { "$geoNear": { "near": { "type": "Point", "coordinates": [35, 35] }, "distanceField": "dist.calculated", "key": "a.geo"} } ]}');    

-- min / maxDistance Explains
EXPLAIN (VERBOSE ON, COSTS OFF) SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "agg_geonear", "pipeline": [ { "$geoNear": { "near": { "type": "Point", "coordinates": [5, 6] }, "distanceField": "dist.calculated", "key": "a.b", "includeLocs": "dist.location", "minDistance": 0, "maxDistance": 500000} } ]}'); -- Upto 500 kms
EXPLAIN (VERBOSE ON, COSTS OFF) SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "agg_geonear", "pipeline": [ { "$geoNear": { "near": [0, 0], "distanceField": "dist.calculated", "key": "a.b", "includeLocs": "dist.location", "minDistance": 0, "maxDistance": 2} } ]}'); -- Upto 2 cartesian distance
EXPLAIN (VERBOSE ON, COSTS OFF) SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "agg_geonear", "pipeline": [ { "$geoNear": { "near": [5, 6], "distanceField": "dist.calculated", "key": "a.b", "spherical": true, "includeLocs": "dist.location", "minDistance": 0, "maxDistance": 0.1} } ]}'); -- Upto 0.1 radians
ROLLBACK;

-- Legacy geonear spherical queries are pushed to 2dsphere index if that is the only index available
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "agg_geonear_legacy", "indexes": [{"key": {"a": "2dsphere"}, "name": "my_2ds_a_idx" }]}', true);
SELECT * FROM documentdb_distributed_test_helpers.get_collection_indexes('db', 'agg_geonear_legacy') ORDER BY collection_id, index_id;
SELECT documentdb_api.insert_one('db','agg_geonear_legacy','{ "_id": 1, "a": [1, 1] }', NULL);
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "agg_geonear_legacy", "pipeline": [ { "$geoNear": { "near": [0, 0], "distanceField": "dist.calculated", "key": "a", "spherical": true } }, { "$addFields": { "dist.calculated": {"$round":[ { "$multiply": ["$dist.calculated", 100000] }] } } } ]}');
EXPLAIN (VERBOSE ON, COSTS OFF) SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "agg_geonear_legacy", "pipeline": [ { "$geoNear": { "near": [0, 0], "distanceField": "dist.calculated", "key": "a", "spherical": true } }, { "$addFields": { "dist.calculated": {"$round":[ { "$multiply": ["$dist.calculated", 100000] }] } } } ]}');
-- Same non spherical query doesn't work though
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "agg_geonear_legacy", "pipeline": [ { "$geoNear": { "near": [0, 0], "distanceField": "dist.calculated", "key": "a", "spherical": false } }, { "$addFields": { "dist.calculated": {"$round":[ { "$multiply": ["$dist.calculated", 100000] }] } } } ]}');


-- Tests with other stages. $lookup
SELECT documentdb_api.insert_one('db','geonear_lookup_1','{ "_id": 1, "a": [10, 10], "x": 5 }', NULL);
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "geonear_lookup_1", "indexes": [{"key": {"a": "2dsphere"}, "name": "lookup1_2ds_a_idx" }]}', true);
SELECT documentdb_api.insert_one('db','geonear_lookup_2','{ "_id": 5, "foo": "bar", "a": [5, 5], "x": 5 }', NULL);

SELECT * FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "geonear_lookup_1", "pipeline": [ { "$geoNear": {"near": {"type": "Point", "coordinates": [0, 1]}, "distanceField": "distance", "spherical": true, "key": "a"}}, {"$lookup": {"from": "geonear_lookup_2", "localField": "x", "foreignField": "x", "as": "new"}}] } ');
EXPLAIN (VERBOSE ON, COSTS OFF) SELECT * FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "geonear_lookup_1", "pipeline": [ { "$geoNear": {"near": {"type": "Point", "coordinates": [0, 1]}, "distanceField": "distance", "spherical": true, "key": "a"}}, {"$lookup": {"from": "geonear_lookup_2", "localField": "x", "foreignField": "x", "as": "new"}}]} ');


-- This fails because no valid index on geonear_lookup_2 for $geoNear
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db','{ "aggregate": "geonear_lookup_1", "pipeline": [ '
'{ "$lookup": { "from": "geonear_lookup_2", "pipeline": [{ "$geoNear": { "near": { "type": "Point", "coordinates": [5, 6] }, "distanceField": "dist.calculated", "key": "a"} } ], "as": "c"  } }]}');

SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "geonear_lookup_2", "indexes": [{"key": {"a": "2dsphere"}, "name": "lookup2_2ds_a_idx" }]}', true);

-- Now it should pass
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db','{ "aggregate": "geonear_lookup_1", "pipeline": [ '
'{ "$lookup": { "from": "geonear_lookup_2", "pipeline": [{ "$geoNear": { "near": { "type": "Point", "coordinates": [5, 6] }, "distanceField": "dist.calculated", "key": "a"} } ], "as": "c"  } }]}');
EXPLAIN (VERBOSE ON, COSTS OFF) SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db','{ "aggregate": "geonear_lookup_1", "pipeline": [ '
'{ "$lookup": { "from": "geonear_lookup_2", "pipeline": [{ "$geoNear": { "near": { "type": "Point", "coordinates": [5, 6] }, "distanceField": "dist.calculated", "key": "a"} } ], "as": "c"  } }]}');

-- Additional checks with legacy pair array
BEGIN;
SELECT documentdb_api.insert_one('db','agg_geonear','{ "_id": 16, "a": { "c": [[10, 10], [20, 20], [30, 30], [40, 40]]} }', NULL);
SELECT documentdb_api.insert_one('db','agg_geonear','{ "_id": 17, "a": { "c": [{"x": 10, "y": 10}, {"x": 20, "y": 20}, {"x": 30, "y": 30}, {"x": 40, "y": 40}]} }', NULL);
SELECT documentdb_api.insert_one('db','agg_geonear','{ "_id": 18, "a": { "c": [[]]} }', NULL);
SELECT documentdb_api.insert_one('db','agg_geonear','{ "_id": 19, "a": { "c": [{}]} }', NULL);

SELECT * FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "agg_geonear", "pipeline": [ { "$geoNear": { "near": [10, 10], "distanceField": "dist.calculated", "key": "a.c" } } , { "$addFields": { "dist.calculated": {"$round":[ { "$multiply": ["$dist.calculated", 100000] }] } } } ]}');
SELECT * FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "agg_geonear", "pipeline": [ { "$geoNear": { "near": [30, 30], "distanceField": "dist.calculated", "key": "a.c" } } , { "$addFields": { "dist.calculated": {"$round":[ { "$multiply": ["$dist.calculated", 100000] }] } } } ]}');
SELECT * FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "agg_geonear", "pipeline": [ { "$geoNear": { "near": [10, 10], "distanceField": "dist.calculated", "key": "a.c", "spherical": true } } , { "$addFields": { "dist.calculated": {"$round":[ { "$multiply": ["$dist.calculated", 100000] }] } } } ]}');
SELECT * FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "agg_geonear", "pipeline": [ { "$geoNear": { "near": [30, 30], "distanceField": "dist.calculated", "key": "a.c", "spherical": true } } , { "$addFields": { "dist.calculated": {"$round":[ { "$multiply": ["$dist.calculated", 100000] }] } } } ]}');

EXPLAIN VERBOSE SELECT * FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "agg_geonear", "pipeline": [ { "$geoNear": { "near": [10, 10], "distanceField": "dist.calculated", "key": "a.c" } } , { "$addFields": { "dist.calculated": {"$round":[ { "$multiply": ["$dist.calculated", 100000] }] } } } ]}');

ROLLBACK;

SELECT documentdb_api.insert_one('db','boundstest','{ "_id": 1, "geo": [ 0, 0]}', NULL);
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "boundstest", "indexes": [{"key": {"geo": "2d"}, "name": "2d_bounds_idx", "max": 1, "min": -1 }]}', true);

-- Errors out with point not in interval based on index bounds.
-- TODO: add index bound tests for sharded collection once ORDER BY pushdown to shards is supported
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "boundstest", "pipeline": [ { "$geoNear": { "near": [5, 6], "distanceField": "dist.calculated", "key": "geo", "includeLocs": "dist.location", "minDistance": 0, "maxDistance": 500000} } ]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "boundstest", "pipeline": [ { "$geoNear": { "near": [0, 6], "distanceField": "dist.calculated", "key": "geo", "includeLocs": "dist.location", "minDistance": 0, "maxDistance": 500000} } ]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "boundstest", "pipeline": [ { "$geoNear": { "near": [5, 0], "distanceField": "dist.calculated", "key": "geo", "includeLocs": "dist.location", "minDistance": 0, "maxDistance": 500000} } ]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "boundstest", "pipeline": [ { "$geoNear": { "near": [1, -1], "distanceField": "dist.calculated", "key": "geo", "includeLocs": "dist.location", "minDistance": 0, "maxDistance": 500000} } ]}');

RESET enable_seqscan;
RESET documentdb.forceUseIndexIfAvailable;
SELECT documentdb_api.drop_collection('db', 'boundstest') IS NOT NULL;


SELECT documentdb_api.insert_one('db','valid_extract','{ "_id" : 1, "a" : { "b" : { "type" : "Point", "coordinates" : [ 10, 10 ] } } }', NULL);
SELECT documentdb_api.insert_one('db','invalid_extract1','{ "_id" : 2, "a" : { "b" : { "type" : "MultiPoint", "coordinates" : [ [ 10, 10 ], [ 15, 15 ] ] } } }', NULL);

-- This now fails to because no valid index, and without index this invalid extract will not happen
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "valid_extract", "pipeline": [ { "$geoNear": { "near": [5, 6], "distanceField": "dist.calculated", "key": "a.b"} } ]}');
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "invalid_extract1", "pipeline": [ { "$geoNear": { "near": [5, 6], "distanceField": "dist.calculated", "key": "a.b"} } ]}');

SELECT documentdb_api.drop_collection('db', 'valid_extract') IS NOT NULL;
SELECT documentdb_api.drop_collection('db', 'invalid_extract1') IS NOT NULL;
