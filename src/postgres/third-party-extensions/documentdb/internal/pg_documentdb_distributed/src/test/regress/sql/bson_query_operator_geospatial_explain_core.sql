-- Insert multiple items - 2d index
SELECT documentdb_api.insert_one('db','geoquerytest','{ "_id" : 1, "a" : { "b": [10, 10] } }', NULL);
SELECT documentdb_api.insert_one('db','geoquerytest','{ "_id" : 2, "a" : { "b": { "long": { "$numberLong": "20" }, "lat": { "$numberInt": "20" } } } }', NULL);
SELECT documentdb_api.insert_one('db','geoquerytest','{ "_id" : 3, "a" : [ {"b": { "long": 25, "lat": 25 }}, [ { "$numberDouble": "30" }, { "$numberDecimal": "30" }], [35, 35]] }', NULL);
SELECT documentdb_api.insert_one('db','geoquerytest','{ "_id" : 4, "a" : { "b": [ [40, 40], [45, 45], [50, 50], [55, 55] ] } }', NULL);

-- Insert multiple items - 2dsphere index
SELECT documentdb_api.insert_one('db','geoquerytest','{ "_id" : 101, "geo" : { "loc": [2.1, 2.2] } }', NULL);
SELECT documentdb_api.insert_one('db','geoquerytest','{ "_id" : 102, "geo" : [ { "loc": [3.1, 3.2] }, { "loc": [4.1, 4.2] } ] }', NULL);
SELECT documentdb_api.insert_one('db','geoquerytest','{ "_id" : 103, "geo" : { "loc": {"type": "Point", "coordinates": [5.1, 5.2] } } }', NULL);
SELECT documentdb_api.insert_one('db','geoquerytest','{ "_id" : 104, "geo" : [ { "loc": {"type": "Point", "coordinates": [6.1, 6.2] } }, { "loc": { "type": "Point", "coordinates": [6.3, 6.4]} }] }', NULL);
SELECT documentdb_api.insert_one('db','geoquerytest','{ "_id" : 105, "geo" : { "loc": { "type": "MultiPoint", "coordinates": [[7.1, 7.2], [7.3, 7.4]] } } }', NULL);
SELECT documentdb_api.insert_one('db','geoquerytest','{ "_id" : 106, "geo" : { "loc": { "type": "LineString", "coordinates": [[8.1, 8.2], [8.3, 8.4]] } } }', NULL);
SELECT documentdb_api.insert_one('db','geoquerytest','{ "_id" : 107, "geo" : { "loc": { "type": "Polygon", "coordinates": [[[10, 10], [10, -10], [-10, -10], [-10, 10], [10, 10]]] } } }', NULL);
-- SELECT documentdb_api.insert_one('db','geoquerytest','{ "_id" : 108, "geo" : { "loc": { "type": "Polygon", "coordinates": [[[10, 10], [10, -10], [-10, -10], [-10, 10], [10, 10]], [[5, 5], [5, -5], [-5, -5], [-5, 5], [5, 5]]] } } }', NULL);
SELECT documentdb_api.insert_one('db','geoquerytest','{ "_id" : 109, "geo" : { "loc": { "type": "MultiLineString", "coordinates": [[[10, 10], [10, -10], [-10, -10], [-10, 10], [10, 10]], [[5, 5], [5, -5], [-5, -5], [-5, 5], [5, 5]]] } } }', NULL);
-- SELECT documentdb_api.insert_one('db','geoquerytest','{ "_id" : 110, "geo" : { "loc": { "type": "MultiPolygon", "coordinates": [[[[10, 10], [10, -10], [-10, -10], [-10, 10], [10, 10]], [[5, 5], [5, -5], [-5, -5], [-5, 5], [5, 5]]]] } } }', NULL);
SELECT documentdb_api.insert_one('db','geoquerytest','{ "_id" : 111, "geo" : { "loc": { "type": "GeometryCollection", "geometries": [ {"type": "Point", "coordinates": [11.1, 11.2]}, {"type": "LineString", "coordinates": [[11.3, 11.4], [11.5, 11.6]]}, {"type": "Polygon", "coordinates": [[[5, 5], [5, -5], [-5, -5], [-5, 5], [5, 5]]]}  ] } } }', NULL);
SELECT documentdb_api.insert_one('db','geoquerytest','{ "_id" : 112, "geo" : { "loc": [120, 89.1 ] } }', NULL);
SELECT documentdb_api.insert_one('db','geoquerytest','{ "_id" : 113, "geo" : { "loc": { "x": 50, "y": 50, "z": 51 } } }', NULL); -- Legacy object format
SELECT documentdb_api.insert_one('db','geoquerytest','{ "_id" : 114, "geo" : { "loc": [51, 51, 52] } }', NULL); -- Legacy array format

