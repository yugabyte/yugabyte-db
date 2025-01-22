
SET search_path TO documentdb_core,documentdb_api,documentdb_api_catalog,documentdb_api_internal;
SET citus.next_shard_id TO 245000;
SET documentdb.next_collection_id TO 2450;
SET documentdb.next_collection_index_id TO 2450;

SELECT documentdb_api.insert_one('db','mapprojectexprs','{"array": [{"_id":"1", "a" : { "b" : 1 } },{"_id":"2", "a" : { "b" : [ 0, 1, 2]} },{"_id":"3", "a" : [ { "b": 0 }, { "b": 1 }, { "b": 3}] },{"_id":"4", "a" : [ { "b": [-1, 1, 2] }, { "b": [0, 1, 2] }, { "b": [0, 1, 7] }] },{"_id":"5", "a" : { "b" : [ { "1" : [ 1, 2, 3 ] } ] } },{"_id":"6", "a" : [ { "c" : 0 }, 2 ] },{"_id":"7", "a" : [ { "c" : 0 }, { "c" : 1 }, { "b" : 2 } ] },{"_id":"8", "a" : { "c" : 1 } },{"_id":"9", "c" : { "d" : 1 } }]}', NULL);


-- field path expressions
SELECT bson_expression_map(document, 'array', '{ "field" : "$a" }') FROM documentdb_api.collection('db', 'mapprojectexprs') ORDER BY object_id;
SELECT bson_expression_map(document, 'array', '{ "field" : "$a.b" }') FROM documentdb_api.collection('db', 'mapprojectexprs') ORDER BY object_id;
SELECT bson_expression_map(document, 'array', '{ "field" : "$a.c" }') FROM documentdb_api.collection('db', 'mapprojectexprs') ORDER BY object_id;

-- field path expressions on bson_expression_get() with nullOnEmpty = true
SELECT bson_expression_map(document, 'array', '{ "field" : "$a" }', true) FROM documentdb_api.collection('db', 'mapprojectexprs') ORDER BY object_id;
SELECT bson_expression_map(document, 'array', '{ "field" : "$a.b" }', true) FROM documentdb_api.collection('db', 'mapprojectexprs') ORDER BY object_id;
SELECT bson_expression_map(document, 'array', '{ "field" : "$a.c" }', true) FROM documentdb_api.collection('db', 'mapprojectexprs') ORDER BY object_id;

-- field path expressions on bson_expression_get() with nullOnEmpty = false
SELECT bson_expression_map(document, 'array', '{ "field" : "$a" }', false) FROM documentdb_api.collection('db', 'mapprojectexprs') ORDER BY object_id;
SELECT bson_expression_map(document, 'array', '{ "field" : "$a.b" }', false) FROM documentdb_api.collection('db', 'mapprojectexprs') ORDER BY object_id;
SELECT bson_expression_map(document, 'array', '{ "field" : "$a.c" }', false) FROM documentdb_api.collection('db', 'mapprojectexprs') ORDER BY object_id;

-- const value expressions
SELECT bson_expression_map(document, 'array', '{ "field" : "someString" }') FROM documentdb_api.collection('db', 'mapprojectexprs') ORDER BY object_id;
SELECT bson_expression_map(document, 'array', '{ "field" : [ "1.0" ] }') FROM documentdb_api.collection('db', 'mapprojectexprs') ORDER BY object_id;
SELECT bson_expression_map(document, 'array', '{ "field" : { "$minKey": 1 } }') FROM documentdb_api.collection('db', 'mapprojectexprs') ORDER BY object_id;
SELECT bson_expression_map(document, 'array', '{ "field" : { "nestedField": "fieldValue" } }') FROM documentdb_api.collection('db', 'mapprojectexprs') ORDER BY object_id;
SELECT bson_expression_map(document, 'array', '{ "field" : { "nestedField": "fieldValue", "nestedField2": "fieldValue2" } }') FROM documentdb_api.collection('db', 'mapprojectexprs') ORDER BY object_id;


