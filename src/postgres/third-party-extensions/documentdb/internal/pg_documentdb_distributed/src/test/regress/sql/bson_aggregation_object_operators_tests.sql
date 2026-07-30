SET search_path TO documentdb_core,documentdb_api,documentdb_api_catalog,documentdb_api_internal;
SET citus.next_shard_id TO 367000;
SET documentdb.next_collection_id TO 3670;
SET documentdb.next_collection_index_id TO 3670;

-- -- $mergeObjects operator
-- -- simple merge
SELECT * FROM bson_dollar_project('{}', '{"result": { "$mergeObjects": {"a": "1"}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$mergeObjects": [{"a": "1"}, {"b": true}]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$mergeObjects": [{"a": "1"}, {"b": true, "c": 2}]}}');

-- null and undefined return empty doc
SELECT * FROM bson_dollar_project('{}', '{"result": { "$mergeObjects": null}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$mergeObjects": "$undefinedField"}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$mergeObjects": [null, null, null]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$mergeObjects": [null, "$undefinedField", null]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$mergeObjects": [{"a": "onlyDoc"}, "$undefinedField", null]}}');

-- with field expressions referencing parent doc
SELECT * FROM bson_dollar_project('{"a": "1", "b": { "d": [true, "1"] }}', '{"result": { "$mergeObjects": "$b"}}');
SELECT * FROM bson_dollar_project('{"a": "1", "b": { "d": [true, "1"] }, "c": {"foo": true}}', '{"result": { "$mergeObjects": ["$b", "$c"]}}');
SELECT * FROM bson_dollar_project('{"a": "1", "b": { "d": [true, "1"] }, "c": {"foo": true}}', '{"result": { "$mergeObjects": ["$c", "$b"]}}');
SELECT * FROM bson_dollar_project('{"a": "1", "b": { "d": [true, "1"] }, "c": {"foo": true}}', '{"result": { "$mergeObjects": ["$c", "$d"]}}');
SELECT * FROM bson_dollar_project('{"a": "1", "b": { "d": { "f": [true, "1"] }}, "c": {"foo": true}}', '{"result": { "$mergeObjects": ["$c.d", "$b.d"]}}');

-- last path found wins when there is a clash
SELECT * FROM bson_dollar_project('{"a": "1", "b": { "d": [true, "1"] }, "c": {"d": false}}', '{"result": { "$mergeObjects": ["$b", "$c"]}}');
SELECT * FROM bson_dollar_project('{"a": "1", "b": { "d": [true, "1"] }, "c": {"d": false}}', '{"result": { "$mergeObjects": ["$b", "$c", {"d": "this is my final string"}]}}');
SELECT * FROM bson_dollar_project('{"a": "1", "b": { "d": [true, "1"] }, "c": {"d": { "z": false}}}', '{"result": { "$mergeObjects": ["$b", {"d": "this is my final string"}, "$c.d"]}}');
SELECT * FROM bson_dollar_project('{"a": "1", "b": { "d": [true, "1"] }, "c": {"d": { "z": false}}}', '{"result": { "$mergeObjects": [{"d": "this is my final string"}, "$b", "$c.d", {"hello": "world"}]}}');

-- nested expressions are evaluated on result document
SELECT * FROM bson_dollar_project('{"_id": 4, "a": 1}', '{"result": { "$mergeObjects": [{"id": { "$add": ["$_id", "$a"]}}]}}');
SELECT * FROM bson_dollar_project('{"a": {"b": ["1","2","3"]}}', '{"result": { "$mergeObjects": ["$a", {"isArray": {"$isArray": "$a.b"}}]}}');
SELECT * FROM bson_dollar_project('{"a": {"b": ["1","2","3"]}}', '{"result": { "$mergeObjects": ["$a", {"b": [{"$literal": "$b"}]}]}}');
SELECT * FROM bson_dollar_project('{"a": {"b": ["1","2","3"]}}', '{"result": { "$mergeObjects": ["$a", {"b": [{"$literal": "$b"}, "$a"]}]}}');

-- -- expressions that evaluate to non objects are not valid
SELECT * FROM bson_dollar_project('{"_id": 4, "a": 1}', '{"result": { "$mergeObjects": [{"id": "$a"}, "$a"]}}');
SELECT * FROM bson_dollar_project('{"_id": 4, "a": "string"}', '{"result": { "$mergeObjects": [{"id": "$a"}, "$a"]}}');
SELECT * FROM bson_dollar_project('{"_id": 4, "a": true}', '{"result": { "$mergeObjects": [{"id": "$a"}, "$a"]}}');
SELECT * FROM bson_dollar_project('{"_id": 4, "a": 2.0}', '{"result": { "$mergeObjects": [{"id": "$a"}, "$a"]}}');
SELECT * FROM bson_dollar_project('{"_id": 4, "a": 2.0}', '{"result": { "$mergeObjects": [{"$add": [1, 2, 3]}]}}');

