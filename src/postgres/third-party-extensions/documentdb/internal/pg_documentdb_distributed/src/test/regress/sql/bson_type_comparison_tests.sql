SET search_path TO documentdb_core,documentdb_api,documentdb_api_catalog,documentdb_api_internal;

SET citus.next_shard_id TO 66100;
SET documentdb.next_collection_id TO 6610;
SET documentdb.next_collection_index_id TO 6610;

-- insert double
SELECT documentdb_api.insert_one('db','bsontypetests', '{ "a" : 3.0 }', NULL);
SELECT documentdb_api.insert_one('db','bsontypetests', '{ "a" : 5.0 }', NULL);
SELECT documentdb_api.insert_one('db','bsontypetests', '{ "a" : -1.0 }', NULL);

-- string
SELECT documentdb_api.insert_one('db','bsontypetests', '{ "a" : "Hell" }', NULL);
SELECT documentdb_api.insert_one('db','bsontypetests', '{ "a" : "hell" }', NULL);
SELECT documentdb_api.insert_one('db','bsontypetests', '{ "a" : "hello world" }', NULL);
SELECT documentdb_api.insert_one('db','bsontypetests', '{ "a" : "Actual string" }', NULL);

-- object
SELECT documentdb_api.insert_one('db','bsontypetests', '{ "a" : { "a": 1 } }', NULL);
SELECT documentdb_api.insert_one('db','bsontypetests', '{ "a" : { "b": 1 } }', NULL);
SELECT documentdb_api.insert_one('db','bsontypetests', '{ "a" : { "ba": 1 } }', NULL);
SELECT documentdb_api.insert_one('db','bsontypetests', '{ "a" : { "c": 1 } }', NULL);

-- array
SELECT documentdb_api.insert_one('db','bsontypetests', '{ "a" : [ 1, 2, 3 ] }', NULL);
SELECT documentdb_api.insert_one('db','bsontypetests', '{ "a" : [ true, "string", 2.0 ] }', NULL);

-- binary
SELECT documentdb_api.insert_one('db','bsontypetests', '{ "a" : { "$binary": { "base64": "VGhpcyBpcyBhIHN0cmluZw==", "subType": "00" }} }', NULL);
SELECT documentdb_api.insert_one('db','bsontypetests', '{ "a" : { "$binary": { "base64": "QSBxdWljayBicm93biBmb3g=", "subType": "00" }} }', NULL);

-- object_id
SELECT documentdb_api.insert_one('db','bsontypetests', '{ "a" : { "$oid": "000102030405060708090A0B" } }', NULL);
SELECT documentdb_api.insert_one('db','bsontypetests', '{ "a" : { "$oid": "0102030405060708090A0B0C" } }', NULL);

-- bool
SELECT documentdb_api.insert_one('db','bsontypetests', '{ "a" : true }', NULL);
SELECT documentdb_api.insert_one('db','bsontypetests', '{ "a" : false }', NULL);

-- date
SELECT documentdb_api.insert_one('db','bsontypetests', '{ "a" : { "$date": { "$numberLong": "123" } } }', NULL);
SELECT documentdb_api.insert_one('db','bsontypetests', '{ "a" : { "$date": { "$numberLong": "5192" } } }', NULL);
SELECT documentdb_api.insert_one('db','bsontypetests', '{ "a" : { "$date": { "$numberLong": "-200" } } }', NULL);
SELECT documentdb_api.insert_one('db','bsontypetests', '{ "a" : { "$date": { "$numberLong": "1647277893736" } } }', NULL);

-- null
SELECT documentdb_api.insert_one('db','bsontypetests', '{ "a" : null }', NULL);

-- regex
SELECT documentdb_api.insert_one('db','bsontypetests', '{ "a" : { "$regularExpression": { "pattern": "^foo$", "options": "gi" }} }', NULL);
SELECT documentdb_api.insert_one('db','bsontypetests', '{ "a" : { "$regularExpression": { "pattern": "^foo$", "options": "" }} }', NULL);
SELECT documentdb_api.insert_one('db','bsontypetests', '{ "a" : { "$regularExpression": { "pattern": "bar$", "options": "g" }} }', NULL);

-- int32
SELECT documentdb_api.insert_one('db','bsontypetests', '{ "a" : { "$numberInt": "25" } }', NULL);
SELECT documentdb_api.insert_one('db','bsontypetests', '{ "a" : { "$numberInt": "13486" } }', NULL);
SELECT documentdb_api.insert_one('db','bsontypetests', '{ "a" : { "$numberInt": "-25" } }', NULL);

-- int64
SELECT documentdb_api.insert_one('db','bsontypetests', '{ "a" : { "$numberLong": "25" } }', NULL);
SELECT documentdb_api.insert_one('db','bsontypetests', '{ "a" : { "$numberLong": "13486" } }', NULL);
SELECT documentdb_api.insert_one('db','bsontypetests', '{ "a" : { "$numberLong": "-25" } }', NULL);

-- timestamp
SELECT documentdb_api.insert_one('db','bsontypetests', '{ "a" : { "$timestamp": { "t": 256, "i": 1 }} }', NULL);
SELECT documentdb_api.insert_one('db','bsontypetests', '{ "a" : { "$timestamp": { "t": 256, "i": 25 }} }', NULL);
SELECT documentdb_api.insert_one('db','bsontypetests', '{ "a" : { "$timestamp": { "t": 200, "i": 10 }} }', NULL);
SELECT documentdb_api.insert_one('db','bsontypetests', '{ "a" : { "$timestamp": { "t": 200, "i": 80 }} }', NULL);

-- minkey, maxkey
SELECT documentdb_api.insert_one('db','bsontypetests', '{ "a" : { "$minKey": 1 } }', NULL);
SELECT documentdb_api.insert_one('db','bsontypetests', '{ "a" : { "$maxKey": 1 } }', NULL);

-- These type comparison don't work.
-- Decimal128

-- these types don't have an extended json representation
-- Javascript
-- Javascript with scope
-- Symbol
-- DBPointer

SELECT document -> 'a' FROM documentdb_api.collection('db', 'bsontypetests') ORDER BY document -> 'a', object_id;
