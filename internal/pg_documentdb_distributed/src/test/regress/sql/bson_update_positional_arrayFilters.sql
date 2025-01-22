SET search_path TO documentdb_core,documentdb_api,documentdb_api_catalog,documentdb_api_internal;
SET citus.next_shard_id TO 220000;
SET documentdb.next_collection_id TO 2200;
SET documentdb.next_collection_index_id TO 2200;

-- arrayFilters with aggregation pipeline
SELECT newDocument as bson_update_document FROM documentdb_api_internal.bson_update_document(
    '{"_id": 1 }','{ "": [ { "$addFields": { "c.d": 1 } }]}', '{}', '{ "": [] }');
SELECT newDocument as bson_update_document FROM documentdb_api_internal.bson_update_document(
    '{"_id": 1 }','{ "": [ { "$addFields": { "c.d": 1 } }]}', '{}', NULL);
SELECT newDocument as bson_update_document FROM documentdb_api_internal.bson_update_document(
    '{"_id": 1 }','{ "": [ { "$addFields": { "c.d": 1 } }]}', '{}', '{ "": [ { "foo": 2 }]}');

-- arrayFilters ignored on replace
SELECT newDocument as bson_update_document FROM documentdb_api_internal.bson_update_document(
    '{"_id": 1 }','{ "": { "a": 2 } }', '{}', '{ "": [ { "foo": 2 }]}');

-- arrayFilters with update fails - missing array filter
SELECT newDocument as bson_update_document FROM documentdb_api_internal.bson_update_document(
    '{"_id": 1 }','{ "": { "$set": { "a.$[a]": 2 }}}', '{}', '{ "": [] }');

-- arrayFilters with update fails - invalid array filters
SELECT newDocument as bson_update_document FROM documentdb_api_internal.bson_update_document(
    '{"_id": 1 }','{ "": { "$set": { "a.$[a]": 2 }}}', '{}', '{ "": [ 1 ] }');
SELECT newDocument as bson_update_document FROM documentdb_api_internal.bson_update_document(
    '{"_id": 1 }','{ "": { "$set": { "a.$[a]": 2 }}}', '{}', '{ "": [ {} ] }');
SELECT newDocument as bson_update_document FROM documentdb_api_internal.bson_update_document(
    '{"_id": 1 }','{ "": { "$set": { "a.$[a]": 2 }}}', '{}', '{ "": [ { "": 1} ] }');
SELECT newDocument as bson_update_document FROM documentdb_api_internal.bson_update_document(
    '{"_id": 1 }','{ "": { "$set": { "a.$[a]": 2 }}}', '{}', '{ "": [ { "a": 1, "b.c": 1 } ] }');
SELECT newDocument as bson_update_document FROM documentdb_api_internal.bson_update_document(
    '{"_id": 1 }','{ "": { "$set": { "a.$[a]": 2 }}}', '{}', '{ "": [ { "a": 1 }, { "a": 2 } ] }');

-- simple array update on equality
SELECT newDocument as bson_update_document FROM documentdb_api_internal.bson_update_document(
    '{"_id": 1, "myArray": [ 0, 1 ] }','{ "": { "$set": { "myArray.$[element]": 2 }}}', '{}', '{ "": [{ "element": 0 }] }');
SELECT newDocument as bson_update_document FROM documentdb_api_internal.bson_update_document(
    '{}','{ "": { "$set": { "myArray.$[element]": 2 }}}', '{"_id": 1, "myArray": [ 0, 1 ] }', '{ "": [{ "element": 0 }] }');
SELECT newDocument as bson_update_document FROM documentdb_api_internal.bson_update_document(
    '{"_id": 1 }','{ "": { "$set": { "myArray.$[element]": 2 }}}', '{}', '{ "": [{ "element": 0 }] }');

-- updates on $gte condition
SELECT newDocument as bson_update_document FROM documentdb_api_internal.bson_update_document(
    '{ "_id" : 1, "grades" : [ 95, 92, 90 ] }','{ "": { "$set": { "grades.$[element]": 100 }}}', '{}', '{ "": [{ "element": { "$gte": 100 } }] }');
