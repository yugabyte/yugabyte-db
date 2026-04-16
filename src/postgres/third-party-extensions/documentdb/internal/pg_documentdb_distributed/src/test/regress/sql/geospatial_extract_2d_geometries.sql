SET search_path TO documentdb_api_catalog, documentdb_distributed_test_helpers;
SET citus.next_shard_id TO 6990000;
SET documentdb.next_collection_id TO 699000;
SET documentdb.next_collection_index_id TO 699000;

-- Case 1: All the below cases are just ignored to have a geodetic type and returns NULL geometry
-- This a check based on the type of the field at the given path, if it is not array or object or empty array and objects then it will return NULL
SELECT bson_extract_geometry(NULL, 'a') IS NULL;
SELECT bson_extract_geometry('{ "a" : "Hello" }', NULL) IS NULL;
SELECT bson_extract_geometry('{ "a" : true }', 'a') IS NULL;
SELECT bson_extract_geometry('{ "a" : false }', 'a') IS NULL;
SELECT bson_extract_geometry('{ "a" : null }', 'a') IS NULL;
SELECT bson_extract_geometry('{ "a" : { "$undefined": true } }', 'a') IS NULL;
SELECT bson_extract_geometry('{ "a" : {} }', 'a') IS NULL; -- empty single point
SELECT bson_extract_geometry('{ "a" : [] }', 'a') IS NULL; -- empty single point
SELECT bson_extract_geometry('{ "a" : [{}, []] }', 'a') IS NULL; -- empty multi points
SELECT bson_extract_geometry('{ "a" : [[], {}] }', 'a') IS NULL; -- empty multi points
SELECT bson_extract_geometry('{ "a" : { "field1": {} } }', 'a') IS NULL; -- surprise!! this doesn't throw error and only return NULL
SELECT bson_extract_geometry('{ "a" : { "field1": [] } }', 'a') IS NULL;
SELECT bson_extract_geometry('{ "a" : { "field1": [], "field2": {} } }', 'a') IS NULL;
SELECT bson_extract_geometry('{ "a" : { "$numberInt": "1" } }', 'a') IS NULL;
SELECT bson_extract_geometry('{ "a" : { "$numberLong": "1" } }', 'a') IS NULL;
SELECT bson_extract_geometry('{ "a" : { "$numberDouble": "1" } }', 'a') IS NULL;
SELECT bson_extract_geometry('{ "a" : { "$numberDecimal": "1" } }', 'a') IS NULL;
SELECT bson_extract_geometry('{ "b" : true }', 'a') IS NULL;

-- Case 2: Top level validation when the geodetic field is either object or array in strict mode
SELECT bson_extract_geometry('{ "a" : { "field1": "Hello" } }', 'a');
SELECT bson_extract_geometry('{ "a" : { "field1": true } }', 'a');
SELECT bson_extract_geometry('{ "a" : { "field1": false } }', 'a');
SELECT bson_extract_geometry('{ "a" : { "field1": null } }', 'a');
SELECT bson_extract_geometry('{ "a" : { "field1": { "$undefined": true } } }', 'a');
SELECT bson_extract_geometry('{ "a" : { "field1": { "$numberInt": "1" } } }', 'a');
SELECT bson_extract_geometry('{ "a" : { "field1": { "$numberLong": "1" } } }', 'a');
SELECT bson_extract_geometry('{ "a" : { "field1": { "$numberDouble": "1" } } }', 'a');
SELECT bson_extract_geometry('{ "a" : { "field1": { "$numberDecimal": "1" } } }', 'a');

SELECT bson_extract_geometry('{ "a" : { "field1": [], "field2": 10 } }', 'a');
SELECT bson_extract_geometry('{ "a" : { "field1": [[]], "field2": 10 } }', 'a');
SELECT bson_extract_geometry('{ "a" : { "field1": [{}], "field2": 10 } }', 'a');
SELECT bson_extract_geometry('{ "a" : { "field1": {}, "field2": 10 } }', 'a');
SELECT bson_extract_geometry('{ "a" : { "field1": 10, "field2": "Hello" } }', 'a');
SELECT bson_extract_geometry('{ "a" : { "field1": 10, "field2": "Hello", "field3": 10 } }', 'a');
SELECT bson_extract_geometry('{ "a" : { "field1": 10, "field2": true } }', 'a');
SELECT bson_extract_geometry('{ "a" : { "field1": 10, "field2": false } }', 'a');
SELECT bson_extract_geometry('{ "a" : { "field1": 10, "field2": null } }', 'a');
SELECT bson_extract_geometry('{ "a" : { "field1": 10, "field2": { "$undefined": true } } }', 'a');