-- Insert couple of items for testing index usage with partial filter expression
SELECT documentdb_api.insert_one('db','geoquerytest','{ "_id" : 5, "d" : [15, 15], "e": 1 }', NULL);
SELECT documentdb_api.insert_one('db','geoquerytest','{ "_id" : 6, "d" : [16, 16], "e": 2 }', NULL);
SELECT documentdb_api.insert_one('db','geoquerytest','{ "_id" : 7, "d" : [17, 17], "e": 3 }', NULL);
SELECT documentdb_api.insert_one('db','geoquerytest','{ "_id" : 8, "d" : [18, 18], "e": 4 }', NULL);
SELECT documentdb_api.insert_one('db','geoquerytest','{ "_id" : 9, "d" : [19, 19], "e": 5 }', NULL);
SELECT documentdb_api.insert_one('db','geoquerytest','{ "_id" : 10, "d" : [20, 20], "e": 6 }', NULL);
SELECT documentdb_api.insert_one('db','geoquerytest','{ "_id" : 11, "d" : [21, 21], "e": 7 }', NULL);
SELECT documentdb_api.insert_one('db','geoquerytest','{ "_id" : 12, "d" : [22, 22], "e": 8 }', NULL);
SELECT documentdb_api.insert_one('db','geoquerytest','{ "_id" : 13, "d" : [23, 23], "e": 9 }', NULL);
SELECT documentdb_api.insert_one('db','geoquerytest','{ "_id" : 14, "d" : [24, 24], "e": 10 }', NULL);

-- Insert couple of items for composite 2dsphere index
SELECT documentdb_api.insert_one('db','geoquerytest','{ "_id" : 200, "geoA" : { "type" : "Point", "coordinates": [ 10, 10] }, "geoB": { "type": "Point", "coordinates": [20, 20] } }', NULL); -- Both geoA and geoB
SELECT documentdb_api.insert_one('db','geoquerytest','{ "_id" : 201, "geoA" : { "type" : "Point", "coordinates": [ 11, 11] } }', NULL); -- Only geoA
SELECT documentdb_api.insert_one('db','geoquerytest','{ "_id" : 202, "geoB" : { "type" : "Point", "coordinates": [ 21, 21] } }', NULL); -- Only geoB
SELECT documentdb_api.insert_one('db','geoquerytest','{ "_id" : 203, "geoC" : { "type" : "Point", "coordinates": [ 30, 30] } }', NULL); -- No geoA, no geoB

-- Insert some items for composited 2dsphere index with pfe
SELECT documentdb_api.insert_one('db','geoquerytest','{ "_id" : 300, "geo1" : { "type" : "Point", "coordinates": [ 10, 10] }, "geo2": { "type": "Point", "coordinates": [20, 20] }, "region": "USA" }', NULL); -- Both geo1 and geo2
SELECT documentdb_api.insert_one('db','geoquerytest','{ "_id" : 301, "geo1" : { "type" : "Point", "coordinates": [ 11, 11] }, "region": "USA" }', NULL); -- Only geo1
SELECT documentdb_api.insert_one('db','geoquerytest','{ "_id" : 302, "geo2" : { "type" : "Point", "coordinates": [ 21, 21] }, "region": "USA" }', NULL); -- Only geo2
SELECT documentdb_api.insert_one('db','geoquerytest','{ "_id" : 303, "geo3" : { "type" : "Point", "coordinates": [ 30, 30] }, "region": "USA" }', NULL); -- No geo1, no geo2

