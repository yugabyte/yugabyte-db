SET search_path TO documentdb_core,documentdb_api,documentdb_api_catalog,documentdb_api_internal;
SET citus.next_shard_id TO 5800000;
SET documentdb.next_collection_id TO 5800;
SET documentdb.next_collection_index_id TO 5800;

-- $ifNull: should return first not null
SELECT * FROM bson_dollar_project('{}', '{"result": {"$ifNull": ["not null", null]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$ifNull": [null, "not null", null]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$ifNull": [null, null, "not null"]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$ifNull": [{"$arrayElemAt": [[1], 2]}, "not null", null]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$ifNull": [null, "$a", "not null", null]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$ifNull": [null, "$a", {"$add":[1,2]}, null]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$ifNull": [null, "$a", [1, 2, {"$add":[1,2]}], null]}}');
SELECT * FROM bson_dollar_project('{"b": 2}', '{"result": {"$ifNull": [null, "$a", "$b", null]}}');

-- $ifNull: should not return result if last expression is undefined field or an expression with no result
SELECT * FROM bson_dollar_project('{}', '{"result": {"$ifNull": [null, "$a", "$b"]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$ifNull": [null, null, {"$arrayElemAt": [[1], 2]}]}}');

-- $ifNull: should have at least 2 arguments
SELECT * FROM bson_dollar_project('{}', '{"result": {"$ifNull": {}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$ifNull": "string"}}');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$ifNull": false}}');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$ifNull": []}}');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$ifNull": [null]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$ifNull": [{"$divide": [1, 0]}]}}');

-- $ifNull: should honor nested expression errors, even if a not null expression is found
SELECT * FROM bson_dollar_project('{}', '{"result": {"$ifNull": [null, "not null", {"$divide": [1, 0]}]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$ifNull": [null, "not null", {"$ifNull": [1]}]}}');

-- $cond: returns then when condition is true
SELECT * FROM bson_dollar_project('{}', '{"result": {"$cond": [true, "then", "else"]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$cond": [{"$gt": [3, 2]}, "then", "else"]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$cond": [{"$lt": [2, 3]}, "then", "else"]}}');
SELECT * FROM bson_dollar_project('{"b": true}', '{"result": {"$cond": ["$b", "then", "else"]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$cond": {"if": true, "then": "then", "else": "else"}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$cond": {"if": true, "else": "else", "then": "then" }}}');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$cond": {"then": "then", "if": true, "else": "else"}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$cond": {"else": "else", "if": true, "then": "then"}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$cond": {"if": {"$gt": [3, 2]}, "then": "then", "else": "else"}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$cond": {"if": {"$lt": [2, 3]}, "then": "then", "else": "else"}}}');
SELECT * FROM bson_dollar_project('{"b": true}', '{"result": {"$cond": {"if": "$b", "then": "then", "else": "else"}}}');

-- $cond: returns else when condition is false
SELECT * FROM bson_dollar_project('{}', '{"result": {"$cond": [false, "then", "else"]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$cond": [{"$gt": [3, 3]}, "then", "else"]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$cond": [{"$lt": [3, 3]}, "then", "else"]}}');
SELECT * FROM bson_dollar_project('{"b": false}', '{"result": {"$cond": ["$b", "then", "else"]}}');
SELECT * FROM bson_dollar_project('{"b": false}', '{"result": {"$cond": ["$b", "then", {"$add": [1, 2]}]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$cond": {"if": false, "then": "then", "else": "else"}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$cond": {"if": {"$gt": [3, 3]}, "then": "then", "else": "else"}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$cond": {"if": {"$lt": [3, 3]}, "then": "then", "else": "else"}}}');
SELECT * FROM bson_dollar_project('{"b": false}', '{"result": {"$cond": {"if": "$b", "then": "then", "else": "else"}}}');
SELECT * FROM bson_dollar_project('{"b": false}', '{"result": {"$cond": {"if": "$b", "then": "then", "else": [1, 2, {"$add": [1, 2]}]}}}');