-- Case 2.1: Same Top level validations are skipped for array or object in bson_extract_geometry_runtime and returns NULL
SELECT bson_extract_geometry_runtime('{ "a" : { "field1": "Hello" } }', 'a') IS NULL;
SELECT bson_extract_geometry_runtime('{ "a" : { "field1": true } }', 'a') IS NULL;
SELECT bson_extract_geometry_runtime('{ "a" : { "field1": false } }', 'a') IS NULL;
SELECT bson_extract_geometry_runtime('{ "a" : { "field1": null } }', 'a') IS NULL;
SELECT bson_extract_geometry_runtime('{ "a" : { "field1": { "$undefined": true } } }', 'a') IS NULL;
SELECT bson_extract_geometry_runtime('{ "a" : { "field1": { "$numberInt": "1" } } }', 'a') IS NULL;
SELECT bson_extract_geometry_runtime('{ "a" : { "field1": { "$numberLong": "1" } } }', 'a') IS NULL;
SELECT bson_extract_geometry_runtime('{ "a" : { "field1": { "$numberDouble": "1" } } }', 'a') IS NULL;
SELECT bson_extract_geometry_runtime('{ "a" : { "field1": { "$numberDecimal": "1" } } }', 'a') IS NULL;

SELECT bson_extract_geometry_runtime('{ "a" : { "field1": [], "field2": 10 } }', 'a') IS NULL;
SELECT bson_extract_geometry_runtime('{ "a" : { "field1": [[]], "field2": 10 } }', 'a') IS NULL;
SELECT bson_extract_geometry_runtime('{ "a" : { "field1": [{}], "field2": 10 } }', 'a') IS NULL;
SELECT bson_extract_geometry_runtime('{ "a" : { "field1": {}, "field2": 10 } }', 'a') IS NULL;
SELECT bson_extract_geometry_runtime('{ "a" : { "field1": 10, "field2": "Hello" } }', 'a') IS NULL;
SELECT bson_extract_geometry_runtime('{ "a" : { "field1": 10, "field2": true } }', 'a') IS NULL;
SELECT bson_extract_geometry_runtime('{ "a" : { "field1": 10, "field2": false } }', 'a') IS NULL;
SELECT bson_extract_geometry_runtime('{ "a" : { "field1": 10, "field2": null } }', 'a') IS NULL;
SELECT bson_extract_geometry_runtime('{ "a" : { "field1": 10, "field2": { "$undefined": true } } }', 'a') IS NULL;

-- Case 3: Top level strict validation for nested paths
SELECT bson_extract_geometry('{ "a" : { "b": ["Text", "Text"] } }', 'a.b');
SELECT bson_extract_geometry('{ "a" : { "b" : { "field1": [], "field2": 10 } } }', 'a.b');
SELECT bson_extract_geometry('{ "a" : { "b" : { "field1": 10, "field2": "Hello" } } }', 'a.b');
SELECT bson_extract_geometry('{ "a" : { "b" : { "field1": 10, "field2": true } } }', 'a.b');
SELECT bson_extract_geometry('{ "a" : { "b" : { "field1": 10, "field2": false } } }', 'a.b');
SELECT bson_extract_geometry('{ "a" : { "b" : { "field1": 10, "field2": null } } }', 'a.b');
SELECT bson_extract_geometry('{ "a" : { "b" : { "field1": 10, "field2": { "$undefined": true } } } }', 'a.b');