-- Insert for large query cap containing all of shapes vertices but not the shape itself.
SELECT documentdb_api.insert_one('db','geoquerytest','{ "_id" : 500, "largeGeo" : { "type" : "LineString", "coordinates": [[96.328125, 5.61598581915534], [153.984375, -6.315298538330033]] }}', NULL);
SELECT documentdb_api.insert_one('db','geoquerytest','{ "_id" : 501, "largeGeo" : { "type" : "Polygon", "coordinates": [[
                [98.96484375, -11.350796722383672],
                [135.35156249999997, -11.350796722383672],
                [135.35156249999997, 0.8788717828324276],
                [98.96484375, 0.8788717828324276],
                [98.96484375, -11.350796722383672]
            ]] }}', NULL);

-- 2d index explain
-- $geoWithin / $within => $box shape operator
EXPLAIN (COSTS OFF) SELECT document FROM documentdb_api.collection('db', 'geoquerytest') WHERE document @@ '{"a.b": {"$geoWithin": {"$box": [[10, 10], [10, 10]]}}}';
EXPLAIN (COSTS OFF) SELECT document FROM documentdb_api.collection('db', 'geoquerytest') WHERE document @@ '{"a.b": {"$within": {"$box": [[10, 10], [10, 10]]}}}';

-- $geoWithin / $within => Hashes of $center/$centerSphere may vary so skipping those here to avoid flakiness
-- $geoWithin / $within => $polygon shape operator
EXPLAIN (COSTS OFF) SELECT document FROM documentdb_api.collection('db', 'geoquerytest') WHERE document @@ '{"a.b": {"$geoWithin": {"$polygon": [[5, 10], [11, 11], [12, 5]]}}}';
EXPLAIN (COSTS OFF) SELECT document FROM documentdb_api.collection('db', 'geoquerytest') WHERE document @@ '{"a.b": {"$within": {"$polygon": [[5, 10], [11, 11], [12, 5]]}}}';

-- Explain plan with partial filters
EXPLAIN (COSTS OFF) SELECT document FROM documentdb_api.collection('db', 'geoquerytest') WHERE document @@ '{"d": {"$geoWithin": {"$box": [[10, 10], [30, 30]]}}, "e": {"$gte": 5}}';
EXPLAIN (COSTS OFF) SELECT document FROM documentdb_api.collection('db', 'geoquerytest') WHERE document @@ '{"d": {"$geoWithin": {"$box": [[10, 10], [30, 30]]}}, "e": {"$eq": 5}}';
EXPLAIN (COSTS OFF) SELECT document FROM documentdb_api.collection('db', 'geoquerytest') WHERE document @@ '{"d": {"$geoWithin": {"$box": [[10, 10], [30, 30]]}}, "e": {"$eq": 3}}';

-- Check Explain for other non-geo operators are not pushed to geo index but regular index if available
EXPLAIN (COSTS OFF) SELECT document FROM documentdb_api.collection('db', 'geoquerytest') WHERE document @@ '{"a.b": {"$eq": [10, 10]}}';