-- $setField operator
-- $setField should be given an Object no array
SELECT * FROM bson_dollar_project('{}', '{"result": { "fieldA": {"$setField": [ "field", "input", "value" ]}}}');
-- Extra param called thing gives an error
SELECT * FROM bson_dollar_project('{}', '{"result": { "fieldA": [{"$setField": { "thing": "a", "field": "a", "input": "$$ROOT", "value": "newValue" } }]}}');

-- All required args -and- input param of field is a number
SELECT * FROM bson_dollar_project('{}', '{"result": { "fieldA": [{"$setField": { "field": 123, "input": "$$ROOT", "value": "newValue" } }]}}');
-- All required args -and- input: $$ROOT
SELECT * FROM bson_dollar_project('{}', '{"result": { "fieldA": [{"$setField": { "field": "a", "input": "$$ROOT", "value": "newValue" } }]}}');
-- Missing param input
SELECT * FROM bson_dollar_project('{}', '{"result": { "fieldA": [{"$setField": { "field": "a", "value": "newValue" } }]}}');
-- Missing param field
SELECT * FROM bson_dollar_project('{}', '{"result": { "fieldA": [{"$setField": { "input": "$$ROOT", "value": "newValue" } }]}}');
-- Missing param value
SELECT * FROM bson_dollar_project('{}', '{"result": { "fieldA": [{"$setField": { "field": "a", "input": "$$ROOT" } }]}}');
-- Wrong type for field, must be a string not null or bool
SELECT * FROM bson_dollar_project('{ "d1": {"val": "a"}}', '{"result": { "fieldA": [ { "$literal": "b"}, {"$setField": { "field": true, "value": "REMOVE" } }]}}');
SELECT * FROM bson_dollar_project('{ "d1": {"val": "a"}}', '{"result": { "fieldA": [ { "$literal": "b"}, {"$setField": { "field": null, "value": "REMOVE" } }]}}');

-- The next item should add  "$x.y.z" : "newValue" to the input doc {"a":"b"}
SELECT * FROM bson_dollar_project('{ "d1": {"val": "a"}}', '{"result": { "fieldA": [{"$setField": { "field": {"$literal" : "$x.y.z"}, "input": {"a":"b"}, "value": "newValue" } }]}}');

-- Not yet implemented fully for $$REMOVE
-- SELECT * FROM bson_dollar_project('{}', '{"result": { "fieldA": [{"$setField": {"field": "a", "input": "$$ROOT", "value": "$$REMOVE" } }]}}');

-- here we test the "value" as magic value $$REMOVE as value, that should remove "baz" from the input, using $literal as means to give field name. Field not present
SELECT * FROM bson_dollar_project('{ "d1": {"val": "a"}}', '{"result": { "fieldA": [{"$setField": { "field": {"$literal" : "$x.y.z"}, "input": {"a":"b"}, "value": "$$REMOVE" } }]}}');
-- here we test the "value" as magic $$REMOVE as part of the value, that should not remove "baz" from the input, using $literal as means to give field name. Field not present
SELECT * FROM bson_dollar_project('{ "d1": {"val": "a"}}', '{"result": { "fieldA": [{"$setField": { "field": {"$literal" : "$x.y.z"}, "input": {"a":"b"}, "value": "no$$REMOVE" } }]}}');
-- here we test the "value" as magic $$REMOVE that should remove "baz" from the input, using $literal as means to give field name. Field *is* present.
SELECT * FROM bson_dollar_project('{ "d1": {"val": "a"}}', '{"result": { "fieldA": [{"$setField": { "field": {"$literal" : "baz"}, "input": {"a":"b"}, "value": "$$REMOVE" } }]}}');
-- here we test the "value" as magic $$REMOVE that should remove "baz" from the input.  Same as prev test w/o using $literal operator.
SELECT * FROM bson_dollar_project('{ "d1": {"val": "a"}}', '{"result": { "fieldA": [{"$setField": { "field": "baz", "input": {"a":"b"}, "value": "$$REMOVE" } }]}}');
-- here we test the "value" as a empty document
SELECT * FROM bson_dollar_project('{ "d1": {"val": "a"}}', '{"result": { "fieldA": [{"$setField": { "field": "baz", "input": {"a":"b"}, "value": {} } }]}}');
-- here we test the "value" as a document with 1 item
SELECT * FROM bson_dollar_project('{ "d1": {"val": "a"}}', '{"result": { "fieldA": [{"$setField": { "field": "baz", "input": {"a":"b"}, "value": { "x" : "y"} } }]}}');

