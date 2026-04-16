-- Insert multiple items
-- Old format legacy points are valid for geography
SELECT documentdb_api.insert_one('db','geoquerytest','{ "_id" : 1, "geo" : { "loc": [2.1, 2.2] } }', NULL);
SELECT documentdb_api.insert_one('db','geoquerytest','{ "_id" : 2, "geo" : [ { "loc": [3.1, 3.2] }, { "loc": [4.1, 4.2] } ] }', NULL);
SELECT documentdb_api.insert_one('db','geoquerytest','{ "_id" : 3, "geo" : { "loc": {"type": "Point", "coordinates": [5.1, 5.2] } } }', NULL);
SELECT documentdb_api.insert_one('db','geoquerytest','{ "_id" : 4, "geo" : [ { "loc": {"type": "Point", "coordinates": [6.1, 6.2] } }, { "loc": { "type": "Point", "coordinates": [6.3, 6.4]} }] }', NULL);
SELECT documentdb_api.insert_one('db','geoquerytest','{ "_id" : 5, "geo" : { "loc": { "type": "MultiPoint", "coordinates": [[7.1, 7.2], [7.3, 7.4]] } } }', NULL);
SELECT documentdb_api.insert_one('db','geoquerytest','{ "_id" : 6, "geo" : { "loc": { "type": "LineString", "coordinates": [[8.1, 8.2], [8.3, 8.4]] } } }', NULL);
SELECT documentdb_api.insert_one('db','geoquerytest','{ "_id" : 7, "geo" : { "loc": { "type": "Polygon", "coordinates": [[[10, 10], [10, -10], [-10, -10], [-10, 10], [10, 10]]] } } }', NULL);
-- SELECT documentdb_api.insert_one('db','geoquerytest','{ "_id" : 8, "geo" : { "loc": { "type": "Polygon", "coordinates": [[[10, 10], [10, -10], [-10, -10], [-10, 10], [10, 10]], [[5, 5], [5, -5], [-5, -5], [-5, 5], [5, 5]]] } } }', NULL);
SELECT documentdb_api.insert_one('db','geoquerytest','{ "_id" : 9, "geo" : { "loc": { "type": "MultiLineString", "coordinates": [[[10, 10], [10, -10], [-10, -10], [-10, 10], [10, 10]], [[5, 5], [5, -5], [-5, -5], [-5, 5], [5, 5]]] } } }', NULL);
SELECT documentdb_api.insert_one('db','geoquerytest','{ "_id" : 10, "geo" : { "loc": { "type": "MultiPolygon", "coordinates": [[[[10, 10], [10, -10], [-10, -10], [-10, 10], [10, 10]], [[5, 5], [5, -5], [-5, -5], [-5, 5], [5, 5]]]] } } }', NULL);
SELECT documentdb_api.insert_one('db','geoquerytest','{ "_id" : 11, "geo" : { "loc": { "type": "GeometryCollection", "geometries": [ {"type": "Point", "coordinates": [11.1, 11.2]}, {"type": "LineString", "coordinates": [[11.3, 11.4], [11.5, 11.6]]}, {"type": "Polygon", "coordinates": [[[5, 5], [5, -5], [-5, -5], [-5, 5], [5, 5]]]}  ] } } }', NULL);
SELECT documentdb_api.insert_one('db','geoquerytest','{ "_id" : 12, "geo" : { "loc": [120, 89.1 ] } }', NULL);
SELECT documentdb_api.insert_one('db','geoquerytest','{ "_id" : 13, "geo" : { "loc": { "x": 50, "y": 50, "z": 51 } } }', NULL); -- Legacy object format
SELECT documentdb_api.insert_one('db','geoquerytest','{ "_id" : 14, "geo" : { "loc": [51, 51, 52] } }', NULL); -- Legacy array format
SELECT documentdb_api.insert_one('db','geoquerytest','{ "_id" : 15, "geo" : { "loc": [ -46.67527188, -23.60076866 ] } }', NULL);
SELECT documentdb_api.insert_one('db','geoquerytest','{ "_id" : 16, "geo" : { "loc": [ -46.67354611, -23.60337759 ] } }', NULL);
SELECT documentdb_api.insert_one('db','geoquerytest','{ "_id" : 50, "a" : { "b": [ [40, 40], [45, 45], [50, 50], [55, 55] ] } }', NULL);
SELECT documentdb_api.insert_one('db','geoquerytest','{ "_id" : 51, "a" : { "b": [ { "x" : 1, "y" : 1 }, { "x" : 2, "y" : 2 }, { "x" : 3, "y" : 3 } ] } }', NULL);
SELECT documentdb_api.insert_one('db','geoquerytest','{ "_id" : 52, "a" : { "b": [ ] } }', NULL);
SELECT documentdb_api.insert_one('db','geoquerytest','{ "_id" : 53, "a" : { "b": { } } }', NULL);
SELECT documentdb_api.insert_one('db','geoquerytest','{ "_id" : 54, "a" : { "b": [ [], [] ] } }', NULL);
SELECT documentdb_api.insert_one('db','geoquerytest','{ "_id" : 55, "a" : { "b": [ [10, 10], [120, 80] ] } }', NULL);
SELECT documentdb_api.insert_one('db','geoquerytest','{ "_id" : 56, "a" : { "b": [ [120, 80], [10, 10] ] } }', NULL);
SELECT documentdb_api.insert_one('db','geoquerytest','{ "_id" : 57, "a" : [{ "b": [120, 80]}, { "b": [10, 10]}] }', NULL);
SELECT documentdb_api.insert_one('db','geoquerytest','{ "_id" : 58, "a" : [{ "b": [10, 10]}, { "b": [120, 80]}] }', NULL);

