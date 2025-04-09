SET search_path TO documentdb_core,documentdb_api,documentdb_api_catalog,documentdb_api_internal;
SET citus.next_shard_id TO 5500000;
SET documentdb.next_collection_id TO 5500;
SET documentdb.next_collection_index_id TO 5500;

-- $and operator
-- should return true
SELECT * FROM bson_dollar_project('{}', '{"result": { "$and": true}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$and": 1}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$and": { }}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$and": "str"}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$and": []}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$and": [true]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$and": [true, true]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$and": [true, 1]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$and": [true, 0.1]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$and": [true, {"$numberDecimal": "0.1"}]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$and": [true, { }]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$and": [true, [ ]]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$and": [true, [ 1, 2]]}}');
SELECT * FROM bson_dollar_project('{"a": true, "b": true, "c": true}', '{"result": { "$and": ["$a", "$b", "$c"]}}');
SELECT * FROM bson_dollar_project('{"a": { "b": true, "c": true}}', '{"result": { "$and": ["$a.b", "$a.c"]}}');
SELECT * FROM bson_dollar_project('{"a": { "c": true}}', '{"result": { "$and": [{"$add": [0, 1]}, "$a.c"]}}');

-- should return false
SELECT * FROM bson_dollar_project('{}', '{"result": { "$and": false}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$and": 0}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$and": null}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$and": "$a"}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$and": [false]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$and": [false, true]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$and": [false, 0]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$and": [0.1, false]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$and": [false, {"$numberDecimal": "0.1"}]}}');
SELECT * FROM bson_dollar_project('{"a": true, "b": true, "c": false}', '{"result": { "$and": ["$a", "$b", "$c"]}}');
SELECT * FROM bson_dollar_project('{"a": { "b": true, "c": false}}', '{"result": { "$and": ["$a.b", "$a.c"]}}');
SELECT * FROM bson_dollar_project('{"a": { "c": true}}', '{"result": { "$and": [{"$add": [0, 0]}, "$a.c"]}}');

-- If nested expression parses to a constant that evaluates to an error, the error from the nested expression will be thrown. 
SELECT * FROM bson_dollar_project('{"a": { "c": true}}', '{"result": { "$and": [false, {"$divide": [1, 0]}, "$a.c"]}}');
SELECT * FROM bson_dollar_project('{"a": { "c": true}}', '{"result": { "$and": [false, "$a.c", {"$subtract": [1, {"$date": {"$numberLong": "11232"}}]}]}}');
SELECT * FROM bson_dollar_project('{"a": { "c": false}}', '{"result": { "$and": [{"$add": [0, 1]}, "$a.c", {"$not": [1,2]}]}}');

-- If nested expression parses to a non-constant (eg: path) that eventually evaluates to an error, shortcircuit evaluation will occur.
SELECT * FROM bson_dollar_project('{"a": { "c": 0}}', '{"result": { "$and": [false, {"$divide": [1, "$a.c"]}, "$a.c"]}}');
SELECT * FROM bson_dollar_project('{"a": { "c": 0}}', '{"result": { "$and": [true, {"$divide": [1, "$a.c"]}, "$a.c"]}}');

-- $or operator
-- should return true
SELECT * FROM bson_dollar_project('{}', '{"result": { "$or": true}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$or": 1}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$or": -1}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$or": -23.453}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$or": { "$numberDecimal": "-0.4" }}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$or": { }}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$or": "str"}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$or": [true]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$or": [true, true]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$or": [true, false]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$or": [false, false, true]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$or": [false, 0.1]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$or": [false, {"$numberDecimal": "0.1"}]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$or": [true, { }]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$or": [false, [ ]]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$or": [[ 1, 2], true]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$or": [null, true]}}');
SELECT * FROM bson_dollar_project('{"a": true, "b": false, "c": true}', '{"result": { "$or": ["$a", "$b", "$c"]}}');
SELECT * FROM bson_dollar_project('{"a": { "b": true, "c": true}}', '{"result": { "$or": ["$z", "$a.b", "$a.c"]}}');
SELECT * FROM bson_dollar_project('{"a": { "c": false}}', '{"result": { "$or": [{"$add": [0, 1]}, "$a.c"]}}');

