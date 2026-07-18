SET search_path TO documentdb_api,documentdb_core,documentdb_api_catalog;

SET documentdb.next_collection_id TO 3600;
SET documentdb.next_collection_index_id TO 3600;

-- $getField operator
SELECT documentdb_api.insert_one('db','test_get_field',' { "_id": 0, "a": 1, "b": "test" }');
-- positive cases
-- entire expression
-- field parsed from $literal
SELECT * FROM documentdb_api_catalog.bson_dollar_project('{}', '{"result": { "fieldValue": {"$getField": {"field": { "$literal": "a" }, "input": {"a": { "b": 3 }}}}}}');
-- field parsed from expression
SELECT * FROM documentdb_api_catalog.bson_dollar_project('{}', '{"result": { "fieldValue": {"$getField": {"field": { "$literal": "a" }, "input": {"a": { "b": 3 }}}}}}');
-- field is a path
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "test_get_field", "pipeline":  [{"$project": {"result": {"fieldValue": {"$getField": {"field": "$b", "input": {"test": { "b": 3 }}}}}}}]}');
-- input be a system variable
SELECT * FROM documentdb_api_catalog.bson_dollar_project('{"a": { "b": 3 }}', '{"result": { "fieldValue": {"$getField": {"field": "a", "input": "$$ROOT"}}}}');
-- input be null
SELECT * FROM documentdb_api_catalog.bson_dollar_project('{"a": { "b": 3 }}', '{"result": { "fieldValue": {"$getField": {"field": "a", "input": null}}}}');
-- input be a path
SELECT * FROM documentdb_api_catalog.bson_dollar_project('{"a": { "b": 3 }}', '{"result": { "fieldValue": {"$getField": {"field": "b", "input": "$a"}}}}');
-- input be a missing path
SELECT * FROM documentdb_api_catalog.bson_dollar_project('{"a": { "b": 3 }}', '{"result": { "fieldValue": {"$getField": {"field": "b", "input": "$adf"}}}}');
-- input be constant
SELECT * FROM documentdb_api_catalog.bson_dollar_project('{"a": { "b": 3 }}', '{"result": { "fieldValue": {"$getField": {"field": "b", "input": "dx"}}}}');
-- get array field value
SELECT * FROM documentdb_api_catalog.bson_dollar_project('{"a": { "b": ["1"] }}', '{"result": { "fieldValue": {"$getField": {"field": "b", "input": "$a"}}}}');
-- get document field value
SELECT * FROM documentdb_api_catalog.bson_dollar_project('{"a": { "b": {"c": "1"} }}', '{"result": { "fieldValue": {"$getField": {"field": "b", "input": "$a"}}}}');
-- field name contains $ and .
SELECT * FROM documentdb_api_catalog.bson_dollar_project('{"a": { "b": {"c": "nested text"}, "b.c": "plain text" }}', '{"result": { "fieldValue": {"$getField": {"field": "b.c", "input": "$a"}}}}');
SELECT * FROM documentdb_api_catalog.bson_dollar_project('{"a": { "$b.01": {"c": "1"} }}', '{"result": { "fieldValue": {"$getField": {"field": { "$literal": "$b.01" }, "input": "$a"}}}}');
-- nested expression
SELECT * FROM documentdb_api_catalog.bson_dollar_project('{"a": { "b": 3 }}', '{"result": { "fieldValue": {"$getField": {"field": "b", "input": { "$getField": "a" }}}}}');
-- test pipeline
SELECT documentdb_api.insert_one('db','getfield','{"_id":"1", "a": null }', NULL);
SELECT documentdb_api.insert_one('db','getfield','{"_id":"2", "a": { "b": 1 } }', NULL);
SELECT documentdb_api.insert_one('db','getfield','{"_id":"3"}', NULL);
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "getfield", "pipeline": [ { "$project": { "fieldValue": { "$getField": { "field": "b", "input": "$a" }}}}], "cursor": {} }');

-- shorthand expression
-- input will be $$CURRENT
SELECT * FROM documentdb_api_catalog.bson_dollar_project('{"a": { "b": 3 }}', '{"result": { "fieldValue": {"$getField": "a"}}}');
SELECT * FROM documentdb_api_catalog.bson_dollar_project('{"a": { "b": 3 }}', '{"result": { "fieldValue": {"$getField": "b"}}}');