-- Case 3.1: Same Top level validations are skipped for nested paths in bson_extract_geometry_runtime function and returns NULL
SELECT bson_extract_geometry_runtime('{ "a" : { "b": ["Text", "Text"] } }', 'a.b') IS NULL;
SELECT bson_extract_geometry_runtime('{ "a" : { "b" : { "field1": [], "field2": 10 } } }', 'a.b') IS NULL;
SELECT bson_extract_geometry_runtime('{ "a" : { "b" : { "field1": 10, "field2": "Hello" } } }', 'a.b') IS NULL;
SELECT bson_extract_geometry_runtime('{ "a" : { "b" : { "field1": 10, "field2": true } } }', 'a.b') IS NULL;
SELECT bson_extract_geometry_runtime('{ "a" : { "b" : { "field1": 10, "field2": false } } }', 'a.b') IS NULL;
SELECT bson_extract_geometry_runtime('{ "a" : { "b" : { "field1": 10, "field2": null } } }', 'a.b') IS NULL;
SELECT bson_extract_geometry_runtime('{ "a" : { "b" : { "field1": 10, "field2": { "$undefined": true } } } }', 'a.b') IS NULL;

-- Case 4: Partial valid multipoint cases, throws error in strict mode
SELECT bson_extract_geometry('{ "a" : [{"b": { "1": 33, "2": 33 }}, {"b": ["Text", "Text"]} ]}', 'a.b');
SELECT bson_extract_geometry('{ "a" : [{"b": { "1": 34, "2": 34 }}, {"b": { "field1": [], "field2": 10 } } ]}', 'a.b');
SELECT bson_extract_geometry('{ "a" : [{"b": { "1": 35, "2": 35 }}, {"b": { "field1": 10, "field2": "Hello" } } ]}', 'a.b');
SELECT bson_extract_geometry('{ "a" : [{"b": { "1": 36, "2": 36 }}, {"b": { "field1": 10, "field2": true } } ]}', 'a.b');
SELECT bson_extract_geometry('{ "a" : [{"b": { "1": 37, "2": 37 }}, {"b": { "field1": 10, "field2": false } } ]}', 'a.b');
SELECT bson_extract_geometry('{ "a" : [{"b": { "1": 38, "2": 38 }}, {"b": { "field1": 10, "field2": null } } ]}', 'a.b');
SELECT bson_extract_geometry('{ "a" : [{"b": { "1": 39, "2": 39 }}, {"b": { "field1": 10, "field2": { "$undefined": true } } } ]}', 'a.b');
SELECT bson_extract_geometry('{ "a" : [{"b": [44, 44]}, {"b": "This is ok"}, {"b": ["This is not", "ok"]}, {"b": [45,45]} ]}', 'a.b');
SELECT bson_extract_geometry('{ "a" : [
    { "b": [ { "long": [70, 80], "lat": 75 }, { "long": [80], "lat": 85 } ] },
    { "b": [ { "long": [90, 100], "lat": 95 }, { "long": [100], "lat": 105 } ] },
    { "b": [ { "long": [110, 120], "lat": 115 }, { "long": [120], "lat": 125 } ] } ]}', 'a.b.long');

-- With GeoJson points, errors in case of strict mode
SELECT bson_extract_geometry('{"a": {"type": "Point", "coordinates": [45,45] } }', 'a');
SELECT bson_extract_geometry('{"a": {"extra": 10, "type": "Point", "coordinates": [46,46] } }', 'a');
SELECT bson_extract_geometry('{"a": {"extra": 10, "type": "Point", "coordinates": [46,46], "last": 20 } }', 'a');
SELECT bson_extract_geometry('{"a": {"extra": 10, "type": "point", "coordinates": [46,46], "last": 20 } }', 'a'); -- Invalid type: point (should be Point)
SELECT bson_extract_geometry('{"a": {"extra": 10, "Type": "point", "coordinates": [46,46], "last": 20 } }', 'a'); -- Invalid field name: Type
SELECT bson_extract_geometry('{"a": {"extra": 10, "type": "point", "Coordinates": [46,46], "last": 20 } }', 'a'); -- Invalid field name: Coordinates
SELECT bson_extract_geometry('{ "a" : [{"b": [44, 44]}, {"b": "This is ok"}, {"b": ["This is not", "ok"]}, {"b": {"type": "Point", "coordinates": [45,45]}} ]}', 'a.b');

