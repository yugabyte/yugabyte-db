
SET search_path TO documentdb_core,documentdb_api,documentdb_api_catalog,documentdb_api_internal;
SET citus.next_shard_id TO 240000;
SET documentdb.next_collection_id TO 2400;
SET documentdb.next_collection_index_id TO 2400;

SELECT documentdb_api.insert_one('db','projectexprs','{"_id":"1", "a" : { "b" : 1 } }', NULL);
SELECT documentdb_api.insert_one('db','projectexprs','{"_id":"2", "a" : { "b" : [ 0, 1, 2]} }', NULL);
SELECT documentdb_api.insert_one('db','projectexprs','{"_id":"3", "a" : [ { "b": 0 }, { "b": 1 }, { "b": 3}] }', NULL);
SELECT documentdb_api.insert_one('db','projectexprs','{"_id":"4", "a" : [ { "b": [-1, 1, 2] }, { "b": [0, 1, 2] }, { "b": [0, 1, 7] }] }', NULL);
SELECT documentdb_api.insert_one('db','projectexprs','{"_id":"5", "a" : { "b" : [ { "1" : [ 1, 2, 3 ] } ] } }', NULL);
SELECT documentdb_api.insert_one('db','projectexprs','{"_id":"6", "a" : [ { "c" : 0 }, 2 ] }', NULL);
SELECT documentdb_api.insert_one('db','projectexprs','{"_id":"7", "a" : [ { "c" : 0 }, { "c" : 1 }, { "b" : 2 } ] }', NULL);
SELECT documentdb_api.insert_one('db','projectexprs','{"_id":"8", "a" : { "c" : 1 } }', NULL);
SELECT documentdb_api.insert_one('db','projectexprs','{"_id":"9", "c" : { "d" : 1 } }', NULL);


-- field path expressions
SELECT bson_expression_get(document, '{ "field" : "$a" }') FROM documentdb_api.collection('db', 'projectexprs') ORDER BY object_id;
SELECT bson_expression_get(document, '{ "field" : "$a.b" }') FROM documentdb_api.collection('db', 'projectexprs') ORDER BY object_id;
SELECT bson_expression_get(document, '{ "field" : "$a.c" }') FROM documentdb_api.collection('db', 'projectexprs') ORDER BY object_id;

-- field path expressions on bson_expression_get() with nullOnEmpty = true
SELECT bson_expression_get(document, '{ "field" : "$a" }', true) FROM documentdb_api.collection('db', 'projectexprs') ORDER BY object_id;
SELECT bson_expression_get(document, '{ "field" : "$a.b" }', true) FROM documentdb_api.collection('db', 'projectexprs') ORDER BY object_id;
SELECT bson_expression_get(document, '{ "field" : "$a.c" }', true) FROM documentdb_api.collection('db', 'projectexprs') ORDER BY object_id;

-- field path expressions on bson_expression_get() with nullOnEmpty = false
SELECT bson_expression_get(document, '{ "field" : "$a" }', false) FROM documentdb_api.collection('db', 'projectexprs') ORDER BY object_id;
SELECT bson_expression_get(document, '{ "field" : "$a.b" }', false) FROM documentdb_api.collection('db', 'projectexprs') ORDER BY object_id;
SELECT bson_expression_get(document, '{ "field" : "$a.c" }', false) FROM documentdb_api.collection('db', 'projectexprs') ORDER BY object_id;

-- const value expressions
SELECT bson_expression_get(document, '{ "field" : "someString" }') FROM documentdb_api.collection('db', 'projectexprs') ORDER BY object_id;
SELECT bson_expression_get(document, '{ "field" : [ "1.0" ] }') FROM documentdb_api.collection('db', 'projectexprs') ORDER BY object_id;
SELECT bson_expression_get(document, '{ "field" : { "$minKey": 1 } }') FROM documentdb_api.collection('db', 'projectexprs') ORDER BY object_id;
SELECT bson_expression_get(document, '{ "field" : { "nestedField": "fieldValue" } }') FROM documentdb_api.collection('db', 'projectexprs') ORDER BY object_id;
SELECT bson_expression_get(document, '{ "field" : { "nestedField": "fieldValue", "nestedField2": "fieldValue2" } }') FROM documentdb_api.collection('db', 'projectexprs') ORDER BY object_id;


