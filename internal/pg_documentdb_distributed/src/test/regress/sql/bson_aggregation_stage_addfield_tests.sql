SET search_path TO helio_core,helio_api,helio_api_catalog,helio_api_internal;

SET citus.next_shard_id TO 310000;
SET helio_api.next_collection_id TO 3100;
SET helio_api.next_collection_index_id TO 3100;

SELECT helio_api.insert_one('db','addFieldOps','{"_id":"1", "int": 10, "a" : { "b" : [ "x", 1, 2.0, true ] } }', NULL);
SELECT helio_api.insert_one('db','addFieldOps','{"_id":"2", "double": 2.0, "a" : { "b" : {"c": 3} } }', NULL);
SELECT helio_api.insert_one('db','addFieldOps','{"_id":"3", "boolean": false, "a" : "no", "b": "yes", "c": true }', NULL);

-- fetch all rows
SELECT shard_key_value, object_id, document FROM helio_api.collection('db', 'addFieldOps') ORDER BY object_id;

-- add newField
SELECT bson_dollar_add_fields(document, '{ "newField" : "1", "a.y": ["p", "q"]}') FROM helio_api.collection('db', 'addFieldOps');

-- add field that evaluates $_id
SELECT bson_dollar_add_fields(document, '{ "newField" : "3", "a": "$_id"}') FROM helio_api.collection('db', 'addFieldOps');

-- Add integer field (note that, {"field" : 1} is treated as inclusion for $project but add for $addFields)
SELECT  bson_dollar_add_fields(document, '{ "int" : 1, "doble": 2.0, "bool": false, "a.d": false, "a.b.c": "$_id"}') FROM helio_api.collection('db', 'addFieldOps');

-- addFields array duplication
-- Expected value of the field "a" of dco(id=1), after the following addField: { "a.b.c": "_"} is:
-- "a": {"b" : ["c" : "-", "c" : "-","c" : "-","c" : "-",]}
SELECT bson_dollar_add_fields(document, '{ "a.b.c" : "-"}') FROM helio_api.collection('db', 'addFieldOps');

-- addFields intege in a nested paths
SELECT bson_dollar_add_fields(document, '{ "a" : {"b" : 1}}') FROM helio_api.collection('db', 'addFieldOps');

-- addFields intege in a nested paths
SELECT bson_dollar_add_fields(document, '{ "a" : {"b" : { "d": 2.5}}}') FROM helio_api.collection('db', 'addFieldOps');


