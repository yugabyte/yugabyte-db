
SET search_path TO documentdb_core,documentdb_api,documentdb_api_catalog,documentdb_api_internal;

SET citus.next_shard_id TO 32000;
SET documentdb.next_collection_id TO 3200;
SET documentdb.next_collection_index_id TO 3200;

SELECT documentdb_api.insert_one('db','projectops','{"_id":"1", "a" : { "b" : 1 } }', NULL);
SELECT documentdb_api.insert_one('db','projectops','{"_id":"2", "a" : { "b" : [ 0, 1, 2]} }', NULL);
SELECT documentdb_api.insert_one('db','projectops','{"_id":"3", "a" : [ { "b": 0 }, { "b": 1 }, { "b": 3}] }', NULL);
SELECT documentdb_api.insert_one('db','projectops','{"_id":"4", "a" : [ { "b": [-1, 1, 2] }, { "b": [0, 1, 2] }, { "b": [0, 1, 7] }] }', NULL);
SELECT documentdb_api.insert_one('db','projectops','{"_id":"5", "a" : { "b" : [ { "1" : [ 1, 2, 3 ] } ] } }', NULL);
SELECT documentdb_api.insert_one('db','projectops','{"_id":"6", "a" : [ { "c" : 0 }, 2 ] }', NULL);
SELECT documentdb_api.insert_one('db','projectops','{"_id":"7", "a" : [ { "c" : 0 }, { "c" : 1 }, { "b" : 2 } ] }', NULL);
SELECT documentdb_api.insert_one('db','projectops','{"_id":"8", "a" : { "c" : 1 } }', NULL);
SELECT documentdb_api.insert_one('db','projectops','{"_id":"9", "c" : { "d" : 1 } }', NULL);

-- fetch all rows
SELECT shard_key_value, object_id, document FROM documentdb_api.collection('db', 'projectops') ORDER BY object_id;

-- basic a.b query with id excluded and included
SELECT bson_dollar_project(document, '{ "a.b" : 1, "_id": 0 }') FROM documentdb_api.collection('db', 'projectops') ORDER BY object_id;
SELECT bson_dollar_project(document, '{ "a.b" : 1 }') FROM documentdb_api.collection('db', 'projectops') ORDER BY object_id;

-- basic a.b expressed as nested object
SELECT bson_dollar_project(document, '{ "a" : { "b": 1 }, "_id": 0 }') FROM documentdb_api.collection('db', 'projectops') ORDER BY object_id;
SELECT bson_dollar_project(document, '{ "a" : { "b": 1 } }') FROM documentdb_api.collection('db', 'projectops') ORDER BY object_id;

-- basic a.b.1 query with id excluded and included
SELECT bson_dollar_project(document, '{ "a.b.1": 1, "_id": 0 }') FROM documentdb_api.collection('db', 'projectops') ORDER BY object_id;
SELECT bson_dollar_project(document, '{ "a.b.1": 1 }') FROM documentdb_api.collection('db', 'projectops') ORDER BY object_id;

-- basic a.b.1 expressed as nested object
SELECT bson_dollar_project(document, '{ "a" : { "b": { "1" : 1 } }, "_id": 0 }') FROM documentdb_api.collection('db', 'projectops') ORDER BY object_id;
SELECT bson_dollar_project(document, '{ "a" : { "b": { "1" : 1 } } }') FROM documentdb_api.collection('db', 'projectops') ORDER BY object_id;

-- path collsion due to conflicting paths
SELECT bson_dollar_project(document, '{ "a.b.1": 1, "a.b": 1, "_id": 0 }') FROM documentdb_api.collection('db', 'projectops') ORDER BY object_id;
SELECT bson_dollar_project(document, '{ "a.b.1": 1, "a.b": 1 }') FROM documentdb_api.collection('db', 'projectops') ORDER BY object_id;