-- $cond: null/undefined conditions should evaluate to false and return else
SELECT * FROM bson_dollar_project('{}', '{"result": {"$cond": [null, "then", "else"]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$cond": ["$a", "then", "else"]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$cond": {"if": null, "then": "then", "else": "else"}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$cond": {"if": "$a", "then": "then", "else": "else"}}}');

-- $cond: returns no result when the returned expression is an undefined field or an expression with no result
SELECT * FROM bson_dollar_project('{}', '{"result": {"$cond": {"if": false, "then": "then", "else": "$a"}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$cond": {"if": {"$gt": [3, 3]}, "then": "then", "else": {"$ifNull": [null, null, {"$arrayElemAt": [[1], 2]}]}}}}');

-- $cond: if is required
SELECT * FROM bson_dollar_project('{}', '{"result": {"$cond": {"then": "then", "else": "$a"}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$cond": {"else": "else", "then": "$a"}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$cond": {"else": "else"}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$cond": {}}}');

-- $cond: then is required
SELECT * FROM bson_dollar_project('{}', '{"result": {"$cond": {"if": "if", "else": "$a"}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$cond": {"else": "else", "if": "$a"}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$cond": {"if": "$a"}}}');

-- $cond: else is required
SELECT * FROM bson_dollar_project('{}', '{"result": {"$cond": {"if": "if", "then": "$a"}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$cond": {"then": "then", "if": "$a"}}}');

-- $cond: unknown argument
SELECT * FROM bson_dollar_project('{}', '{"result": {"$cond": {"if": false, "then": "then", "else": "$a", "foo": "blah"}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$cond": {"foo": "blah"}}}');

-- $cond: requires 3 arguments error
SELECT * FROM bson_dollar_project('{}', '{"result": {"$cond": [false, "else"]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$cond": []}}');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$cond": [true, true, true, true, true]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$cond": "str"}}');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$cond": true}}');

-- $cond: nested expression errors honored even if result is not affected by it
SELECT * FROM bson_dollar_project('{}', '{"result": {"$cond": [false, {"$divide": [1, 0]}, "else"]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$cond": [true, "then", {"$divide": [1, 0]}]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$cond": {"if": false, "then": {"$divide": [1, 0]}, "else": "else"}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$cond": {"if": true, "then": "then", "else": {"$divide": [1, 0]}}}}');

-- $switch: returns expected branch
SELECT * FROM bson_dollar_project('{}', '{"result": {"$switch": {"branches": [{"case": true, "then": "first branch is true"}]}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$switch": {"branches": [{"case": false, "then": "first branch is true"}, {"case": true, "then": "second branch is true"}]}}}');
SELECT * FROM bson_dollar_project('{"a": 1, "b": 2}', '{"result": {"$switch": {"branches": [{"case": {"$gt": ["$a", "$b"]}, "then": "a > b"}, {"case": {"$lt": ["$a", "$b"]}, "then": "a < b"}]}}}');
SELECT * FROM bson_dollar_project('{"a": 3, "b": 2}', '{"result": {"$switch": {"branches": [{"case": {"$gt": ["$a", "$b"]}, "then": "a > b"}, {"case": {"$lt": ["$a", "$b"]}, "then": "a < b"}], "default": "a = b"}}}');
SELECT * FROM bson_dollar_project('{"a": 2, "b": 2}', '{"result": {"$switch": {"branches": [{"case": {"$gt": ["$a", "$b"]}, "then": "a > b"}, {"case": {"$not": {"$eq": ["$a", "$b"]}}, "then": "a != b"}], "default": "a = b"}}}');
SELECT * FROM bson_dollar_project('{"a": 0, "b": 2}', '{"result": {"$switch": {"branches": [{"case": {"$gt": ["$a", "$b"]}, "then": "a > b"}, {"case": {"$not": {"$eq": ["$a", "$b"]}}, "then": "a != b"}], "default": "a = b"}}}');

-- $switch: null/undefined should evaluate to false
SELECT * FROM bson_dollar_project('{}', '{"result": {"$switch": {"branches": [{"case": null, "then": "first branch is true"}], "default": "it was false"}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$switch": {"branches": [{"case": "$a", "then": "first branch is true"}], "default": "it was false"}}}');

