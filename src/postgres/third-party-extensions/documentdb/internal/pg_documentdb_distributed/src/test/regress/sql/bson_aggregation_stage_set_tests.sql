SET search_path TO documentdb_core,documentdb_api,documentdb_api_catalog,documentdb_api_internal;

SET citus.next_shard_id TO 360000;
SET documentdb.next_collection_id TO 3600;
SET documentdb.next_collection_index_id TO 3600;

SELECT documentdb_api.insert_one('db','setOps','{"_id":"1", "int": 10, "a" : { "b" : [ "x", 1, 2.0, true ] } }', NULL);
SELECT documentdb_api.insert_one('db','setOps','{"_id":"2", "double": 2.0, "a" : { "b" : {"c": 3} } }', NULL);
SELECT documentdb_api.insert_one('db','setOps','{"_id":"3", "boolean": false, "a" : "no", "b": "yes", "c": true }', NULL);

-- fetch all rows
SELECT shard_key_value, object_id, document FROM documentdb_api.collection('db', 'setOps') ORDER BY object_id;

-- add newField
SELECT bson_dollar_set(document, '{ "newField" : "1", "a.y": ["p", "q"]}') FROM documentdb_api.collection('db', 'setOps');

-- add field that evaluates $_id
SELECT bson_dollar_set(document, '{ "newField" : "3", "a": "$_id"}') FROM documentdb_api.collection('db', 'setOps');

-- Add integer field (note that, {"field" : 1} is treated as inclusion for $project but add for $set)
SELECT bson_dollar_set(document, '{ "int" : 1, "doble": 2.0, "bool": false, "a.b": false, "a.b.c": 2.0, "a": "$_id"}') FROM documentdb_api.collection('db', 'setOps');

-- $set array duplication
-- Expected value of the field "a" of dco(id=1), after the following $set: { "a.b.c": "_"} is:
-- "a": {"b" : ["c" : "-", "c" : "-","c" : "-","c" : "-",]}
SELECT bson_dollar_set(document, '{ "a.b.c" : "-"}') FROM documentdb_api.collection('db', 'setOps');

-- $set integer in a nested paths
SELECT bson_dollar_set(document, '{ "a" : {"b" : 1}}') FROM documentdb_api.collection('db', 'setOps');

-- $set integer in a nested paths
SELECT bson_dollar_set(document, '{ "a" : {"b" : { "d": 2.5}}}') FROM documentdb_api.collection('db', 'setOps');


-- $set to check the a.b field of doc(id=2) changes 
-- from: {"a" : { "b" : {"c": 3}}
-- to: "a" : { "b" : {"c": 3, "d": "-"}
SELECT bson_dollar_set(document, '{ "a.b.d" : "-"}') FROM documentdb_api.collection('db', 'setOps');

-- $set: Applying multiple expressions with overlapping field paths
SELECT bson_dollar_set(document, '{ "a.b.c" : "_c", "a.b.d" : "_d", "a.b.e": "_e"}') FROM documentdb_api.collection('db', 'setOps');

-- $set: Applying multiple expressions with overlapping field paths
SELECT bson_dollar_set(document, '{ "a.b.c" : "$_id", "a.b.d" : "$b", "a.b.e": "_e"}') FROM documentdb_api.collection('db', 'setOps');

-- $set: If matching subpath in a document has an array, remaining path of the $set spec tree is duplicated for all array elelemnts. Remain subpath may need recursion.
SELECT bson_dollar_set(document, '{ "a.b.c.d.e" : "_c", "a.b.d" : "_d", "a.b.e": "_e"}') FROM documentdb_api.collection('db', 'setOps');

-- $set where "$a.b" needs to be evaluated while writing a path
SELECT bson_dollar_set(document, '{ "a.b.c.d.e" : "$a.b", "a.b.d" : "_d", "a.b.e": "_e"}') FROM documentdb_api.collection('db', 'setOps');

-- Evaluating array expression i.e., <field> : [<expression>, <expression>] is not a documented behavior  
SELECT bson_dollar_set(document, '{ "newarray" : [{ "$literal": 1.0 }, {"copyId": "$_id"}]}') FROM documentdb_api.collection('db', 'setOps');

-- add newField with concatArrays
SELECT bson_dollar_set(document, '{ "newField" : "1", "a.b": {"$concatArrays" : [[7]]}}') FROM documentdb_api.collection('db', 'setOps');

SELECT bson_dollar_set(document, '{ "_id" : false}') FROM documentdb_api.collection('db', 'setOps');
SELECT bson_dollar_set(document, '{ "_id" : 121}') FROM documentdb_api.collection('db', 'setOps');
SELECT bson_dollar_set(document, '{ "_id" : 212.2}') FROM documentdb_api.collection('db', 'setOps');
SELECT bson_dollar_set(document, '{ "_id" : "someString"}') FROM documentdb_api.collection('db', 'setOps');
