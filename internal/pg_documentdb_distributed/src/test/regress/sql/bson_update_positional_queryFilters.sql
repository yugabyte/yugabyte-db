SET search_path TO documentdb_core,documentdb_api,documentdb_api_catalog,documentdb_api_internal;
SET citus.next_shard_id TO 200000;
SET documentdb.next_collection_id TO 2000;
SET documentdb.next_collection_index_id TO 2000;

-- simple array update on equality
SELECT newDocument as bson_update_document FROM documentdb_api_internal.bson_update_document(
    '{ "_id" : 1, "grades" : [ 85, 80, 80 ] }','{ "": { "$set": { "grades.$" : 82 } } }', '{ "_id": 1, "grades": 80 }', NULL);
SELECT newDocument as bson_update_document FROM documentdb_api_internal.bson_update_document(
    '{ "_id" : 2, "grades" : [ 88, 90, 92 ] }','{ "": { "$set": { "grades.$" : 82 } } }', '{ "_id": 1, "grades": 80 }', NULL);
SELECT newDocument as bson_update_document FROM documentdb_api_internal.bson_update_document(
    '{ "_id" : 4, "grades": [ { "grade": 80, "mean": 75, "std": 8 }, { "grade": 85, "mean": 90, "std": 5 }, { "grade": 85, "mean": 85, "std": 8 } ] }',
    '{ "": { "$set": { "grades.$.std" : 6 } } }', '{ "_id": 4, "grades.grade": 85 }', NULL);

SELECT newDocument as bson_update_document FROM documentdb_api_internal.bson_update_document(
    '{ "_id" : 4, "grades": [ { "grade": 80, "mean": 75, "std": 8 }, { "grade": 85, "mean": 90, "std": 5 }, { "grade": 85, "mean": 85, "std": 8 } ] }',
    '{ "": { "$set": { "grades.$.std" : 6 } } }', '{ "_id": 5, "grades": { "$elemMatch": { "grade": { "$lte": 90 }, "mean": { "$gt": 80 } } } }', NULL);


-- positional on match that doesn't work
SELECT newDocument as bson_update_document FROM documentdb_api_internal.bson_update_document(
    '{ "_id": 1, "a": [ 1, 2, 3 ], "b": [ 4, 5, 6 ], "c": 8 }', '{ "": { "$set": { "a.$": 100 } } }', '{ "c": 8 }');
SELECT newDocument as bson_update_document FROM documentdb_api_internal.bson_update_document(
    '{ "_id": 1, "a": [ 1, 2, 3 ], "b": [ 4, 5, 6 ], "c": 8 }', '{ "": { "$set": { "a.$": 100 } } }', '{ "$or": [ { "b": 5 }, { "c": 8 } ] }');
SELECT newDocument as bson_update_document FROM documentdb_api_internal.bson_update_document(
    '{ "_id": 1, "a": [ 1, 2, 3 ], "b": [ 4, 5, 6 ], "c": 8 }', '{ "": { "$set": { "a.$": 100 } } }', '{ "$nor": [ { "b": 5 }, { "c": 8 } ] }');
SELECT newDocument as bson_update_document FROM documentdb_api_internal.bson_update_document(
    '{ "_id": 1, "a": [ 1, 2, 3 ], "b": [ 4, 5, 6 ], "c": 8 }', '{ "": { "$set": { "a.$": 100 } } }', '{ "b": { "$not": { "$eq": 8 } } }');

-- positional on alternate array (pick first match)
SELECT newDocument as bson_update_document FROM documentdb_api_internal.bson_update_document(
    '{ "_id": 1, "a": [ 1, 2, 3 ], "b": [ 4, 5, 6 ], "c": 8 }', '{ "": { "$set": { "a.$": 100 } } }', '{ "b": { "$in": [ 5, 6 ]} }');

-- test $or with simple filters
SELECT newDocument as bson_update_document FROM documentdb_api_internal.bson_update_document(
    '{ "_id": 1, "a": [ 1, 2, 3 ], "b": [ 4, 5, 6 ], "c": 8 }', '{ "": { "$set": { "a.$": 100 } } }', '{ "$or": [ { "b": 5 }, { "b": 6 } ] }');