-- Case 4.1: Partial points are returned without any error in non strict mode
-- public.ST_ASEWKT is used to convert the geometry to Extended Well known text format of postgis
SELECT public.ST_ASEWKT(bson_extract_geometry_runtime('{ "a" : [{"b": { "1": 33, "2": 33 }}, {"b": ["Text", "Text"]} ]}', 'a.b')) AS geoms;
SELECT public.ST_ASEWKT(bson_extract_geometry_runtime('{ "a" : [{"b": { "1": 34, "2": 34 }}, {"b": { "field1": [], "field2": 10 } } ]}', 'a.b')) AS geoms;
SELECT public.ST_ASEWKT(bson_extract_geometry_runtime('{ "a" : [{"b": { "1": 35, "2": 35 }}, {"b": { "field1": 10, "field2": "Hello" } } ]}', 'a.b')) AS geoms;
SELECT public.ST_ASEWKT(bson_extract_geometry_runtime('{ "a" : [{"b": { "1": 36, "2": 36 }}, {"b": { "field1": 10, "field2": true } } ]}', 'a.b')) AS geoms;
SELECT public.ST_ASEWKT(bson_extract_geometry_runtime('{ "a" : [{"b": { "1": 37, "2": 37 }}, {"b": { "field1": 10, "field2": false } } ]}', 'a.b')) AS geoms;
SELECT public.ST_ASEWKT(bson_extract_geometry_runtime('{ "a" : [{"b": { "1": 38, "2": 38 }}, {"b": { "field1": 10, "field2": null } } ]}', 'a.b')) AS geoms;
SELECT public.ST_ASEWKT(bson_extract_geometry_runtime('{ "a" : [{"b": { "1": 39, "2": 39 }}, {"b": { "field1": 10, "field2": { "$undefined": true } } } ]}', 'a.b')) AS geoms;
SELECT public.ST_ASEWKT(bson_extract_geometry_runtime('{ "a" : [{"b": [44, 44]}, {"b": "This is ok"}, {"b": ["This is not", "ok"]}, {"b": [45,45]} ]}', 'a.b')) AS geoms;
SELECT public.ST_ASEWKT(bson_extract_geometry_runtime('{ "a" : [
    { "b": [ { "long": [70, 80], "lat": 75 }, { "long": [80], "lat": 85 } ] },
    { "b": [ { "long": [90, 100], "lat": 95 }, { "long": [100], "lat": 105 } ] },
    { "b": [ { "long": [110, 120], "lat": 115 }, { "long": [120], "lat": 125 } ] } ]}', 'a.b.long')) AS geoms;

-- With GeoJson points, no errors in non-strict mode
SELECT public.ST_ASEWKT(bson_extract_geometry_runtime('{"a": {"type": "Point", "coordinates": [45,45] } }', 'a')) AS geoms;
SELECT public.ST_ASEWKT(bson_extract_geometry_runtime('{"a": {"extra": 10, "type": "Point", "coordinates": [46,46] } }', 'a')) AS geoms;
SELECT public.ST_ASEWKT(bson_extract_geometry_runtime('{"a": {"extra": 10, "type": "Point", "coordinates": [47,47], "last": 20 } }', 'a')) AS geoms;
SELECT public.ST_ASEWKT(bson_extract_geometry_runtime('{"a": {"extra": 10, "type": "point", "coordinates": [48,48], "last": 20 } }', 'a')) AS geoms; -- Invalid type: array is empty
SELECT public.ST_ASEWKT(bson_extract_geometry_runtime('{"a": {"extra": 10, "Type": "point", "coordinates": [49,49], "last": 20 } }', 'a')) AS geoms; -- Invalid field name: Type, array is empty
SELECT public.ST_ASEWKT(bson_extract_geometry_runtime('{"a": {"extra": 10, "type": "point", "Coordinates": [50,50], "last": 20 } }', 'a')) AS geoms; -- Invalid field name: Coordinates, array is empty
SELECT public.ST_ASEWKT(bson_extract_geometry_runtime('{ "a" : [{"b": [44, 44]}, {"b": "This is ok"}, {"b": ["This is not", "ok"]}, {"b": {"type": "Point", "coordinates": [51,51]}} ]}', 'a.b')) AS geoms;