-- for $centerSphere edge test
SELECT documentdb_api.insert_one('db','geoquerytest','{ "_id" : 17, "geo" : { "loc": [4.9757, 2.66966] } }', NULL);
SELECT documentdb_api.insert_one('db','geoquerytest','{ "_id" : 18, "geo" : { "loc": [-4.9757, -2.66966] } }', NULL);
SELECT documentdb_api.insert_one('db','geoquerytest','{ "_id" : 19, "geoPrec" : { "loc": [1, 0], "d": {"$numberDouble": "0.017453292519943295"} } }', NULL);
SELECT documentdb_api.insert_one('db','geoquerytest','{ "_id" : 20, "geoPrec" : { "loc": [-1, 0], "d": {"$numberDouble": "0.017453292519943295"} } }', NULL);
SELECT documentdb_api.insert_one('db','geoquerytest','{ "_id" : 21, "geoPrec" : { "loc": [0, 1], "d": {"$numberDouble": "0.017453292519943295"} } }', NULL);
SELECT documentdb_api.insert_one('db','geoquerytest','{ "_id" : 22, "geoPrec" : { "loc": [0, -1], "d": {"$numberDouble": "0.017453292519943295"} } }', NULL);

-- Insert some items for testing composite 2dsphere index
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
SELECT documentdb_api.insert_one('db','geoquerytest','{ "_id" : 400, "largeGeo" : { "type" : "LineString", "coordinates": [[96.328125, 5.61598581915534], [153.984375, -6.315298538330033]] }}', NULL);
SELECT documentdb_api.insert_one('db','geoquerytest','{ "_id" : 401, "largeGeo" : { "type" : "Polygon", "coordinates": [[
                [98.96484375, -11.350796722383672],
                [135.35156249999997, -11.350796722383672],
                [135.35156249999997, 0.8788717828324276],
                [98.96484375, 0.8788717828324276],
                [98.96484375, -11.350796722383672]
            ]] }}', NULL);

-- Valid polygon inserts
SELECT documentdb_api.insert_one('db','geoquerytest','{"_id": 601, "geo" : { "loc" : {"type": "Polygon", "coordinates": [[[0.0, 0.0], [60.0, 0.0], [90.0, 0.0], [0.0, 0.0]]] } } }', NULL); -- all points on equator
SELECT documentdb_api.insert_one('db','geoquerytest','{"_id": 602, "geo" : { "loc" : {"type": "Polygon", "coordinates": [[[-120, 60.0], [0.0, 60.0], [60.0, 60.0], [90.0, 60.0], [-120.0, 60.0]]] } } }', NULL); -- all points on same latitude
SELECT documentdb_api.insert_one('db','geoquerytest','{"_id": 603, "geo" : { "loc" : {"type": "Polygon", "coordinates": [[[0, 0], [0 ,4], [0, 6], [0, 8], [0, 0]]] } } }', NULL); -- all points on same longitude

