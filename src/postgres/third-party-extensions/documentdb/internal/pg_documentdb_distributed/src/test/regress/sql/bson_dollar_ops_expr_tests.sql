set search_path to documentdb_core,documentdb_api,documentdb_api_catalog,pg_catalog;
SET citus.next_shard_id TO 7500000;
SET documentdb.next_collection_id TO 7500;
SET documentdb.next_collection_index_id TO 7500;

SELECT documentdb_api.insert_one('db', 'bsonexprtests', '{ "_id": 1, "a": [ 1, 2, 3 ] }');
SELECT documentdb_api.insert_one('db', 'bsonexprtests', '{ "_id": 2, "a": 5 }');
SELECT documentdb_api.insert_one('db', 'bsonexprtests', '{ "_id": 3, "a": "someValue" }');
SELECT documentdb_api.insert_one('db', 'bsonexprtests', '{ "_id": 4, "a": 2 }');

SELECT document FROM documentdb_api.collection('db', 'bsonexprtests') WHERE document @@ '{ "$expr": { "$in": [ "$a", [ 2, "someValue" ] ] } }';
SELECT document FROM documentdb_api.collection('db', 'bsonexprtests') WHERE document @@ '{ "$expr": { "$gte": [ "$a", 3 ] } }';
SELECT document FROM documentdb_api.collection('db', 'bsonexprtests') WHERE document @@ '{ "$expr": { "$isArray": "$a" } }';
SELECT document FROM documentdb_api.collection('db', 'bsonexprtests') WHERE document @@ '{ "$expr": "$a.0" }';

-- invalid scenarios
SELECT document FROM documentdb_api.collection('db', 'bsonexprtests') WHERE document @@ '{ "a": { "$elemMatch": { "$expr": { "$isArray": "$a" } } } }';
SELECT newDocument as bson_update_document FROM documentdb_api_internal.bson_update_document('{"_id": 1, "a": [1,2,3,4,5]}', '{ "": { "$pull": { "a": {"$expr": "$a" } } } }', '{}');
SELECT document FROM documentdb_api.collection('db', 'bsonexprtests') WHERE document @@ '{ "$expr": "$$a" }';