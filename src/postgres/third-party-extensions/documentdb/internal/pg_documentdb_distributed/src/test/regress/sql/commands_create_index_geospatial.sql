SET search_path TO documentdb_api_catalog, postgis_public;
SET citus.next_shard_id TO 167200;
SET documentdb.next_collection_id TO 16720;
SET documentdb.next_collection_index_id TO 16720;

SELECT documentdb_api.create_collection('db', '2dindextest') IS NOT NULL;
SELECT documentdb_api.create_collection('db', '2dsphereindextest') IS NOT NULL;

-- Test invalid 2d index creation scenarios
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "2dindextest", "indexes": [{"key": {"a": "2d"}, "name": "my_2d_a_idx", "unique": true }]}', true); -- 2d with unique
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "2dindextest", "indexes": [{"key": {"a": "2d", "b": "2d"}, "name": "my_2d_a_idx" }]}', true); -- Multi 2d
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "2dindextest", "indexes": [{"key": {"a": "2d", "b": 1}, "name": "my_2d_a_idx" } ]}', true); -- Multi 2d
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "2dindextest", "indexes": [{"key": {"b": 1, "a": "2d"}, "name": "my_2d_a_idx" }]}', true); -- Multi 2d and 2d index is not the first one
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "2dindextest", "indexes": [{"key": {"a.$**": "2d"}, "name": "my_2d_a_idx" }]}', true); -- 2d with wildcard
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "2dindextest", "indexes": [{"key": {"a": "2d"}, "name": "my_2d_a_idx", "max": -190 } ]}', true); -- 2d with max -170 and min -180 (default)
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "2dindextest", "indexes": [{"key": {"a": "2d"}, "name": "my_2d_a_idx", "min": 190 } ]}', true); -- 2d with min 190 and max 180 (default)
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "2dindextest", "indexes": [{"key": {"a": "2d"}, "name": "my_2d_a_idx", "max": 180, "min": 180} ]}', true); -- 2d with min and max equal to 180
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "2dindextest", "indexes": [{"key": {"a": "2d"}, "name": "my_2d_a_idx", "expireAfterSeconds": 20 } ]}', true); -- With TTL
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "2dindextest", "indexes": [{"key": {"a": "2d", "b": "text"}, "name": "my_2d_a_idx" } ]}', true); -- 2d with text
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "2dindextest", "indexes": [{"key": {"a": "2d", "b.$**": 1}, "name": "my_2d_a_idx" } ]}', true); -- 2d followed by wildcard
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "2dindextest", "indexes": [{"key": {"a": "2d"}, "name": "my_2d_a_idx", "bits": 0 } ]}', true); -- bits 0
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "2dindextest", "indexes": [{"key": {"a": "2d"}, "name": "my_2d_a_idx", "bits": 33 } ]}', true); -- bits more than 32

-- Test invalid 2dsphere index creation scenarios
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "2dsphereindextest", "indexes": [{"key": {"a": "2dsphere"}, "name": "my_2ds_a_idx", "unique": true }]}', true); -- 2dsphere with unique
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "2dsphereindextest", "indexes": [{"key": {"a": "2dsphere", "b": 1}, "name": "my_2ds_a_idx" } ]}', true); -- Compound 2dsphere
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "2dsphereindextest", "indexes": [{"key": {"b": 1, "a": "2dsphere"}, "name": "my_2ds_a_idx" }]}', true); -- Compound 2dsphere with 1 being regular index
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "2dsphereindextest", "indexes": [{"key": {"a.$**": "2dsphere"}, "name": "my_2ds_a_idx" }]}', true); -- 2dsphere with wildcard
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "2dsphereindextest", "indexes": [{"key": {"a": "2dsphere"}, "name": "my_2ds_a_idx", "coarsestIndexedLevel": -1 } ]}', true); -- 2dsphere with coarestIndexedLevel invalid value
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "2dsphereindextest", "indexes": [{"key": {"a": "2dsphere"}, "name": "my_2ds_a_idx", "finestIndexedLevel": 31 } ]}', true); -- 2dsphere with finestIndexedLevel invalid value
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "2dsphereindextest", "indexes": [{"key": {"a": "2dsphere"}, "name": "my_2ds_a_idx", "coarsestIndexedLevel": 32, "finestIndexedLevel": 30 } ]}', true); -- 2dsphere with finestIndexedLevel invalid value
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "2dsphereindextest", "indexes": [{"key": {"a": "2dsphere"}, "name": "my_2ds_a_idx", "expireAfterSeconds": 20 } ]}', true); -- With TTL
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "2dsphereindextest", "indexes": [{"key": {"a": "2dsphere", "b": "text"}, "name": "my_2ds_a_idx" } ]}', true); -- 2dsphere with different
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "2dsphereindextest", "indexes": [{"key": {"a": "2dsphere", "b.$**": 1}, "name": "my_2ds_a_idx" } ]}', true); -- 2dsphere followed by wildcard