-- negative cases
-- full expression
-- field is required
SELECT * FROM documentdb_api_catalog.bson_dollar_project('{}', '{"result": { "fieldValue": {"$getField": {}}}}');
SELECT * FROM documentdb_api_catalog.bson_dollar_project('{}', '{"result": { "fieldValue": {"$getField": {"input": {}}}}}');
-- input is required
SELECT * FROM documentdb_api_catalog.bson_dollar_project('{}', '{"result": { "fieldValue": {"$getField": {"field": "a"}}}}');
-- field must be a string
SELECT * FROM documentdb_api_catalog.bson_dollar_project('{}', '{"result": { "fieldValue": {"$getField": {"field": null, "input": {}}}}}');
SELECT * FROM documentdb_api_catalog.bson_dollar_project('{}', '{"result": { "fieldValue": {"$getField": {"field": 1, "input": {}}}}}');
SELECT * FROM documentdb_api_catalog.bson_dollar_project('{}', '{"result": { "fieldValue": {"$getField": {"field": [], "input": {}}}}}');
-- shorthand expression
-- field must be a string
SELECT * FROM documentdb_api_catalog.bson_dollar_project('{}', '{"result": { "fieldValue": {"$getField": []}}}');


-- $unsetField
-- postive cases
-- null input
SELECT * FROM documentdb_api_catalog.bson_dollar_project('{}', '{"result": { "fieldValue": {"$unsetField": {"field": "a", "input": null}}}}');
-- empty input
SELECT * FROM documentdb_api_catalog.bson_dollar_project('{}', '{"result": { "fieldValue": {"$unsetField": {"field": "a", "input": {}}}}}');
-- remove from input argument not current document
SELECT * FROM documentdb_api_catalog.bson_dollar_project('{}', '{"result": { "fieldValue": {"$unsetField": {"field": "a", "input": {"a": 1, "b": 2}}}}}');
-- won't traverse objects automatically with dotted field
SELECT * FROM documentdb_api_catalog.bson_dollar_project('{"a": {"b": 1}, "a.b": 2}', '{"result": { "fieldValue": {"$unsetField": {"field": "a.b", "input": "$$ROOT"}}}}');
-- field name starts with $
SELECT * FROM documentdb_api_catalog.bson_dollar_project('{"$a": 1, "b": 2}', '{"result": { "fieldValue": {"$unsetField": {"field": { "$const": "$a" }, "input": "$$ROOT"}}}}');
-- take specific path from current document
SELECT * FROM documentdb_api_catalog.bson_dollar_project('{"a": {"b": 1, "c": 2}}', '{"result": { "fieldValue": {"$unsetField": {"field": "b", "input": "$a"}}}}');
-- cooperate with getField
SELECT * FROM documentdb_api_catalog.bson_dollar_project('{"a": {"b": 1, "c": 2}}', '{"result": { "fieldValue": {"$unsetField": {"field": "b", "input": {"$getField": "a"}}}}}');
-- unset an array
SELECT * FROM documentdb_api_catalog.bson_dollar_project('{"a": {"b": 1, "c": 2}, "d": [2, 3]}', '{"result": { "fieldValue": {"$unsetField": {"field": "d", "input": "$$ROOT"}}}}');

-- negative cases
SELECT * FROM documentdb_api_catalog.bson_dollar_project('{}', '{"result": { "fieldValue": {"$unsetField": 1}}}');
SELECT * FROM documentdb_api_catalog.bson_dollar_project('{}', '{"result": { "fieldValue": {"$unsetField": {"field": "a"}}}}');
SELECT * FROM documentdb_api_catalog.bson_dollar_project('{}', '{"result": { "fieldValue": {"$unsetField": {"input": null}}}}');
SELECT * FROM documentdb_api_catalog.bson_dollar_project('{}', '{"result": { "fieldValue": {"$unsetField": {"field": "a", "input": null, "value": 1}}}}');
SELECT * FROM documentdb_api_catalog.bson_dollar_project('{}', '{"result": { "fieldValue": {"$unsetField": {"field": {"$add": [2, 3]}, "input": null}}}}');
SELECT * FROM documentdb_api_catalog.bson_dollar_project('{}', '{"result": { "fieldValue": {"$unsetField": {"field": "$a", "input": null}}}}');
SELECT * FROM documentdb_api_catalog.bson_dollar_project('{}', '{"result": { "fieldValue": {"$unsetField": {"field": 5, "input": null}}}}');
SELECT * FROM documentdb_api_catalog.bson_dollar_project('{}', '{"result": { "fieldValue": {"$unsetField": {"field": ["a"], "input": null}}}}');
SELECT * FROM documentdb_api_catalog.bson_dollar_project('{}', '{"result": { "fieldValue": {"$unsetField": {"field": null, "input": null}}}}');
SELECT * FROM documentdb_api_catalog.bson_dollar_project('{}', '{"result": { "fieldValue": {"$unsetField": {"field": "a", "input": 3}}}}');
SELECT * FROM documentdb_api_catalog.bson_dollar_project('{}', '{"result": { "fieldValue": {"$unsetField": {"field": "a", "input": {"$add": [2, 3]}}}}}');