-- path collsion due to conflicting paths
SELECT bson_dollar_project(document, '{ "a.b": 1, "a.b.1": 1, "_id": 0 }') FROM documentdb_api.collection('db', 'projectops') ORDER BY object_id;
SELECT bson_dollar_project(document, '{ "a.b": 1, "a.b.1": 1 }') FROM documentdb_api.collection('db', 'projectops') ORDER BY object_id;


-- basic a.b, and a.c query with id excluded and included - should be both a.b and a.c
SELECT bson_dollar_project(document, '{ "a.b": 1, "a.c": 1, "_id": 0 }') FROM documentdb_api.collection('db', 'projectops') ORDER BY object_id;
SELECT bson_dollar_project(document, '{ "a.b": 1, "a.c": 1 }') FROM documentdb_api.collection('db', 'projectops') ORDER BY object_id;

-- basic a.b and a.c expressed as nested object
SELECT bson_dollar_project(document, '{ "a" : { "b": 1, "c": 1 }, "_id": 0 }') FROM documentdb_api.collection('db', 'projectops') ORDER BY object_id;
SELECT bson_dollar_project(document, '{ "a" : { "b": 1, "c": 1 } }') FROM documentdb_api.collection('db', 'projectops') ORDER BY object_id;

-- Hybrid: a.c and a.b as object and dotted.
SELECT bson_dollar_project(document, '{ "a" : { "b": 1 }, "a.c": 1, "_id": 0 }') FROM documentdb_api.collection('db', 'projectops') ORDER BY object_id;
SELECT bson_dollar_project(document, '{ "a" : { "c": 1 }, "a.b": 1 }') FROM documentdb_api.collection('db', 'projectops') ORDER BY object_id;

-- basic a.b excluded query with id excluded and included
SELECT bson_dollar_project(document, '{ "a.b" : 0, "_id": 0 }') FROM documentdb_api.collection('db', 'projectops') ORDER BY object_id;
SELECT bson_dollar_project(document, '{ "a.b" : 0 }') FROM documentdb_api.collection('db', 'projectops') ORDER BY object_id;

-- basic a.b and a.c with mixed inclusion (error path) query with id excluded and included
SELECT bson_dollar_project(document, '{ "a.b" : 1, "a.c": 0, "_id": 0 }') FROM documentdb_api.collection('db', 'projectops') ORDER BY object_id;
SELECT bson_dollar_project(document, '{ "a.b" : 1, "a.c": 0 }') FROM documentdb_api.collection('db', 'projectops') ORDER BY object_id;

-- special case projections
SELECT bson_dollar_project(document, '{ "_id" : 1 }') FROM documentdb_api.collection('db', 'projectops') ORDER BY object_id;
SELECT bson_dollar_project(document, '{ "_id" : 0 }') FROM documentdb_api.collection('db', 'projectops') ORDER BY object_id;

-- field 'const' projections - this just projects a const value for all rows.
SELECT bson_dollar_project(document, '{ "_id" : 1, "c": "someString", "a": [ 1, 2, 3] }') FROM documentdb_api.collection('db', 'projectops') ORDER BY object_id;
SELECT bson_dollar_project(document, '{ "_id" : 0, "c": "someString", "a": [ 1, 2, 3] }') FROM documentdb_api.collection('db', 'projectops') ORDER BY object_id;

-- mix and match field path projection and const value projection.
SELECT bson_dollar_project(document, '{ "_id" : 1, "c": [ "1" ], "a.b": 1 }') FROM documentdb_api.collection('db', 'projectops') ORDER BY object_id;
SELECT bson_dollar_project(document, '{ "_id" : 0, "c": [ "1" ], "a.b": 1 }') FROM documentdb_api.collection('db', 'projectops') ORDER BY object_id;