-- Test valid 2d & 2dsphere index creation scenarios
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "2dindextest", "indexes": [{"key": {"a": "2d"}, "name": "my_2d_a_idx" }]}', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "2dindextest", "indexes": [{"key": {"b.c": "2d"}, "name": "my_2d_b_c_idx" }]}', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "2dindextest", "indexes": [{"key": {"d": "2d"}, "name": "my_2d_d_idx", "min": -100, "max": 100 }]}', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "2dindextest", "indexes": [{"key": {"e": "2d"}, "name": "my_2d_e_idx", "bits": 32 } ]}', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "2dindextest", "indexes": [{"key": {"pfe": "2d"}, "name": "my_2d_pfe_idx", "partialFilterExpression": { "region": { "$eq": "USA" } } }]}', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "2dsphereindextest", "indexes": [{"key": {"a": "2dsphere" }, "name": "my_2ds_a_idx" } ]}', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "2dsphereindextest", "indexes": [{"key": {"b.c": "2dsphere" }, "name": "my_2ds_bc_idx", "coarsestIndexedLevel": 20 } ]}', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "2dsphereindextest", "indexes": [{"key": {"c": "2dsphere" }, "name": "my_2ds_c_idx", "coarsestIndexedLevel": 5, "finestIndexedLevel": 25 } ]}', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "2dsphereindextest", "indexes": [{"key": {"d": "2dsphere" }, "name": "my_2ds_d_idx", "finestIndexedLevel": 25 } ]}', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "2dsphereindextest", "indexes": [{"key": {"e": "2dsphere", "f": "2dsphere" }, "name": "my_2ds_ef_idx" } ]}', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "2dsphereindextest", "indexes": [{"key": {"g": "2dsphere", "h": "2dsphere" }, "name": "my_2ds_gh_pfe_idx", "partialFilterExpression": { "region": { "$eq": "USA" } } } ]}', true);

SELECT * FROM documentdb_distributed_test_helpers.get_collection_indexes('db', '2dindextest') ORDER BY collection_id, index_id;
SELECT * FROM documentdb_distributed_test_helpers.get_collection_indexes('db', '2dsphereindextest') ORDER BY collection_id, index_id;

-- 2d index strict mode insert restrictions
SELECT documentdb_api.insert_one('db','2dindextest','{"a" : { "x": "Hello" } }', NULL);
SELECT documentdb_api.insert_one('db','2dindextest','{"a" : { "x": true } }', NULL);
SELECT documentdb_api.insert_one('db','2dindextest','{"a" : { "x": false } }', NULL);
SELECT documentdb_api.insert_one('db','2dindextest','{"a" : { "x": null } }', NULL);
SELECT documentdb_api.insert_one('db','2dindextest','{"a" : { "x": { "$undefined" : true } } }', NULL);
SELECT documentdb_api.insert_one('db','2dindextest','{"a" : { "x": { "$numberInt": "1" }  } }', NULL);
SELECT documentdb_api.insert_one('db','2dindextest','{"a" : { "x": { "$numberLong": "1" }  } }', NULL);
SELECT documentdb_api.insert_one('db','2dindextest','{"a" : { "x": { "$numberDouble": "1" }  } }', NULL);
SELECT documentdb_api.insert_one('db','2dindextest','{"a" : { "x": { "$numberDecimal": "1" }  } }', NULL);
SELECT documentdb_api.insert_one('db','2dindextest','{"a" : { "x": { "$numberInt": "1" }, "y": "Invalid"  } }', NULL);
SELECT documentdb_api.insert_one('db','2dindextest','{"a" : { "x": { "$numberInt": "1" }, "y": true  } }', NULL);
SELECT documentdb_api.insert_one('db','2dindextest','{"a" : { "x": { "$numberInt": "1" }, "y": false  } }', NULL);
SELECT documentdb_api.insert_one('db','2dindextest','{"a" : { "x": { "$numberInt": "1" }, "y": null  } }', NULL);
SELECT documentdb_api.insert_one('db','2dindextest','{"a" : { "x": { "$numberInt": "1" }, "y": { "$undefined": true }  } }', NULL);
SELECT documentdb_api.insert_one('db','2dindextest','{"a" : { "x": { "$numberInt": "1" }, "y": "Invalid", "z": 10  } }', NULL);