--$geoIntersects operator
SELECT document FROM documentdb_api.collection('db', 'geoquerytest') WHERE document @@ '{"geo.loc": {"$geoIntersects": {"$geometry": { "type": "MultiPoint", "coordinates": [[1.1, 1.2], [2.1, 2.2], [3.1, 3.2]] } }}}' ORDER BY object_id; -- Validate legacy format works
SELECT document FROM documentdb_api.collection('db', 'geoquerytest') WHERE document @@ '{"geo.loc": {"$geoIntersects": {"$geometry": { "type": "Point", "coordinates": {"lon": 3.1, "lat": 3.2} } }}}' ORDER BY object_id; -- this weird GeoJson mongo native syntax is also supported
SELECT document FROM documentdb_api.collection('db', 'geoquerytest') WHERE document @@ '{"geo.loc": {"$geoIntersects": {"$geometry": { "type": "MultiPoint", "coordinates": [[7.1, 7.2], [4.1, 4.2]] } }}}' ORDER BY object_id;
SELECT document FROM documentdb_api.collection('db', 'geoquerytest') WHERE document @@ '{"geo.loc": {"$geoIntersects": {"$geometry": { "type": "LineString", "coordinates": [[7.1, 7.2], [4.1, 4.2], [7.3, 7.4], [2.1, 2.2]] } }}}' ORDER BY object_id;
SELECT document FROM documentdb_api.collection('db', 'geoquerytest') WHERE document @@ '{"geo.loc": {"$geoIntersects": {"$geometry": { "type": "Polygon", "coordinates": [[ [0, 0], [0, 10], [10, 10], [10, 0], [0, 0] ]] } }}}' ORDER BY object_id;
-- with holes, should not include document with _id 4
SELECT document FROM documentdb_api.collection('db', 'geoquerytest') WHERE document @@ '{"geo.loc": {"$geoIntersects": {"$geometry": { "type": "Polygon", "coordinates": [[ [0, 0], [0, 10], [10, 10], [10, 0], [0, 0] ], [ [4, 4], [4, 7], [7, 7], [7, 4], [4, 4] ]] } }}}' ORDER BY object_id;
SELECT document FROM documentdb_api.collection('db', 'geoquerytest') WHERE document @@ '{"geo.loc": {"$geoIntersects": {"$geometry": { "type": "LineString", "coordinates": [ [25, 25], [-25, -25] ] } }}}' ORDER BY object_id;
SELECT document FROM documentdb_api.collection('db', 'geoquerytest') WHERE document @@ '{"geo.loc": {"$geoIntersects": {"$geometry": { "type": "GeometryCollection", "geometries": [ {"type": "LineString", "coordinates": [[-25, -25], [25, 25]]}, {"type": "Point", "coordinates": [2.1, 2.2]} ] } }}}' ORDER BY object_id;
-- Legacy formats with $geometry works
SELECT document FROM documentdb_api.collection('db', 'geoquerytest') WHERE document @@ '{"geo.loc": {"$geoIntersects": {"$geometry": { "lon": 50, "lat": 50 } }}}' ORDER BY object_id;
SELECT document FROM documentdb_api.collection('db', 'geoquerytest') WHERE document @@ '{"geo.loc": {"$geoIntersects": {"$geometry": [51, 51] }}}' ORDER BY object_id;


-- Check if a north pole region geography finds a point with _id 12, this is invalid polygon in 2d but valid as a spherical geometry
SELECT document FROM documentdb_api.collection('db', 'geoquerytest') WHERE document @@ '{"geo.loc": {"$geoIntersects": {"$geometry": { "type": "Polygon", "coordinates": [[[-120.0, 89.0], [0.0, 89.0], [120.0, 89.0], [-120.0, 89.0]]] } }}}' ORDER BY object_id;

-- $geoWithin operator works with spherical queries with $geometry (Polygon and Multipolygons only) and $centerSphere
SELECT document FROM documentdb_api.collection('db', 'geoquerytest') WHERE document @@ '{"geo.loc": {"$geoWithin": {"$geometry": { "type": "Polygon", "coordinates": [[ [0, 0], [0, 10], [10, 10], [10, 0], [0, 0] ]] } }}}' ORDER BY object_id;
-- SELECT document FROM documentdb_api.collection('db', 'geoquerytest') WHERE document @@ '{"geo.loc": {"$geoWithin": {"$geometry": { "type": "Polygon", "coordinates": [[ [0, 0], [0, 10], [10, 10], [10, 0], [0, 0] ], [ [4, 4], [4, 7], [7, 7], [7, 4], [4, 4] ]] } }}}' ORDER BY object_id;
SELECT document FROM documentdb_api.collection('db', 'geoquerytest') WHERE document @@ '{"geo.loc": {"$geoWithin": {"$geometry": { "type": "Polygon", "coordinates": [[[-120.0, 89.0], [0.0, 89.0], [120.0, 89.0], [-120.0, 89.0]]] } }}}' ORDER BY object_id;

