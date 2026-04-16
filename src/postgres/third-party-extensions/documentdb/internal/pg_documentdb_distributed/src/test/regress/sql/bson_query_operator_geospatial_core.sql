-- Insert multiple items
SELECT documentdb_api.insert_one('db','geoquerytest','{ "_id" : 1, "a" : { "b": [10, 10] } }', NULL);
SELECT documentdb_api.insert_one('db','geoquerytest','{ "_id" : 2, "a" : { "b": { "long": { "$numberLong": "20" }, "lat": { "$numberInt": "20" } } } }', NULL);
SELECT documentdb_api.insert_one('db','geoquerytest','{ "_id" : 3, "a" : [ {"b": { "long": 25, "lat": 25 }}, [ { "$numberDouble": "30" }, { "$numberDecimal": "30" }], [35, 35]] }', NULL);
SELECT documentdb_api.insert_one('db','geoquerytest','{ "_id" : 4, "a" : { "b": [ [40, 40], [45, 45], [50, 50], [55, 55] ] } }', NULL);

-- Insert items for $center corner case test
SELECT documentdb_api.insert_one('db','geoquerytest','{ "_id" : 201, "geo" : { "loc": [ 2, 5 ] } }', NULL);
SELECT documentdb_api.insert_one('db','geoquerytest','{ "_id" : 202, "geo" : { "loc": [ 5, 2 ] } }', NULL);
SELECT documentdb_api.insert_one('db','geoquerytest','{ "_id" : 203, "geo" : { "loc": [ 5, 8 ] } }', NULL);
SELECT documentdb_api.insert_one('db','geoquerytest','{ "_id" : 204, "geo" : { "loc": [ 4, 5 ] } }', NULL);
SELECT documentdb_api.insert_one('db','geoquerytest','{ "_id" : 205, "geo" : { "loc": [ 5, 4 ] } }', NULL);
SELECT documentdb_api.insert_one('db','geoquerytest','{ "_id" : 206, "geo" : { "loc": [ 5, 6 ] } }', NULL);
SELECT documentdb_api.insert_one('db','geoquerytest','{ "_id" : 207, "geo" : { "loc": [ 6, 5 ] } }', NULL);
SELECT documentdb_api.insert_one('db','geoquerytest','{ "_id" : 208, "geo" : { "loc": [ 0, 0 ] } }', NULL);
SELECT documentdb_api.insert_one('db','geoquerytest','{ "_id" : 209, "geo" : { "loc": [ 0, 10 ] } }', NULL);

-- $geoWithin / $within => $box shape operator
SELECT document FROM documentdb_api.collection('db', 'geoquerytest') WHERE document @@ '{"a.b": {"$geoWithin": {"$box": [[10, 10], [10, 10]]}}}' ORDER BY object_id; -- boundary check
SELECT document FROM documentdb_api.collection('db', 'geoquerytest') WHERE document @@ '{"a.b": {"$geoWithin": {"$box": [[19.00001, 19.00002], [29.9999, 29.9999]]}}}' ORDER BY object_id;
SELECT document FROM documentdb_api.collection('db', 'geoquerytest') WHERE document @@ '{"a.b": {"$geoWithin": {"$box": [[10, 10], [64, 64]]}}}' ORDER BY object_id;
SELECT document FROM documentdb_api.collection('db', 'geoquerytest') WHERE document @@ '{"a.b": {"$geoWithin": {"$box": [[10, 10], [65, 65]]}}}' ORDER BY object_id;
SELECT document FROM documentdb_api.collection('db', 'geoquerytest') WHERE document @@ '{"a.b": {"$geoWithin": {"$box": {"bottomLeft": {"x": 10, "y": 10}, "bottomRight": {"x": 10, "y": 10}} }}}' ORDER BY object_id;
SELECT document FROM documentdb_api.collection('db', 'geoquerytest') WHERE document @@ '{"a.b": {"$geoWithin": {"$box": {"bottomLeft": {"x": 10, "y": 10, "z": 20}, "bottomRight": {"x": 10, "y": 10, "z": 20 }}}}}' ORDER BY object_id;

