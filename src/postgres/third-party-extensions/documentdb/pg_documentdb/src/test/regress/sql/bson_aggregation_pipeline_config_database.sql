SET search_path TO documentdb_api,documentdb_core;

SET documentdb.next_collection_id TO 7000;
SET documentdb.next_collection_index_id TO 7000;


-- these run the default commands used by sh.status().
SELECT document FROM documentdb_api_catalog.bson_aggregation_find('config', '{ "find": "version", "projection": { } }');
SELECT document FROM documentdb_api_catalog.bson_aggregation_find('config', '{ "find": "databases", "projection": { }, "sort": { "name": 1 } }');
SELECT document FROM documentdb_api_catalog.bson_aggregation_find('config', '{ "find": "collections", "projection": { }, "sort": { "name": 1 } }');
SELECT document FROM documentdb_api_catalog.bson_aggregation_find('config', '{ "find": "chunks", "projection": { } }');

-- create 1 db 2 colls.
SELECT documentdb_api.create_collection('test', 'my_coll1');
SELECT documentdb_api.create_collection('test', 'my_coll2');
SELECT document FROM documentdb_api_catalog.bson_aggregation_find('config', '{ "find": "databases", "projection": { }, "sort": { "name": 1 } }');
SELECT document FROM documentdb_api_catalog.bson_aggregation_find('config', '{ "find": "collections", "projection": { }, "sort": { "name": 1 } }');
SELECT document FROM documentdb_api_catalog.bson_aggregation_find('config', '{ "find": "chunks", "projection": { } }');

-- add second db
SELECT documentdb_api.create_collection('test2', 'my_coll1');
SELECT document FROM documentdb_api_catalog.bson_aggregation_find('config', '{ "find": "databases", "projection": { }, "sort": { "name": 1 } }');
SELECT document FROM documentdb_api_catalog.bson_aggregation_find('config', '{ "find": "collections", "projection": { }, "sort": { "name": 1 } }');
SELECT document FROM documentdb_api_catalog.bson_aggregation_find('config', '{ "find": "chunks", "projection": { } }');

-- now shard mycoll1
SELECT documentdb_api.shard_collection('test2', 'my_coll1', '{ "nameValue": "hashed" }', false);
SELECT document FROM documentdb_api_catalog.bson_aggregation_find('config', '{ "find": "databases", "projection": { }, "sort": { "name": 1 } }');
SELECT document FROM documentdb_api_catalog.bson_aggregation_find('config', '{ "find": "collections", "projection": { }, "sort": { "name": 1 } }');
SELECT document FROM documentdb_api_catalog.bson_aggregation_find('config', '{ "find": "chunks", "projection": { } }');