-- literal expressions
SELECT bson_expression_get(document, '{ "field" : { "$literal": 1.0 } }') FROM documentdb_api.collection('db', 'projectexprs') ORDER BY object_id;
SELECT bson_expression_get(document, '{ "field" : { "$literal": [1, 2, 3] } }') FROM documentdb_api.collection('db', 'projectexprs') ORDER BY object_id;
SELECT bson_expression_get(document, '{ "field" : { "$literal": "some literal" } }') FROM documentdb_api.collection('db', 'projectexprs') ORDER BY object_id;

-- isArray expressions: top level expression
SELECT bson_expression_get(document, '{ "field" : { "$isArray": "some literal" } }') FROM documentdb_api.collection('db', 'projectexprs') ORDER BY object_id;
SELECT bson_expression_get(document, '{ "field" : { "$isArray": { "$literal": [ 1, 2, 3 ] } } }') FROM documentdb_api.collection('db', 'projectexprs') ORDER BY object_id;
SELECT bson_expression_get(document, '{ "field" : { "$isArray": { "$literal": "someLiteral" } } }') FROM documentdb_api.collection('db', 'projectexprs') ORDER BY object_id;
SELECT bson_expression_get(document, '{ "field" : { "$isArray": { "nestedObj": "someValue" } } }') FROM documentdb_api.collection('db', 'projectexprs') ORDER BY object_id;
-- project $a.b along with isArray to validate the values easily.
SELECT bson_expression_get(document, '{ "field" : { "$isArray":  "$a.b" } }'), bson_expression_get(document, '{ "field" : "$a.b" }') FROM documentdb_api.collection('db', 'projectexprs') ORDER BY object_id;

-- test nested operator expansion (this should all be false since the inner operator returns true/false and is never an array)
SELECT bson_expression_get(document, '{ "field" : { "$isArray": { "$isArray":  "$a.b" } } }') FROM documentdb_api.collection('db', 'projectexprs') ORDER BY object_id;

-- isArray expressions: array declaration.
SELECT bson_expression_get(document, '{ "field" : { "$isArray": [ "some literal" ] } }') FROM documentdb_api.collection('db', 'projectexprs') ORDER BY object_id;
SELECT bson_expression_get(document, '{ "field" : { "$isArray": [ { "$literal": [ 1, 2, 3 ] } ] } }') FROM documentdb_api.collection('db', 'projectexprs') ORDER BY object_id;
SELECT bson_expression_get(document, '{ "field" : { "$isArray": [ { "$literal": "someLiteral" } ] } }') FROM documentdb_api.collection('db', 'projectexprs') ORDER BY object_id;
SELECT bson_expression_get(document, '{ "field" : { "$isArray": [ { "nestedObj": "someValue" } ] } }') FROM documentdb_api.collection('db', 'projectexprs') ORDER BY object_id;
SELECT bson_expression_get(document, '{ "field" : { "$isArray": [ "$a.b" ] } }') FROM documentdb_api.collection('db', 'projectexprs') ORDER BY object_id;
SELECT bson_expression_get(document, '{ "field" : { "$isArray": [ [1, 2, 3 ] ] } }') FROM documentdb_api.collection('db', 'projectexprs') ORDER BY object_id;

-- isArray expressions: array declaration invalid
SELECT bson_expression_get(document, '{ "field" : { "$isArray": [ ] } }') FROM documentdb_api.collection('db', 'projectexprs') ORDER BY object_id;
SELECT bson_expression_get(document, '{ "field" : { "$isArray": [ 1, 2 ] } }') FROM documentdb_api.collection('db', 'projectexprs') ORDER BY object_id;

-- unsupported operators
SELECT bson_expression_get(document, '{ "field" : { "$unknownop": "some literal" } }') FROM documentdb_api.collection('db', 'projectexprs') ORDER BY object_id;