-- 2dsphere index strict mode insert restriction
SELECT documentdb_api.insert_one('db','2dsphereindextest','{"_id": 1, "a" : "Hello" }', NULL);
SELECT documentdb_api.insert_one('db','2dsphereindextest','{"_id": 1, "a" : true }', NULL);
SELECT documentdb_api.insert_one('db','2dsphereindextest','{"_id": 1, "a" : false }', NULL);
SELECT documentdb_api.insert_one('db','2dsphereindextest','{"_id": 1, "a" : { "$numberInt": "1" } }', NULL);
SELECT documentdb_api.insert_one('db','2dsphereindextest','{"_id": 1, "a" : { "$numberLong": "1" } }', NULL);
SELECT documentdb_api.insert_one('db','2dsphereindextest','{"_id": 1, "a" : { "$numberDouble": "1" } }', NULL);
SELECT documentdb_api.insert_one('db','2dsphereindextest','{"_id": 1, "a" : { "$numberDecimal": "1" } }', NULL);
SELECT documentdb_api.insert_one('db','2dsphereindextest','{"_id": 1, "a" : { "x": 1 } }', NULL); -- Legacy object invalid format
SELECT documentdb_api.insert_one('db','2dsphereindextest','{"_id": 1, "a" : { "x": 1, "y": "Text" } }', NULL);
SELECT documentdb_api.insert_one('db','2dsphereindextest','{"_id": 1, "a" : [1] }', NULL); -- Legacy array invalid format
SELECT documentdb_api.insert_one('db','2dsphereindextest','{"_id": 1, "a" : [1, "text"] }', NULL);
SELECT documentdb_api.insert_one('db','2dsphereindextest','{"_id": 1, "a" : {"type": "point"} }', NULL); -- Wrong type case-sensitive
SELECT documentdb_api.insert_one('db','2dsphereindextest','{"_id": 1, "a" : {"type": "Point"} }', NULL); -- coordinates missing
SELECT documentdb_api.insert_one('db','2dsphereindextest','{"_id": 1, "a" : {"type": "Point", "coords": [1, 2]} }', NULL); -- Invalid 'coords'
SELECT documentdb_api.insert_one('db','2dsphereindextest','{"_id": 1, "a" : {"type": "Point", "coordinates": [1, "hello"]} }', NULL);
SELECT documentdb_api.insert_one('db','2dsphereindextest','{"_id": 1, "a" : {"type": "Point", "coordinates": "non-array" } }', NULL);
SELECT documentdb_api.insert_one('db','2dsphereindextest','{"_id": 1, "a" : {"type": "Point", "coordinates": [[1 , 2], [3, 4]] } }', NULL);
SELECT documentdb_api.insert_one('db','2dsphereindextest','{"_id": 1, "a" : {"type": "Point", "coordinates": [] } }', NULL);
SELECT documentdb_api.insert_one('db','2dsphereindextest','{"_id": 1, "a" : {"type": "Multipoint", "coordinates": [[1 , 2], [3, 4]] } }', NULL); -- Wrong case sensitive type
SELECT documentdb_api.insert_one('db','2dsphereindextest','{"_id": 1, "a" : {"type": "MultiPoint", "coordinates": [3, 4] } }', NULL);
SELECT documentdb_api.insert_one('db','2dsphereindextest','{"_id": 1, "a" : {"type": "MultiPoint", "coordinates": [[1 , "Hello"], [3, 4]] } }', NULL);
SELECT documentdb_api.insert_one('db','2dsphereindextest','{"_id": 1, "a" : {"type": "MultiPoint", "coordinates": "non-array" } }', NULL);
SELECT documentdb_api.insert_one('db','2dsphereindextest','{"_id": 1, "a" : {"type": "MultiPoint", "coordinates": [] } }', NULL);
SELECT documentdb_api.insert_one('db','2dsphereindextest','{"_id": 1, "a" : {"type": "Linestring", "coordinates": [[1,2], [3,4]] } }', NULL);
SELECT documentdb_api.insert_one('db','2dsphereindextest','{"_id": 1, "a" : {"type": "LineString", "coordinates": "non-array" } }', NULL);
SELECT documentdb_api.insert_one('db','2dsphereindextest','{"_id": 1, "a" : {"type": "LineString", "coordinates": [[1, 2], [1, 2]] } }', NULL);
SELECT documentdb_api.insert_one('db','2dsphereindextest','{"_id": 1, "a" : {"type": "LineString", "coordinates": [[1, 2], [1, 2]] } }', NULL);
SELECT documentdb_api.insert_one('db','2dsphereindextest','{"_id": 1, "a" : {"type": "Multilinestring", "coordinates": [] } }', NULL);
SELECT documentdb_api.insert_one('db','2dsphereindextest','{"_id": 1, "a" : {"type": "MultiLineString", "coordinates": [[1,2], [3,4]] } }', NULL);
SELECT documentdb_api.insert_one('db','2dsphereindextest','{"_id": 1, "a" : {"type": "MultiLineString", "coordinates": "non-array" } }', NULL);
SELECT documentdb_api.insert_one('db','2dsphereindextest','{"_id": 1, "a" : {"type": "MultiLineString", "coordinates": [[[1, 2], [1, 2]]] } }', NULL);
SELECT documentdb_api.insert_one('db','2dsphereindextest','{"_id": 1, "a" : {"type": "MultiLineString", "coordinates": [[[1, 2], [1, 2]]] } }', NULL);
SELECT documentdb_api.insert_one('db','2dsphereindextest','{"_id": 1, "a" : {"type": "MultiLineString", "coordinates": [] } }', NULL);
SELECT documentdb_api.insert_one('db','2dsphereindextest','{"_id": 1, "a" : {"type": "polygon", "coordinates": [] } }', NULL);
SELECT documentdb_api.insert_one('db','2dsphereindextest','{"_id": 1, "a" : {"type": "Polygon", "coordinates": [] } }', NULL);
SELECT documentdb_api.insert_one('db','2dsphereindextest','{"_id": 1, "a" : {"type": "Polygon", "coordinates": "non-array" } }', NULL);
SELECT documentdb_api.insert_one('db','2dsphereindextest','{"_id": 1, "a" : {"type": "Polygon", "coordinates": [1, 2] } }', NULL);
SELECT documentdb_api.insert_one('db','2dsphereindextest','{"_id": 1, "a" : {"type": "Polygon", "coordinates": [[1, 2]] } }', NULL);
SELECT documentdb_api.insert_one('db','2dsphereindextest','{"_id": 1, "a" : {"type": "Polygon", "coordinates": [[[1, 2]]] } }', NULL);
SELECT documentdb_api.insert_one('db','2dsphereindextest','{"_id": 1, "a" : {"type": "Polygon", "coordinates": [[[1, 2], [1, 2], [1,3]]] } }', NULL);
SELECT documentdb_api.insert_one('db','2dsphereindextest','{"_id": 1, "a" : {"type": "Polygon", "coordinates": [[[1, 2], [1, 2], [1,3], [1,2]]] } }', NULL);
SELECT documentdb_api.insert_one('db','2dsphereindextest','{"_id": 1, "a" : {"type": "Polygon", "coordinates": [[[1, 1], [1, 5], [3, 4], [0, 3], [1, 1]]] } }', NULL);
SELECT documentdb_api.insert_one('db','2dsphereindextest','{"_id": 1, "a" : {"type": "Polygon", "coordinates": [[[0, 0], [0, 1], [1, 1], [-2, -1], [0, 0]]] } }', NULL); -- self intersecting polygon with 0 geometrical area
SELECT documentdb_api.insert_one('db','2dsphereindextest','{"_id": 1, "a" : {"type": "Polygon", "coordinates": [[[1, 0], [0, 10], [10, 10], [0, 0], [1, 0]]] } }', NULL); -- self intersecting polygon with non-zero geometrical area
SELECT documentdb_api.insert_one('db','2dsphereindextest','{"_id": 1, "a" : {"type": "Polygon", "coordinates": [[[0, 0], [0, 80], [80, 80], [80, 0], [0, 0]],[[0, 10], [0, 70], [75, 75], [75, 25], [0, 10]]] } }', NULL); -- hole edge lies on outer ring edge
SELECT documentdb_api.insert_one('db','2dsphereindextest','{"_id": 1, "a" : {"type": "Polygon", "coordinates": [[[0, 0], [0, 80], [80, 80], [80, 0], [0, 0]],[[0, 0], [0, 80], [75, 75], [75, 25], [0, 0]]] } }', NULL); -- hole shares an edge with outer ring
SELECT documentdb_api.insert_one('db','2dsphereindextest','{"_id": 1, "a" : {"type": "Polygon", "coordinates": [[[0, 0], [0, 80], [80, 80], [80, 0], [0, 0]],[[10, 10], [10, 70], [75, 75], [75, 25], [10, 10]],[[10,20], [10,30], [50, 50], [10, 20]]] } }', NULL); -- 3rd ring edge lies on 2nd ring edge

