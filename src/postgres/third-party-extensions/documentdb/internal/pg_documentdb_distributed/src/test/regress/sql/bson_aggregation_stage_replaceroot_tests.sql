SET search_path TO documentdb_core,documentdb_api,documentdb_api_catalog,documentdb_api_internal;

SET citus.next_shard_id TO 33000;
SET documentdb.next_collection_id TO 3300;
SET documentdb.next_collection_index_id TO 3300;

SELECT documentdb_api.insert_one('db','replaceRootOps','{"_id":"1", "int": 10, "x" : {"y" : [1, 2]}, "a" : { "b" : { "arr": [ "x", 1, 2.0, true ]} } }', NULL);
SELECT documentdb_api.insert_one('db','replaceRootOps','{"_id":"2", "double": 2.0, "a" : { "b" : {"c": 3} } }', NULL);
SELECT documentdb_api.insert_one('db','replaceRootOps','{"_id":"3", "boolean": false, "a" : {"x" : "no", "b": {"id": "$_id"}}, "b": "yes", "c": true }', NULL);

-- fetch all rows
SELECT shard_key_value, object_id, document FROM documentdb_api.collection('db', 'replaceRootOps') ORDER BY object_id;

-- replace by newRoot
SELECT bson_dollar_replace_root(document, '{ "newRoot": {"myArr": ["$_id", "$a"]}}') FROM documentdb_api.collection('db', 'replaceRootOps');

SELECT bson_dollar_replace_root(document, '{ "newRoot": {"myArr": ["$_id", "$a"], "2" : { "x" : "$a.b"} }}') FROM documentdb_api.collection('db', 'replaceRootOps');

-- Multiple "newRoot". Uses the most recent one.
SELECT bson_dollar_replace_root(document, '{ "newRoot": {}, "newRoot": {"myArr": ["$_id", "$a"]}}') FROM documentdb_api.collection('db', 'replaceRootOps');

-- Can't have anything other than "newRoot"
SELECT bson_dollar_replace_root(document, '{ "newRoot1": {"myArr": ["$_id", "$a"]}}') FROM documentdb_api.collection('db', 'replaceRootOps');
SELECT bson_dollar_replace_root(document, '{ "newRoot": {"myArr": ["$_id", "$a"]}, "b": "c"}') FROM documentdb_api.collection('db', 'replaceRootOps');

-- 'newRoot' is empty document
SELECT bson_dollar_replace_root(document, '{ "newRoot": { } }') FROM documentdb_api.collection('db', 'replaceRootOps');


-- newRoot is required
SELECT bson_dollar_replace_root(document, '{ }') FROM documentdb_api.collection('db', 'replaceRootOps');

SELECT bson_dollar_replace_root(document, '{ "newRoot": 10 }') FROM documentdb_api.collection('db', 'replaceRootOps');

SELECT bson_dollar_replace_root(document, '{ "newRoot": "$a" }') FROM documentdb_api.collection('db', 'replaceRootOps');

SELECT bson_dollar_replace_root(document, '{ "newRoot": "$a.b" }') FROM documentdb_api.collection('db', 'replaceRootOps');

SELECT bson_dollar_replace_root(document, '{ "newRoot": "$x.y" }') FROM documentdb_api.collection('db', 'replaceRootOps');

-- newRoot is an operator expression
SELECT bson_dollar_replace_root(document, '{ "newRoot": { "$isArray" : "$a.b.arr"} }') FROM documentdb_api.collection('db', 'replaceRootOps');
SELECT bson_dollar_replace_root(document, '{ "newRoot": { "$literal" : 2.0 } }') FROM documentdb_api.collection('db', 'replaceRootOps');
-- if the operator expression references a field which is a field path expression in the original document {"a": {"b": {"id": "$_id"}}}, "$_id" should be treated as a literal.
SELECT bson_dollar_replace_root(document, '{ "newRoot": { "$mergeObjects":  [ { "dogs": 0, "cats": 0, "birds": 0, "fish": 0 }, "$a.b" ] } }') FROM documentdb_api.collection('db', 'replaceRootOps');

-- 
SELECT bson_dollar_replace_root(document, '{ "newRoot": { "$mergeObjects":  [ ["dogs", "cats", "birds", "fish"], "$a.b" ] } }') FROM documentdb_api.collection('db', 'replaceRootOps');

-- negative
SELECT bson_dollar_replace_root('{}', '{ "newRoot": 1 }') FROM documentdb_api.collection('db', 'replaceRootOps');
SELECT bson_dollar_replace_root('{}', '{ "newRoot": "$x" }') FROM documentdb_api.collection('db', 'replaceRootOps');