-- addFileds to check the a.b field of doc(id=2) changes 
-- from: {"a" : { "b" : {"c": 3}}
-- to: "a" : { "b" : {"c": 3, "d": "-"}
SELECT bson_dollar_add_fields(document, '{ "a.b.d" : "-"}') FROM helio_api.collection('db', 'addFieldOps');

-- addFields: Applying multiple expressions with overlapping field paths
SELECT bson_dollar_add_fields(document, '{ "a.b.c" : "_c", "a.b.d" : "_d", "a.b.e": "_e"}') FROM helio_api.collection('db', 'addFieldOps');

-- addFields: Applying multiple expressions with overlapping field paths
SELECT bson_dollar_add_fields(document, '{ "a.b.c" : "$_id", "a.b.d" : "$b", "a.b.e": "_e"}') FROM helio_api.collection('db', 'addFieldOps');

-- addFields: If matching subpath in a document has an array, remaining path of the addFields spec tree is duplicated for all array elelemnts. Remain subpath may need recursion.
SELECT bson_dollar_add_fields(document, '{ "a.b.c.d.e" : "_c", "a.b.d" : "_d", "a.b.e": "_e"}') FROM helio_api.collection('db', 'addFieldOps');

-- addFields where "$a.b" needs to be evaluated while writing a path
SELECT bson_dollar_add_fields(document, '{ "a.b.c.d.e" : "$a.b", "a.b.d" : "_d", "a.b.e": "_e"}') FROM helio_api.collection('db', 'addFieldOps');

-- Evaluating array of expessions i.e., <field> : [<expression>, <expression>] is not a documented behavior  
SELECT bson_dollar_add_fields(document, '{ "newarray" : [{ "$literal": 1.0 }, {"copyId": "$_id"}]}') FROM helio_api.collection('db', 'addFieldOps');

-- add newField with concatArrays
SELECT bson_dollar_add_fields(document, '{ "newField" : "1", "a.b": {"$concatArrays" : [[7], [8], [9]]}}') FROM helio_api.collection('db', 'addFieldOps');

SELECT bson_dollar_add_fields(document, '{ "_id" : false}') FROM helio_api.collection('db', 'addFieldOps');
SELECT bson_dollar_add_fields(document, '{ "_id" : 121}') FROM helio_api.collection('db', 'addFieldOps');
SELECT bson_dollar_add_fields(document, '{ "_id" : 212.2}') FROM helio_api.collection('db', 'addFieldOps');
SELECT bson_dollar_add_fields(document, '{ "_id" : "someString"}') FROM helio_api.collection('db', 'addFieldOps');

-- path collision tests
SELECT bson_dollar_add_fields(document, '{ "a.b.c.d.e" : "_c", "a.b.c" : "_d"}') FROM helio_api.collection('db', 'addFieldOps');

SELECT bson_dollar_add_fields(document, '{ "a.b.c" : "_c", "a.b" : "_d"}') FROM helio_api.collection('db', 'addFieldOps');

SELECT  bson_dollar_add_fields(document, '{"a.b": 1, "a.b.c": 1}')  FROM helio_api.collection('db', 'addFieldOps') ORDER BY object_id;

SELECT  bson_dollar_add_fields(document, '{"a.b": 1, "a" : { "b" : { "c": "1"}}}')  FROM helio_api.collection('db', 'addFieldOps') ORDER BY object_id;

SELECT  bson_dollar_add_fields(document, '{"a.b": 1, "a" : { "b" : { "c": "$_id"}}}')  FROM helio_api.collection('db', 'addFieldOps') ORDER BY object_id;

SELECT  bson_dollar_add_fields(document, '{"a.b": {"c" : 1.0}, "a" : { "b" : { "c": { "d": "$_id"}}}}')  FROM helio_api.collection('db', 'addFieldOps') ORDER BY object_id;

SELECT  bson_dollar_add_fields(document, '{"a.b": {"c" : "$a.b"}, "a" : { "b" : { "c": { "d": "$_id"}}}}')  FROM helio_api.collection('db', 'addFieldOps') ORDER BY object_id;

SELECT  bson_dollar_add_fields(document, '{"a.b.c": "$_id", "a.b": "1.0"}')  FROM helio_api.collection('db', 'addFieldOps') ORDER BY object_id;

SELECT  bson_dollar_add_fields(document, '{"a" : { "b" : { "c": { "d": "$_id"}}}, "a.b": {"c" : 1.0}}')  FROM helio_api.collection('db', 'addFieldOps') ORDER BY object_id;

-- Array Expression and Nested Expression evaluation tests
SELECT bson_dollar_add_fields(document, '{ "new" : ["$_id"]}') FROM helio_api.collection('db', 'addFieldOps');
SELECT bson_dollar_add_fields(document, '{ "new" : ["$a"]}') FROM helio_api.collection('db', 'addFieldOps');
SELECT bson_dollar_add_fields(document, '{ "new" : ["$a.b"]}') FROM helio_api.collection('db', 'addFieldOps');
SELECT bson_dollar_add_fields(document, '{ "new" : {"val": ["$a.b"]}}') FROM helio_api.collection('db', 'addFieldOps');
SELECT bson_dollar_add_fields(document, '{ "field" : { "$isArray": [ "$a.b" ] } }') FROM helio_api.collection('db', 'addFieldOps');

-- Spec trees are equivalent
SELECT  bson_dollar_add_fields(document, '{"a.b": {"c" : "value"}}')  FROM helio_api.collection('db', 'addFieldOps') ORDER BY object_id;
SELECT  bson_dollar_add_fields(document, '{"a.b.c": "value"}')  FROM helio_api.collection('db', 'addFieldOps') ORDER BY object_id;

-- Test nested array projections 
SELECT bson_dollar_add_fields('{"_id":"1", "a" : [1, {"d":1}, [3, 4], "x"] }', '{"a" : { "c" : { "d": "1"}}}');
SELECT bson_dollar_add_fields('{"_id":"1", "a" : [1, {"d":1}, [ { "c" : { "b" : 1 } },4], "x"] }', '{"a" : { "c" : { "d": "1"}}}');

-- Empty spec is a no-op according to 4.4.13\jstests\aggregation\sources\addFields\use_cases.js
SELECT  bson_dollar_add_fields(document, '{}')  FROM helio_api.collection('db', 'addFieldOps') ORDER BY object_id;
