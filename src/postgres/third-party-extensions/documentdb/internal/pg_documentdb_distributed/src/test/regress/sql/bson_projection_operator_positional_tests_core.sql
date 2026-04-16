SET search_path TO documentdb_core,documentdb_api,documentdb_api_catalog,documentdb_api_internal;
SET citus.next_shard_id TO 270000;
SET documentdb.next_collection_id TO 2700;
SET documentdb.next_collection_index_id TO 2700;

-- Invalid scenarios
SELECT * FROM bson_dollar_project_find('{ "a" :[1,2,3] }', '{ "a.$": 1, "b.$": 1}', '{}');
SELECT * FROM bson_dollar_project_find('{ "a" :[1,2,3] }', '{ "a..$": 1}', '{}');
SELECT * FROM bson_dollar_project_find('{ "a" :[1,2,3] }', '{ "a.$": {"$slice": 1}}', '{}');
SELECT * FROM bson_dollar_project_find('{ "a" :[1,2,3] }', '{ "a.$": 0}', '{}');
SELECT * FROM bson_dollar_project_find('{ "a" :[1,2,3] }', '{ "a.$": [1,2,3]}', '{}');
SELECT * FROM bson_dollar_project_find('{ "a" :[1,2,3] }', '{ "a.$": "Hello"}', '{}');
SELECT * FROM bson_dollar_project_find('{ "a" :[1,2,3] }', '{ "a.$": {"$numberDecimal": "0"}}', '{}');
SELECT * FROM bson_dollar_project_find('{ "a" :[1,2,3] }', '{ "a": 1, "a.$": 1 }', '{}');
SELECT * FROM bson_dollar_project_find('{ "a" :[1,2,3] }', '{ "a.b": 1, "a.b.$": 1 }', '{}');
SELECT * FROM bson_dollar_project_find('{ "a" :[1,2,3] }', '{ "a": 1, "a.b.$": 1 }', '{}');
SELECT * FROM bson_dollar_project_find('{ "a" :[1,2,3] }', '{ "a.$": 1, "a": 1 }', '{}');
SELECT * FROM bson_dollar_project_find('{ "a" :[1,2,3] }', '{ "a.b.$": 1, "a.b": 1 }', '{}');
SELECT * FROM bson_dollar_project_find('{ "a" :[1,2,3] }', '{ "a.b": 1, "a.$": 1 }', '{}');

-- some basic positional projection test
SELECT * FROM bson_dollar_project_find('{ "a" :[1,2,3] }', '{ "a.$": {"$numberDecimal": "Infinity"}}', '{"a": 1}');
SELECT * FROM bson_dollar_project_find('{ "a" :[1,2,3] }', '{ "a.$": {"$numberDecimal": "NaN"}}', '{"a": 2}');
SELECT * FROM bson_dollar_project_find('{ "_id": 1, "a" :[1,2,3] }', '{ "a.$": 1}', '{"a": 1}');
SELECT * FROM bson_dollar_project_find('{ "_id": 1, "a" :[1,2,3] }', '{ "a.$": 1}', '{"a": 2}');
SELECT * FROM bson_dollar_project_find('{ "_id": 1, "a" :[1,2,3] }', '{ "a.$": 1}', '{"a": 3}');
SELECT * FROM bson_dollar_project_find('{ "_id": 1, "a" :[1,2,3], "b": [6,5,4] }', '{ "a.$": 1}', '{"b": 6}');
SELECT * FROM bson_dollar_project_find('{ "_id": 1, "a" :[1,2,3], "b": [6,5,4], "c": [7,8,9] }', '{ "b.$": 1}', '{"a": 1, "c": 8, "b": 5}');

-- with $and
SELECT * FROM bson_dollar_project_find('{ "a" :[1,2,3], "b": [6,5,4], "c": [7,8,9] }', '{ "b.$": 1}',
                                        '{"$and": [{"a": 1}, {"c": 8}, {"b": 5}]}');