-- Not yet implemented, fully for $$ROOT
-- here we use $$ROOT in probably wrong "value" field, should pick up the record { "d1": {"val": "a"}} as the value of the new field
SELECT * FROM bson_dollar_project('{ "d1": {"val": "a"}}', '{"result": { "fieldA": [{"$setField": { "field": "baz", "input": {"a":"b"}, "value": "$$ROOT" } }]}}');

-- here we use $$ROOT that pickups the { "d1": {"val": "a"}} and should add "baz" : "foooooo"
SELECT * FROM bson_dollar_project('{ "d1": {"val": "a"}}', '{"result": { "fieldA": [{"$setField": { "field": "baz", "input": "$$ROOT", "value": "foooooo" } }]}}');

-- Check that we can use dot path like $$ROOT.d1:
SELECT * FROM bson_dollar_project('{ "d1": {"val": "a"}}', '{"result": { "fieldA": [{"$setField": { "field": "baz", "input": "$$ROOT.d1", "value": "foooooo" } }]}}');

-- Check that we can use dot path like $$REMOVE in a non $setField context
SELECT * FROM bson_dollar_project('{ "d1": {"val": "a"}}', '{"result": { "fieldA": [{"$concat": "$$REMOVE" } ]}}');

-- Inject a null as "input", via "input"
SELECT * FROM bson_dollar_project('{ "d1": {"val": "a"}}', '{"result": { "fieldA": [{"$setField": { "field": "baz", "input": null, "value": "foooooo" } }]}}');

-- insert where we overwrite the tail.
SELECT * FROM bson_dollar_project('{"a1": { "b": 1, "c": 1, "d": 1 }, "b1": { "d": 2, "e": 3 } }', '{"result": { "$mergeObjects": [ "$a1", "$b1" ]}}');

-- testing multiple scenarios with $$REMOVE to check all work properly
SELECT * FROM bson_dollar_project('{"a": 1}', '{ "result": { "$bsonSize": "$$REMOVE" } }');
SELECT * FROM bson_dollar_project('{"a": 1}', '{ "result": { "$bsonSize": { "a": 1, "test": "$$REMOVE" } } }');
SELECT * FROM bson_dollar_project('{ "_id": 16, "group": 2, "obj": { "a": 1, "b": 1 } }', '{ "mergedDocument": { "$mergeObjects": ["$obj", { "b": "$$REMOVE" } ] } }');

-- $getField operator
SELECT insert_one('db','test_get_field',' { "_id": 0, "a": 1, "b": "test" }');
-- positive cases
-- entire expression
-- field parsed from $literal
SELECT * FROM bson_dollar_project('{}', '{"result": { "fieldValue": {"$getField": {"field": { "$literal": "a" }, "input": {"a": { "b": 3 }}}}}}');
-- field parsed from expression
SELECT * FROM bson_dollar_project('{}', '{"result": { "fieldValue": {"$getField": {"field": { "$literal": "a" }, "input": {"a": { "b": 3 }}}}}}');
-- field is a path
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "test_get_field", "pipeline":  [{"$project": {"result": {"fieldValue": {"$getField": {"field": "$b", "input": {"test": { "b": 3 }}}}}}}]}');
-- input be a system variable
SELECT * FROM bson_dollar_project('{"a": { "b": 3 }}', '{"result": { "fieldValue": {"$getField": {"field": "a", "input": "$$ROOT"}}}}');
-- input be null
SELECT * FROM bson_dollar_project('{"a": { "b": 3 }}', '{"result": { "fieldValue": {"$getField": {"field": "a", "input": null}}}}');
-- input be a path
SELECT * FROM bson_dollar_project('{"a": { "b": 3 }}', '{"result": { "fieldValue": {"$getField": {"field": "b", "input": "$a"}}}}');
-- input be a missing path
SELECT * FROM bson_dollar_project('{"a": { "b": 3 }}', '{"result": { "fieldValue": {"$getField": {"field": "b", "input": "$adf"}}}}');
-- input be constant
SELECT * FROM bson_dollar_project('{"a": { "b": 3 }}', '{"result": { "fieldValue": {"$getField": {"field": "b", "input": "dx"}}}}');
-- get array field value
SELECT * FROM bson_dollar_project('{"a": { "b": ["1"] }}', '{"result": { "fieldValue": {"$getField": {"field": "b", "input": "$a"}}}}');
-- get document field value
SELECT * FROM bson_dollar_project('{"a": { "b": {"c": "1"} }}', '{"result": { "fieldValue": {"$getField": {"field": "b", "input": "$a"}}}}');
-- field name contains $ and .
SELECT * FROM bson_dollar_project('{"a": { "b": {"c": "nested text"}, "b.c": "plain text" }}', '{"result": { "fieldValue": {"$getField": {"field": "b.c", "input": "$a"}}}}');
SELECT * FROM bson_dollar_project('{"a": { "$b.01": {"c": "1"} }}', '{"result": { "fieldValue": {"$getField": {"field": { "$literal": "$b.01" }, "input": "$a"}}}}');
-- nested expression
SELECT * FROM bson_dollar_project('{"a": { "b": 3 }}', '{"result": { "fieldValue": {"$getField": {"field": "b", "input": { "$getField": "a" }}}}}');
-- test pipeline
SELECT documentdb_api.insert_one('db','getfield','{"_id":"1", "a": null }', NULL);
SELECT documentdb_api.insert_one('db','getfield','{"_id":"2", "a": { "b": 1 } }', NULL);
SELECT documentdb_api.insert_one('db','getfield','{"_id":"3"}', NULL);
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "getfield", "pipeline": [ { "$project": { "fieldValue": { "$getField": { "field": "b", "input": "$a" }}}}], "cursor": {} }');