SELECT document FROM documentdb_api.collection('db', 'geoquerytest') WHERE document @@ '{"a.b": {"$within": {"$box": [[10, 10], [10, 10]]}}}' ORDER BY object_id; -- boundary check
SELECT document FROM documentdb_api.collection('db', 'geoquerytest') WHERE document @@ '{"a.b": {"$within": {"$box": [[19.00001, 19.00002], [29.9999, 29.9999]]}}}' ORDER BY object_id;
SELECT document FROM documentdb_api.collection('db', 'geoquerytest') WHERE document @@ '{"a.b": {"$within": {"$box": [[10, 10], [64, 64]]}}}' ORDER BY object_id;
SELECT document FROM documentdb_api.collection('db', 'geoquerytest') WHERE document @@ '{"a.b": {"$within": {"$box": [[10, 10], [65, 65]]}}}' ORDER BY object_id;
SELECT document FROM documentdb_api.collection('db', 'geoquerytest') WHERE document @@ '{"a.b": {"$within": {"$box": {"bottomLeft": {"x": 10, "y": 10}, "bottomRight": {"x": 10, "y": 10}} }}}' ORDER BY object_id;
SELECT document FROM documentdb_api.collection('db', 'geoquerytest') WHERE document @@ '{"a.b": {"$within": {"$box": {"bottomLeft": {"x": 10, "y": 10, "z": 20}, "bottomRight": {"x": 10, "y": 10, "z": 20 }}}}}' ORDER BY object_id;

-- $geoWithin / $within => $center shape operator
SELECT document FROM documentdb_api.collection('db', 'geoquerytest') WHERE document @@ '{"a.b": {"$geoWithin": {"$center": [[5, 10], 5]}}}' ORDER BY object_id; -- boundary check
SELECT document FROM documentdb_api.collection('db', 'geoquerytest') WHERE document @@ '{"a.b": {"$geoWithin": {"$center": [[15, 14], 10]}}}' ORDER BY object_id;
SELECT document FROM documentdb_api.collection('db', 'geoquerytest') WHERE document @@ '{"a.b": {"$geoWithin": {"$center": [[50, 50], 50]}}}' ORDER BY object_id; -- This will not include [10, 10]
SELECT document FROM documentdb_api.collection('db', 'geoquerytest') WHERE document @@ '{"a.b": {"$geoWithin": {"$center": [[50, 50], 60]}}}' ORDER BY object_id; -- The radius is enough to include [10, 10] now
SELECT document FROM documentdb_api.collection('db', 'geoquerytest') WHERE document @@ '{"a.b": {"$geoWithin": {"$center": [ { "x": 5, "y": 10 }, 5]}}}' ORDER BY object_id;

SELECT document FROM documentdb_api.collection('db', 'geoquerytest') WHERE document @@ '{"a.b": {"$within": {"$center": [[5, 10], 5]}}}' ORDER BY object_id; -- boundary check
SELECT document FROM documentdb_api.collection('db', 'geoquerytest') WHERE document @@ '{"a.b": {"$within": {"$center": [[15, 14], 10]}}}' ORDER BY object_id;
SELECT document FROM documentdb_api.collection('db', 'geoquerytest') WHERE document @@ '{"a.b": {"$within": {"$center": [[50, 50], 50]}}}' ORDER BY object_id; -- This will not include [10, 10]
SELECT document FROM documentdb_api.collection('db', 'geoquerytest') WHERE document @@ '{"a.b": {"$within": {"$center": [[50, 50], 60]}}}' ORDER BY object_id; -- The radius is enough to include [10, 10] now
SELECT document FROM documentdb_api.collection('db', 'geoquerytest') WHERE document @@ '{"a.b": {"$within": {"$center": [ { "x": 5, "y": 10 }, 5]}}}' ORDER BY object_id;
SELECT document FROM documentdb_api.collection('db', 'geoquerytest') WHERE document @@ '{"a.b": {"$within": {"$center": [[0,0], { "$numberDecimal" : "Infinity" }]}}}' ORDER BY object_id; -- infinite radius

