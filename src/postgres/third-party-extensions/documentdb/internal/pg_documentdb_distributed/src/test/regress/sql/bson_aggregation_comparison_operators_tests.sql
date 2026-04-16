SET search_path TO documentdb_core,documentdb_api,documentdb_api_catalog,documentdb_api_internal;
SET citus.next_shard_id TO 5700000;
SET documentdb.next_collection_id TO 5700;
SET documentdb.next_collection_index_id TO 5700;
SET client_min_messages TO DEBUG3;

-- $cmp
-- should return 0
SELECT * FROM bson_dollar_project('{}', '{"result": { "$cmp": [1, 1]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$cmp": [-1, -1]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$cmp": [null, null]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$cmp": ["$a", "$b"]}}');
SELECT * FROM bson_dollar_project('{"a": null}', '{"result": { "$cmp": ["$a", null]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$cmp": ["str", "str"]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$cmp": [0.00002, 0.00002]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$cmp": [true, true]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$cmp": [[3,2,1,2,4,6], [3,2,1,2,4,6]]}}');
SELECT * FROM bson_dollar_project('{"a": {}}', '{"result": { "$cmp": ["$a", {}]}}');
SELECT * FROM bson_dollar_project('{"a": {"b": "$a"}}', '{"result": { "$cmp": [{"$literal": "$a"}, "$a.b"]}}');

-- should return 1
SELECT * FROM bson_dollar_project('{}', '{"result": { "$cmp": [2, 1]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$cmp": [0, -1]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$cmp": [null, "$a"]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$cmp": ["str", null]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$cmp": ["str", "s"]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$cmp": [true, false]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$cmp": [0.0001, 0.00001]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$cmp": [0.00001, {"$numberDouble": "NaN"}]}}');
SELECT * FROM bson_dollar_project('{"a": 1}', '{"result": { "$cmp": [{"$add": [2, "$a"]}, {"$subtract": [2, "$a"]}]}}');
SELECT * FROM bson_dollar_project('{"a": [1, 2, 3, 4]}', '{"result": { "$cmp": ["$a", [1, 2, 3, 3]]}}');
SELECT * FROM bson_dollar_project('{"a": [1, 2, 3, 4]}', '{"result": { "$cmp": ["$a", [1, 2, 3, 3, 5]]}}');
SELECT * FROM bson_dollar_project('{"a": [1, 2, 3, 4]}', '{"result": { "$cmp": ["$a", [1, 2, 3]]}}');
SELECT * FROM bson_dollar_project('{"a": {"b": 1, "c": 1, "d": 0}}', '{"result": { "$cmp": ["$a", {"b": 1, "c": 1}]}}');
SELECT * FROM bson_dollar_project('{"a": {"b": "$a"}}', '{"result": { "$cmp": [{"b": "$a"}, "$a"]}}');

-- should return -1
SELECT * FROM bson_dollar_project('{}', '{"result": { "$cmp": [1, 2]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$cmp": [-1, 0]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$cmp": ["$a", null]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$cmp": [null, "str"]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$cmp": ["string", "stringa"]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$cmp": ["String", "string"]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$cmp": [false, true]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$cmp": [0.00001, 0.0001]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$cmp": [{"$numberDouble": "NaN"}, 0.00001]}}');
SELECT * FROM bson_dollar_project('{"a": 3}', '{"result": { "$cmp": [{"$add": [2, "$a"]}, {"$multiply": [2, "$a"]}]}}');
SELECT * FROM bson_dollar_project('{"a": [1, 2, 3, 4]}', '{"result": { "$cmp": ["$a", [1, 2, 3, 5]]}}');
SELECT * FROM bson_dollar_project('{"a": [1, 2, 3, 4]}', '{"result": { "$cmp": ["$a", [1, 2, 3, 4, 5]]}}');
SELECT * FROM bson_dollar_project('{"a": [1, 2, 3, 4]}', '{"result": { "$cmp": ["$a", [3, 2, 3, 4]]}}');
SELECT * FROM bson_dollar_project('{"a": {"b": 1, "c": 1, "d": 0}}', '{"result": { "$cmp": ["$a", {"b": 1, "c": 1, "d": 2}]}}');