-- 2dSphere index explains
-- $geoIntersects explain and index pushdown on 2dsphere
EXPLAIN (COSTS OFF) SELECT document FROM documentdb_api.collection('db', 'geoquerytest') WHERE document @@ '{"geo.loc": {"$geoIntersects": {"$geometry": { "type": "MultiPoint", "coordinates": [[1.1, 1.2], [2.1, 2.2], [3.1, 3.2]] } }}}';
EXPLAIN (COSTS OFF) SELECT document FROM documentdb_api.collection('db', 'geoquerytest') WHERE document @@ '{"geo.loc": {"$geoIntersects": {"$geometry": { "type": "Point", "coordinates": {"lon": 3.1, "lat": 3.2} } }}}';
EXPLAIN (COSTS OFF) SELECT document FROM documentdb_api.collection('db', 'geoquerytest') WHERE document @@ '{"geo.loc": {"$geoIntersects": {"$geometry": { "type": "MultiPoint", "coordinates": [[7.1, 7.2], [4.1, 4.2]] } }}}';
EXPLAIN (COSTS OFF) SELECT document FROM documentdb_api.collection('db', 'geoquerytest') WHERE document @@ '{"geo.loc": {"$geoIntersects": {"$geometry": { "type": "LineString", "coordinates": [[7.1, 7.2], [4.1, 4.2], [7.3, 7.4], [2.1, 2.2]] } }}}';
EXPLAIN (COSTS OFF) SELECT document FROM documentdb_api.collection('db', 'geoquerytest') WHERE document @@ '{"geo.loc": {"$geoIntersects": {"$geometry": { "type": "Polygon", "coordinates": [[ [0, 0], [0, 10], [10, 10], [10, 0], [0, 0] ]] } }}}';
EXPLAIN (COSTS OFF) SELECT document FROM documentdb_api.collection('db', 'geoquerytest') WHERE document @@ '{"geo.loc": {"$geoIntersects": {"$geometry": { "type": "Polygon", "coordinates": [[ [0, 0], [0, 10], [10, 10], [10, 0], [0, 0] ], [ [4, 4], [4, 7], [7, 7], [7, 4], [4, 4] ]] } }}}';
EXPLAIN (COSTS OFF) SELECT document FROM documentdb_api.collection('db', 'geoquerytest') WHERE document @@ '{"geo.loc": {"$geoIntersects": {"$geometry": { "type": "LineString", "coordinates": [ [25, 25], [-25, -25] ] } }}}';
EXPLAIN (COSTS OFF) SELECT document FROM documentdb_api.collection('db', 'geoquerytest') WHERE document @@ '{"geo.loc": {"$geoIntersects": {"$geometry": { "type": "GeometryCollection", "geometries": [ {"type": "LineString", "coordinates": [[-25, -25], [25, 25]]}, {"type": "Point", "coordinates": [2.1, 2.2]} ] } }}}';
EXPLAIN (COSTS OFF) SELECT document FROM documentdb_api.collection('db', 'geoquerytest') WHERE document @@ '{"geo.loc": {"$geoIntersects": {"$geometry": { "type": "Polygon", "coordinates": [[[-120.0, 89.0], [0.0, 89.0], [120.0, 89.0], [-120.0, 89.0]]] } }}}';

-- With pfe
EXPLAIN (COSTS OFF) SELECT document FROM documentdb_api.collection('db', 'geoquerytest') WHERE document @@ '{"d": {"$geoIntersects": {"$geometry": {"type": "Polygon", "coordinates": [[[10, 10], [10, 30], [30, 30], [30, 10], [10, 10]]]} }}, "e": {"$gte": 5}}';
EXPLAIN (COSTS OFF) SELECT document FROM documentdb_api.collection('db', 'geoquerytest') WHERE document @@ '{"d": {"$geoIntersects": {"$geometry": {"type": "Polygon", "coordinates": [[[10, 10], [10, 30], [30, 30], [30, 10], [10, 10]]]} }}, "e": {"$eq": 5}}';
EXPLAIN (COSTS OFF) SELECT document FROM documentdb_api.collection('db', 'geoquerytest') WHERE document @@ '{"d": {"$geoIntersects": {"$geometry": {"type": "Polygon", "coordinates": [[[10, 10], [10, 30], [30, 30], [30, 10], [10, 10]]]} }}, "e": {"$eq": 3}}';

-- Check Explain for other non-geo operators are not pushed to geo index but regular index if available
EXPLAIN (COSTS OFF) SELECT document FROM documentdb_api.collection('db', 'geoquerytest') WHERE document @@ '{"geo.loc": {"$eq": [2.1, 2.2]}}';

-- $geoWithin explain and index pushdown on 2dsphere
EXPLAIN (COSTS OFF) SELECT document FROM documentdb_api.collection('db', 'geoquerytest') WHERE document @@ '{"geo.loc": {"$geoWithin": {"$geometry": { "type": "Polygon", "coordinates": [[ [0, 0], [0, 10], [10, 10], [10, 0], [0, 0] ]] } }}}';
-- EXPLAIN (COSTS OFF) SELECT document FROM documentdb_api.collection('db', 'geoquerytest') WHERE document @@ '{"geo.loc": {"$geoWithin": {"$geometry": { "type": "Polygon", "coordinates": [[ [0, 0], [0, 10], [10, 10], [10, 0], [0, 0] ], [ [4, 4], [4, 7], [7, 7], [7, 4], [4, 4] ]] } }}}';
EXPLAIN (COSTS OFF) SELECT document FROM documentdb_api.collection('db', 'geoquerytest') WHERE document @@ '{"geo.loc": {"$geoWithin": {"$geometry": { "type": "Polygon", "coordinates": [[[-120.0, 89.0], [0.0, 89.0], [120.0, 89.0], [-120.0, 89.0]]] } }}}';

