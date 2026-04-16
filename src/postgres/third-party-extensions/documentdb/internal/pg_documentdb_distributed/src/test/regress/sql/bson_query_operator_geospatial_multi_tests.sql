SET search_path TO documentdb_core,documentdb_api,documentdb_api_catalog,documentdb_api_internal;
SET citus.next_shard_id TO 567100;
SET documentdb.next_collection_id TO 56710;
SET documentdb.next_collection_index_id TO 56710;

SELECT documentdb_api.drop_collection('db', 'geo_multi') IS NOT NULL;
SELECT documentdb_api.create_collection('db', 'geo_multi') IS NOT NULL;

-- Insert multiple items
SELECT documentdb_api.insert_one('db','geo_multi','{ "_id" : 1, "name": "PointA", "geo": { "type": "Point", "coordinates": [100, 0] } }', NULL);
SELECT documentdb_api.insert_one('db','geo_multi','{ "_id" : 2, "name": "PointB", "geo": { "type": "Point", "coordinates": [102, 2] } }', NULL);
SELECT documentdb_api.insert_one('db','geo_multi','{ "_id" : 3, "name": "MultiPointAB", "geo": { "type": "MultiPoint", "coordinates": [[100, 0], [101, 1], [102, 2], [103, 3]] } }', NULL);
SELECT documentdb_api.insert_one('db','geo_multi','{ "_id" : 4, "name": "LineStringA", "geo": { "type": "LineString", "coordinates": [[100, 0], [100.5, 0.5], [101, 1]] } }', NULL);
SELECT documentdb_api.insert_one('db','geo_multi','{ "_id" : 5, "name": "LineStringB", "geo": { "type": "LineString", "coordinates": [[102, 2], [102.5, 2.5], [103, 3]] } }', NULL);
SELECT documentdb_api.insert_one('db','geo_multi','{ "_id" : 6, "name": "MultiLineStringAB", "geo": { "type": "MultiLineString", "coordinates": [ [[100, 0], [100.5, 0.5], [101, 1]], [[102, 2], [102.5, 2.5], [103, 3]] ] } }', NULL);
SELECT documentdb_api.insert_one('db','geo_multi','{ "_id" : 7, "name": "PolygonA", "geo": { "type": "Polygon", "coordinates": [ [ [100, 0], [101, 0], [101, 1], [100, 1], [100, 0] ] ] } }', NULL);
SELECT documentdb_api.insert_one('db','geo_multi','{ "_id" : 8, "name": "PolygonB", "geo": { "type": "Polygon", "coordinates": [ [ [102, 2], [103, 2], [103, 3], [102, 3], [102, 2] ] ] } }', NULL);
-- commenting out the polygon with holes for now
-- SELECT documentdb_api.insert_one('db','geo_multi','{ "_id" : 9, "name": "PolygonA_WithHole", "geo": { "type": "Polygon", "coordinates": [ [ [100, 0], [101, 0], [101, 1], [100, 1], [100, 0] ], [ [100.2, 0.2], [100.8, 0.2], [100.8, 0.8], [100.2, 0.8], [100.2, 0.2] ] ] } }', NULL);
SELECT documentdb_api.insert_one('db','geo_multi','{ "_id" : 10, "name": "MultiPolygonAB", "geo": { "type": "MultiPolygon", "coordinates": [ [ [ [102, 2], [103, 2], [103, 3], [102, 3], [102, 2] ] ], [ [ [100, 0], [101, 0], [101, 1], [100, 1], [100, 0] ] ] ]} }', NULL);
SELECT documentdb_api.insert_one('db','geo_multi','{ "_id" : 11, "name": "GeometryCollectionAll_Withouthole", "geo": { "type": "GeometryCollection", "geometries": [ { "type": "Point", "coordinates": [100, 0] }, { "type": "Point", "coordinates": [102, 2] }, { "type": "MultiPoint", "coordinates": [[100, 0], [101, 1], [102, 2], [103, 3]] }, { "type": "LineString", "coordinates": [[100, 0], [100.5, 0.5], [101, 1]] }, { "type": "LineString", "coordinates": [[102, 2], [102.5, 2.5], [103, 3]] }, { "type": "MultiLineString", "coordinates": [ [[100, 0], [100.5, 0.5], [101, 1]], [[102, 2], [102.5, 2.5], [103, 3]] ] }, { "type": "Polygon", "coordinates": [ [ [100, 0], [101, 0], [101, 1], [100, 1], [100, 0] ] ] }, { "type": "Polygon", "coordinates": [ [ [102, 2], [103, 2], [103, 3], [102, 3], [102, 2] ] ] }, { "type": "MultiPolygon", "coordinates": [ [ [ [102, 2], [103, 2], [103, 3], [102, 3], [102, 2] ] ], [ [ [100, 0], [101, 0], [101, 1], [100, 1], [100, 0] ] ] ]}] } }', NULL);
-- commenting out the polygon with holes for now
-- SELECT documentdb_api.insert_one('db','geo_multi','{ "_id" : 12, "name": "GeometryCollectionAll_Withhole", "geo": { "type": "GeometryCollection", "geometries": [ { "type": "Point", "coordinates": [100, 0] }, { "type": "Point", "coordinates": [102, 2] }, { "type": "MultiPoint", "coordinates": [[100, 0], [101, 1], [102, 2], [103, 3]] }, { "type": "LineString", "coordinates": [[100, 0], [100.5, 0.5], [101, 1]] }, { "type": "LineString", "coordinates": [[102, 2], [102.5, 2.5], [103, 3]] }, { "type": "MultiLineString", "coordinates": [ [[100, 0], [100.5, 0.5], [101, 1]], [[102, 2], [102.5, 2.5], [103, 3]] ] }, { "type": "Polygon", "coordinates": [ [ [100, 0], [101, 0], [101, 1], [100, 1], [100, 0] ] ] }, { "type": "Polygon", "coordinates": [ [ [102, 2], [103, 2], [103, 3], [102, 3], [102, 2] ] ] }, { "type": "Polygon", "coordinates": [ [ [100, 0], [101, 0], [101, 1], [100, 1], [100, 0] ], [ [100.2, 0.2], [100.8, 0.2], [100.8, 0.8], [100.2, 0.8], [100.2, 0.2] ] ] }, { "type": "MultiPolygon", "coordinates": [ [ [ [102, 2], [103, 2], [103, 3], [102, 3], [102, 2] ] ], [ [ [100, 0], [101, 0], [101, 1], [100, 1], [100, 0] ] ] ]}] } }', NULL);
SELECT documentdb_api.insert_one('db','geo_multi','{ "_id" : 13, "name": "Polygon_Exterior", "geo": { "type": "Polygon", "coordinates": [ [ [0, 0], [1, 0], [1, 1], [0, 1], [0, 0] ] ] } }', NULL);

