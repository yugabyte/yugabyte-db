SET search_path TO documentdb_core,documentdb_api,documentdb_api_catalog,documentdb_api_internal;
SET citus.next_shard_id TO 200000;
SET documentdb.next_collection_id TO 2000;
SET documentdb.next_collection_index_id TO 2000;

-- simple array update on equality
SELECT documentdb_api_internal.update_bson_document (
    '{ "_id" : 1, "grades" : [ 85, 80, 80 ] }','{ "": { "$set": { "grades.$" : 82 } } }', '{ "_id": 1, "grades": 80 }', NULL::documentdb_core.bson, NULL::documentdb_core.bson, NULL::TEXT);
SELECT documentdb_api_internal.update_bson_document (
    '{ "_id" : 2, "grades" : [ 88, 90, 92 ] }','{ "": { "$set": { "grades.$" : 82 } } }', '{ "_id": 1, "grades": 80 }', NULL::documentdb_core.bson, NULL::documentdb_core.bson, NULL::TEXT);
SELECT documentdb_api_internal.update_bson_document (
    '{ "_id" : 4, "grades": [ { "grade": 80, "mean": 75, "std": 8 }, { "grade": 85, "mean": 90, "std": 5 }, { "grade": 85, "mean": 85, "std": 8 } ] }',
    '{ "": { "$set": { "grades.$.std" : 6 } } }', '{ "_id": 4, "grades.grade": 85 }', NULL::documentdb_core.bson, NULL::documentdb_core.bson, NULL::TEXT);

SELECT documentdb_api_internal.update_bson_document (
    '{ "_id" : 4, "grades": [ { "grade": 80, "mean": 75, "std": 8 }, { "grade": 85, "mean": 90, "std": 5 }, { "grade": 85, "mean": 85, "std": 8 } ] }',
    '{ "": { "$set": { "grades.$.std" : 6 } } }', '{ "_id": 5, "grades": { "$elemMatch": { "grade": { "$lte": 90 }, "mean": { "$gt": 80 } } } }', NULL::documentdb_core.bson, NULL::documentdb_core.bson, NULL::TEXT);


-- positional on match that doesn't work
SELECT documentdb_api_internal.update_bson_document (
    '{ "_id": 1, "a": [ 1, 2, 3 ], "b": [ 4, 5, 6 ], "c": 8 }', '{ "": { "$set": { "a.$": 100 } } }', '{ "c": 8 }', NULL::documentdb_core.bson, NULL::documentdb_core.bson, NULL::TEXT);
SELECT documentdb_api_internal.update_bson_document (
    '{ "_id": 1, "a": [ 1, 2, 3 ], "b": [ 4, 5, 6 ], "c": 8 }', '{ "": { "$set": { "a.$": 100 } } }', '{ "$or": [ { "b": 5 }, { "c": 8 } ] }', NULL::documentdb_core.bson, NULL::documentdb_core.bson, NULL::TEXT);
SELECT documentdb_api_internal.update_bson_document (
    '{ "_id": 1, "a": [ 1, 2, 3 ], "b": [ 4, 5, 6 ], "c": 8 }', '{ "": { "$set": { "a.$": 100 } } }', '{ "$nor": [ { "b": 5 }, { "c": 8 } ] }', NULL::documentdb_core.bson, NULL::documentdb_core.bson, NULL::TEXT);
SELECT documentdb_api_internal.update_bson_document (
    '{ "_id": 1, "a": [ 1, 2, 3 ], "b": [ 4, 5, 6 ], "c": 8 }', '{ "": { "$set": { "a.$": 100 } } }', '{ "b": { "$not": { "$eq": 8 } } }', NULL::documentdb_core.bson, NULL::documentdb_core.bson, NULL::TEXT);

-- positional on alternate array (pick first match)
SELECT documentdb_api_internal.update_bson_document (
    '{ "_id": 1, "a": [ 1, 2, 3 ], "b": [ 4, 5, 6 ], "c": 8 }', '{ "": { "$set": { "a.$": 100 } } }', '{ "b": { "$in": [ 5, 6 ]} }', NULL::documentdb_core.bson, NULL::documentdb_core.bson, NULL::TEXT);

-- test $or with simple filters
SELECT documentdb_api_internal.update_bson_document (
    '{ "_id": 1, "a": [ 1, 2, 3 ], "b": [ 4, 5, 6 ], "c": 8 }', '{ "": { "$set": { "a.$": 100 } } }', '{ "$or": [ { "b": 5 }, { "b": 6 } ] }', NULL::documentdb_core.bson, NULL::documentdb_core.bson, NULL::TEXT);

-- test various filter operators
SELECT documentdb_api_internal.update_bson_document (
    '{ "_id": 1, "a": [ 1, 2, 3 ], "b": [ 4, 5, 6 ], "c": 8 }', '{ "": { "$set": { "a.$": 100 } } }', '{ "b": 5 }', NULL::documentdb_core.bson, NULL::documentdb_core.bson, NULL::TEXT);