SELECT newDocument as bson_update_document FROM documentdb_api_internal.bson_update_document(
    '{ "_id" : 3, "grades" : [ 95, 110, 100, 98, 102 ] }','{ "": { "$set": { "grades.$[element]": 100 }}}', '{}', '{ "": [{ "element": { "$gte": 100 } }] }');

-- nested arrayFilters.
SELECT newDocument as bson_update_document FROM documentdb_api_internal.bson_update_document(
    '{ "_id" : 3, "grades" : [ { "grade": 80, "mean": 75, "std": 6}, { "grade": 85, "mean": 90, "std": 4 }, { "grade": 87, "mean": 85, "std": 6 } ] }',
    '{ "": { "$set": { "grades.$[elem].mean": 100 }}}', '{}', '{ "": [{ "elem.grade": { "$gte": 85 } }] }');
SELECT newDocument as bson_update_document FROM documentdb_api_internal.bson_update_document(
    '{ "_id" : 3, "grades" : [ { "grade": 80, "mean": 75, "std": 6}, { "grade": 85, "mean": 90, "std": 4 }, { "grade": 87, "mean": 85, "std": 6 } ] }',
    '{ "": { "$inc": { "grades.$[elem].std": -50 }}}', '{}', '{ "": [{ "elem.grade": { "$gte": 85 }, "elem.std": { "$gte": 5 } }] }');
SELECT newDocument as bson_update_document FROM documentdb_api_internal.bson_update_document(
    '{ "_id" : 3, "grades" : [ { "grade": 80, "mean": 75, "std": 6}, { "grade": 85, "mean": 90, "std": 4 }, { "grade": 87, "mean": 85, "std": 6 } ] }',
    '{ "": { "$inc": { "grades.$[elem].std": -50 }}}', '{}', '{ "": [{ "elem.grade": { "$gte": 85 }, "elem.std": { "$gte": 4 } }] }');


-- negation operators
SELECT newDocument as bson_update_document FROM documentdb_api_internal.bson_update_document(
    '{ "_id" : 1, "degrees" : [ { "level": "Master" }, { "level": "Bachelor" } ] }',
    '{ "": { "$set" : { "degrees.$[degree].gradcampaign" : 1 }} }', '{}', '{ "": [{ "degree.level": { "$ne": "Bachelor" } }] }');

-- multiple positional operators
SELECT newDocument as bson_update_document FROM documentdb_api_internal.bson_update_document(
    '{ "_id" : 1, "grades" : [ { "type": "quiz", "questions": [ 10, 8, 5 ] }, { "type": "quiz", "questions": [ 8, 9, 6 ] }, { "type": "hw", "questions": [ 5, 4, 3 ] }, { "type": "exam", "questions": [ 25, 10, 23, 0 ] }] }',
    '{ "": { "$inc": { "grades.$[t].questions.$[score]": 90 }} }', '{}', '{ "": [{ "t.type": "quiz" }, { "score": { "$gte": 8 } }] }');
SELECT newDocument as bson_update_document FROM documentdb_api_internal.bson_update_document(
    '{ "_id" : 1, "grades" : [ { "type": "quiz", "questions": [ 10, 8, 5 ] }, { "type": "quiz", "questions": [ 8, 9, 6 ] }, { "type": "hw", "questions": [ 5, 4, 3 ] }, { "type": "exam", "questions": [ 25, 10, 23, 0 ] }] }',
    '{ "": { "$inc": { "grades.$[].questions.$[score]": 90 }} }', '{}', '{ "": [{ "score": { "$gte": 8 } }] }');
SELECT newDocument as bson_update_document FROM documentdb_api_internal.bson_update_document(
    '{ "_id" : 1, "grades" : [ { "type": "quiz", "questions": [ 10, 8, 5 ] }, { "type": "quiz", "questions": [ 8, 9, 6 ] }, { "type": "hw", "questions": [ 5, 4, 3 ] }, { "type": "exam", "questions": [ 25, 10, 23, 0 ] }] }',
    '{ "": { "$inc": { "grades.$[t].questions.$[]": 90 }} }', '{}', '{ "": [{ "t.type": "quiz" }] }');

-- arrayFilters for all Update operators should recurse if for a single level nested array
-- array update operators
SELECT newDocument as bson_update_document FROM documentdb_api_internal.bson_update_document(
    '{ "_id" : 1, "array" : [ [0], [1] ] }',
    '{ "": { "$addToSet": { "array.$[i]": 2 }} }', '{}', '{ "": [{ "i": 0 }] }');
