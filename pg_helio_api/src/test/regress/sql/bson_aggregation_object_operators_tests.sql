SET search_path TO helio_api,helio_core;

SET helio_api.next_collection_id TO 3600;
SET helio_api.next_collection_index_id TO 3600;

-- $getField operator

-- positive cases
-- entire expression
-- field parsed from $literal
SELECT * FROM helio_api_catalog.bson_dollar_project('{}', '{"result": { "fieldValue": {"$getField": {"field": { "$literal": "a" }, "input": {"a": { "b": 3 }}}}}}');
-- input be a system variable
SELECT * FROM helio_api_catalog.bson_dollar_project('{"a": { "b": 3 }}', '{"result": { "fieldValue": {"$getField": {"field": "a", "input": "$$ROOT"}}}}');
-- input be null
SELECT * FROM helio_api_catalog.bson_dollar_project('{"a": { "b": 3 }}', '{"result": { "fieldValue": {"$getField": {"field": "a", "input": null}}}}');
-- input be a path
SELECT * FROM helio_api_catalog.bson_dollar_project('{"a": { "b": 3 }}', '{"result": { "fieldValue": {"$getField": {"field": "b", "input": "$a"}}}}');
-- input be a missing path
SELECT * FROM helio_api_catalog.bson_dollar_project('{"a": { "b": 3 }}', '{"result": { "fieldValue": {"$getField": {"field": "b", "input": "$adf"}}}}');
-- input be constant
SELECT * FROM helio_api_catalog.bson_dollar_project('{"a": { "b": 3 }}', '{"result": { "fieldValue": {"$getField": {"field": "b", "input": "dx"}}}}');
-- get array field value
SELECT * FROM helio_api_catalog.bson_dollar_project('{"a": { "b": ["1"] }}', '{"result": { "fieldValue": {"$getField": {"field": "b", "input": "$a"}}}}');
-- get document field value
SELECT * FROM helio_api_catalog.bson_dollar_project('{"a": { "b": {"c": "1"} }}', '{"result": { "fieldValue": {"$getField": {"field": "b", "input": "$a"}}}}');
-- field name contains $ and .
SELECT * FROM helio_api_catalog.bson_dollar_project('{"a": { "b": {"c": "nested text"}, "b.c": "plain text" }}', '{"result": { "fieldValue": {"$getField": {"field": "b.c", "input": "$a"}}}}');
SELECT * FROM helio_api_catalog.bson_dollar_project('{"a": { "$b.01": {"c": "1"} }}', '{"result": { "fieldValue": {"$getField": {"field": { "$literal": "$b.01" }, "input": "$a"}}}}');
-- nested expression
SELECT * FROM helio_api_catalog.bson_dollar_project('{"a": { "b": 3 }}', '{"result": { "fieldValue": {"$getField": {"field": "b", "input": { "$getField": "a" }}}}}');

-- shorthand expression
-- input will be $$CURRENT
SELECT * FROM helio_api_catalog.bson_dollar_project('{"a": { "b": 3 }}', '{"result": { "fieldValue": {"$getField": "a"}}}');
SELECT * FROM helio_api_catalog.bson_dollar_project('{"a": { "b": 3 }}', '{"result": { "fieldValue": {"$getField": "b"}}}');

-- negative cases
-- full expression
-- field is required
SELECT * FROM helio_api_catalog.bson_dollar_project('{}', '{"result": { "fieldValue": {"$getField": {}}}}');
SELECT * FROM helio_api_catalog.bson_dollar_project('{}', '{"result": { "fieldValue": {"$getField": {"input": {}}}}}');
-- input is required
SELECT * FROM helio_api_catalog.bson_dollar_project('{}', '{"result": { "fieldValue": {"$getField": {"field": "a"}}}}');
-- field must be a string
SELECT * FROM helio_api_catalog.bson_dollar_project('{}', '{"result": { "fieldValue": {"$getField": {"field": null, "input": {}}}}}');
SELECT * FROM helio_api_catalog.bson_dollar_project('{}', '{"result": { "fieldValue": {"$getField": {"field": 1, "input": {}}}}}');
SELECT * FROM helio_api_catalog.bson_dollar_project('{}', '{"result": { "fieldValue": {"$getField": {"field": [], "input": {}}}}}');
-- field is a path
SELECT * FROM helio_api_catalog.bson_dollar_project('{}', '{"result": { "fieldValue": {"$getField": {"field": "$a", "input": {"a": { "b": 3 }}}}}}');
-- shorthand expression
-- field must be a string
SELECT * FROM helio_api_catalog.bson_dollar_project('{}', '{"result": { "fieldValue": {"$getField": []}}}');
-- field is an operator
SELECT * FROM helio_api_catalog.bson_dollar_project('{}', '{"result": { "fieldValue": {"$getField": { "$add": [2, 3 ]}}}}');