-- const field and path projection with the same field name - see which one wins - it's the one on the right.
SELECT bson_dollar_project(document, '{ "_id" : 0, "a": [ "1" ], "a": 1 }') FROM documentdb_api.collection('db', 'projectops') ORDER BY object_id;
SELECT bson_dollar_project(document, '{ "_id" : 0, "a": 1, "a": [ "1" ] }') FROM documentdb_api.collection('db', 'projectops') ORDER BY object_id;

-- mix and match field path projection and const value projection.
SELECT bson_dollar_project(document, '{ "_id" : 1, "a.g": [ "1" ], "a.b": 1 }') FROM documentdb_api.collection('db', 'projectops') ORDER BY object_id;
SELECT bson_dollar_project(document, '{ "_id" : 0, "a.g": [ "1" ], "a.b": 1 }') FROM documentdb_api.collection('db', 'projectops') ORDER BY object_id;
SELECT bson_dollar_project(document, '{ "_id" : 0, "a.g": [ "1" ] }') FROM documentdb_api.collection('db', 'projectops') ORDER BY object_id;
SELECT bson_dollar_project(document, '{ "_id" : 0, "a": { "g": [ "1" ] }, "a.b": 1 }') FROM documentdb_api.collection('db', 'projectops') ORDER BY object_id;
SELECT bson_dollar_project(document, '{ "_id" : 0, "a": { "g": [ "1" ], "b": 1 } }') FROM documentdb_api.collection('db', 'projectops') ORDER BY object_id;

-- mix and match field path projection and const values on different roots.
SELECT bson_dollar_project(document, '{ "_id" : 0, "e.f": [ "1" ] }') FROM documentdb_api.collection('db', 'projectops') ORDER BY object_id;
SELECT bson_dollar_project(document, '{ "_id" : 0, "a.b": 1, "e.f": [ "1" ] }') FROM documentdb_api.collection('db', 'projectops') ORDER BY object_id;
SELECT bson_dollar_project(document, '{ "_id" : 0, "c": 1, "e.f": [ "1" ] }') FROM documentdb_api.collection('db', 'projectops') ORDER BY object_id;
SELECT bson_dollar_project(document, '{ "_id" : 0, "a.b": 1, "a.c.d": [ "1" ] }') FROM documentdb_api.collection('db', 'projectops') ORDER BY object_id;
SELECT bson_dollar_project(document, '{ "_id" : 0, "a.b": 1, "a.c": [ "1" ] }') FROM documentdb_api.collection('db', 'projectops') ORDER BY object_id;

-- field selector that is an object
-- TODO - actual expression support
SELECT bson_dollar_project(document, '{ "_id" : 0, "a.b": 1, "a.c": { "$literal" : 1.0 } }') FROM documentdb_api.collection('db', 'projectops') ORDER BY object_id;
SELECT bson_dollar_project(document, '{ "_id" : 0, "a.b": 1, "a": { "c": { "$literal" : 1.0 } } }') FROM documentdb_api.collection('db', 'projectops') ORDER BY object_id;

SELECT bson_dollar_project(document, '{ "_id" : 0, "a.b": 1, "c": "$a.b" }') FROM documentdb_api.collection('db', 'projectops') ORDER BY object_id;

SELECT bson_dollar_project(document, '{ "_id" : 0, "a.b": 1, "a": { "c": "$a.b" } }') FROM documentdb_api.collection('db', 'projectops') ORDER BY object_id;

SELECT bson_dollar_project(document, '{ "_id" : 0, "a.b": 1, "a": { "c": "$a" } }') FROM documentdb_api.collection('db', 'projectops') ORDER BY object_id;

SELECT bson_dollar_project(document, '{ "_id" : 0, "a.b": { "$literal": 1.0 } }') FROM documentdb_api.collection('db', 'projectops') ORDER BY object_id;

-- Fields selector, intermediate node having hasField set tests 
SELECT bson_dollar_project(document, '{ "_id" : 0, "c": { "b" : "1"} , "x" : "1"}') FROM documentdb_api.collection('db', 'projectops') ORDER BY object_id;
SELECT bson_dollar_project(document, '{ "_id" : 0, "x" : "1", "c": { "b" : "1" }}') FROM documentdb_api.collection('db', 'projectops') ORDER BY object_id;