-- $eq operator
-- should return true
SELECT * FROM bson_dollar_project('{}', '{"result": { "$eq": [1, 1]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$eq": [-1, -1]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$eq": [null, null]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$eq": ["$a", "$b"]}}');
SELECT * FROM bson_dollar_project('{"a": null}', '{"result": { "$eq": ["$a", null]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$eq": ["str", "str"]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$eq": [0.00002, 0.00002]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$eq": [true, true]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$eq": [{"$numberDouble": "NaN"}, {"$numberDouble": "NaN"}]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$eq": [[3,2,1,2,4,6], [3,2,1,2,4,6]]}}');
SELECT * FROM bson_dollar_project('{"a": {}}', '{"result": { "$eq": ["$a", {}]}}');
SELECT * FROM bson_dollar_project('{"a": {"b": "$a"}}', '{"result": { "$eq": [{"$literal": "$a"}, "$a.b"]}}');

-- should return false
SELECT * FROM bson_dollar_project('{}', '{"result": { "$eq": [2, 1]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$eq": [0, -1]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$eq": [null, "$a"]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$eq": ["str", null]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$eq": ["str", "s"]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$eq": [true, false]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$eq": [0.0001, 0.00001]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$eq": ["string", "stringa"]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$eq": ["String", "string"]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$eq": [false, true]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$eq": [0.00001, 0.0001]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$eq": [{"$numberDouble": "NaN"}, 0.00001]}}');
SELECT * FROM bson_dollar_project('{"a": 3}', '{"result": { "$eq": [{"$add": [2, "$a"]}, {"$multiply": [2, "$a"]}]}}');
SELECT * FROM bson_dollar_project('{"a": [1, 2, 3, 4]}', '{"result": { "$eq": ["$a", [1, 2, 3, 5]]}}');
SELECT * FROM bson_dollar_project('{"a": [1, 2, 3, 4]}', '{"result": { "$eq": ["$a", [1, 2, 3, 4, 5]]}}');
SELECT * FROM bson_dollar_project('{"a": 1}', '{"result": { "$eq": [{"$add": [2, "$a"]}, {"$subtract": [2, "$a"]}]}}');
SELECT * FROM bson_dollar_project('{"a": [1, 2, 3, 4]}', '{"result": { "$eq": ["$a", [1, 2, 3, 3]]}}');
SELECT * FROM bson_dollar_project('{"a": [1, 2, 3, 4]}', '{"result": { "$eq": ["$a", [1, 2, 3, 3, 5]]}}');
SELECT * FROM bson_dollar_project('{"a": [1, 2, 3, 4]}', '{"result": { "$eq": ["$a", [1, 2, 3]]}}');
SELECT * FROM bson_dollar_project('{"a": {"b": 1, "c": 1, "d": 0}}', '{"result": { "$eq": ["$a", {"b": 1, "c": 1}]}}');
SELECT * FROM bson_dollar_project('{"a": {"b": "$a"}}', '{"result": { "$eq": [{"b": "$a"}, "$a"]}}');

-- $ne operator
-- should return true
SELECT * FROM bson_dollar_project('{}', '{"result": { "$ne": [2, 1]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$ne": [0, -1]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$ne": [null, "$a"]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$ne": ["str", null]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$ne": ["str", "s"]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$ne": [true, false]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$ne": [0.0001, 0.00001]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$ne": ["string", "stringa"]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$ne": ["String", "string"]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$ne": [false, true]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$ne": [0.00001, 0.0001]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$ne": [{"$numberDouble": "NaN"}, 0.00001]}}');
SELECT * FROM bson_dollar_project('{"a": 3}', '{"result": { "$ne": [{"$add": [2, "$a"]}, {"$multiply": [2, "$a"]}]}}');
SELECT * FROM bson_dollar_project('{"a": [1, 2, 3, 4]}', '{"result": { "$ne": ["$a", [1, 2, 3, 5]]}}');
SELECT * FROM bson_dollar_project('{"a": [1, 2, 3, 4]}', '{"result": { "$ne": ["$a", [1, 2, 3, 4, 5]]}}');
SELECT * FROM bson_dollar_project('{"a": 1}', '{"result": { "$ne": [{"$add": [2, "$a"]}, {"$subtract": [2, "$a"]}]}}');
SELECT * FROM bson_dollar_project('{"a": [1, 2, 3, 4]}', '{"result": { "$ne": ["$a", [1, 2, 3, 3]]}}');
SELECT * FROM bson_dollar_project('{"a": [1, 2, 3, 4]}', '{"result": { "$ne": ["$a", [1, 2, 3, 3, 5]]}}');
SELECT * FROM bson_dollar_project('{"a": [1, 2, 3, 4]}', '{"result": { "$ne": ["$a", [1, 2, 3]]}}');
SELECT * FROM bson_dollar_project('{"a": {"b": 1, "c": 1, "d": 0}}', '{"result": { "$ne": ["$a", {"b": 1, "c": 1}]}}');
SELECT * FROM bson_dollar_project('{"a": {"b": "$a"}}', '{"result": { "$ne": [{"b": "$a"}, "$a"]}}');