-- $switch: shouldn't return result if resulting expression is undefined field or expression with undef result
SELECT * FROM bson_dollar_project('{}', '{"result": {"$switch": {"branches": [{"case": true, "then": "$b"}]}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$switch": {"branches": [{"case": true, "then": {"$arrayElemAt": [[1], 2]}}]}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$switch": {"branches": [{"case": false, "then": ""}], "default": {"$arrayElemAt": [[1], 2]}}}}');

-- $switch: requires object argument
SELECT * FROM bson_dollar_project('{}', '{"result": {"$switch": []}}');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$switch": true}}');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$switch": "Str"}}');

-- $switch: branches must be an array
SELECT * FROM bson_dollar_project('{}', '{"result": {"$switch": {"branches": {}}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$switch": {"branches": "true"}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$switch": {"branches": false}}}');

-- $switch: found an unknown argument
SELECT * FROM bson_dollar_project('{}', '{"result": {"$switch": {"branches": [], "foo": "blah"}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$switch": {"foo": "blah", "branches": {}}}}');

-- $switch: requires at least one branch
SELECT * FROM bson_dollar_project('{}', '{"result": {"$switch": {"branches": []}}}');

-- $switch: default is required only when there is no matching branch
SELECT * FROM bson_dollar_project('{}', '{"result": {"$switch": {"branches": [{"case": false, "then": "$b"}]}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$switch": {"branches": [{"case": false, "then": "$b"}, {"case": null, "then": "it is null"}]}}}');

-- $switch: found unknown argument to a branch
SELECT * FROM bson_dollar_project('{}', '{"result": {"$switch": {"branches": [{"case": false, "then": "$b"}, {"foo": "blah"}]}}}');

-- $switch: branch requires a case expression
SELECT * FROM bson_dollar_project('{}', '{"result": {"$switch": {"branches": [{"case": false, "then": "$b"}, {"then": "blah"}]}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$switch": {"branches": [{"case": false, "then": "$b"}, {}]}}}');

-- $switch: branch requires a then expression
SELECT * FROM bson_dollar_project('{}', '{"result": {"$switch": {"branches": [{"case": false, "then": "$b"}, {"case": "blah"}]}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$switch": {"branches": [{"case": false}]}}}');

-- Fails for constant error expressions
SELECT * FROM bson_dollar_project('{}', '{"result": {"$switch": {"branches": [{"case": true, "then": "first branch is true"}, {"case": false, "then": {"$ifNull": []}}]}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$switch": {"branches": [{"case": true, "then": "first branch is true"}, {"case": false, "then": "false"}], "default": {"$ifNull": []}}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$switch": {"branches": [{"case": false, "then": "first branch is true"}, {"case": false, "then": "false"}], "default": {"$ifNull": []}}}}');

SELECT * FROM bson_dollar_project('{}', '{"result": {"$switch": {"branches": [{"case": true, "then": "first branch is true"}, {"case": {"$divide": [1, 0]}, "then": "false"}], "default": {"$ifNull": []}}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$switch": {"branches": [{"case": false, "then": "first branch is true"}, {"case": {"$divide": [1, 0]}, "then": "false"}], "default": {"$ifNull": []}}}}');

-- Fail for non-constant error branch expressions (eg: paths) if that branch is matched
SELECT * FROM bson_dollar_project('{"a": 0}', '{"result": {"$switch": {"branches": [{"case": false, "then": "first branch is true"}, {"case": true, "then": {"$divide": [1, "$a"]}}]}}}');

-- Does not fail for non-constant error expressions (eg: paths) if a valid branch is found 
SELECT * FROM bson_dollar_project('{"a": 0}', '{"result": {"$switch": {"branches": [{"case": true, "then": "first branch is true"}, {"case": false, "then": {"$divide": [1, "$a"]}}]}}}');

-- Does not fail for non-constant branch error expressions (eg: paths) if the default expression is matched.
SELECT * FROM bson_dollar_project('{"a": 0}', '{"result": {"$switch": {"branches": [{"case": false, "then": "first branch is true"}, {"case": false, "then": {"$divide": [1, "$a"]}}], "default": "I am the default"}}}');