-- test various filter operators
SELECT newDocument as bson_update_document FROM documentdb_api_internal.bson_update_document(
    '{ "_id": 1, "a": [ 1, 2, 3 ], "b": [ 4, 5, 6 ], "c": 8 }', '{ "": { "$set": { "a.$": 100 } } }', '{ "b": 5 }');
SELECT newDocument as bson_update_document FROM documentdb_api_internal.bson_update_document(
    '{ "_id": 1, "a": [ 1, 2, 3 ], "b": [ 4, 5, 6 ], "c": 8 }', '{ "": { "$set": { "a.$": 100 } } }', '{ "b": { "$ne": 5 } }');
SELECT newDocument as bson_update_document FROM documentdb_api_internal.bson_update_document(
    '{ "_id": 1, "a": [ 1, 2, 3 ], "b": [ 4, 5, 6 ], "c": 8 }', '{ "": { "$set": { "a.$": 100 } } }', '{ "b": { "$nin": [ 4, 5] } }');
SELECT newDocument as bson_update_document FROM documentdb_api_internal.bson_update_document(
    '{ "_id": 1, "a": [ 1, 2, 3 ], "b": [ 4, 5, 6 ], "c": 8 }', '{ "": { "$set": { "a.$": 100 } } }', '{ "b": { "$gte": 5 } }');
SELECT newDocument as bson_update_document FROM documentdb_api_internal.bson_update_document(
    '{ "_id": 1, "a": [ 1, 2, 3 ], "b": [ 4, 5, 6 ], "c": 8 }', '{ "": { "$set": { "a.$": 100 } } }', '{ "b": { "$lte": 5 } }');
SELECT newDocument as bson_update_document FROM documentdb_api_internal.bson_update_document(
    '{ "_id": 1, "a": [ 1, 2, 3 ], "b": [ "bat", "bar", "baz" ], "c": 8 }', '{ "": { "$set": { "a.$": 100 } } }', '{ "b": { "$regularExpression": { "pattern": "^ba[rz]", "options": "" } } }');
SELECT newDocument as bson_update_document FROM documentdb_api_internal.bson_update_document(
    '{ "_id": 1, "a": [ 1, 2, 3 ], "b": [ "bat", "bar", "baz" ], "c": 8 }', '{ "": { "$set": { "a.$": 100 } } }', '{ "b": { "$exists": true } }');
SELECT newDocument as bson_update_document FROM documentdb_api_internal.bson_update_document(
    '{ "_id": 1, "a": [ 1, 2, 3 ], "b": [ "bat", "bar", "baz" ], "c": 8 }', '{ "": { "$set": { "a.$": 100 } } }', '{ "b": { "$type": "number" } }');
SELECT newDocument as bson_update_document FROM documentdb_api_internal.bson_update_document(
    '{ "_id": 1, "a": [ 1, 2, 3 ], "b": [ true, "bar", "baz" ], "c": 8 }', '{ "": { "$set": { "a.$": 100 } } }', '{ "b": { "$type": "string" } }');
SELECT newDocument as bson_update_document FROM documentdb_api_internal.bson_update_document(
    '{ "_id": 1, "a": [ 1, 2, 3 ], "b": [ { "c": 1 }, { "c": 2 }, { "c": 3 } ], "c": 8 }', '{ "": { "$set": { "a.$": 100 } } }', '{ "b.c": { "$elemMatch": { "$eq": 2 }} }');
SELECT newDocument as bson_update_document FROM documentdb_api_internal.bson_update_document(
    '{ "_id": 1, "a": [ 1, 2, 3 ], "b": [ { "c": 1 }, { "c": 2 }, { "c": 3 } ], "c": 8 }', '{ "": { "$set": { "a.$": 100 } } }', '{ "b.c": { "$elemMatch": { "$eq": 2 }} }');
SELECT newDocument as bson_update_document FROM documentdb_api_internal.bson_update_document(
    '{ "_id": 1, "a": [ 1, 2, 3 ], "b": [ 4, 5, 6 ], "c": 8 }', '{ "": { "$set": { "a.$": 100 } } }', '{ "b": { "$all": [ 5, 6 ] } }');