-- shorthand expression
-- input will be $$CURRENT
SELECT * FROM bson_dollar_project('{"a": { "b": 3 }}', '{"result": { "fieldValue": {"$getField": "a"}}}');
SELECT * FROM bson_dollar_project('{"a": { "b": 3 }}', '{"result": { "fieldValue": {"$getField": "b"}}}');

-- negative cases
-- full expression
-- field is required
SELECT * FROM bson_dollar_project('{}', '{"result": { "fieldValue": {"$getField": {}}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "fieldValue": {"$getField": {"input": {}}}}}');
-- input is required
SELECT * FROM bson_dollar_project('{}', '{"result": { "fieldValue": {"$getField": {"field": "a"}}}}');
-- field must be a string
SELECT * FROM bson_dollar_project('{}', '{"result": { "fieldValue": {"$getField": {"field": null, "input": {}}}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "fieldValue": {"$getField": {"field": 1, "input": {}}}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "fieldValue": {"$getField": {"field": [], "input": {}}}}}');
-- shorthand expression
-- field must be a string
SELECT * FROM bson_dollar_project('{}', '{"result": { "fieldValue": {"$getField": []}}}');

-- $unsetField
-- postive cases
-- null input
SELECT * FROM bson_dollar_project('{}', '{"result": { "fieldValue": {"$unsetField": {"field": "a", "input": null}}}}');
-- empty input
SELECT * FROM bson_dollar_project('{}', '{"result": { "fieldValue": {"$unsetField": {"field": "a", "input": {}}}}}');
-- remove from input argument not current document
SELECT * FROM bson_dollar_project('{}', '{"result": { "fieldValue": {"$unsetField": {"field": "a", "input": {"a": 1, "b": 2}}}}}');
-- won't traverse objects automatically with dotted field
SELECT * FROM bson_dollar_project('{"a": {"b": 1}, "a.b": 2}', '{"result": { "fieldValue": {"$unsetField": {"field": "a.b", "input": "$$ROOT"}}}}');
-- field name starts with $
SELECT * FROM bson_dollar_project('{"$a": 1, "b": 2}', '{"result": { "fieldValue": {"$unsetField": {"field": { "$const": "$a" }, "input": "$$ROOT"}}}}');
-- take specific path from current document
SELECT * FROM bson_dollar_project('{"a": {"b": 1, "c": 2}}', '{"result": { "fieldValue": {"$unsetField": {"field": "b", "input": "$a"}}}}');
-- cooperate with getField
SELECT * FROM bson_dollar_project('{"a": {"b": 1, "c": 2}}', '{"result": { "fieldValue": {"$unsetField": {"field": "b", "input": {"$getField": "a"}}}}}');
-- unset an array
SELECT * FROM bson_dollar_project('{"a": {"b": 1, "c": 2}, "d": [2, 3]}', '{"result": { "fieldValue": {"$unsetField": {"field": "d", "input": "$$ROOT"}}}}');

-- negative cases
SELECT * FROM bson_dollar_project('{}', '{"result": { "fieldValue": {"$unsetField": 1}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "fieldValue": {"$unsetField": {"field": "a"}}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "fieldValue": {"$unsetField": {"input": null}}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "fieldValue": {"$unsetField": {"field": "a", "input": null, "value": 1}}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "fieldValue": {"$unsetField": {"field": {"$add": [2, 3]}, "input": null}}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "fieldValue": {"$unsetField": {"field": "$a", "input": null}}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "fieldValue": {"$unsetField": {"field": 5, "input": null}}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "fieldValue": {"$unsetField": {"field": ["a"], "input": null}}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "fieldValue": {"$unsetField": {"field": null, "input": null}}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "fieldValue": {"$unsetField": {"field": "a", "input": 3}}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "fieldValue": {"$unsetField": {"field": "a", "input": {"$add": [2, 3]}}}}}');