SELECT * FROM bson_dollar_project_find('{ "a" :[1,2,3], "b": [6,5,4], "c": [7,8,9] }', '{ "b.$": 1}',
                                        '{"$and": [{"a": 2}, {"a": 3}, {"a": 1}]}');
-- with $nor and $not and complex $or - Error
SELECT * FROM bson_dollar_project_find('{ "a" :[1,2,3], "b": [6,5,4], "c": [7,8,9] }', '{ "b.$": 1}',
                                        '{"$nor": [{"a": 1}, {"c": 8}, {"b": 5}]}');
SELECT * FROM bson_dollar_project_find('{ "a" :[1,2,3], "b": [6,5,4], "c": [7,8,9] }', '{ "b.$": 1}',
                                        '{"a": {"$not": {"$eq": 3}}}');
SELECT * FROM bson_dollar_project_find('{ "a" :[1,2,3], "b": [6,5,4], "c": [7,8,9] }', '{ "b.$": 1}',
                                        '{"$or": [{"a": 3}, {"b": 6}]}');
SELECT * FROM bson_dollar_project_find('{ "a" :[1,2,3], "b": [6,5,4], "c": [7,8,9] }', '{ "b.$": 1}',
                                        '{"$or": [{"a": {"$gte": 1}}, {"a": {"$lt": 3}}]}');

-- with $in
SELECT * FROM bson_dollar_project_find('{ "a" :[1,2,3], "b": [6,5,4], "c": [7,8,9] }', '{ "b.$": 1}',
                                        '{"a": {"$in": [1,2,3]}}');
SELECT * FROM bson_dollar_project_find('{ "a" :[1,2,3], "b": [6,5,4], "c": [7,8,9] }', '{ "b.$": 1}',
                                        '{"a": {"$in": [4,5,3]}}');

-- $or with simple expressions converted to $in
SELECT * FROM bson_dollar_project_find('{ "a" :[1,2,3], "b": [6,5,4], "c": [7,8,9] }', '{ "b.$": 1}',
                                        '{"$or": [{"a": 2}, {"a": 3}, {"a": 1}]}');
SELECT * FROM bson_dollar_project_find('{ "a" :[1,2,3], "b": [6,5,4], "c": [7,8,9] }', '{ "b.$": 1}',
                                        '{"$or": [{"a": {"$in": [3]}}, {"a": {"$in": [2]}}, {"a": {"$in": [1]}}]}');

-- positional with various conditions
SELECT * FROM bson_dollar_project_find('{ "a" :[1,2,3], "b": [6,5,4], "c": [7,8,9] }', '{ "b.$": 1}',
                                        '{"a": {"$elemMatch": {"$eq": 2}}}');
SELECT * FROM bson_dollar_project_find('{ "a" :[[1],[2],[3]], "b": [6,5,4], "c": [7,8,9] }', '{ "b.$": 1}',
                                        '{"a": {"$elemMatch": {"$elemMatch": {"$eq": 3}}}}');
SELECT * FROM bson_dollar_project_find('{ "a" :[[1,2], [3,4]], "b": [[5,6], [7,8]], "c": [7,8,9] }', '{ "b.$": 1}',
                                        '{"a": {"$elemMatch": {"$elemMatch": {"$eq": 3}}}}');
SELECT * FROM bson_dollar_project_find('{ "a" :[{"b": [1,2,3]}, {"b": [4,5,6]}, {"b": [7,8,9]}], "c": [10, 11, 12] }', '{ "c.$": 1}',
                                        '{"a": {"$elemMatch": {"b": {"$elemMatch": {"$eq": 6}}}}}');
SELECT * FROM bson_dollar_project_find('{ "x": [{"y": 1}, {"y": 2}, {"y": 3}] }', '{ "x.$": 1}',
                                        '{"x": {"$elemMatch": {"y": {"$gt": 1}}}}');