-- Big enough single polygon to match all the documents except Polygon_Exterior
SELECT document FROM documentdb_api.collection('db', 'geo_multi') WHERE document @@
    '{"geo": {"$geoWithin": { "$geometry": { "type": "Polygon", "coordinates": [ [ [100, 0], [103, 0], [103, 3], [100, 3], [100, 0] ] ] } } }}';

-- PolygonA matches PointA, LineStringA, PolygonA, LineStringA
SELECT document FROM documentdb_api.collection('db', 'geo_multi') WHERE document @@
    '{"geo": {"$geoWithin": { "$geometry": { "type": "Polygon", "coordinates": [ [ [100, 0], [101, 0], [101, 1], [100, 1], [100, 0] ] ] } } }}';

-- PolygonB matches PointB, LineStringB, PolygonB,
SELECT document FROM documentdb_api.collection('db', 'geo_multi') WHERE document @@
    '{"geo": {"$geoWithin": { "$geometry": { "type": "Polygon", "coordinates": [ [ [102, 2], [103, 2], [103, 3], [102, 3], [102, 2] ] ] } } }}';

-- MultiPolygonAB creates a covered region that matches all except Polygon_Exterior
SELECT document FROM documentdb_api.collection('db', 'geo_multi') WHERE document @@
    '{"geo": {"$geoWithin": { "$geometry": { "type": "MultiPolygon", "coordinates": [ [ [ [102, 2], [103, 2], [103, 3], [102, 3], [102, 2] ] ], [ [ [100, 0], [101, 0], [101, 1], [100, 1], [100, 0] ] ] ] } } }}';

-- PolygonA_WithHole matches PointA and PolygonA (Bug) and doesn't match itself
-- Limitation1: Matching itself is different behavior in mongo and postgis doesn't behave same out of the box for 2 identical polygons with holes.
-- Limitation2: PolygonA is matched because of this limitation that the outer ring of polygon with hole covers the polygonB and in this case the hole is not considered
SELECT document FROM documentdb_api.collection('db', 'geo_multi') WHERE document @@
    '{"geo": {"$geoWithin": { "$geometry": { "type": "Polygon", "coordinates": [ [ [100, 0], [101, 0], [101, 1], [100, 1], [100, 0] ], [ [100.2, 0.2], [100.8, 0.2], [100.8, 0.8], [100.2, 0.8], [100.2, 0.2] ] ] } } }}';