-- literal expressions
SELECT bson_expression_map(document, 'array', '{ "field" : { "$literal": 1.0 } }') FROM documentdb_api.collection('db', 'mapprojectexprs') ORDER BY object_id;
SELECT bson_expression_map(document, 'array', '{ "field" : { "$literal": [1, 2, 3] } }') FROM documentdb_api.collection('db', 'mapprojectexprs') ORDER BY object_id;
SELECT bson_expression_map(document, 'array', '{ "field" : { "$literal": "some literal" } }') FROM documentdb_api.collection('db', 'mapprojectexprs') ORDER BY object_id;

-- isArray expressions: top level expression
SELECT bson_expression_map(document, 'array', '{ "field" : { "$isArray": "some literal" } }') FROM documentdb_api.collection('db', 'mapprojectexprs') ORDER BY object_id;
SELECT bson_expression_map(document, 'array', '{ "field" : { "$isArray": { "$literal": [ 1, 2, 3 ] } } }') FROM documentdb_api.collection('db', 'mapprojectexprs') ORDER BY object_id;
SELECT bson_expression_map(document, 'array', '{ "field" : { "$isArray": { "$literal": "someLiteral" } } }') FROM documentdb_api.collection('db', 'mapprojectexprs') ORDER BY object_id;
SELECT bson_expression_map(document, 'array', '{ "field" : { "$isArray": { "nestedObj": "someValue" } } }') FROM documentdb_api.collection('db', 'mapprojectexprs') ORDER BY object_id;
-- project $a.b along with isArray to validate the values easily.
SELECT bson_expression_map(document, 'array', '{ "field" : { "$isArray":  "$a.b" } }'), bson_expression_map(document, 'array', '{ "field" : "$a.b" }') FROM documentdb_api.collection('db', 'mapprojectexprs') ORDER BY object_id;

-- test nested operator expansion (this should all be false since the inner operator returns true/false and is never an array)
SELECT bson_expression_map(document, 'array', '{ "field" : { "$isArray": { "$isArray":  "$a.b" } } }') FROM documentdb_api.collection('db', 'mapprojectexprs') ORDER BY object_id;

-- isArray expressions: array declaration.
SELECT bson_expression_map(document, 'array', '{ "field" : { "$isArray": [ "some literal" ] } }') FROM documentdb_api.collection('db', 'mapprojectexprs') ORDER BY object_id;
SELECT bson_expression_map(document, 'array', '{ "field" : { "$isArray": [ { "$literal": [ 1, 2, 3 ] } ] } }') FROM documentdb_api.collection('db', 'mapprojectexprs') ORDER BY object_id;
SELECT bson_expression_map(document, 'array', '{ "field" : { "$isArray": [ { "$literal": "someLiteral" } ] } }') FROM documentdb_api.collection('db', 'mapprojectexprs') ORDER BY object_id;
SELECT bson_expression_map(document, 'array', '{ "field" : { "$isArray": [ { "nestedObj": "someValue" } ] } }') FROM documentdb_api.collection('db', 'mapprojectexprs') ORDER BY object_id;
SELECT bson_expression_map(document, 'array', '{ "field" : { "$isArray": [ "$a.b" ] } }') FROM documentdb_api.collection('db', 'mapprojectexprs') ORDER BY object_id;
SELECT bson_expression_map(document, 'array', '{ "field" : { "$isArray": [ [1, 2, 3 ] ] } }') FROM documentdb_api.collection('db', 'mapprojectexprs') ORDER BY object_id;

-- isArray expressions: array declaration invalid
SELECT bson_expression_map(document, 'array', '{ "field" : { "$isArray": [ ] } }') FROM documentdb_api.collection('db', 'mapprojectexprs') ORDER BY object_id;
SELECT bson_expression_map(document, 'array', '{ "field" : { "$isArray": [ 1, 2 ] } }') FROM documentdb_api.collection('db', 'mapprojectexprs') ORDER BY object_id;

-- unsupported operators
SELECT bson_expression_map(document, 'array', '{ "field" : { "$unknownop": "some literal" } }') FROM documentdb_api.collection('db', 'mapprojectexprs') ORDER BY object_id;


-- Array Expression and Nested Expression evaluation tests