SELECT documentdb_api.insert_one('db','2dsphereindextest','{"_id": 1, "a" : {"type": "Polygon", "coordinates": [[[1, 1], [1, 5], [5, 5], [5, 1], [1, 1]], [[0, 0], [0, 6], [6, 6], [6, 0], [0, 0]]] } }', NULL);
SELECT documentdb_api.insert_one('db','2dsphereindextest','{"_id": 1, "a" : {"type": "Polygon", "coordinates": [[[1, 2], [2, 3], [1, 2], [3, 4], [1, 2]]] } }', NULL);
SELECT documentdb_api.insert_one('db','2dsphereindextest','{"_id": 1, "a" : {"type": "Multipolygon", "coordinates": [] } }', NULL);
SELECT documentdb_api.insert_one('db','2dsphereindextest','{"_id": 1, "a" : {"type": "MultiPolygon", "coordinates": [] } }', NULL);
SELECT documentdb_api.insert_one('db','2dsphereindextest','{"_id": 1, "a" : {"type": "MultiPolygon", "coordinates": "non-array" } }', NULL);
SELECT documentdb_api.insert_one('db','2dsphereindextest','{"_id": 1, "a" : {"type": "MultiPolygon", "coordinates": [1, 2] } }', NULL);
SELECT documentdb_api.insert_one('db','2dsphereindextest','{"_id": 1, "a" : {"type": "MultiPolygon", "coordinates": [[1, 2]] } }', NULL);
SELECT documentdb_api.insert_one('db','2dsphereindextest','{"_id": 1, "a" : {"type": "MultiPolygon", "coordinates": [[[1, 2]]] } }', NULL);
SELECT documentdb_api.insert_one('db','2dsphereindextest','{"_id": 1, "a" : {"type": "MultiPolygon", "coordinates": [[[[1, 2], [1, 2], [1,3]]]] } }', NULL);
SELECT documentdb_api.insert_one('db','2dsphereindextest','{"_id": 1, "a" : {"type": "MultiPolygon", "coordinates": [[[[1, 2], [1, 2], [1,3], [1,2]]]] } }', NULL);
SELECT documentdb_api.insert_one('db','2dsphereindextest','{"_id": 1, "a" : {"type": "MultiPolygon", "coordinates": [[[[1, 1], [1, 5], [3, 4], [0, 3], [1, 1]]]] } }', NULL);
SELECT documentdb_api.insert_one('db','2dsphereindextest','{"_id": 1, "a" : {"type": "MultiPolygon", "coordinates": [[[[1, 1], [1, 5], [5, 5], [5, 1], [1, 1]], [[0, 0], [0, 6], [6, 6], [6, 0], [0, 0]]]] } }', NULL);
SELECT documentdb_api.insert_one('db','2dsphereindextest','{"_id": 1, "a" : {"type": "MultiPolygon", "coordinates": [[[[1, 2], [2, 3], [1, 2], [3, 4], [1, 2]]]] } }', NULL);
SELECT documentdb_api.insert_one('db','2dsphereindextest','{"_id": 1, "a" : { "type": "GeometryCollection", "geometries": {} } }', NULL);
SELECT documentdb_api.insert_one('db','2dsphereindextest','{"_id": 1, "a" : { "type": "GeometryCollection", "geometries": [] } }', NULL);
SELECT documentdb_api.insert_one('db','2dsphereindextest','{"_id": 1, "a" : { "type": "GeometryCollection", "coordinates": [] } }', NULL);
SELECT documentdb_api.insert_one('db','2dsphereindextest','{"_id": 1, "a" : { "type": "GeometryCollection", "geometries": [1, 2] } }', NULL);
SELECT documentdb_api.insert_one('db','2dsphereindextest','{"_id": 1, "a" : { "type": "GeometryCollection", "geometries": [{"type": "Point", "coordinates": [1, 2]}, 2] } }', NULL);
SELECT documentdb_api.insert_one('db','2dsphereindextest','{"_id": 1, "a" : { "type": "GeometryCollection", "geometries": [{"type": "Point", "coordinates": ["Hello", 2]}] } }', NULL);