-- should return false
SELECT * FROM bson_dollar_project('{}', '{"result": { "$ne": [1, 1]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$ne": [-1, -1]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$ne": [null, null]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$ne": ["$a", "$b"]}}');
SELECT * FROM bson_dollar_project('{"a": null}', '{"result": { "$ne": ["$a", null]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$ne": ["str", "str"]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$ne": [0.00002, 0.00002]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$ne": [true, true]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$ne": [{"$numberDouble": "NaN"}, {"$numberDouble": "NaN"}]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$ne": [[3,2,1,2,4,6], [3,2,1,2,4,6]]}}');
SELECT * FROM bson_dollar_project('{"a": {}}', '{"result": { "$ne": ["$a", {}]}}');
SELECT * FROM bson_dollar_project('{"a": {"b": "$a"}}', '{"result": { "$ne": [{"$literal": "$a"}, "$a.b"]}}');

-- $gt operator
-- should return true
SELECT * FROM bson_dollar_project('{}', '{"result": { "$gt": [2, 1]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$gt": [0, -1]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$gt": [null, "$a"]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$gt": ["str", null]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$gt": ["str", "s"]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$gt": [true, false]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$gt": [0.0001, 0.00001]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$gt": [0.1, {"$numberDouble": "NaN"}]}}');
SELECT * FROM bson_dollar_project('{"a": 1}', '{"result": { "$gt": [{"$add": [2, "$a"]}, {"$subtract": [2, "$a"]}]}}');
SELECT * FROM bson_dollar_project('{"a": [1, 2, 3, 4]}', '{"result": { "$gt": ["$a", [1, 2, 3, 3]]}}');
SELECT * FROM bson_dollar_project('{"a": [1, 2, 3, 4]}', '{"result": { "$gt": ["$a", [1, 2, 3, 3, 5]]}}');
SELECT * FROM bson_dollar_project('{"a": [1, 2, 3, 4]}', '{"result": { "$gt": ["$a", [1, 2, 3]]}}');
SELECT * FROM bson_dollar_project('{"a": {"b": 1, "c": 1, "d": 0}}', '{"result": { "$gt": ["$a", {"b": 1, "c": 1}]}}');

-- should return false
SELECT * FROM bson_dollar_project('{}', '{"result": { "$gt": [-1, -1]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$gt": [null, null]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$gt": ["$a", "$b"]}}');
SELECT * FROM bson_dollar_project('{"a": null}', '{"result": { "$gt": ["$a", null]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$gt": ["str", "str"]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$gt": [0.00002, 0.00002]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$gt": [true, true]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$gt": [[3,2,1,2,4,6], [3,2,1,2,4,6]]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$gt": [{"$numberDouble": "NaN"}, 0.1]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$gt": [null, "str"]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$gt": ["string", "stringa"]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$gt": ["String", "string"]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$gt": [false, true]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$gt": [0.00001, 0.0001]}}');
SELECT * FROM bson_dollar_project('{"a": 3}', '{"result": { "$gt": [{"$add": [2, "$a"]}, {"$multiply": [2, "$a"]}]}}');
SELECT * FROM bson_dollar_project('{"a": [1, 2, 3, 4]}', '{"result": { "$gt": ["$a", [1, 2, 3, 5]]}}');
SELECT * FROM bson_dollar_project('{"a": [1, 2, 3, 4]}', '{"result": { "$gt": ["$a", [1, 2, 3, 4, 5]]}}');
SELECT * FROM bson_dollar_project('{"a": {}}', '{"result": { "$gt": ["$a", {}]}}');