-- Array Expression and Nested Expression evaluation tests

SELECT bson_expression_get(document, '{ "new" : ["$_id"]}') FROM documentdb_api.collection('db', 'projectexprs');
SELECT bson_expression_get(document, '{ "new" : ["$a"]}') FROM documentdb_api.collection('db', 'projectexprs');
SELECT bson_expression_get(document, '{ "new" : ["$a.b"]}') FROM documentdb_api.collection('db', 'projectexprs');
SELECT bson_expression_get(document, '{ "new" : {"val": ["$a.b"]}}') FROM documentdb_api.collection('db', 'projectexprs');
SELECT bson_expression_get(document, '{ "new" : {"val": ["1", "2", "$a.b"]}}') FROM documentdb_api.collection('db', 'projectexprs');
SELECT bson_expression_get(document, '{ "new" : {"val": ["1", "2", "$a.b"], "val2": "$_id"}}') FROM documentdb_api.collection('db', 'projectexprs');
SELECT bson_expression_get(document, '{ "field" : { "$isArray": [ "$a.b" ] } }') FROM documentdb_api.collection('db', 'projectexprs');
SELECT bson_expression_get(document, '{ "field" : { "$isArray": [ "$a.b" ] , "a": 1.0} }') FROM documentdb_api.collection('db', 'projectexprs');
SELECT bson_expression_get(document, '{ "field" : { "first": 1.0, "$isArray": [ "$a.b" ]} }') FROM documentdb_api.collection('db', 'projectexprs');
SELECT bson_expression_get(document, '{ "field" : ["$_id", ["$a", "$b"], { "arrayobj" : [{},"$_id"]}] }') FROM documentdb_api.collection('db', 'projectexprs');
SELECT bson_expression_get(document, '{ "field" : [{"$literal" : 3.12}] }') FROM documentdb_api.collection('db', 'projectexprs');
SELECT bson_expression_get(document, '{ "field" : [{"$isArray" : [1,2]}] }') FROM documentdb_api.collection('db', 'projectexprs');
SELECT bson_expression_get(document, '{ "field" : {"$literal" : 3.12} }') FROM documentdb_api.collection('db', 'projectexprs');
SELECT bson_expression_get(document, '{ "field" : {"$isArray" : "$a.b"} }') FROM documentdb_api.collection('db', 'projectexprs');

-- Expressions with undefined paths.
SELECT documentdb_api.insert_one('db', 'projectexprwithundefined','{"_id":"1", "a": [ 1, 2 ] }', NULL);
SELECT documentdb_api.insert_one('db', 'projectexprwithundefined','{"_id":"2", "a": { "b": 1 } }', NULL);
SELECT documentdb_api.insert_one('db', 'projectexprwithundefined','{"_id":"3", "a" : [ { "b": 1 }, 2, 3 ] }', NULL);
SELECT documentdb_api.insert_one('db', 'projectexprwithundefined','{"_id":"4", "a" : { "b": [ 1, 2 ] } }', NULL);

SELECT bson_expression_get(document, '{"field": "$z"}') FROM documentdb_api.collection('db', 'projectexprwithundefined');
SELECT bson_expression_get(document, '{"field": "$a.b.c"}') FROM documentdb_api.collection('db', 'projectexprwithundefined');
SELECT bson_expression_get(document, '{"field": ["$z", 2]}') FROM documentdb_api.collection('db', 'projectexprwithundefined');
SELECT bson_expression_get(document, '{"field": ["$a.b.c", 2]}') FROM documentdb_api.collection('db', 'projectexprwithundefined');
SELECT bson_expression_get(document, '{"field": ["$a.x.y", 2]}') FROM documentdb_api.collection('db', 'projectexprwithundefined');
SELECT bson_expression_get(document, '{"field": ["$z.x.y", 2]}') FROM documentdb_api.collection('db', 'projectexprwithundefined');
SELECT bson_expression_get(document, '{"field": [ ["$a.x.y"], 2, ["$z"]]}') FROM documentdb_api.collection('db', 'projectexprwithundefined');
SELECT bson_expression_get(document, '{"field": [ ["$a.x.y"], 2, [{"$arrayElemAt": [[1], 2]}]]}') FROM documentdb_api.collection('db', 'projectexprwithundefined');
SELECT bson_expression_get(document, '{"field": [ ["$a.x.y"], 2, [{"$arrayElemAt": [[1], 0]}]]}') FROM documentdb_api.collection('db', 'projectexprwithundefined');