-- Add multiple function calls
SELECT bson_dollar_project(document, '{ "a.b" : 1, "_id": 0 }'), bson_dollar_project(document, '{"a.c": 0, "_id": 0 }') FROM documentdb_api.collection('db', 'projectops') ORDER BY object_id;


SELECT bson_dollar_unset('{ "_id": 1, "x": 1, "y": [{ "z": 1, "foo": 1 }] }', '{ "": ["x", "y.z"] }');
SELECT bson_dollar_unset('{ "_id": 2, "title": "Bees Babble", "isbn": "999999999333", "author": { "last": "Bumble", "first": "Bee" }, "copies": [{ "warehouse": "A", "qty": 2 }, { "warehouse": "B", "qty": 5 }] }', 
                                               '{ "": "copies" }');
SELECT bson_dollar_unset('{ "_id": 2, "title": "Bees Babble", "isbn": "999999999333", "author": { "last": "Bumble", "first": "Bee" }, "copies": [{ "warehouse": "A", "qty": 2 }, { "warehouse": "B", "qty": 5 }] }', 
                                               '{ "": ["isbn", "author.first", "copies.warehouse"] }');

-- in aggregation you can unset or project the _id away.
SELECT bson_dollar_unset('{"_id": 1, "a": 1, "b": 2 }', '{ "": "_id" }');
SELECT bson_dollar_project('{"_id": 1, "a": 1, "b": 2 }', '{ "_id": 0 }');

-- Spec tree equivalency tests
SELECT bson_dollar_project(document, '{"a.b.c" :  "1"}')  FROM documentdb_api.collection('db', 'projectops') ORDER BY object_id;
SELECT bson_dollar_project(document, '{"a.b" : { "c": "1"}}')  FROM documentdb_api.collection('db', 'projectops') ORDER BY object_id;
SELECT bson_dollar_project(document, '{"a" : { "b.c": "1"}}')  FROM documentdb_api.collection('db', 'projectops') ORDER BY object_id;
SELECT bson_dollar_project(document, '{"a" : { "b" : { "c": "1"}}}')  FROM documentdb_api.collection('db', 'projectops') ORDER BY object_id;

-- path collision tests
SELECT bson_dollar_project(document, '{"a.b": 1, "a.b.c": 1}')  FROM documentdb_api.collection('db', 'projectops') ORDER BY object_id;

SELECT bson_dollar_project(document, '{"a.b": 1, "a" : { "b" : { "c": "1"}}}')  FROM documentdb_api.collection('db', 'projectops') ORDER BY object_id;

SELECT bson_dollar_project(document, '{"a.b": 1, "a" : { "b" : { "c": "$_id"}}}')  FROM documentdb_api.collection('db', 'projectops') ORDER BY object_id;

SELECT bson_dollar_project(document, '{"a.b": 1, "a" : { "b" : { "c": "$_id"}}}')  FROM documentdb_api.collection('db', 'projectops') ORDER BY object_id;

SELECT bson_dollar_project(document, '{"a.b": {"c" : 1.0}, "a" : { "b" : { "c": { "d": "$_id"}}}}')  FROM documentdb_api.collection('db', 'projectops') ORDER BY object_id;

SELECT bson_dollar_project(document, '{"a.b": {"c" : "$a.b"}, "a" : { "b" : { "c": { "d": "$_id"}}}}')  FROM documentdb_api.collection('db', 'projectops') ORDER BY object_id;

SELECT bson_dollar_project(document, '{"a.b.c": "$_id", "a.b": "1.0"}')  FROM documentdb_api.collection('db', 'projectops') ORDER BY object_id;