-- $gte operator
-- should return true
SELECT * FROM bson_dollar_project('{}', '{"result": { "$gte": [2, 1]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$gte": [0, -1]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$gte": [null, "$a"]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$gte": ["str", null]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$gte": ["str", "s"]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$gte": [true, false]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$gte": [0.0001, 0.00001]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$gte": [-1, -1]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$gte": [null, null]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$gte": ["$a", "$b"]}}');
SELECT * FROM bson_dollar_project('{"a": null}', '{"result": { "$gte": ["$a", null]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$gte": ["str", "str"]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$gte": [0.1, {"$numberDouble": "NaN"}]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$gte": [{"$numberDouble": "NaN"}, {"$numberDouble": "NaN"}]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$gte": [0.00002, 0.00002]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$gte": [true, true]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$gte": [[3,2,1,2,4,6], [3,2,1,2,4,6]]}}');
SELECT * FROM bson_dollar_project('{"a": 1}', '{"result": { "$gte": [{"$add": [2, "$a"]}, {"$subtract": [2, "$a"]}]}}');
SELECT * FROM bson_dollar_project('{"a": [1, 2, 3, 4]}', '{"result": { "$gte": ["$a", [1, 2, 3, 3]]}}');
SELECT * FROM bson_dollar_project('{"a": [1, 2, 3, 4]}', '{"result": { "$gte": ["$a", [1, 2, 3, 3, 5]]}}');
SELECT * FROM bson_dollar_project('{"a": [1, 2, 3, 4]}', '{"result": { "$gte": ["$a", [1, 2, 3]]}}');
SELECT * FROM bson_dollar_project('{"a": {"b": 1, "c": 1, "d": 0}}', '{"result": { "$gte": ["$a", {"b": 1, "c": 1}]}}');
SELECT * FROM bson_dollar_project('{"a": {}}', '{"result": { "$gte": ["$a", {}]}}');

-- should return false
SELECT * FROM bson_dollar_project('{}', '{"result": { "$gte": [null, "str"]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$gte": ["string", "stringa"]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$gte": ["String", "string"]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$gte": [false, true]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$gte": [0.00001, 0.0001]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$gte": [{"$numberDouble": "NaN"}, 0.1]}}');
SELECT * FROM bson_dollar_project('{"a": 3}', '{"result": { "$gte": [{"$add": [2, "$a"]}, {"$multiply": [2, "$a"]}]}}');
SELECT * FROM bson_dollar_project('{"a": [1, 2, 3, 4]}', '{"result": { "$gte": ["$a", [1, 2, 3, 5]]}}');
SELECT * FROM bson_dollar_project('{"a": [1, 2, 3, 4]}', '{"result": { "$gte": ["$a", [1, 2, 3, 4, 5]]}}');

-- $lt operator
-- should return true
SELECT * FROM bson_dollar_project('{}', '{"result": { "$lt": [1, 2]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$lt": [-1, 0]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$lt": ["$a", null]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$lt": [null, "str"]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$lt": ["string", "stringa"]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$lt": ["String", "string"]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$lt": [false, true]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$lt": [0.00001, 0.0001]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$lt": [{"$numberDouble": "NaN"}, 0.1]}}');
SELECT * FROM bson_dollar_project('{"a": 3}', '{"result": { "$lt": [{"$add": [2, "$a"]}, {"$multiply": [2, "$a"]}]}}');
SELECT * FROM bson_dollar_project('{"a": [1, 2, 3, 4]}', '{"result": { "$lt": ["$a", [1, 2, 3, 5]]}}');
SELECT * FROM bson_dollar_project('{"a": [1, 2, 3, 4]}', '{"result": { "$lt": ["$a", [1, 2, 3, 4, 5]]}}');
SELECT * FROM bson_dollar_project('{"a": [1, 2, 3, 4]}', '{"result": { "$lt": ["$a", [3, 2, 3, 4]]}}');
SELECT * FROM bson_dollar_project('{"a": {"b": 1, "c": 1, "d": 0}}', '{"result": { "$lt": ["$a", {"b": 1, "c": 1, "d": 2}]}}');

-- should return false
SELECT * FROM bson_dollar_project('{}', '{"result": { "$lt": [2, 1]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$lt": [0, -1]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$lt": [null, "$a"]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$lt": ["str", null]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$lt": ["str", "s"]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$lt": [true, false]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$lt": [0.0001, 0.00001]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$lt": [-1, -1]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$lt": [null, null]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$lt": ["$a", "$b"]}}');
SELECT * FROM bson_dollar_project('{"a": null}', '{"result": { "$lt": ["$a", null]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$lt": ["str", "str"]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$lt": [0.00002, 0.00002]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$lt": [true, true]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$lt": [[3,2,1,2,4,6], [3,2,1,2,4,6]]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$lt": [0.1, {"$numberDouble": "NaN"}]}}');
SELECT * FROM bson_dollar_project('{"a": 1}', '{"result": { "$lt": [{"$add": [2, "$a"]}, {"$subtract": [2, "$a"]}]}}');
SELECT * FROM bson_dollar_project('{"a": [1, 2, 3, 4]}', '{"result": { "$lt": ["$a", [1, 2, 3, 3]]}}');
SELECT * FROM bson_dollar_project('{"a": [1, 2, 3, 4]}', '{"result": { "$lt": ["$a", [1, 2, 3, 3, 5]]}}');
SELECT * FROM bson_dollar_project('{"a": [1, 2, 3, 4]}', '{"result": { "$lt": ["$a", [1, 2, 3]]}}');
SELECT * FROM bson_dollar_project('{"a": {"b": 1, "c": 1, "d": 0}}', '{"result": { "$lt": ["$a", {"b": 1, "c": 1}]}}');
SELECT * FROM bson_dollar_project('{"a": {}}', '{"result": { "$lt": ["$a", {}]}}');