-- Multi key failure cases
SELECT documentdb_api.insert_one('db','2dindextest','{"b" : { "c": [ [10, 10], "Invalid point" ] } }', NULL);
SELECT documentdb_api.insert_one('db','2dindextest','{"b" : { "c": [ [10, 10], [20, true] ] } }', NULL);
SELECT documentdb_api.insert_one('db','2dindextest','{"b" : { "c": [ [10, 10], [20, false] ] } }', NULL);
SELECT documentdb_api.insert_one('db','2dindextest','{"b" : { "c": [ [10, 10], [20, null] ] } }', NULL);
SELECT documentdb_api.insert_one('db','2dindextest','{"b" : { "c": [ [10, 10], [20, "point"] ] } }', NULL);
SELECT documentdb_api.insert_one('db','2dindextest','{"b" : { "c": [ [10, 10], [20, { "$undefined": true }] ] } }', NULL);
SELECT documentdb_api.insert_one('db','2dindextest','{"b" : [ { "c": [10, 20] }, { "c": { "long": "100", "lat": "100" } }, [ 30, "text"] ] }', NULL);
SELECT documentdb_api.insert_one('db','2dsphereindextest','{"_id": 1, "a" : [{"type": "Point", "coordinates": [10, 20]}, {"type": "Point", "coordinates": [10, 20]}] }', NULL); -- This is not supported as multi key
SELECT documentdb_api.insert_one('db','2dsphereindextest','{"_id": 1, "a" : [[10, 20], [20, 20]] }', NULL); -- This is not supported as multi key legacy
SELECT documentdb_api.insert_one('db','2dsphereindextest','{"_id": 1, "a" : [ { "x" : 1, "y" : 1 }, { "x" : 2, "y" : 2 }, { "x" : 3, "y" : 3 } ] }', NULL); -- This is not supported as multi key legacy
SELECT documentdb_api.insert_one('db','2dsphereindextest','{"_id": 1, "b" : [ {"c" : {"type": "Point", "coordinates": [10, 20]} }, {"c" : {"type": "Point", "coordinates": ["Text", 20]} } ] }', NULL);

