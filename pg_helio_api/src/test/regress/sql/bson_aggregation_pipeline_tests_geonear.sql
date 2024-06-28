SET search_path TO helio_api,helio_core,helio_api_catalog;

SET helio_api.next_collection_id TO 4900;
SET helio_api.next_collection_index_id TO 4900;

SELECT helio_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "agg_geonear", "indexes": [{"key": {"a.b": "2d"}, "name": "my_2d_ab_idx" }, {"key": {"a.b": "2dsphere"}, "name": "my_2ds_ab_idx" }, {"key": {"a.geo": "2dsphere"}, "name": "my_2ds_ageo_idx" }]}');

SELECT helio_api.insert_one('db','agg_geonear','{ "_id": 1, "a": { "b": [ 0, 0]} }', NULL);
SELECT helio_api.insert_one('db','agg_geonear','{ "_id": 2, "a": { "b": [ 1.1, 1.1]} }', NULL);
SELECT helio_api.insert_one('db','agg_geonear','{ "_id": 3, "a": { "b": [ 2.29, 2.29]} }', NULL);
SELECT helio_api.insert_one('db','agg_geonear','{ "_id": 4, "a": { "b": [ 3.31, 3.31]} }', NULL);
SELECT helio_api.insert_one('db','agg_geonear','{ "_id": 5, "a": { "b": [ 4.42, 4.42]} }', NULL);
SELECT helio_api.insert_one('db','agg_geonear','{ "_id": 6, "a": { "b": [ 5.5, 5.5]} }', NULL);
SELECT helio_api.insert_one('db','agg_geonear','{ "_id": 7, "a": { "b": [ 6.66, 6.66]} }', NULL);
SELECT helio_api.insert_one('db','agg_geonear','{ "_id": 8, "a": { "b": [ 7.74, 7.74]} }', NULL);
SELECT helio_api.insert_one('db','agg_geonear','{ "_id": 9, "a": { "b": [ 8.81, 8.81]} }', NULL);

-- Validations (more validations to follow)
SELECT document FROM helio_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "agg_geonear", "pipeline": [ { "$match": { "query": {  } } }, { "$geoNear": { "near": [5, 6], "key": "a.b", "distanceField": "dist.calculated" } } ]}');
SELECT document FROM helio_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "agg_geonear", "pipeline": [ { "$geoNear": { "near": [5, 6], "distanceField": "dist.calculated" } } ]}');
SELECT document FROM helio_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "agg_geonear", "pipeline": [ { "$geoNear": {  } } ]}');
SELECT document FROM helio_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "agg_geonear", "pipeline": [ { "$geoNear": "value" } ]}');
SELECT document FROM helio_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "agg_geonear", "pipeline": [ { "$geoNear": { "near": true, "distanceField": "dist.calculated", "key": "a" } } ]}');

SELECT helio_test_helpers.drop_primary_key('db','agg_geonear');
SELECT * FROM helio_test_helpers.get_collection_indexes('db', 'agg_geonear') ORDER BY collection_id, index_id;

BEGIN;
SET LOCAL enable_seqscan to off;
SET LOCAL seq_page_cost TO 9999999;
-- Find using $geoNear
SELECT document FROM bson_aggregation_pipeline('db',
    '{ "aggregate": "agg_geonear", "pipeline": [ { "$geoNear": { "near": [5, 6], "distanceField": "dist.calculated", "key": "a.b" } }, { "$addFields": { "dist.calculated": {"$round":[ "$dist.calculated", 15 ] } } } , { "$addFields": { "dist.calculated": {"$round":[ { "$multiply": ["$dist.calculated", 100000] }] } } } ]}');
SELECT document FROM bson_aggregation_pipeline('db',
    '{ "aggregate": "agg_geonear", "pipeline": [ { "$geoNear": { "near": [5, 6], "distanceField": "dist.calculated", "key": "a.b", "includeLocs": "dist.location" } } , { "$addFields": { "dist.calculated": {"$round":[ { "$multiply": ["$dist.calculated", 100000] }] } } } ]}');
SELECT document FROM bson_aggregation_pipeline('db',
    '{ "aggregate": "agg_geonear", "pipeline": [ { "$geoNear": { "near": [5, 6], "distanceField": "dist.calculated", "spherical": true, "key": "a.b", "includeLocs": "dist.location" } } , { "$addFields": { "dist.calculated": {"$round":[ { "$multiply": ["$dist.calculated", 100000] }] } } } ]}');
SELECT document FROM bson_aggregation_pipeline('db',
    '{ "aggregate": "agg_geonear", "pipeline": [ { "$geoNear": { "near": [5, 6], "distanceField": "dist.calculated", "spherical": true, "key": "a.b", "includeLocs": "dist.location", "distanceMultiplier": 6378.1 } } , { "$addFields": { "dist.calculated": {"$round":[ { "$multiply": ["$dist.calculated", 100000] }] } } } ]}');
SELECT document FROM bson_aggregation_pipeline('db',
    '{ "aggregate": "agg_geonear", "pipeline": [ { "$geoNear": { "near": [5, 6], "distanceField": "dist.calculated", "spherical": false, "key": "a.b", "includeLocs": "dist.location" } } , { "$addFields": { "dist.calculated": {"$round":[ { "$multiply": ["$dist.calculated", 100000] }] } } } ]}');
SELECT document FROM bson_aggregation_pipeline('db',
    '{ "aggregate": "agg_geonear", "pipeline": [ { "$geoNear": { "near": { "type": "Point", "coordinates": [5, 6] }, "distanceField": "dist.calculated", "spherical": false, "key": "a.b", "includeLocs": "dist.location" } } , { "$addFields": { "dist.calculated": {"$round":[ { "$multiply": ["$dist.calculated", 100000] }] } } } ]}');