SELECT document FROM documentdb_api.collection('db', 'geoquerytest') WHERE document @@ '{"geo.loc": {"$within": {"$center": [[5, 5], 3]}}}' ORDER BY object_id; -- _id 201,202,203 on boundary
SELECT document FROM documentdb_api.collection('db', 'geoquerytest') WHERE document @@ '{"geo.loc": {"$within": {"$center": [[5, 5], 1]}}}' ORDER BY object_id; -- _id 204,205,206 on boundary
SELECT document FROM documentdb_api.collection('db', 'geoquerytest') WHERE document @@ '{"geo.loc": {"$within": {"$center": [[0, 5], 5]}}}' ORDER BY object_id; -- _id 208,209 on boundary

-- $geoWithin / $within => $polygon shape operator
SELECT document FROM documentdb_api.collection('db', 'geoquerytest') WHERE document @@ '{"a.b": {"$geoWithin": {"$polygon": [[5, 10], [11, 11], [12, 5]]}}}' ORDER BY object_id; -- [10, 10] included
SELECT document FROM documentdb_api.collection('db', 'geoquerytest') WHERE document @@ '{"a.b": {"$geoWithin": {"$polygon": [[10, 10], [10, 10], [10, 10]]}}}' ORDER BY object_id; -- boundary check
SELECT document FROM documentdb_api.collection('db', 'geoquerytest') WHERE document @@ '{"a.b": {"$geoWithin": {"$polygon": [[10, 10], [10, 12], [11, 11], [9, 11]]}}}' ORDER BY object_id; -- Invalid polygon with self intersection
SELECT document FROM documentdb_api.collection('db', 'geoquerytest') WHERE document @@ '{"a.b": {"$geoWithin": {"$polygon": [[10, 10], [20, 20], [26, 26], [25, 12]]}}}' ORDER BY object_id;
SELECT document FROM documentdb_api.collection('db', 'geoquerytest') WHERE document @@ '{"a.b": {"$geoWithin": {"$polygon": [[10, 10], [20, 20], [26, 26], [25, 12]]}}}' ORDER BY object_id;
SELECT document FROM documentdb_api.collection('db', 'geoquerytest') WHERE document @@ '{"a.b": {"$geoWithin": {"$polygon": [[10, 10], [20, 20], [26, 26], [25, 12]]}}}' ORDER BY object_id;
SELECT document FROM documentdb_api.collection('db', 'geoquerytest') WHERE document @@ '{"a.b": {"$geoWithin": {"$polygon": [[10, 10], [80, 80], [60, 60], [20, 20]]}}}' ORDER BY object_id;
SELECT document FROM documentdb_api.collection('db', 'geoquerytest') WHERE document @@ '{"a.b": {"$geoWithin": {"$polygon": [[5, 10], {"x": 11, "y": 11}, [12, 5]]}}}' ORDER BY object_id;

SELECT document FROM documentdb_api.collection('db', 'geoquerytest') WHERE document @@ '{"a.b": {"$within": {"$polygon": [[5, 10], [11, 11], [12, 5]]}}}' ORDER BY object_id; -- [10, 10] included
SELECT document FROM documentdb_api.collection('db', 'geoquerytest') WHERE document @@ '{"a.b": {"$within": {"$polygon": [[10, 10], [10, 10], [10, 10]]}}}' ORDER BY object_id; -- boundary check
SELECT document FROM documentdb_api.collection('db', 'geoquerytest') WHERE document @@ '{"a.b": {"$within": {"$polygon": [[10, 10], [10, 12], [11, 11], [9, 11]]}}}' ORDER BY object_id; -- Invalid polygon with self intersection
SELECT document FROM documentdb_api.collection('db', 'geoquerytest') WHERE document @@ '{"a.b": {"$within": {"$polygon": [[10, 10], [20, 20], [26, 26], [25, 12]]}}}' ORDER BY object_id;
SELECT document FROM documentdb_api.collection('db', 'geoquerytest') WHERE document @@ '{"a.b": {"$within": {"$polygon": [[10, 10], [20, 20], [26, 26], [25, 12]]}}}' ORDER BY object_id;
SELECT document FROM documentdb_api.collection('db', 'geoquerytest') WHERE document @@ '{"a.b": {"$within": {"$polygon": [[10, 10], [20, 20], [26, 26], [25, 12]]}}}' ORDER BY object_id;
SELECT document FROM documentdb_api.collection('db', 'geoquerytest') WHERE document @@ '{"a.b": {"$within": {"$polygon": [[10, 10], [80, 80], [60, 60], [20, 20]]}}}' ORDER BY object_id;
SELECT document FROM documentdb_api.collection('db', 'geoquerytest') WHERE document @@ '{"a.b": {"$within": {"$polygon": [[5, 10], {"x": 11, "y": 11}, [12, 5]]}}}' ORDER BY object_id;