-- Case 5: Valid cases
SELECT public.ST_ASEWKT(bson_extract_geometry('{ "a" : { "field1": 10, "field2": 10 } }', 'a'));
SELECT public.ST_ASEWKT(bson_extract_geometry('{ "a" : { "thisCanBeAnything": 10, "Doesn''t matter": 10 } }', 'a'));
SELECT public.ST_ASEWKT(bson_extract_geometry('{ "a" : { "long": 10, "lat": 10, "extra": "Ignored" } }', 'a'));
SELECT public.ST_ASEWKT(bson_extract_geometry('{ "a" : { "long": { "$numberInt": "10" }, "lat": { "$numberInt": "10" } } }', 'a'));
SELECT public.ST_ASEWKT(bson_extract_geometry('{ "a" : { "long": { "$numberLong": "10" }, "lat": { "$numberLong": "10" } } }', 'a'));
SELECT public.ST_ASEWKT(bson_extract_geometry('{ "a" : { "long": { "$numberDouble": "10" }, "lat": { "$numberDouble": "10" } } }', 'a'));
SELECT public.ST_ASEWKT(bson_extract_geometry('{ "a" : { "long": { "$numberDecimal": "10" }, "lat": { "$numberDecimal": "10" } } }', 'a'));
SELECT public.ST_ASEWKT(bson_extract_geometry('{ "a" : { "long": { "$numberDouble": "1.23122e-2" }, "lat": { "$numberDecimal": "1.3160e1" } } }', 'a'));

SELECT public.ST_ASEWKT(bson_extract_geometry('{ "a" : [10, 10] }', 'a'));
SELECT public.ST_ASEWKT(bson_extract_geometry('{ "a" : [10, 10, 20, 30, 40, 50] }', 'a'));
SELECT public.ST_ASEWKT(bson_extract_geometry('{ "a" : [{"$numberInt": "10"}, {"$numberInt": "10"}] }', 'a'));
SELECT public.ST_ASEWKT(bson_extract_geometry('{ "a" : [{"$numberLong": "10"}, {"$numberLong": "10"}] }', 'a'));
SELECT public.ST_ASEWKT(bson_extract_geometry('{ "a" : [{"$numberDouble": "10"}, {"$numberDouble": "10"}] }', 'a'));
SELECT public.ST_ASEWKT(bson_extract_geometry('{ "a" : [{"$numberDecimal": "10"}, {"$numberDecimal": "10"}] }', 'a'));
SELECT public.ST_ASEWKT(bson_extract_geometry('{ "a" : [{"$numberDouble": "1.23122e-2"}, {"$numberDecimal": "1.3160e1"}] }', 'a'));

SELECT public.ST_ASEWKT(bson_extract_geometry('{ "a" : [{ "1": 33, "2": 33 }, { "1": 35, "2": 35  }]}', 'a'));
SELECT public.ST_ASEWKT(bson_extract_geometry('{ "a" : [[10, 10], [15, 15], [20, 20]] }', 'a'));
SELECT public.ST_ASEWKT(bson_extract_geometry('{ "a" : [[10, 10], { "point": 15, "point2": 15 }, [20, 20]] }', 'a'));
SELECT public.ST_ASEWKT(bson_extract_geometry('{ "a" : [[10, 10, "extra"], [15, 15, "extra"], [20, 20, "extra"]] }', 'a'));
SELECT public.ST_ASEWKT(bson_extract_geometry('{ "a" : [[{"$numberInt": "10"}, {"$numberInt": "10"}], [{"$numberLong": "15"}, {"$numberLong": "15"}], [{"$numberDouble": "20"}, {"$numberDouble": "20"}], [{"$numberDecimal": "25"}, {"$numberDecimal": "25"}] ] }', 'a'));
SELECT public.ST_ASEWKT(bson_extract_geometry('{ "a" : [-180, 180] }', 'a'));