SELECT bson_dollar_project(document, '{"a" : { "b" : { "c": { "d": "$_id"}}}, "a.b": {"c" : 1.0}}')  FROM documentdb_api.collection('db', 'projectops') ORDER BY object_id;

-- Invalid key format tests
SELECT bson_dollar_project(document, '{"$a.b": 1, "a.b.c": 1}')  FROM documentdb_api.collection('db', 'projectops') ORDER BY object_id;
SELECT bson_dollar_project(document, '{"$isArray": 1}')  FROM documentdb_api.collection('db', 'projectops') ORDER BY object_id;
SELECT bson_dollar_project(document, '{"$keyName": "newVal"}')  FROM documentdb_api.collection('db', 'projectops') ORDER BY object_id;


SELECT documentdb_api.insert_one('db','projectops','{"_id":"10", "a" : { "b" : { "c" : "abc" } } }', NULL);

-- Spec tree equivalency tests
SELECT bson_dollar_project(document, '{"a.b.c" :  1}')  FROM documentdb_api.collection('db', 'projectops') ORDER BY object_id;
SELECT bson_dollar_project(document, '{"a.b" : { "c": 1}}')  FROM documentdb_api.collection('db', 'projectops') ORDER BY object_id;
SELECT bson_dollar_project(document, '{"a" : { "b.c": 1}}')  FROM documentdb_api.collection('db', 'projectops') ORDER BY object_id;
SELECT bson_dollar_project(document, '{"a" : { "b" : { "c": 1}}}')  FROM documentdb_api.collection('db', 'projectops') ORDER BY object_id;

SELECT documentdb_api.insert_one('db','projectops','{"_id":"11", "a" : { "b_" : { "c" : { "d" : 100 } }, "c" : { "d" : { "e" : "_e"}}}}', NULL);

-- Spec tree test to check if a node is incorrectly marked with Intermediate_WithField
SELECT bson_dollar_project(document, '{"a.b.c" : { "d" : 1 }, "a.c.d" : {"e" : "1"}}')  FROM documentdb_api.collection('db', 'projectops') ORDER BY object_id;

-- test to check if internal "_id" respects the inclusion/exclusion
SELECT documentdb_api.insert_one('db','projectops','{"_id":"12", "a" : { "_id" : { "idVal" : 100 }, "b" : { "c" : { "_id" : "idVal"}}}}', NULL);
SELECT bson_dollar_project(document, '{"_id" : 1, "a" : {"_id" : 1}, "a.b" : { "c" : {"_id" : 0}}}')  FROM documentdb_api.collection('db', 'projectops') ORDER BY object_id;

-- Test nested array projections 
SELECT bson_dollar_project('{"_id":"1", "a" : [1, {"d":1}, [3, 4], "x"] }', '{"a" : { "c" : { "d": "1"}}}');
SELECT bson_dollar_project('{"_id":"1", "a" : [1, {"d":1}, [ { "c" : { "b" : 1 } },4], "x"] }', '{"a" : { "c" : { "d": "1"}}}');


-- a.b.0 excluded with _id included should still write out non-intermediate paths
SELECT bson_dollar_project('{ "_id": 3, "x": { "y": 1 } }', '{ "_id" : 1, "x.y": 0 }');
SELECT bson_dollar_project('{ "_id": 5, "x": { "y": [ 1, 2, 3]}, "v": { "w": [ 4, 5, 6 ]}}', '{ "_id" : 1, "x.y": 0 }');
SELECT bson_dollar_project('{ "_id": 6, "x": { "y": 4}, "v": { "w": 4}}', '{ "_id" : 1, "x.y": 0 }');
SELECT bson_dollar_project('{ "_id": 10, "a": 10, "x": 1 }', '{ "_id" : 1, "x.y": 0 }');

-- Empty spec is a no-op
SELECT bson_dollar_project(document, '{}')  FROM documentdb_api.collection('db', 'projectops') ORDER BY object_id;
