-- Test $elemMatch projection operator

SET search_path TO documentdb_core,documentdb_api,documentdb_api_catalog,documentdb_api_internal;
SET citus.next_shard_id TO 280000;
SET documentdb.next_collection_id TO 2800;
SET documentdb.next_collection_index_id TO 2800;

SELECT * FROM bson_dollar_project_find('{ "_id": 1, "a": [ 11, 21, 31, 41 ], "z": "World" }', '{ "a":{ "$elemMatch":{ "$gt":25 }} }', '{}');

SELECT * FROM bson_dollar_project_find('{ "_id": 2, "a": [ 11, 21, 31, 41 ], "f": [ { "g": [ 14, 24, 34, 44 ], "h": "Hello" }, { "g": [ 15, 25, 35, 45 ], "h": "world" }], "z": "World" }', 
                                  '{ "f": {"$elemMatch": {"g": {"$elemMatch": {"$gt":25 }}}} }', '{}');

-- Empty $elemMatch Predicate should match the first array or object value
SELECT * FROM bson_dollar_project_find(
    '{ "_id": 2, "a": [ 11, 21, 31, 41, {"b": 51}, 61 ] }', 
    '{ "a": {"$elemMatch": {} } }',
    '{}'
);

SELECT * FROM bson_dollar_project_find(
    '{ "_id": 2, "a": [ 11, 21, 31, 41, [51,52,53], 61 ] }', 
    '{ "a": {"$elemMatch": {} } }',
    '{}'
);

SELECT * FROM bson_dollar_project_find(
    '{ "_id": 2, "a": [ 11, 21, 31, 41, [51,52,53], 61 ] }', 
    '{ "a": {"$elemMatch": {"$or": [{}, {}]} } }',
    '{}'
);

-- this is negation so won't match any
SELECT * FROM bson_dollar_project_find(
    '{ "_id": 2, "a": [ 11, 21, 31, 41, [51,52,53], 61 ] }', 
    '{ "a": {"$elemMatch": {"$nor": [{}, {}]} } }',
    '{}'
);

SELECT * FROM bson_dollar_project_find(
    '{ "_id": 2, "a": [ 11, 21, 31, 41, [51,52,53], 61 ] }', 
    '{ "a": {"$elemMatch": {"$and": [{}, {}]} } }',
    '{}'
);

-- $elemMatch on non array fields are skipped

SELECT * FROM bson_dollar_project_find(
    '{ "_id": 2, "a": [ 11, 21, 31, 41, [51,52,53], 61 ], "b": {"c": 2}, "d": 71 }', 
    '{ "b": {"$elemMatch": {"$and": [{}, {}]} } }',
    '{}'
);

SELECT * FROM bson_dollar_project_find(
    '{ "_id": 2, "a": [ 11, 21, 31, 41, [51,52,53], 61 ], "b": {"c": 2}, "d": 71 }', 
    '{ "d": {"$elemMatch": {"$and": [{}, {}]} } }',
    '{}'
);

-- Testing order of $elemMatch projection for inclusion and exclusion projections
-- Inclusion projection - elemMatch is projected at the end & multiple elemMatch projections are projected based on projection spec order
SELECT * FROM bson_dollar_project_find(
    '{ "_id": 2, "a": [ 11, 21, 31 ], "b": [41, 51, 61], "d": 71, "e": {"f": "good"} }', 
    '{ "b": {"$elemMatch": {"$gt": 45} }, "a": {"$elemMatch": {"$lt": 25}}, "e": 1, "d": 1 }',
    '{}'
);

SELECT * FROM bson_dollar_project_find(
    '{ "_id": 2, "a": [ 11, 21, 31 ], "b": [41, 51, 61], "d": 71, "e": {"f": "good"} }', 
    '{ "b": {"$elemMatch": {"$gt": 45} }, "d": 1, "e": {"$add": [2,3]} }',
    '{}'
);

SELECT * FROM bson_dollar_project_find(
    '{"x": [1, 2, 3], "y": [4, 5, 6], "z": [7, 8, 9]}',
    '{"z": {"$elemMatch": {"$eq": 8} }, "x": {"$elemMatch": {"$eq": 3} }, "y": {"$elemMatch": {"$eq": 4} }}',
    '{}'
);

-- Exclusions projection - elemMatch follows document order
SELECT * FROM bson_dollar_project_find(
    '{ "_id": 2, "a": [ 11, 21, 31 ], "b": [41, 51, 61], "d": 71, "e": {"f": "good"} }', 
    '{ "b": {"$elemMatch": {"$gt": 45} }, "a": {"$elemMatch": {"$lt": 25}}, "e": 0, "d": 0 }',
    '{}'
);