-- nested path valid cases
SELECT public.ST_ASEWKT(bson_extract_geometry('{ "a" : { "b" : [50, 50] } }', 'a.b'));
SELECT public.ST_ASEWKT(bson_extract_geometry('{ "a" : [ { "b": [10, 10] }, {"b": [20, 20] }, { "b": [30, 30] } ] }', 'a.b'));
SELECT public.ST_ASEWKT(bson_extract_geometry('{ "a" : [ { "b": [10, 10] }, {"b": [20, 20] }, { "b": false } ] }', 'a.b'));
SELECT public.ST_ASEWKT(bson_extract_geometry('{ "a" : [ { }, [ ], { "b": [25, 25 ]} ] }', 'a.b'));
SELECT public.ST_ASEWKT(bson_extract_geometry('{ "a" : [ { }, { "b": [30, 30 ]}, [], { "b": {"val1": 45, "val2": 45 } } ] }', 'a.b'));
SELECT public.ST_ASEWKT(bson_extract_geometry('{ "a" : [ {"b": { "1": 33, "2": 33 }}, {"b": "Not valid representation"} ]}', 'a.b'));
SELECT public.ST_ASEWKT(bson_extract_geometry('{ "a" : [ {"b": { "1": 33, "2": 33 }}, {"b": []} ]}', 'a.b'));
SELECT public.ST_ASEWKT(bson_extract_geometry('{ "a" : [ {"noBHere": [10, 10]}, {"b": { "1": 33, "2": 33 }}, {"b": []} ]}', 'a.b'));
SELECT public.ST_ASEWKT(bson_extract_geometry('{ "a" : [
    { "b": [ { "long": [70, 80], "lat": 75 }, { "long": [80, 90], "lat": 85 } ] },
    { "b": [ { "long": [90, 100], "lat": 95 }, { "long": [100, 110], "lat": 105 } ] },
    { "b": [ { "long": [110, 120], "lat": 115 }, { "long": [120, 130], "lat": 125 } ] } ]}', 'a.b.long'));


-- Case 5.1: All Valid cases work even for non strict mode
SELECT public.ST_ASEWKT(bson_extract_geometry_runtime('{ "a" : { "field1": 10, "field2": 10 } }', 'a')) AS geoms;
SELECT public.ST_ASEWKT(bson_extract_geometry_runtime('{ "a" : { "thisCanBeAnything": 10, "Doesn''t matter": 10 } }', 'a')) AS geoms;
SELECT public.ST_ASEWKT(bson_extract_geometry_runtime('{ "a" : { "long": 10, "lat": 10, "extra": "Ignored" } }', 'a')) AS geoms;
SELECT public.ST_ASEWKT(bson_extract_geometry_runtime('{ "a" : { "long": { "$numberInt": "10" }, "lat": { "$numberInt": "10" } } }', 'a')) AS geoms;
SELECT public.ST_ASEWKT(bson_extract_geometry_runtime('{ "a" : { "long": { "$numberLong": "10" }, "lat": { "$numberLong": "10" } } }', 'a')) AS geoms;
SELECT public.ST_ASEWKT(bson_extract_geometry_runtime('{ "a" : { "long": { "$numberDouble": "10" }, "lat": { "$numberDouble": "10" } } }', 'a')) AS geoms;
SELECT public.ST_ASEWKT(bson_extract_geometry_runtime('{ "a" : { "long": { "$numberDecimal": "10" }, "lat": { "$numberDecimal": "10" } } }', 'a')) AS geoms;
SELECT public.ST_ASEWKT(bson_extract_geometry_runtime('{ "a" : { "long": { "$numberDouble": "1.23122e-2" }, "lat": { "$numberDecimal": "1.3160e1" } } }', 'a')) AS geoms;