-- $centerSphere operator
SELECT document FROM documentdb_api.collection('db', 'geoquerytest') WHERE document @@ '{"geo.loc": {"$geoWithin": {"$centerSphere": [[0,0], 1] }}}' ORDER BY object_id;
SELECT document FROM documentdb_api.collection('db', 'geoquerytest') WHERE document @@ '{"geo.loc": {"$geoWithin": {"$centerSphere": [[0,0], 4] }}}' ORDER BY object_id; -- still returns results with bigger buffer
SELECT document FROM documentdb_api.collection('db', 'geoquerytest') WHERE document @@ '{"geo.loc": {"$geoWithin": {"$centerSphere": [[0,0], 0.0984] }}}' ORDER BY object_id; -- edge test for $centerSphere
SELECT document FROM documentdb_api.collection('db', 'geoquerytest') WHERE document @@ '{"geo.loc": {"$geoWithin": {"$centerSphere": [[0,0], 0.21] }}}' ORDER BY object_id; -- 0.21 is enough to include Point(5 5) and not Point(10 10) so shouldn't include some docs 
SELECT document FROM documentdb_api.collection('db', 'geoquerytest') WHERE document @@ '{"geo.loc": {"$geoWithin": {"$centerSphere": [[0,0], { "$numberDecimal" : "Infinity" }] }}}' ORDER BY object_id; -- infinite radius should include all docs
SELECT document FROM documentdb_api.collection('db', 'geoquerytest') WHERE document @@ '{"largeGeo": {"$geoWithin": {"$centerSphere": [[-59.80246852929814, -2.3633072488322853], 2.768403272464979]}}}' ORDER BY object_id; -- no result
SELECT document FROM documentdb_api.collection('db', 'geoquerytest') WHERE document @@ '{"largeGeo": {"$geoWithin": {"$centerSphere": [[-61.52266094410311, 17.79937981451866], 2.9592242752161573]}}}' ORDER BY object_id; -- big enough for linestring but not for polygon
SELECT document FROM documentdb_api.collection('db', 'geoquerytest') WHERE document @@ '{"largeGeo": {"$geoWithin": {"$centerSphere": [[-61.52266094410311, 17.79937981451866], 3.15]}}}' ORDER BY object_id; -- radius > pi, both docs in result

-- Testing composite 2dsphere index
SELECT document FROM documentdb_api.collection('db', 'geoquerytest') WHERE document @@ '{"geoA": {"$geoIntersects": {"$geometry": { "type": "Polygon", "coordinates": [[[0, 0], [0, 50], [50, 50], [50, 0], [0, 0]]]} }}}' ORDER BY object_id;
SELECT document FROM documentdb_api.collection('db', 'geoquerytest') WHERE document @@ '{"geoB": {"$geoIntersects": {"$geometry": { "type": "Polygon", "coordinates": [[[0, 0], [0, 50], [50, 50], [50, 0], [0, 0]]]} }}}' ORDER BY object_id;
SELECT document FROM documentdb_api.collection('db', 'geoquerytest') 
    WHERE document @@ '{"geoA": {"$geoIntersects": {"$geometry": { "type": "Polygon", "coordinates": [[[0, 0], [0, 50], [50, 50], [50, 0], [0, 0]]]} }}, 
                        "geoB": {"$geoIntersects": {"$geometry": { "type": "Polygon", "coordinates": [[[0, 0], [0, 50], [50, 50], [50, 0], [0, 0]]]} }}}' ORDER BY object_id;

-- Testing composite 2dsphere with pfe
SELECT document FROM documentdb_api.collection('db', 'geoquerytest') WHERE document @@ '{"geo1": {"$geoIntersects": {"$geometry": { "type": "Polygon", "coordinates": [[[0, 0], [0, 50], [50, 50], [50, 0], [0, 0]]]} }}, "region": {"$eq": "USA" }}' ORDER BY object_id;
SELECT document FROM documentdb_api.collection('db', 'geoquerytest') WHERE document @@ '{"geo2": {"$geoIntersects": {"$geometry": { "type": "Polygon", "coordinates": [[[0, 0], [0, 50], [50, 50], [50, 0], [0, 0]]]} }}, "region": {"$eq": "USA" }}' ORDER BY object_id;
SELECT document FROM documentdb_api.collection('db', 'geoquerytest') 
    WHERE document @@ '{"geo1": {"$geoIntersects": {"$geometry": { "type": "Polygon", "coordinates": [[[0, 0], [0, 50], [50, 50], [50, 0], [0, 0]]]} }} ,
                        "geo2": {"$geoIntersects": {"$geometry": { "type": "Polygon", "coordinates": [[[0, 0], [0, 50], [50, 50], [50, 0], [0, 0]]]} }} ,
                        "region": { "$eq": "USA" } }' ORDER BY object_id;