-- Some case which are valid where the field is either empty object/document or not of type object/document
-- These cases generate NULL geometries and are not indexed
SELECT documentdb_api.insert_one('db','2dindextest','{ "_id" : 1, "a" : "Hello" }', NULL);
SELECT documentdb_api.insert_one('db','2dindextest','{ "_id" : 2, "a" : true }', NULL);
SELECT documentdb_api.insert_one('db','2dindextest','{ "_id" : 3, "a" : false }', NULL);
SELECT documentdb_api.insert_one('db','2dindextest','{ "_id" : 4, "a" : 10 }', NULL);
SELECT documentdb_api.insert_one('db','2dindextest','{ "_id" : 5, "a" : [] }', NULL);
SELECT documentdb_api.insert_one('db','2dindextest','{ "_id" : 6, "a" : { } }', NULL);
SELECT documentdb_api.insert_one('db','2dindextest','{ "_id" : 7, "a" : { "$undefined": true } }', NULL);
SELECT documentdb_api.insert_one('db','2dindextest','{ "_id" : 8, "a" : [ { }, [ ] ] }', NULL);
SELECT documentdb_api.insert_one('db','2dindextest','{ "_id" : 9, "a" : { "field": [] } }', NULL);
SELECT documentdb_api.insert_one('db','2dindextest','{ "_id" : 10, "a" : { "field": { } } }', NULL);
SELECT documentdb_api.insert_one('db','2dindextest','{ "_id" : 101, "a" : null }', NULL);
SELECT documentdb_api.insert_one('db', '2dindextest','{ "_id": 102, "b": [{"c": null}, {"c": {"$undefined": true}}]}');