-- should return false
SELECT * FROM bson_dollar_project('{}', '{"result": { "$or": false}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$or": 0}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$or": null}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$or": "$z"}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$or": []}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$or": [false]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$or": [false, false]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$or": [false, false, 0]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$or": [false, 0.0]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$or": [false, {"$numberDecimal": "0.0"}]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$or": [false, null]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$or": [false, "$d"]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$or": [null, false]}}');
SELECT * FROM bson_dollar_project('{"a": false, "b": false, "c": false}', '{"result": { "$or": ["$a", "$b", "$c"]}}');
SELECT * FROM bson_dollar_project('{"a": { "b": false, "c": false}}', '{"result": { "$or": ["$z", "$a.b", "$a.c"]}}');
SELECT * FROM bson_dollar_project('{"a": { "c": false}}', '{"result": { "$or": [{"$add": [0, 0]}, "$a.c"]}}');

-- If nested expression parses to a constant that evaluates to an error, the error from the nested expression will be thrown. 
SELECT * FROM bson_dollar_project('{"a": false, "b": false, "c": false}', '{"result": { "$or": ["$a", "$b", "$c", {"$divide": []}]}}');
SELECT * FROM bson_dollar_project('{"a": { "c": true}}', '{"result": { "$or": [false, {"$divide": [1, 0]}, "$a.c"]}}');
SELECT * FROM bson_dollar_project('{"a": { "c": true}}', '{"result": { "$or": [false, "$a.c", {"$subtract": [1, {"$date": {"$numberLong": "11232"}}]}]}}');
SELECT * FROM bson_dollar_project('{"a": { "c": false}}', '{"result": { "$or": [{"$add": [0, 1]}, "$a.c", {"$not": [1,2]}]}}');

-- If nested expression parses to a non-constant (eg: path) that eventually evaluates to an error, shortcircuit evaluation will occur.
SELECT * FROM bson_dollar_project('{"a": { "c": 0}}', '{"result": { "$or": [true, {"$divide": [1, "$a.c"]}, "$a.c"]}}');
SELECT * FROM bson_dollar_project('{"a": { "c": 0}}', '{"result": { "$or": [false, {"$divide": [1, "$a.c"]}, "$a.c"]}}');

-- $not operator
-- should return true
SELECT * FROM bson_dollar_project('{}', '{"result": { "$not": false}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$not": {"$numberDecimal": "0"}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$not": null}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$not": [null]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$not": [false]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$not": ["$z"]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$not": {"$and": [false, true]}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$not": {"$or": [false, false]}}}');
SELECT * FROM bson_dollar_project('{"a": false}', '{"result": { "$not": ["$a"]}}');
SELECT * FROM bson_dollar_project('{"a": 0}', '{"result": { "$not": ["$a"]}}');
SELECT * FROM bson_dollar_project('{"a": 0.0}', '{"result": { "$not": ["$a"]}}');

-- should return false
SELECT * FROM bson_dollar_project('{}', '{"result": { "$not": true}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$not": 1}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$not": -1}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$not": -1.23}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$not": {"$numberDecimal": "-0.3"}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$not": {}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$not": ["str"]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$not": [true]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$not": {"$and": [true, true]}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$not": {"$or": [false, true]}}}');
SELECT * FROM bson_dollar_project('{"a": true}', '{"result": { "$not": ["$a"]}}');
SELECT * FROM bson_dollar_project('{"a": 1}', '{"result": { "$not": ["$a"]}}');
SELECT * FROM bson_dollar_project('{"a": 0.1}', '{"result": { "$not": ["$a"]}}');

-- should return error for wrong number of args
SELECT * FROM bson_dollar_project('{}', '{"result": { "$not": []}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$not": [true, false]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$not": [true, false, 2, 3]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$not": [{"$divide": [1, 0]}, false, 2, 3]}}');