SELECT * FROM bson_dollar_project_find('{ "a" :[1, {"b": [4,5,6]}, {"b": [7,8,9]}], "c": [10, 11, 12] }', '{ "c.$": 1}',
                                        '{"a": {"$elemMatch": {}}}');
SELECT * FROM bson_dollar_project_find('{ "a" :[1,2,3], "b": [6,5,4], "c": [7,8,9] }', '{ "b.$": 1}',
                                        '{"a": {"$type": "int"}}');
SELECT * FROM bson_dollar_project_find('{ "a" :[1,2,"string"], "b": [6,5,4], "c": [7,8,9] }', '{ "b.$": 1}',
                                        '{"a": {"$type": "string"}}');
SELECT * FROM bson_dollar_project_find('{ "a" :[{"c": 1}, {"c": 2}, {"b": 3}], "b": [6,5,4], "c": [7,8,9] }', '{ "b.$": 1}',
                                        '{"a.b": {"$exists": true}}');

-- non existent fields
SELECT * FROM bson_dollar_project_find('{"x": [{"a": 1, "b": 2}, {"a": 2, "c": 3}, {"a": 1, "d": 5}], "y": [{"aa": 1, "bb": 2}, {"aa": 2, "cc": 3}, {"aa": 1, "dd": 5}]}', '{"g.$": 1}', '{}');

-- positional on non array fields
SELECT * FROM bson_dollar_project_find('{ "a" :[{"c": 1}, {"c": 2}, {"b": 3}], "b": 25, "c": [7,8,9] }', '{ "b.$": 1}',
                                        '{"a.b": {"$exists": true}}');

-- on nested path
SELECT * FROM bson_dollar_project_find('{"x": [-1, 1, 2], "a": {"b": {"d": [1,2,3]}, "f": 456}, "e": 123}', '{"a.b.c.$": 1}', '{"x": {"$gt": 0}}');
SELECT * FROM bson_dollar_project_find('{"x": {"y": [{"a": 1, "b": 1}, {"a": 1, "b": 2}]}}', '{"x.y.$": 1}', '{"x.y.a": 1}');
SELECT * FROM bson_dollar_project_find('{"x": [1], "a": [{"b":[[[{"c": 1, "d": 2}]]]}]}', '{"a.b.c.$": 1}', '{"x": 1}'); -- Only relevant path is included

-- in a nested path match the query on the first array and include only the fields requested in positional
SELECT * FROM bson_dollar_project_find('{"x": {"y": {"z": [0,1,2]}}, "a": [{"b": {"c": [1,2,3]}}, {"b": {"c": [4,5,6]}}, {"b": {"c": [7,8,9]}}]}', '{"a.b.c.$": 1}', '{"x.y.z": 1}');
SELECT * FROM bson_dollar_project_find('{"x": {"y": {"z": [0,1,2]}}, "a": {"b": [{"c": [1,2,3]}, {"c": [4,5,6]}, {"c": [7,8,9]}]}}', '{"a.b.c.$": 1}', '{"x.y.z": 1}');
SELECT * FROM bson_dollar_project_find('{"x": {"y": {"z": [0,1,2]}}, "a": {"b": {"c": [1,2,3]}}}', '{"a.b.c.$": 1}', '{"x.y.z": 1}');

-- errors if no match found or matched index is greater than size of array
SELECT * FROM bson_dollar_project_find('{ "_id": 1, "a" :[1,2,3], "b": [4,5,6,7] }', '{ "a.$": 1}', '{"b": 2}');
SELECT * FROM bson_dollar_project_find('{ "_id": 1, "a" :[1,2,3], "b": [4,5,6,7] }', '{ "a.$": 1}', '{"b": 7}');