-- Validate non-geo query operator are not pushed to geo indexes but regular indexes and works fine
SELECT document FROM documentdb_api.collection('db', 'geoquerytest') WHERE document @@ '{"a.b": {"$eq": [10, 10]}}' ORDER BY object_id;

-- Test valid polygons in query
SELECT document FROM documentdb_api.collection('db', 'geoquerytest') WHERE document @@ '{"a.b": {"$geoIntersects": { "$geometry": { "type": "Polygon", "coordinates": [[[0.0, 0.0], [60.0, 0.0], [90.0, 0.0], [0.0, 0.0]]]}}}}'; -- Polygon with points on a great circle of earth
SELECT document FROM documentdb_api.collection('db', 'geoquerytest') WHERE document @@ '{"a.b": {"$geoIntersects": { "$geometry": { "type": "Polygon", "coordinates": [[[0, 0], [0 ,4], [0, 6], [0, 8], [0, 0]]]}}}}'; -- Polygon with points on the same longitude
SELECT document FROM documentdb_api.collection('db', 'geoquerytest') WHERE document @@ '{"a.b": {"$geoIntersects": { "$geometry": { "type": "Polygon", "coordinates": [[[-120, 60.0], [0.0, 60.0], [60.0, 60.0], [90.0, 60.0], [-120.0, 60.0]]]}}}}'; -- Polygon with points on a single latitude, appear as straight line in 2d
SELECT document FROM documentdb_api.collection('db', 'geoquerytest') WHERE document @@ '{"a.b": {"$geoIntersects": { "$geometry": { "type": "Polygon", "coordinates": [[[-120.0, 89], [-60, 89], [0, 89], [120, 89], [-120.0, 89]]]}}}}'; -- Polygon with points on a single latitude close to north pole
SELECT document FROM documentdb_api.collection('db', 'geoquerytest') WHERE document @@ '{"a.b": {"$geoIntersects": { "$geometry": { "type": "Polygon", "coordinates": [[[-120.0, 60], [-60, 60], [0, 60], [120, 60], [-120.0, 60]], [[-120.0, 70], [-60, 70], [0, 70], [120, 70], [-120.0, 70]]]}}}}'; -- Polygon with points on a single latitude, points going around the earth with similar hole

-- Special case for array of legacy pairs
SELECT documentdb_api.insert_one('db','geoquerytest','{ "_id" : 5, "c" : { "d": [ [10, 10], [100, 100] ] } }', NULL);
SELECT documentdb_api.insert_one('db','geoquerytest','{ "_id" : 6, "c" : { "d": [ [100, 100], [10, 10] ] } }', NULL);
SELECT documentdb_api.insert_one('db','geoquerytest','{ "_id" : 7, "c" : [{ "d": [100, 100]}, { "d": [10, 10]}] }', NULL);
SELECT documentdb_api.insert_one('db','geoquerytest','{ "_id" : 8, "c" : [{ "d": [10, 10]}, { "d": [100, 100]}] }', NULL);

-- Test that no matter the order of legacy pairs in the array, if 1 matches the document is returned
SELECT document FROM documentdb_api.collection('db', 'geoquerytest') WHERE document @@ '{"c.d": {"$geoWithin": {"$center": [[0, 0], 20]}}}' ORDER BY object_id;
SELECT document FROM documentdb_api.collection('db', 'geoquerytest') WHERE document @@ '{"c.d": {"$geoWithin": {"$center": [[90, 90], 20]}}}' ORDER BY object_id;