SELECT bson_expression_map(document, 'array', '{ "new" : ["$_id"]}') FROM documentdb_api.collection('db', 'mapprojectexprs');
SELECT bson_expression_map(document, 'array', '{ "new" : ["$a"]}') FROM documentdb_api.collection('db', 'mapprojectexprs');
SELECT bson_expression_map(document, 'array', '{ "new" : ["$a.b"]}') FROM documentdb_api.collection('db', 'mapprojectexprs');
SELECT bson_expression_map(document, 'array', '{ "new" : {"val": ["$a.b"]}}') FROM documentdb_api.collection('db', 'mapprojectexprs');
SELECT bson_expression_map(document, 'array', '{ "new" : {"val": ["1", "2", "$a.b"]}}') FROM documentdb_api.collection('db', 'mapprojectexprs');
SELECT bson_expression_map(document, 'array', '{ "new" : {"val": ["1", "2", "$a.b"], "val2": "$_id"}}') FROM documentdb_api.collection('db', 'mapprojectexprs');
SELECT bson_expression_map(document, 'array', '{ "field" : { "$isArray": [ "$a.b" ] } }') FROM documentdb_api.collection('db', 'mapprojectexprs');
SELECT bson_expression_map(document, 'array', '{ "field" : { "$isArray": [ "$a.b" ] , "a": 1.0} }') FROM documentdb_api.collection('db', 'mapprojectexprs');
SELECT bson_expression_map(document, 'array', '{ "field" : { "first": 1.0, "$isArray": [ "$a.b" ]} }') FROM documentdb_api.collection('db', 'mapprojectexprs');
SELECT bson_expression_map(document, 'array', '{ "field" : ["$_id", ["$a", "$b"], { "arrayobj" : [{},"$_id"]}] }') FROM documentdb_api.collection('db', 'mapprojectexprs');
SELECT bson_expression_map(document, 'array', '{ "field" : [{"$literal" : 3.12}] }') FROM documentdb_api.collection('db', 'mapprojectexprs');
SELECT bson_expression_map(document, 'array', '{ "field" : [{"$isArray" : [1,2]}] }') FROM documentdb_api.collection('db', 'mapprojectexprs');
SELECT bson_expression_map(document, 'array', '{ "field" : {"$literal" : 3.12} }') FROM documentdb_api.collection('db', 'mapprojectexprs');
SELECT bson_expression_map(document, 'array', '{ "field" : {"$isArray" : "$a.b"} }') FROM documentdb_api.collection('db', 'mapprojectexprs');

-- Expressions with undefined paths.
SELECT documentdb_api.insert_one('db', 'mapprojectexprwithundefined','{"array": [{"_id":"1", "a": [ 1, 2 ] },{"_id":"2", "a": { "b": 1 } },{"_id":"3", "a" : [ { "b": 1 }, 2, 3 ] },{"_id":"4", "a" : { "b": [ 1, 2 ] } } ] }', NULL);

SELECT bson_expression_map(document, 'array', '{"field": "$z"}') FROM documentdb_api.collection('db', 'mapprojectexprwithundefined');
SELECT bson_expression_map(document, 'array', '{"field": "$a.b.c"}') FROM documentdb_api.collection('db', 'mapprojectexprwithundefined');
SELECT bson_expression_map(document, 'array', '{"field": ["$z", 2]}') FROM documentdb_api.collection('db', 'mapprojectexprwithundefined');
SELECT bson_expression_map(document, 'array', '{"field": ["$a.b.c", 2]}') FROM documentdb_api.collection('db', 'mapprojectexprwithundefined');
SELECT bson_expression_map(document, 'array', '{"field": ["$a.x.y", 2]}') FROM documentdb_api.collection('db', 'mapprojectexprwithundefined');
SELECT bson_expression_map(document, 'array', '{"field": ["$z.x.y", 2]}') FROM documentdb_api.collection('db', 'mapprojectexprwithundefined');
SELECT bson_expression_map(document, 'array', '{"field": [ ["$a.x.y"], 2, ["$z"]]}') FROM documentdb_api.collection('db', 'mapprojectexprwithundefined');
SELECT bson_expression_map(document, 'array', '{"field": [ ["$a.x.y"], 2, [{"$arrayElemAt": [[1], 2]}]]}') FROM documentdb_api.collection('db', 'mapprojectexprwithundefined');
SELECT bson_expression_map(document, 'array', '{"field": [ ["$a.x.y"], 2, [{"$arrayElemAt": [[1], 0]}]]}') FROM documentdb_api.collection('db', 'mapprojectexprwithundefined');