-- Valid insertions where 2d geometery index terms are generated
SELECT documentdb_api.insert_one('db','2dindextest','{ "_id" : 11, "a" : [11, 11] }', NULL);
SELECT documentdb_api.insert_one('db','2dindextest','{ "_id" : 12, "a" : { "long": 12, "lat": 12 } }', NULL);
SELECT documentdb_api.insert_one('db','2dindextest','{ "_id" : 13, "a" : [13, 13, 11] }', NULL);
SELECT documentdb_api.insert_one('db','2dindextest','{ "_id" : 14, "a" : { "long": 14, "lat": 14, "extra": 1414 } }', NULL);
SELECT documentdb_api.insert_one('db','2dindextest','{ "_id" : 15, "a" : [ [ 15.1, 15.1 ], [15.2, 15.2] ] }', NULL);
SELECT documentdb_api.insert_one('db','2dindextest','{ "_id" : 16, "a" : [ [ 16.1, 16.1 ], [16.2, 16.2], { }, [16.3, 16.3], [16.4, 16.4] ] }', NULL);
SELECT documentdb_api.insert_one('db','2dindextest','{ "_id" : 18, "a" : [ [ { "$numberLong": "18" }, { "$numberInt": "18" } ], [ { "$numberDouble": "18.1" }, { "$numberDecimal": "18.2" }] ] }', NULL);

-- Out of boundary check- errors
SELECT documentdb_api.insert_one('db','2dindextest','{ "d" : [-120, 10] }', NULL);
SELECT documentdb_api.insert_one('db','2dindextest','{ "d" : [10, 120] }', NULL);
SELECT documentdb_api.insert_one('db','2dindextest','{ "d" : [ { "$numberDouble": "-100.000001" }, 80] }', NULL);
SELECT documentdb_api.insert_one('db','2dindextest','{ "d" : [ { "$numberDouble": "80" }, { "$numberDecimal": "100.000001" }] }', NULL);
SELECT documentdb_api.insert_one('db','2dindextest','{ "d" : [ { "$numberDouble": "-inf" }, { "$numberDecimal": "20" }] }', NULL);
SELECT documentdb_api.insert_one('db','2dindextest','{ "d" : [ { "$numberDouble": "20" }, { "$numberDecimal": "inf" }] }', NULL);
SELECT documentdb_api.insert_one('db','2dindextest','{ "d" : [ [10, 20], [-200, 200]] }', NULL);