SELECT * FROM bson_dollar_project_find(
    '{ "_id": 2, "a": [ 11, 21, 31 ], "b": [41, 51, 61], "d": 71, "e": {"f": "good"} }', 
    '{ "b": {"$elemMatch": {"$gt": 45} }, "e": 0}',
    '{}'
);

-- should error
SELECT * FROM bson_dollar_project_find(
    '{ "_id": 2, "a": [ 11, 21, 31 ], "b": [41, 51, 61], "d": 71, "e": {"f": "good"} }', 
    '{ "e": 1, "b": {"$elemMatch": {"$gt": 45} }, "a": {"$elemMatch": {"$lt": 25}}, "d": 0 }',
    '{}'
);

-- various predicates
SELECT * FROM bson_dollar_project_find(
    '{"x": [{"a": 1}, {"a": 180}, {"a": 4}]}', 
    '{"x": {"$elemMatch": {"$or": [{"a": {"$eq": 4}}, {"$and": [{"a": {"$mod": [12, 0]}}, {"a": {"$mod": [15, 0]}}]}]}}}',
    '{}'
);

SELECT * FROM bson_dollar_project_find(
    '{"x": [{"a": 1, "b": 2}, {"a": 2, "c": 3}, {"a": 1, "d": 5}], "y": [{"aa": 1, "bb": 2}, {"aa": 2, "cc": 3}, {"aa": 1, "dd": 5}]}',
    '{"x": {"$elemMatch": {"d": {"$exists": true}}}}',
    '{}'
);

SELECT * FROM bson_dollar_project_find(
    '{"x": [[1, 2, 3], [4, 5, 6], [7, 8, 9]]}',
    '{"x": {"$elemMatch": {"$elemMatch": {"$gt": 5, "$lt": 7}}}}',
    '{}'
);

-- Multiple elemMatch projections
SELECT * FROM bson_dollar_project_find(
    '{"x": [1, 2, 3], "y": [4, 5, 6], "z": [7, 8, 9]}',
    '{"z": {"$elemMatch": {"$eq": 8} }, "x": {"$elemMatch": {"$eq": 3} }, "y": {"$elemMatch": {"$eq": 4} }}',
    '{}'
);

-- -- Error Cases
-- $elemMatch on nested fields
SELECT * FROM bson_dollar_project_find('{ "_id": 1, "a": {"b" : [ 11, 21, 31, 41 ], "z": "World" }}', '{"a.b": {"$elemMatch": {"$gt":25} } }', '{}');
SELECT * FROM bson_dollar_project_find('{ "_id": 1, "a": {"b" : [ 11, 21, 31, 41 ], "z": "World" }}', '{"a": {"b": {"$elemMatch": {"$gt":25} } } }', '{}');

-- When positional and elemMatch projection are given together
SELECT * FROM bson_dollar_project_find(
    '{ "_id": 2, "a": [ 11, 21, 31 ], "b": [41, 51, 61], "d": 71, "e": {"f": "good"} }', 
    '{ "a": {"$elemMatch": {"$gt": 15} }, "b.$": 1 }',
    '{ "a": {"$elemMatch": {"$gt": 15} } }'
);

SELECT * FROM bson_dollar_project_find(
    '{ "_id": 2, "a": [ 11, 21, 31 ], "b": [41, 51, 61], "d": 71, "e": {"f": "good"} }', 
    '{ "b.$": 1, "a": {"$elemMatch": {"$gt": 15} } }',
    '{ "a": {"$elemMatch": {"$gt": 15} } }'
);

-- $jsonSchema doesn't work with projection $elemMatch
SELECT * FROM bson_dollar_project_find(
    '{ "_id": 2, "a": [ 11, 21, 31 ], "b": [41, 51, 61], "d": 71, "e": {"f": "good"} }', 
    '{ "a": {"$elemMatch": {"$jsonSchema": {}} } }',
    '{ }'
);


-- This is a bad place for jsonSchema spec, because $jsonSchema comes at top level field
SELECT * FROM bson_dollar_project_find(
    '{ "_id": 2, "a": [ 11, 21, 31 ], "b": [41, 51, 61], "d": 71, "e": {"f": "good"} }', 
    '{ "a": {"$elemMatch": {"b" : {"$jsonSchema": {}} } } }',
    '{ }'
);
--unresolved fields
select bson_dollar_project_find('{"_id":"1", "a" :[[1],[2],[3],[4]]}', '{"c" : { "$elemMatch" : {}}}',NULL);
select bson_dollar_project_find('{"_id":"1", "a" :[1,2,3,4]}', '{"a" : { "$elemMatch" : {"$gt" : 1}}, "c" : { "$elemMatch" : {}}}',NULL);