SELECT newDocument as bson_update_document FROM documentdb_api_internal.bson_update_document(
    '{ "_id": 1, "a": [ 1, 2, 3 ], "b": [ 4, 5, 6 ], "c": 8 }', '{ "": { "$set": { "a.$": 100 } } }', '{ "b": { "$bitsAnySet": [ 0, 1 ]} }');

-- $elemMatch with empty document
SELECT newDocument as bson_update_document FROM documentdb_api_internal.bson_update_document(
    '{ "_id": 1, "a": [ 1, 2, 3 ], "b": [ 4, { "c": 5 }, 6 ], "c": 8 }', '{ "": { "$set": { "a.$": 100 } } }', '{ "b": { "$elemMatch": {} }}');

-- $all with $elemmatch
SELECT newDocument as bson_update_document FROM documentdb_api_internal.bson_update_document(
    '{ "_id": 1, "a": [ 1, 2, 3 ], "b": [ { "c": 1 }, { "c": 2 }, { "c": 3 } ], "c": 8 }', '{ "": { "$set": { "a.$": 100 } } }', '{ "b": { "$all": [ { "$elemMatch": { "c": 2 } } ]} }');
SELECT newDocument as bson_update_document FROM documentdb_api_internal.bson_update_document(
    '{ "_id": 1, "a": [ 1, 2, 3 ], "b": [ { "c": 1 }, { "c": 2 }, { "c": 3 } ], "c": 8 }', '{ "": { "$set": { "a.$": 100 } } }', '{ "b": { "$all": [ { "$elemMatch": { "c": 2 } } , { "$elemMatch": { "c": 3 } } ]} }');

-- upsert cases
SELECT newDocument as bson_update_document FROM documentdb_api_internal.bson_update_document(
    '{}', '{ "": { "$set": { "a.$": 100 } } }', '{ "b": [ 1, 2, 3 ], "a": [ 4, 5, 6 ] }');

-- simple example for non-array
SELECT newDocument as bson_update_document FROM documentdb_api_internal.bson_update_document(
    '{ "_id": 3, "a": { "b": [ { "c": 1, "d": 2 } ] } }', '{ "": { "$set": { "a.$.c": 11 } }}', '{ "_id": 1 }');
SELECT newDocument as bson_update_document FROM documentdb_api_internal.bson_update_document(
    '{ "_id": 3, "a": { "b": [ { "c": 1, "d": 2 } ] } }', '{ "": { "$set": { "a.$.c": 11 } }}', '{ "a.b.c": 1 }');

-- miscellenous cases
SELECT newDocument as bson_update_document FROM documentdb_api_internal.bson_update_document('{ "_id": 1, "a" : [1,2,3,4], "b" : [5,6,7,8] }', '{ "": { "$set": { "a.$": "new" } }}', '{ "b": 6 }');
SELECT newDocument as bson_update_document FROM documentdb_api_internal.bson_update_document('{ "_id": 1, "a" : [1,2,3,4], "b" : [5,6,7,8] }', '{ "": { "$set": { "a.$": "new", "b.$" : "new" } }}', '{"a" : 3 ,"b": 6 }');
SELECT newDocument as bson_update_document FROM documentdb_api_internal.bson_update_document('{"_id" : 1, "key":[{"id":1,"a":1},{"id":2,"a":1}],"key2":[{"id":1,"a":1}]}', '{ "": { "$set": { "key.$.a": 5 } }}', '{"key.id": 2, "key2.id" : 3}');
SELECT newDocument as bson_update_document FROM documentdb_api_internal.bson_update_document('{ "_id": 1, "a" : [1,2,3,4], "b" : [5,6,7,8], "c" : 1 }', '{ "": { "$set": { "a.$": "new", "b.$" : "new" } }}', '{"a" : 3 ,"b": 6, "c" : 1 }');
SELECT newDocument as bson_update_document FROM documentdb_api_internal.bson_update_document('{ "_id": 1, "a" : [1,2,3,4], "b" : [5,6,7,8], "c" : 1 }', '{ "": { "$set": { "a.$": "new", "b.$" : "new" } }}', '{"c" : 1 }');
SELECT newDocument as bson_update_document FROM documentdb_api_internal.bson_update_document('{ "_id": 1, "a" : 1, "c" : 1 }', '{ "": { "$set": { "a.$": "new"} }}', '{"a" :  1}');