-- Test with multiple docs
BEGIN;
SELECT documentdb_api.insert_one('db','positionalProjection', '{"_id": 1, "a" : { "b" : [{"c": 1, "d": 1}, {"c": 2, "d": 2}]}, "x": [1,2]}', NULL);
SELECT documentdb_api.insert_one('db','positionalProjection', '{"_id": 2, "a" : { "b" : {"c": [11, 12], "d": [13, 14]} }, "x": [1,2]}', NULL);
SELECT bson_dollar_project_find(document, '{"a.b.c.$": 1}', '{"x": 2}')
    FROM documentdb_api.collection('db', 'positionalProjection')
    WHERE document @@ '{ "x": 2}' 
    ORDER BY object_id;
ROLLBACK;

-- Empty or null query Spec
SELECT * FROM bson_dollar_project_find('{ "_id": 1, "a" :[1,2,3], "b": {"c": [{"d": 1, "e": 2}]} }', '{ "a.$": 1}', NULL);
SELECT * FROM bson_dollar_project_find('{ "_id": 1, "a" :[1,2,3], "b": {"c": [{"d": 1, "e": 2}]} }', '{ "a.$": 1}', '{}');
SELECT * FROM bson_dollar_project_find('{ "_id": 1, "a" :[1,2,3], "b": {"c": [{"d": 1, "e": 2}]} }', '{ "b.c.d.$": 1}', NULL);
SELECT * FROM bson_dollar_project_find('{ "_id": 1, "a" :[1,2,3], "b": {"c": [{"d": 1, "e": 2}]} }', '{ "b.c.d.$": 1}', '{}');

-- Empty or null with non array spec field works
SELECT * FROM bson_dollar_project_find('{ "_id": 1, "a" :[1,2,3], "b": {"c": [{"d": 1, "e": 2}]} }', '{ "b.$": 1}', NULL);
SELECT * FROM bson_dollar_project_find('{ "_id": 1, "a" :[1,2,3], "b": {"c": {"d": 1, "e": 2}} }', '{ "b.c.d.$": 1}', '{}');


-- $project and $addFields doesn't support positional, TODO throw native mongo error for $project
SELECT * FROM bson_dollar_project('{ "_id": 1, "a" :[{ "b": {"c": [1,3,2]}, "d": {"e": [4,5,6]}}]} ', '{"a.b.c.$": 1 }');
SELECT bson_dollar_add_fields('{"a": {"b": [1,2,3]}}', '{ "a.b.$" : "1", "a.y": ["p", "q"]}');

--sharded collection tests
BEGIN;
-- Insert data into a new collection to be sharded
SELECT documentdb_api.insert_one('db','positional_sharded',' { "_id" : 0, "key": {"a": "b"}, "a" : [{"b": 1, "c": 1}, {"b": 1, "c": 2}], "x":[11,12,13] }', NULL);
SELECT documentdb_api.insert_one('db','positional_sharded',' { "_id" : 1, "key": {"a": "b"}, "a" : [{"b": {"c": 1}}, {"b": {"c": 2}}], "x":[11,12,13] }', NULL);
SELECT documentdb_api.insert_one('db','positional_sharded',' { "_id" : 2, "key": {"b": "c"}, "a" : { "b": [{"c": 1, "d": 1}, {"c": 2, "d": 2}] }, "x":[11,12,13] }', NULL);
SELECT documentdb_api.insert_one('db','positional_sharded',' { "_id" : 3, "key": {"c": "d"}, "a" : { "b": {"c": [{"d": 1, "d": 2}], "e": [1,2,3]} }, "x":[11,12,13] }', NULL);

-- Shard orders collection on key
SELECT documentdb_api.shard_collection('db','positional_sharded', '{"key":"hashed"}', false);
SELECT bson_dollar_project_find(document, '{"a.b.c.$": 1}', '{"x": 11}')
    FROM documentdb_api.collection('db', 'positional_sharded')
    WHERE document @@ '{ "x": 11}' 
    ORDER BY object_id;
ROLLBACK;