-- Error cases when a field is an empty string
SELECT bson_expression_get(document, '{ "field" : {"" : "$a.b"} }') FROM documentdb_api.collection('db', 'projectexprs');
SELECT bson_expression_get(document, '{ "new" : {"val": ["1", "2", {"": "hello"}], "val2": "$_id"}}') FROM documentdb_api.collection('db', 'projectexprs');
SELECT bson_expression_get(document, '{ "field" : [{"literal" : [2, {"" : "empty"}]}] }') FROM documentdb_api.collection('db', 'projectexprs');

-- Field not present or null, NullOnEmpty = false
SELECT bson_expression_get('{}', '{ "": "$a" }', false), 
bson_expression_get('{"a" : null}', '{ "": "$a" }', false),
bson_expression_get('{}', '{ "": { "a" : "$a" }}', false),
bson_expression_get('{"a" : null}', '{ "": { "a" : "$a" }}', false);

-- Field not present or null, NullOnEmpty = true
SELECT bson_expression_get('{}', '{ "": "$a" }', true), 
bson_expression_get('{"a" : null}', '{ "": "$a" }', true),
bson_expression_get('{}', '{ "": { "a" : "$a" }}', true),
bson_expression_get('{"a" : null}', '{ "": { "a" : "$a" }}', true);

-- Document and array expressions with nested expressions that should be converted to a constant at the tree parse stage
SET client_min_messages TO DEBUG3;
SELECT bson_expression_get('{}', '{"result": {"a": [ { "$literal" : "foo" } ] }}');
SELECT bson_expression_get('{}', '{"result": [ { "$literal" : "foo" } ] }');
SELECT bson_expression_get('{}', '{"result": [ { "$undefined" : true } ] }');
SELECT bson_expression_get('{}', '{"result": [ { "$literal" : "foo" }, {"$substr": ["wehello world", 2, -1]} ] }');
SELECT bson_expression_get('{}', '{"result": {"a": [ { "$literal" : "foo" } ], "a": {"$const": "foo2"} }}');
SELECT bson_expression_get('{}', '{"document": {"a": { "$literal" : {"a": "$$REMOVE" } }, "b": {"$const": "b is 2"} }}');
SELECT bson_expression_get('{}', '{"document": {"a": { "b" : {"a": "foo", "d": {"$substr": ["hello world", 6, -1]}} }, "c": {"$const": "c is 2"} }}');
SELECT bson_expression_get('{}', '{"document": {"a": {"d": { "$literal" : {"a": "$$REMOVE" } }, "c": [{"$literal": "$$REMOVE"}, true, false]}, "b": {"$const": "b is 2"}}}');
SELECT bson_dollar_project('{}', '{"array": [{"$substr": ["hello world", 0, -1]}, "1", 2, true, false, {"$literal": [{ "$add":[1, 2] }]}], "document": {"a": { "$literal" : {"a": "$$REMOVE" } }, "b": {"$const": "b is 2"} }}');
SELECT bson_dollar_project('{}', '{"array": [{"$substr": ["hello world", 0, -1]}, "1", 2, true, false, {"$literal": [{ "$add":[1, 2] }]}], "document": {"a": { "$literal" : {"a": "$$REMOVE" } }, "b": {"$const": "b is 2"}}, "array2": [{"$literal": "$$ROOT"}, {"$literal": "$$NOW"}, true], "document2": {"result": {"$substr": ["result is: blah", 11, -1]}}}');
SET client_min_messages TO DEFAULT;