-- $lte operator
-- should return true
SELECT * FROM bson_dollar_project('{}', '{"result": { "$lte": [1, 2]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$lte": [-1, 0]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$lte": ["$a", null]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$lte": [null, "str"]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$lte": ["string", "stringa"]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$lte": ["String", "string"]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$lte": [false, true]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$lte": [0.00001, 0.0001]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$lte": [-1, -1]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$lte": [null, null]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$lte": ["$a", "$b"]}}');
SELECT * FROM bson_dollar_project('{"a": null}', '{"result": { "$lte": ["$a", null]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$lte": ["str", "str"]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$lte": [0.00002, 0.00002]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$lte": [true, true]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$lte": [[3,2,1,2,4,6], [3,2,1,2,4,6]]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$lte": [{"$numberDouble": "NaN"}, {"$numberDouble": "NaN"}]}}');
SELECT * FROM bson_dollar_project('{"a": 3}', '{"result": { "$lte": [{"$add": [2, "$a"]}, {"$multiply": [2, "$a"]}]}}');
SELECT * FROM bson_dollar_project('{"a": [1, 2, 3, 4]}', '{"result": { "$lte": ["$a", [1, 2, 3, 5]]}}');
SELECT * FROM bson_dollar_project('{"a": [1, 2, 3, 4]}', '{"result": { "$lte": ["$a", [1, 2, 3, 4, 5]]}}');
SELECT * FROM bson_dollar_project('{"a": [1, 2, 3, 4]}', '{"result": { "$lte": ["$a", [3, 2, 3, 4]]}}');
SELECT * FROM bson_dollar_project('{"a": {"b": 1, "c": 1, "d": 0}}', '{"result": { "$lte": ["$a", {"b": 1, "c": 1, "d": 2}]}}');

-- should return false
SELECT * FROM bson_dollar_project('{}', '{"result": { "$lte": [2, 1]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$lte": [0, -1]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$lte": [null, "$a"]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$lte": ["str", null]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$lte": ["str", "s"]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$lte": [true, false]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$lte": [0.0001, 0.00001]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$lte": [0.1, {"$numberDouble": "NaN"}]}}');
SELECT * FROM bson_dollar_project('{"a": 1}', '{"result": { "$lte": [{"$add": [2, "$a"]}, {"$subtract": [2, "$a"]}]}}');
SELECT * FROM bson_dollar_project('{"a": [1, 2, 3, 4]}', '{"result": { "$lte": ["$a", [1, 2, 3, 3]]}}');
SELECT * FROM bson_dollar_project('{"a": [1, 2, 3, 4]}', '{"result": { "$lte": ["$a", [1, 2, 3, 3, 5]]}}');
SELECT * FROM bson_dollar_project('{"a": [1, 2, 3, 4]}', '{"result": { "$lte": ["$a", [1, 2, 3]]}}');
SELECT * FROM bson_dollar_project('{"a": {"b": 1, "c": 1, "d": 0}}', '{"result": { "$lte": ["$a", {"b": 1, "c": 1}]}}');

-- should error for wrong number of args
SELECT * FROM bson_dollar_project('{}', '{"result": { "$cmp": [2, 3, 2]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$eq": [2]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$ne": []}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$gt": 2}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$gte": [1, 2, 3, 4, 5]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$lt": [2, 1, 0]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$lte": [1]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$lte": [{"$divide": [1, 0]}]}}');

-- should error if there is an error in nested expressions
SELECT * FROM bson_dollar_project('{}', '{"result": { "$cmp": [2, {"$subtract": [1, {"$date": { "$numberLong": "1" }}]}]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$eq": [{"$divide": [1, 0]}, 2]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$ne": [{"$not": []}, 3]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$gt": [{"$in": [1, 2]}, 2]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$gte": [{"$divide": [1, 0, 3]}, 2]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$lt": [{"$divide": [1, 0, {"$not": []}]}, 2]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$lte": [{"$add": [{"$date": { "$numberLong": "1" }}, {"$date": { "$numberLong": "1" }}]}, 3]}}');