-- Error cases when a field is an empty string
SELECT bson_expression_map(document, 'array', '{ "field" : {"" : "$a.b"} }') FROM documentdb_api.collection('db', 'mapprojectexprs');
SELECT bson_expression_map(document, 'array', '{ "new" : {"val": ["1", "2", {"": "hello"}], "val2": "$_id"}}') FROM documentdb_api.collection('db', 'mapprojectexprs');
SELECT bson_expression_map(document, 'array', '{ "field" : [{"literal" : [2, {"" : "empty"}]}] }') FROM documentdb_api.collection('db', 'mapprojectexprs');

-- Field not present or null, NullOnEmpty = false
SELECT bson_expression_map('{"array": [{}]}', 'array', '{ "": "$a" }', false), 
bson_expression_map('{"array": [{"a" : null}]}', 'array', '{ "": "$a" }', false),
bson_expression_map('{"array": [{}]}', 'array', '{ "": { "a" : "$a" }}', false),
bson_expression_map('{"array": [{"a" : null}]}', 'array', '{ "": { "a" : "$a" }}', false);

-- Field not present or null, NullOnEmpty = true
SELECT bson_expression_map('{"array": [{}]}', 'array', '{ "": "$a" }', true), 
bson_expression_map('{"array": [{"a" : null}]}', 'array', '{ "": "$a" }', true),
bson_expression_map('{"array": [{}]}', 'array', '{ "": { "a" : "$a" }}', true),
bson_expression_map('{"array": [{"a" : null}]}', 'array', '{ "": { "a" : "$a" }}', true);

-- Array not present or null or not an array
SELECT bson_expression_map('{"array": {}}', 'array', '{ "": "$a" }', true);
SELECT bson_expression_map('{"array": null}', 'array', '{ "": "$a" }', true);
SELECT bson_expression_map('{"array": []}', 'array', '{ "": { "a" : "$a" }}', true);
SELECT bson_expression_map('{"notarray": [{"a" : null}]}', 'array', '{ "": { "a" : "$a" }}', true);

-- Document and array expressions with nested expressions that should be converted to a constant at the tree parse stage
SET client_min_messages TO DEBUG3;
SELECT bson_expression_map('{"array": [{}]}', 'array', '{"result": {"a": [ { "$literal" : "foo" } ] }}');
SELECT bson_expression_map('{"array": [{}]}', 'array', '{"result": [ { "$literal" : "foo" } ] }');
SELECT bson_expression_map('{"array": [{}]}', 'array', '{"result": [ { "$undefined" : true } ] }');
SELECT bson_expression_map('{"array": [{}]}', 'array', '{"result": [ { "$literal" : "foo" }, {"$substr": ["wehello world", 2, -1]} ] }');
SELECT bson_expression_map('{"array": [{}]}', 'array', '{"result": {"a": [ { "$literal" : "foo" } ], "a": {"$const": "foo2"} }}');
SELECT bson_expression_map('{"array": [{}]}', 'array', '{"document": {"a": { "$literal" : {"a": "$$REMOVE" } }, "b": {"$const": "b is 2"} }}');
SELECT bson_expression_map('{"array": [{}]}', 'array', '{"document": {"a": { "b" : {"a": "foo", "d": {"$substr": ["hello world", 6, -1]}} }, "c": {"$const": "c is 2"} }}');
SELECT bson_expression_map('{"array": [{}]}', 'array', '{"document": {"a": {"d": { "$literal" : {"a": "$$REMOVE" } }, "c": [{"$literal": "$$REMOVE"}, true, false]}, "b": {"$const": "b is 2"}}}');
SET client_min_messages TO DEFAULT;