EXPLAIN (VERBOSE ON, COSTS OFF) SELECT document FROM documentdb_api.collection('db', 'geo_multi') WHERE document @@
    '{"geo": {"$geoWithin": { "$geometry": { "type": "MultiPolygon", "coordinates": [ [ [ [102, 2], [103, 2], [103, 3], [102, 3], [102, 2] ] ], [ [ [100, 0], [101, 0], [101, 1], [100, 1], [100, 0] ] ] ] } } }}';

-- Create Index and should get the same result
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "geo_multi", "indexes": [{"key": {"geo": "2dsphere"}, "name": "my_geo_indx" }]}', true);

BEGIN;
set local enable_seqscan TO off;
set local documentdb.forceUseIndexIfAvailable to on;

-- Big enough single polygon to match all the documents except Polygon_Exterior
SELECT document FROM documentdb_api.collection('db', 'geo_multi') WHERE document @@
    '{"geo": {"$geoWithin": { "$geometry": { "type": "Polygon", "coordinates": [ [ [100, 0], [103, 0], [103, 3], [100, 3], [100, 0] ] ] } } }}';

-- PolygonA matches PointA, LineStringA, PolygonA, LineStringA
SELECT document FROM documentdb_api.collection('db', 'geo_multi') WHERE document @@
    '{"geo": {"$geoWithin": { "$geometry": { "type": "Polygon", "coordinates": [ [ [100, 0], [101, 0], [101, 1], [100, 1], [100, 0] ] ] } } }}';

-- PolygonB matches PointB, LineStringB, PolygonB,
SELECT document FROM documentdb_api.collection('db', 'geo_multi') WHERE document @@
    '{"geo": {"$geoWithin": { "$geometry": { "type": "Polygon", "coordinates": [ [ [102, 2], [103, 2], [103, 3], [102, 3], [102, 2] ] ] } } }}';

-- MultiPolygonAB creates a covered region that matches all except Polygon_Exterior
SELECT document FROM documentdb_api.collection('db', 'geo_multi') WHERE document @@
    '{"geo": {"$geoWithin": { "$geometry": { "type": "MultiPolygon", "coordinates": [ [ [ [102, 2], [103, 2], [103, 3], [102, 3], [102, 2] ] ], [ [ [100, 0], [101, 0], [101, 1], [100, 1], [100, 0] ] ] ] } } }}';


EXPLAIN (VERBOSE ON, COSTS OFF) SELECT document FROM documentdb_api.collection('db', 'geo_multi') WHERE document @@
    '{"geo": {"$geoWithin": { "$geometry": { "type": "MultiPolygon", "coordinates": [ [ [ [102, 2], [103, 2], [103, 3], [102, 3], [102, 2] ] ], [ [ [100, 0], [101, 0], [101, 1], [100, 1], [100, 0] ] ] ] } } }}';
ROLLBACK;

-- Shard the collection
SELECT documentdb_api.shard_collection('db', 'geo_multi', '{"_id": "hashed"}', false);

BEGIN;
set local enable_seqscan TO off;
set local citus.enable_local_execution TO OFF;
set local documentdb.forceUseIndexIfAvailable to on;
SELECT document FROM documentdb_api.collection('db', 'geo_multi') WHERE document @@
    '{"geo": {"$geoWithin": { "$geometry": { "type": "MultiPolygon", "coordinates": [ [ [ [102, 2], [103, 2], [103, 3], [102, 3], [102, 2] ] ], [ [ [100, 0], [101, 0], [101, 1], [100, 1], [100, 0] ] ] ] } } }}' ORDER BY object_id;
EXPLAIN (VERBOSE ON, COSTS OFF) SELECT document FROM documentdb_api.collection('db', 'geo_multi') WHERE document @@
    '{"geo": {"$geoWithin": { "$geometry": { "type": "MultiPolygon", "coordinates": [ [ [ [102, 2], [103, 2], [103, 3], [102, 3], [102, 2] ] ], [ [ [100, 0], [101, 0], [101, 1], [100, 1], [100, 0] ] ] ] } } }}';
ROLLBACK;