SELECT public.ST_ASEWKT(bson_extract_geometry_runtime('{ "a" : [10, 10] }', 'a')) AS geoms;
SELECT public.ST_ASEWKT(bson_extract_geometry_runtime('{ "a" : [10, 10, 20, 30, 40, 50] }', 'a')) AS geoms;
SELECT public.ST_ASEWKT(bson_extract_geometry_runtime('{ "a" : [{"$numberInt": "10"}, {"$numberInt": "10"}] }', 'a')) AS geoms;
SELECT public.ST_ASEWKT(bson_extract_geometry_runtime('{ "a" : [{"$numberLong": "10"}, {"$numberLong": "10"}] }', 'a')) AS geoms;
SELECT public.ST_ASEWKT(bson_extract_geometry_runtime('{ "a" : [{"$numberDouble": "10"}, {"$numberDouble": "10"}] }', 'a')) AS geoms;
SELECT public.ST_ASEWKT(bson_extract_geometry_runtime('{ "a" : [{"$numberDecimal": "10"}, {"$numberDecimal": "10"}] }', 'a')) AS geoms;
SELECT public.ST_ASEWKT(bson_extract_geometry_runtime('{ "a" : [{"$numberDouble": "1.23122e-2"}, {"$numberDecimal": "1.3160e1"}] }', 'a')) AS geoms;

SELECT public.ST_ASEWKT(bson_extract_geometry_runtime('{ "a" : [{ "1": 33, "2": 33 }, { "1": 35, "2": 35  }]}', 'a')) AS geoms;
SELECT public.ST_ASEWKT(bson_extract_geometry_runtime('{ "a" : [[10, 10], [15, 15], [20, 20]] }', 'a')) AS geoms;
SELECT public.ST_ASEWKT(bson_extract_geometry_runtime('{ "a" : [[10, 10], { "point": 15, "point2": 15 }, [20, 20]] }', 'a')) AS geoms;
SELECT public.ST_ASEWKT(bson_extract_geometry_runtime('{ "a" : [[10, 10, "extra"], [15, 15, "extra"], [20, 20, "extra"]] }', 'a')) AS geoms;
SELECT public.ST_ASEWKT(bson_extract_geometry_runtime('{ "a" : [[{"$numberInt": "10"}, {"$numberInt": "10"}], [{"$numberLong": "15"}, {"$numberLong": "15"}], [{"$numberDouble": "20"}, {"$numberDouble": "20"}], [{"$numberDecimal": "25"}, {"$numberDecimal": "25"}] ] }', 'a')) AS geoms;
SELECT public.ST_ASEWKT(bson_extract_geometry_runtime('{ "a" : [-180, 180] }', 'a')) AS geoms;

-- nested path valid cases
SELECT public.ST_ASEWKT(bson_extract_geometry_runtime('{ "a" : { "b" : [50, 50] } }', 'a.b')) AS geoms;
SELECT public.ST_ASEWKT(bson_extract_geometry_runtime('{ "a" : [ { "b": [10, 10] }, {"b": [20, 20] }, { "b": [30, 30] } ] }', 'a.b')) AS geoms;
SELECT public.ST_ASEWKT(bson_extract_geometry_runtime('{ "a" : [ { "b": [10, 10] }, {"b": [20, 20] }, { "b": false } ] }', 'a.b')) AS geoms;
SELECT public.ST_ASEWKT(bson_extract_geometry_runtime('{ "a" : [ { }, [ ], { "b": [25, 25 ]} ] }', 'a.b')) AS geoms;
SELECT public.ST_ASEWKT(bson_extract_geometry_runtime('{ "a" : [ { }, { "b": [30, 30 ]}, [], { "b": {"val1": 45, "val2": 45 } } ] }', 'a.b')) AS geoms;
SELECT public.ST_ASEWKT(bson_extract_geometry_runtime('{ "a" : [ {"b": { "1": 33, "2": 33 }}, {"b": "Not valid representation"} ]}', 'a.b')) AS geoms;
SELECT public.ST_ASEWKT(bson_extract_geometry_runtime('{ "a" : [ {"b": { "1": 33, "2": 33 }}, {"b": []} ]}', 'a.b')) AS geoms;
SELECT public.ST_ASEWKT(bson_extract_geometry_runtime('{ "a" : [ {"noBHere": [10, 10]}, {"b": { "1": 33, "2": 33 }}, {"b": []} ]}', 'a.b')) AS geoms;
SELECT public.ST_ASEWKT(bson_extract_geometry_runtime('{ "a" : [
    { "b": [ { "long": [70, 80], "lat": 75 }, { "long": [80, 90], "lat": 85 } ] },
    { "b": [ { "long": [90, 100], "lat": 95 }, { "long": [100, 110], "lat": 105 } ] },
    { "b": [ { "long": [110, 120], "lat": 115 }, { "long": [120, 130], "lat": 125 } ] } ]}', 'a.b.long')) AS geoms;