SELECT document FROM bson_aggregation_pipeline('db',
    '{ "aggregate": "agg_geonear", "pipeline": [ { "$geoNear": { "near": { "type": "Point", "coordinates": [5, 6] }, "distanceField": "dist.calculated", "spherical": false, "key": "a.b", "includeLocs": "dist.location", "distanceMultiplier": 0.001 } } , { "$addFields": { "dist.calculated": {"$round":[ { "$multiply": ["$dist.calculated", 100000] }] } } } ]}');
SELECT document FROM bson_aggregation_pipeline('db',
    '{ "aggregate": "agg_geonear", "pipeline": [ { "$geoNear": { "near": { "type": "Point", "coordinates": [5, 6] }, "distanceField": "dist.calculated", "spherical": true, "key": "a.b", "includeLocs": "dist.location" } } , { "$addFields": { "dist.calculated": {"$round":[ { "$multiply": ["$dist.calculated", 100000] }] } } } ]}');
SELECT document FROM bson_aggregation_pipeline('db',
    '{ "aggregate": "agg_geonear", "pipeline": [ { "$geoNear": { "near": { "type": "Point", "coordinates": [35, 35] }, "distanceField": "dist.calculated", "key": "a.geo"} } , { "$addFields": { "dist.calculated": {"$round":[ { "$multiply": ["$dist.calculated", 100000] }] } } } ]}');

--min/max distance test
SELECT document FROM bson_aggregation_pipeline('db',
    '{ "aggregate": "agg_geonear", "pipeline": [ { "$geoNear": { "near": { "type": "Point", "coordinates": [5, 6] }, "distanceField": "dist.calculated", "key": "a.b", "includeLocs": "dist.location", "minDistance": 0, "maxDistance": 500000} } , { "$addFields": { "dist.calculated": {"$round":[ { "$multiply": ["$dist.calculated", 100000] }] } } } ]}'); -- Upto 500 kms
SELECT document FROM bson_aggregation_pipeline('db',
    '{ "aggregate": "agg_geonear", "pipeline": [ { "$geoNear": { "near": [0, 0], "distanceField": "dist.calculated", "key": "a.b", "includeLocs": "dist.location", "minDistance": 0, "maxDistance": 2} } , { "$addFields": { "dist.calculated": {"$round":[ { "$multiply": ["$dist.calculated", 100000] }] } } } ]}'); -- Upto 2 cartesian distance
SELECT document FROM bson_aggregation_pipeline('db',
    '{ "aggregate": "agg_geonear", "pipeline": [ { "$geoNear": { "near": [5, 6], "distanceField": "dist.calculated", "key": "a.b", "spherical": true, "includeLocs": "dist.location", "minDistance": 0, "maxDistance": 0.1} } , { "$addFields": { "dist.calculated": {"$round":[ { "$multiply": ["$dist.calculated", 100000] }] } } } ]}'); -- Upto 0.1 radians

EXPLAIN (VERBOSE ON, COSTS OFF) SELECT document FROM helio_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "agg_geonear", "pipeline": [ { "$geoNear": { "near": { "type": "Point", "coordinates": [5, 6] }, "distanceField": "dist.calculated", "spherical": true, "key": "a.b", "includeLocs": "dist.location" } } ]}');
EXPLAIN (VERBOSE ON, COSTS OFF) SELECT document FROM helio_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "agg_geonear", "pipeline": [ { "$geoNear": { "near": [5, 6], "distanceField": "dist.calculated", "spherical": true, "key": "a.b", "includeLocs": "dist.location" } } ]}');
EXPLAIN SELECT document FROM helio_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "agg_geonear", "pipeline": [ { "$geoNear": { "near": { "type": "Point", "coordinates": [35, 35] }, "distanceField": "dist.calculated", "key": "a.geo"} } ]}');    

-- min / maxDistance Explains
EXPLAIN (VERBOSE ON, COSTS OFF) SELECT document FROM helio_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "agg_geonear", "pipeline": [ { "$geoNear": { "near": { "type": "Point", "coordinates": [5, 6] }, "distanceField": "dist.calculated", "key": "a.b", "includeLocs": "dist.location", "minDistance": 0, "maxDistance": 500000} } ]}'); -- Upto 500 kms
EXPLAIN (VERBOSE ON, COSTS OFF) SELECT document FROM helio_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "agg_geonear", "pipeline": [ { "$geoNear": { "near": [0, 0], "distanceField": "dist.calculated", "key": "a.b", "includeLocs": "dist.location", "minDistance": 0, "maxDistance": 2} } ]}'); -- Upto 2 cartesian distance
EXPLAIN (VERBOSE ON, COSTS OFF) SELECT document FROM helio_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "agg_geonear", "pipeline": [ { "$geoNear": { "near": [5, 6], "distanceField": "dist.calculated", "key": "a.b", "spherical": true, "includeLocs": "dist.location", "minDistance": 0, "maxDistance": 0.1} } ]}'); -- Upto 0.1 radians
ROLLBACK;


SET helio_api.enableGeospatial to off;

-- Make sure we fail if the helio_api.enableGeospatial is not set
SELECT helio_api.insert_one('db','not_enabled_test','{ "_id": 1, "a": { "b": [ 0, 0]} }', NULL);
SELECT document FROM helio_api.collection('db', 'not_enabled_test') WHERE document @@
    '{"a.b": {"$geoWithin": { "$geometry": { "type": "Polygon", "coordinates": [ [ [100, 0], [103, 0], [103, 3], [100, 3], [100, 0] ] ] } } }}';

RESET helio_api.enableGeospatial;