SELECT document FROM documentdb_api.collection('db', 'geoquerytest') 
    WHERE document @@ '{"geo1": {"$geoIntersects": {"$geometry": { "type": "Polygon", "coordinates": [[[0, 0], [0, 50], [50, 50], [50, 0], [0, 0]]]} }}, 
                        "geo2": {"$geoIntersects": {"$geometry": { "type": "Polygon", "coordinates": [[[0, 0], [0, 50], [50, 50], [50, 0], [0, 0]]]} }}}' ORDER BY object_id; -- This one should not use index because pfe doesn't match but should return same values


-- Make sure a really big query that requires buffer reallocs don't mess up geometry/geography and return the result
SELECT document FROM documentdb_api.collection('db', 'geoquerytest') WHERE document @@ '{ "geo.loc" : { "$geoIntersects" : { "$geometry" : '
                '{ "type" : "MultiPolygon", "coordinates" : [[[[-46.67914894304542, -23.39591591], [-46.69148969424672, -23.39647184], [-46.70371190620094, -23.39813429], '
                '[-46.7156981694656, -23.40088729], [-46.72733332377327, -23.40470439], [-46.7385055566174, -23.40954892], [-46.749107470871145, -23.41537434], '
                '[-46.75903711150689, -23.42212468], [-46.768198941818476, -23.42973505], [-46.77650475996186, -23.43813231], [-46.78387454712577, -23.44723573], '
                '[-46.79023723921629, -23.45695777], [-46.7955314145885, -23.46720494], [-46.79970589107978, -23.47787865], [-46.80272022639018, -23.48887621], '
                '[-46.80454511671058, -23.50009177], [-46.805162689414196, -23.51141736], [-46.804566686594455, -23.52274393], [-46.80276253724614, -23.53396238], '
                '[-46.79976731693784, -23.54496462], [-46.79560959490368, -23.55564464], [-46.79032916958054, -23.56589949], [-46.783976694723066, -23.5756303], '
                '[-46.77661319932978, -23.58474322], [-46.76830950569968, -23.59315037], [-46.75914555099343, -23.60077063], [-46.74920961868761, -23.60753048], [-46.73859748726804, -23.61336466], '
                '[-46.727411504398404, -23.61821688], [-46.71575959561004, -23.62204028], [-46.703754217276085, -23.62479797], [-46.691511264249215, -23.6264633], '
                '[-46.67914894304542, -23.6270202], [-46.66678662184163, -23.6264633], [-46.65454366881477, -23.62479797], [-46.642538290480815, -23.62204028], '
                '[-46.63088638169245, -23.61821688], [-46.619700398822815, -23.61336466], [-46.60908826740324, -23.60753048], [-46.59915233509742, -23.60077063], '
                '[-46.589988380391155, -23.59315037], [-46.58168468676106, -23.58474322], [-46.57432119136778, -23.5756303], [-46.56796871651031, -23.56589949], '
                '[-46.56268829118717, -23.55564464], [-46.558530569153, -23.54496462], [-46.5555353488447, -23.53396238], [-46.55373119949638, -23.52274393], '
                '[-46.55313519667665, -23.51141736], [-46.55375276938027, -23.50009177], [-46.55557765970067, -23.48887621], [-46.55859199501106, -23.47787865], '
                '[-46.56276647150235, -23.46720494], [-46.56806064687455, -23.45695777], [-46.57442333896507, -23.44723573], [-46.58179312612898, -23.43813231], '
                '[-46.59009894427236, -23.42973505], [-46.59926077458395, -23.42212468], [-46.60919041521971, -23.41537434], [-46.61979232947344, -23.40954892], '
                '[-46.63096456231758, -23.40470439], [-46.64259971662524, -23.40088729], [-46.65458597988991, -23.39813429], [-46.66680819184413, -23.39647184], '
                '[-46.67914894304542, -23.39591591]]]]}}}}' ORDER BY object_id;