-- $geometry with legacy formats
EXPLAIN (COSTS OFF) SELECT document FROM documentdb_api.collection('db', 'geoquerytest') WHERE document @@ '{"geo.loc": {"$geoIntersects": {"$geometry": { "lon": 50, "lat": 50 } }}}';
EXPLAIN (COSTS OFF) SELECT document FROM documentdb_api.collection('db', 'geoquerytest') WHERE document @@ '{"geo.loc": {"$geoIntersects": {"$geometry": [51, 51] }}}';


-- Testing composite 2dsphere index
EXPLAIN (COSTS OFF) SELECT document FROM documentdb_api.collection('db', 'geoquerytest') WHERE document @@ '{"geoA": {"$geoIntersects": {"$geometry": { "type": "Polygon", "coordinates": [[[0, 0], [0, 50], [50, 50], [50, 0], [0, 0]]]} }}}';
EXPLAIN (COSTS OFF) SELECT document FROM documentdb_api.collection('db', 'geoquerytest') WHERE document @@ '{"geoB": {"$geoIntersects": {"$geometry": { "type": "Polygon", "coordinates": [[[0, 0], [0, 50], [50, 50], [50, 0], [0, 0]]]} }}}';
EXPLAIN (COSTS OFF) SELECT document FROM documentdb_api.collection('db', 'geoquerytest') 
    WHERE document @@ '{"geoA": {"$geoIntersects": {"$geometry": { "type": "Polygon", "coordinates": [[[0, 0], [0, 50], [50, 50], [50, 0], [0, 0]]]} }}, 
                         "geoB": {"$geoIntersects": {"$geometry": { "type": "Polygon", "coordinates": [[[0, 0], [0, 50], [50, 50], [50, 0], [0, 0]]]} }}}';

-- Testing composite 2dsphere with pfe
EXPLAIN (COSTS OFF) SELECT document FROM documentdb_api.collection('db', 'geoquerytest') WHERE document @@ '{"geo1": {"$geoIntersects": {"$geometry": { "type": "Polygon", "coordinates": [[[0, 0], [0, 50], [50, 50], [50, 0], [0, 0]]]} }}, "region": {"$eq": "USA" }}' ORDER BY object_id;
EXPLAIN (COSTS OFF) SELECT document FROM documentdb_api.collection('db', 'geoquerytest') WHERE document @@ '{"geo2": {"$geoIntersects": {"$geometry": { "type": "Polygon", "coordinates": [[[0, 0], [0, 50], [50, 50], [50, 0], [0, 0]]]} }}, "region": {"$eq": "USA" }}' ORDER BY object_id;
EXPLAIN (COSTS OFF) SELECT document FROM documentdb_api.collection('db', 'geoquerytest') 
    WHERE document @@ '{"geo1": {"$geoIntersects": {"$geometry": { "type": "Polygon", "coordinates": [[[0, 0], [0, 50], [50, 50], [50, 0], [0, 0]]]} }}, 
                        "geo2": {"$geoIntersects": {"$geometry": { "type": "Polygon", "coordinates": [[[0, 0], [0, 50], [50, 50], [50, 0], [0, 0]]]} }},
                        "region": {"$eq": "USA" }}' ORDER BY object_id;
EXPLAIN (COSTS OFF) SELECT document FROM documentdb_api.collection('db', 'geoquerytest') 
    WHERE document @@ '{"geo1": {"$geoIntersects": {"$geometry": { "type": "Polygon", "coordinates": [[[0, 0], [0, 50], [50, 50], [50, 0], [0, 0]]]} }}, 
                        "geo2": {"$geoIntersects": {"$geometry": { "type": "Polygon", "coordinates": [[[0, 0], [0, 50], [50, 50], [50, 0], [0, 0]]]} }}}' ORDER BY object_id; -- This one should not use index but should return same values