-- Test to check if segmentize does leave geometries invalid in few cases. The test below shows a perfectly fine geography can be made into an invalid
-- or intersecting points geometry by ST_SEGMENTIZE and to fix this we should use ST_MAKEVALID.
SELECT public.ST_ISVALID('SRID=4326;POLYGON((-180 40, -157.5 40, -157.5 55, -180 55, -180 40))'::public.geography::public.geometry);

SET client_min_messages = 'warning';
SELECT public.ST_ISVALID(public.ST_SEGMENTIZE('SRID=4326;POLYGON((-180 40, -157.5 40, -157.5 55, -180 55, -180 40))'::public.geography, 500000)::public.geometry); -- 500km is our default segment length
RESET client_min_messages;

-- This is the base segmentize query when run without ST_MAKEVALID

DO $$
DECLARE
    errorMessage text;
BEGIN
    SELECT public.ST_SUBDIVIDE(
        public.ST_SEGMENTIZE('SRID=4326;POLYGON((-180 40, -157.5 40, -157.5 55, -180 55, -180 40))'::public.geography, 500000)::public.geometry,
        8 -- 8 is the default number of max vertices a divide segment can have
    )::public.geography;
    EXCEPTION WHEN OTHERS THEN
        errorMessage := SQLERRM;
        IF errorMessage LIKE '%Self-intersection at%' THEN
            errorMessage := REGEXP_REPLACE(errorMessage, '[0-9]+(\.[0-9]+)? [0-9]+(\.[0-9]+)?', '<longitude> <latitude>');
        END IF;
        RAISE EXCEPTION '%', errorMessage using ERRCODE = SQLSTATE;
END $$;

-- Now run this again with ST_MAKEVALID to see the difference, ST_ASTEXT is used to pretty print the output
SELECT public.ST_ASTEXT(
    public.ST_SUBDIVIDE(
        public.ST_MAKEVALID(public.ST_SEGMENTIZE('SRID=4326;POLYGON((-180 40, -157.5 40, -157.5 55, -180 55, -180 40))'::public.geography, 500000)::public.geometry),
        8 -- 8 is the default number of max vertices a divide segment can have
    )::public.geography
);

SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "segmentsTest", "indexes": [{"key": {"a": "2dsphere"}, "name": "my_2ds" }]}', true);

--Insert 2 points one out one at the boundary and one inside the original polygon
SELECT documentdb_api.insert_one('db','segmentsTest','{ "_id" : 1, "a" : { "type": "Point", "coordinates": [ -167, 48] } }', NULL); -- inside
SELECT documentdb_api.insert_one('db','segmentsTest','{ "_id" : 2, "a" : { "type": "Point", "coordinates": [ -180, 40] } }', NULL); -- overlap with a vertex
SELECT documentdb_api.insert_one('db','segmentsTest','{ "_id" : 3, "a" : { "type": "Point", "coordinates": [ -142, 49] } }', NULL); -- outside
                                                 
SELECT document FROM documentdb_api.collection('db', 'segmentsTest') WHERE document @@ '{"a": {"$geoIntersects": { "$geometry": { "type": "Polygon", "coordinates": [[[-180, 40], [-157.5, 40], [-157.5, 55], [-180, 55], [-180, 40]]]}}}}';
EXPLAIN SELECT document FROM documentdb_api.collection('db', 'segmentsTest') WHERE document @@ '{"a": {"$geoIntersects": { "$geometry": { "type": "Polygon", "coordinates": [[[-180, 40], [-157.5, 40], [-157.5, 55], [-180, 55], [-180, 40]]]}}}}'; -- Polygon with points on a great circle of earth

RESET search_path;