SELECT documentdb_api_internal.update_bson_document (
    '{ "_id": 1, "a": [ 1, 2, 3 ], "b": [ 4, 5, 6 ], "c": 8 }', '{ "": { "$set": { "a.$": 100 } } }', '{ "b": { "$ne": 5 } }', NULL::documentdb_core.bson, NULL::documentdb_core.bson, NULL::TEXT);
SELECT documentdb_api_internal.update_bson_document (
    '{ "_id": 1, "a": [ 1, 2, 3 ], "b": [ 4, 5, 6 ], "c": 8 }', '{ "": { "$set": { "a.$": 100 } } }', '{ "b": { "$nin": [ 4, 5] } }', NULL::documentdb_core.bson, NULL::documentdb_core.bson, NULL::TEXT);
SELECT documentdb_api_internal.update_bson_document (
    '{ "_id": 1, "a": [ 1, 2, 3 ], "b": [ 4, 5, 6 ], "c": 8 }', '{ "": { "$set": { "a.$": 100 } } }', '{ "b": { "$gte": 5 } }', NULL::documentdb_core.bson, NULL::documentdb_core.bson, NULL::TEXT);
SELECT documentdb_api_internal.update_bson_document (
    '{ "_id": 1, "a": [ 1, 2, 3 ], "b": [ 4, 5, 6 ], "c": 8 }', '{ "": { "$set": { "a.$": 100 } } }', '{ "b": { "$lte": 5 } }', NULL::documentdb_core.bson, NULL::documentdb_core.bson, NULL::TEXT);
SELECT documentdb_api_internal.update_bson_document (
    '{ "_id": 1, "a": [ 1, 2, 3 ], "b": [ "bat", "bar", "baz" ], "c": 8 }', '{ "": { "$set": { "a.$": 100 } } }', '{ "b": { "$regularExpression": { "pattern": "^ba[rz]", "options": "" } } }', NULL::documentdb_core.bson, NULL::documentdb_core.bson, NULL::TEXT);
SELECT documentdb_api_internal.update_bson_document (
    '{ "_id": 1, "a": [ 1, 2, 3 ], "b": [ "bat", "bar", "baz" ], "c": 8 }', '{ "": { "$set": { "a.$": 100 } } }', '{ "b": { "$exists": true } }', NULL::documentdb_core.bson, NULL::documentdb_core.bson, NULL::TEXT);
SELECT documentdb_api_internal.update_bson_document (
    '{ "_id": 1, "a": [ 1, 2, 3 ], "b": [ "bat", "bar", "baz" ], "c": 8 }', '{ "": { "$set": { "a.$": 100 } } }', '{ "b": { "$type": "number" } }', NULL::documentdb_core.bson, NULL::documentdb_core.bson, NULL::TEXT);
SELECT documentdb_api_internal.update_bson_document (
    '{ "_id": 1, "a": [ 1, 2, 3 ], "b": [ true, "bar", "baz" ], "c": 8 }', '{ "": { "$set": { "a.$": 100 } } }', '{ "b": { "$type": "string" } }', NULL::documentdb_core.bson, NULL::documentdb_core.bson, NULL::TEXT);
SELECT documentdb_api_internal.update_bson_document (
    '{ "_id": 1, "a": [ 1, 2, 3 ], "b": [ { "c": 1 }, { "c": 2 }, { "c": 3 } ], "c": 8 }', '{ "": { "$set": { "a.$": 100 } } }', '{ "b.c": { "$elemMatch": { "$eq": 2 }} }', NULL::documentdb_core.bson, NULL::documentdb_core.bson, NULL::TEXT);
SELECT documentdb_api_internal.update_bson_document (
    '{ "_id": 1, "a": [ 1, 2, 3 ], "b": [ { "c": 1 }, { "c": 2 }, { "c": 3 } ], "c": 8 }', '{ "": { "$set": { "a.$": 100 } } }', '{ "b.c": { "$elemMatch": { "$eq": 2 }} }', NULL::documentdb_core.bson, NULL::documentdb_core.bson, NULL::TEXT);
SELECT documentdb_api_internal.update_bson_document (
    '{ "_id": 1, "a": [ 1, 2, 3 ], "b": [ 4, 5, 6 ], "c": 8 }', '{ "": { "$set": { "a.$": 100 } } }', '{ "b": { "$all": [ 5, 6 ] } }', NULL::documentdb_core.bson, NULL::documentdb_core.bson, NULL::TEXT);
SELECT documentdb_api_internal.update_bson_document (
    '{ "_id": 1, "a": [ 1, 2, 3 ], "b": [ 4, 5, 6 ], "c": 8 }', '{ "": { "$set": { "a.$": 100 } } }', '{ "b": { "$bitsAnySet": [ 0, 1 ]} }', NULL::documentdb_core.bson, NULL::documentdb_core.bson, NULL::TEXT);