-- NULL values for 2dsphere index
SELECT documentdb_api.insert_one('db','2dsphereindextest','{"a" : null }', NULL);
SELECT documentdb_api.insert_one('db','2dsphereindextest','{"a" : { "$undefined": true } }', NULL);
SELECT documentdb_api.insert_one('db','2dsphereindextest','{"b": [{"c": null}, {"c": {"$undefined": true}}]}');
SELECT documentdb_api.insert_one('db','2dsphereindextest','{"z" : "Text" }', NULL); -- index field missing

-- Valid GeoJson inserts for 2dsphere indexes
SELECT documentdb_api.insert_one('db','2dsphereindextest','{"a" : [ 5, 10] }', NULL);
SELECT documentdb_api.insert_one('db','2dsphereindextest','{"a" : { "x": 5 , "y": 10} }', NULL);
SELECT documentdb_api.insert_one('db','2dsphereindextest','{"a" : {"type": "Point", "coordinates": [5, 10] } }', NULL);
SELECT documentdb_api.insert_one('db','2dsphereindextest','{"a" : {"type": "Point", "coordinates": { "x": 5, "y": 10 } } }', NULL);
SELECT documentdb_api.insert_one('db','2dsphereindextest','{"a" : {"type": "MultiPoint", "coordinates": [[1 , 2], [3, 4]] } }', NULL);
SELECT documentdb_api.insert_one('db','2dsphereindextest','{"a" : {"type": "LineString", "coordinates": [[1,2], [3,4]] } }', NULL);
SELECT documentdb_api.insert_one('db','2dsphereindextest','{"a" : {"type": "MultiLineString", "coordinates": [[[1,2], [3,4]]] } }', NULL);
SELECT documentdb_api.insert_one('db','2dsphereindextest','{"a" : {"type": "Polygon", "coordinates": [[[1, 2], [1, 2], [1,3], [2, 3], [2, 2], [1, 2]]] } }', NULL);
SELECT documentdb_api.insert_one('db','2dsphereindextest','{"a" : {"type": "MultiPolygon", "coordinates": [[[[1, 2], [1, 2], [1,3], [2, 3], [2, 2], [1, 2]]]] } }', NULL);
SELECT documentdb_api.insert_one('db','2dsphereindextest','{"a" : { "type": "GeometryCollection", "geometries": [{"type": "Point", "coordinates": [1, 2]}] } }', NULL);

-- Valid polygon inserts for 2dsphere index
SELECT documentdb_api.insert_one('db','2dsphereindextest','{"a" : {"type": "Polygon", "coordinates": [[[0.0, 0.0], [60.0, 0.0], [90.0, 0.0], [0.0, 0.0]]] } }', NULL); -- all points on equator
SELECT documentdb_api.insert_one('db','2dsphereindextest','{"a" : {"type": "Polygon", "coordinates": [[[0, 0], [0 ,4], [0, 6], [0, 8], [0, 0]]] } }', NULL); -- all points on the same longitude
SELECT documentdb_api.insert_one('db','2dsphereindextest','{"a" : {"type": "Polygon", "coordinates": [[[-120, 60.0], [0.0, 60.0], [60.0, 60.0], [90.0, 60.0], [-120.0, 60.0]]] } }', NULL); -- all points on same latitude
SELECT documentdb_api.insert_one('db','2dsphereindextest','{"a" : {"type": "Polygon", "coordinates": [[[-120.0, 60], [-60, 60], [0, 60], [120, 60], [-120.0, 60]], [[-120.0, 70], [-60, 70], [0, 70], [120, 70], [-120.0, 70]]] } }', NULL); -- points on a single latitude, polygon going around the earth with similar hole


-- Test whether invalid document after valid documents are correctly errored out without OOM
BEGIN;
set local enable_seqscan TO on;

-- Valid and then invalid doc for `a` 2dsphere field
SELECT documentdb_api.insert_one('db','2dsOOMTest','{ "_id" : 1, "a": [2.1, 2.2] }', NULL);
SELECT documentdb_api.insert_one('db','2dsOOMTest','{ "_id" : 2, "a": "Text" }', NULL);

-- error while building the index and should not OOM
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "2dsOOMTest", "indexes": [{"key": {"a": "2dsphere"}, "name": "my_2ds_oom_idx" }]}', true);
ROLLBACK;