SELECT newDocument as bson_update_document FROM documentdb_api_internal.bson_update_document(
    '{ "_id" : 1, "array" : [ [0, 1], [1, 2] ] }',
    '{ "": { "$pop": { "array.$[i]": 1 }} }', '{}', '{ "": [{ "i": 0 }] }');
SELECT newDocument as bson_update_document FROM documentdb_api_internal.bson_update_document(
    '{ "_id" : 1, "array" : [ [0, 1], [1, 2] ] }',
    '{ "": { "$pull": { "array.$[i]": 1 }} }', '{}', '{ "": [{ "i": 2 }] }');
SELECT newDocument as bson_update_document FROM documentdb_api_internal.bson_update_document(
    '{ "_id" : 1, "array" : [ [0, 1], [1, 2] ] }',
    '{ "": { "$pull": { "array.$[i]": 1 }} }', '{}', '{ "": [{ "i": 2 }] }');
SELECT newDocument as bson_update_document FROM documentdb_api_internal.bson_update_document(
    '{ "_id" : 1, "array" : [ [0, 1], [2, 3] ] }',
    '{ "": { "$push": { "array.$[i]": 1 }} }', '{}', '{ "": [{ "i": 1 }] }');

-- field update operators, should be able to match but apply update based on the type requirement
SELECT newDocument as bson_update_document FROM documentdb_api_internal.bson_update_document(
    '{ "_id" : 1, "array" : [ [0], [1] ] }',
    '{ "": { "$inc": { "array.$[i]": 10 }} }', '{}', '{ "": [{ "i": 0 }] }');
SELECT newDocument as bson_update_document FROM documentdb_api_internal.bson_update_document(
    '{ "_id" : 1, "array" : [ [0], [1] ] }',
    '{ "": { "$min": { "array.$[i]": 10 }} }', '{}', '{ "": [{ "i": 0 }] }');
SELECT newDocument as bson_update_document FROM documentdb_api_internal.bson_update_document(
    '{ "_id" : 1, "array" : [ [0], [1] ] }',
    '{ "": { "$max": { "array.$[i]": 10 }} }', '{}', '{ "": [{ "i": 0 }] }');
SELECT newDocument as bson_update_document FROM documentdb_api_internal.bson_update_document(
    '{ "_id" : 1, "array" : [ [0], [1] ] }',
    '{ "": { "$mul": { "array.$[i]": 2 }} }', '{}', '{ "": [{ "i": 0 }] }');
SELECT newDocument as bson_update_document FROM documentdb_api_internal.bson_update_document(
    '{ "_id" : 1, "array" : [ [0], [1] ] }',
    '{ "": { "$rename": { "array.$[i]": "a.3" }} }', '{}', '{ "": [{ "i": 0 }] }');
SELECT newDocument as bson_update_document FROM documentdb_api_internal.bson_update_document(
    '{ "_id" : 1, "array" : [ [0], [1] ] }',
    '{ "": { "$set": { "array.$[i]": "newValue" }} }', '{}', '{ "": [{ "i": 0 }] }');

-- bit operator
SELECT newDocument as bson_update_document FROM documentdb_api_internal.bson_update_document(
    '{ "_id" : 1, "array" : [ [0], [1] ] }',
    '{ "": { "$bit": { "array.$[i]": {"or": 5} }} }', '{}', '{ "": [{ "i": 0 }] }');

-- Check array value should also match in arrayFilters
SELECT newDocument as bson_update_document FROM documentdb_api_internal.bson_update_document(
    '{ "_id" : 1, "array" : [ [1,2,3], [4,5,6] ] }',
    '{ "": { "$set": { "array.$[i]": [11,12,13] }} }', '{}', '{ "": [{ "i": [1,2,3] }] }');
SELECT newDocument as bson_update_document FROM documentdb_api_internal.bson_update_document(
    '{ "_id" : 1, "array" : [ [1,2,3], [4,5,6] ] }',
    '{ "": { "$set": { "array.$[i]": 3 }} }', '{}', '{ "": [{ "i": {"$size": 3} }] }');