-- $elemMatch with empty document
SELECT documentdb_api_internal.update_bson_document (
    '{ "_id": 1, "a": [ 1, 2, 3 ], "b": [ 4, { "c": 5 }, 6 ], "c": 8 }', '{ "": { "$set": { "a.$": 100 } } }', '{ "b": { "$elemMatch": {} }}', NULL::documentdb_core.bson, NULL::documentdb_core.bson, NULL::TEXT);

-- $all with $elemmatch
SELECT documentdb_api_internal.update_bson_document (
    '{ "_id": 1, "a": [ 1, 2, 3 ], "b": [ { "c": 1 }, { "c": 2 }, { "c": 3 } ], "c": 8 }', '{ "": { "$set": { "a.$": 100 } } }', '{ "b": { "$all": [ { "$elemMatch": { "c": 2 } } ]} }', NULL::documentdb_core.bson, NULL::documentdb_core.bson, NULL::TEXT);
SELECT documentdb_api_internal.update_bson_document (
    '{ "_id": 1, "a": [ 1, 2, 3 ], "b": [ { "c": 1 }, { "c": 2 }, { "c": 3 } ], "c": 8 }', '{ "": { "$set": { "a.$": 100 } } }', '{ "b": { "$all": [ { "$elemMatch": { "c": 2 } } , { "$elemMatch": { "c": 3 } } ]} }', NULL::documentdb_core.bson, NULL::documentdb_core.bson, NULL::TEXT);

-- upsert cases
SELECT documentdb_api_internal.update_bson_document (
    '{}', '{ "": { "$set": { "a.$": 100 } } }', '{ "b": [ 1, 2, 3 ], "a": [ 4, 5, 6 ] }', NULL::documentdb_core.bson, NULL::documentdb_core.bson, NULL::TEXT);

-- simple example for non-array
SELECT documentdb_api_internal.update_bson_document (
    '{ "_id": 3, "a": { "b": [ { "c": 1, "d": 2 } ] } }', '{ "": { "$set": { "a.$.c": 11 } }}', '{ "_id": 1 }', NULL::documentdb_core.bson, NULL::documentdb_core.bson, NULL::TEXT);
SELECT documentdb_api_internal.update_bson_document (
    '{ "_id": 3, "a": { "b": [ { "c": 1, "d": 2 } ] } }', '{ "": { "$set": { "a.$.c": 11 } }}', '{ "a.b.c": 1 }', NULL::documentdb_core.bson, NULL::documentdb_core.bson, NULL::TEXT);

-- miscellenous cases
SELECT documentdb_api_internal.update_bson_document ('{ "_id": 1, "a" : [1,2,3,4], "b" : [5,6,7,8] }', '{ "": { "$set": { "a.$": "new" } }}', '{ "b": 6 }', NULL::documentdb_core.bson, NULL::documentdb_core.bson, NULL::TEXT);
SELECT documentdb_api_internal.update_bson_document ('{ "_id": 1, "a" : [1,2,3,4], "b" : [5,6,7,8] }', '{ "": { "$set": { "a.$": "new", "b.$" : "new" } }}', '{"a" : 3 ,"b": 6 }', NULL::documentdb_core.bson, NULL::documentdb_core.bson, NULL::TEXT);
SELECT documentdb_api_internal.update_bson_document ('{"_id" : 1, "key":[{"id":1,"a":1},{"id":2,"a":1}],"key2":[{"id":1,"a":1}]}', '{ "": { "$set": { "key.$.a": 5 } }}', '{"key.id": 2, "key2.id" : 3}', NULL::documentdb_core.bson, NULL::documentdb_core.bson, NULL::TEXT);
SELECT documentdb_api_internal.update_bson_document ('{ "_id": 1, "a" : [1,2,3,4], "b" : [5,6,7,8], "c" : 1 }', '{ "": { "$set": { "a.$": "new", "b.$" : "new" } }}', '{"a" : 3 ,"b": 6, "c" : 1 }', NULL::documentdb_core.bson, NULL::documentdb_core.bson, NULL::TEXT);
SELECT documentdb_api_internal.update_bson_document ('{ "_id": 1, "a" : [1,2,3,4], "b" : [5,6,7,8], "c" : 1 }', '{ "": { "$set": { "a.$": "new", "b.$" : "new" } }}', '{"c" : 1 }', NULL::documentdb_core.bson, NULL::documentdb_core.bson, NULL::TEXT);
SELECT documentdb_api_internal.update_bson_document ('{ "_id": 1, "a" : 1, "c" : 1 }', '{ "": { "$set": { "a.$": "new"} }}', '{"a" :  1}', NULL::documentdb_core.bson, NULL::documentdb_core.bson, NULL::TEXT);