SELECT document FROM documentdb_api.collection('db', 'geoquerytest') WHERE document @@ '{ "geo.loc" : { "$geoWithin" : { "$geometry" : '
                '{ "type" : "MultiPolygon", "coordinates" : [[[[-46.67914894304542, -23.39591591], [-46.69148969424672, -23.39647184], [-46.70371190620094, -23.39813429], '
                '[-46.7156981694656, -23.40088729], [-46.72733332377327, -23.40470439], [-46.7385055566174, -23.40954892], [-46.749107470871145, -23.41537434], '
                '[-46.75903711150689, -23.42212468], [-46.768198941818476, -23.42973505], [-46.77650475996186, -23.43813231], [-46.78387454712577, -23.44723573], '
                '[-46.79023723921629, -23.45695777], [-46.7955314145885, -23.46720494], [-46.79970589107978, -23.47787865], [-46.80272022639018, -23.48887621], '
                '[-46.80454511671058, -23.50009177], [-46.805162689414196, -23.51141736], [-46.804566686594455, -23.52274393], [-46.80276253724614, -23.53396238], '
                '[-46.79976731693784, -23.54496462], [-46.79560959490368, -23.55564464], [-46.79032916958054, -23.56589949], [-46.783976694723066, -23.5756303], '
                '[-46.77661319932978, -23.58474322], [-46.76830950569968, -23.59315037], [-46.75914555099343, -23.60077063], [-46.74920961868761, -23.60753048], [-46.73859748726804, -23.61336466], '
                '[-46.727411504398404, -23.61821688], [-46.71575959561004, -23.62204028], [-46.703754217276085, -23.62479797], [-46.691511264249215, -23.6264633], '
                '[-46.67914894304542, -23.6270202], [-46.66678662184163, -23.6264633], [-46.65454366881477, -23.62479797], [-46.642538290480815, -23.62204028], '
                '[-46.63088638169245, -23.61821688], [-46.619700398822815, -23.61336466], [-46.60908826740324, -23.60753048], [-46.59915233509742, -23.60077063], '
                '[-46.589988380391155, -23.59315037], [-46.58168468676106, -23.58474322], [-46.57432119136778, -23.5756303], [-46.56796871651031, -23.56589949], '
                '[-46.56268829118717, -23.55564464], [-46.558530569153, -23.54496462], [-46.5555353488447, -23.53396238], [-46.55373119949638, -23.52274393], '
                '[-46.55313519667665, -23.51141736], [-46.55375276938027, -23.50009177], [-46.55557765970067, -23.48887621], [-46.55859199501106, -23.47787865], '
                '[-46.56276647150235, -23.46720494], [-46.56806064687455, -23.45695777], [-46.57442333896507, -23.44723573], [-46.58179312612898, -23.43813231], '
                '[-46.59009894427236, -23.42973505], [-46.59926077458395, -23.42212468], [-46.60919041521971, -23.41537434], [-46.61979232947344, -23.40954892], '
                '[-46.63096456231758, -23.40470439], [-46.64259971662524, -23.40088729], [-46.65458597988991, -23.39813429], [-46.66680819184413, -23.39647184], '
                '[-46.67914894304542, -23.39591591]]]]}}}}' ORDER BY object_id;

-- $centerSphere precision test
SELECT document FROM documentdb_api.collection('db', 'geoquerytest') WHERE document @@ '{"geoPrec.loc": {"$geoWithin": {"$centerSphere": [[0,0], {"$numberDouble": "0.017453292519943295"}] }}}' ORDER BY object_id;

-- $centerSphere test with array of legacy coordinates in document
SELECT document FROM documentdb_api.collection('db', 'geoquerytest') WHERE document @@ '{"a.b": {"$geoWithin": {"$centerSphere": [[0,0], 2] }}}' ORDER BY object_id;

-- Test that no matter the order of legacy pairs in the array, if 1 matches the document is returned
SELECT document FROM documentdb_api.collection('db', 'geoquerytest') WHERE document @@ '{"a.b": {"$geoWithin": {"$centerSphere": [[0, 0], 1]}}}' ORDER BY object_id;
SELECT document FROM documentdb_api.collection('db', 'geoquerytest') WHERE document @@ '{"a.b": {"$geoWithin": {"$centerSphere": [[90, 90], 1]}}}' ORDER BY object_id;