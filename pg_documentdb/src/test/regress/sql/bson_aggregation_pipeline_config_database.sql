SET search_path TO helio_api,helio_core;

SET helio_api.next_collection_id TO 7000;
SET helio_api.next_collection_index_id TO 7000;


-- these run the default commands used by sh.status().
SELECT document FROM helio_api_catalog.bson_aggregation_find('config', '{ "find": "version", "projection": { } }');
SELECT document FROM helio_api_catalog.bson_aggregation_find('config', '{ "find": "databases", "projection": { }, "sort": { "name": 1 } }');
SELECT document FROM helio_api_catalog.bson_aggregation_find('config', '{ "find": "collections", "projection": { }, "sort": { "name": 1 } }');
SELECT document FROM helio_api_catalog.bson_aggregation_find('config', '{ "find": "chunks", "projection": { } }');

-- create 1 db 2 colls.
SELECT helio_api.create_collection('test', 'my_coll1');
SELECT helio_api.create_collection('test', 'my_coll2');
SELECT document FROM helio_api_catalog.bson_aggregation_find('config', '{ "find": "databases", "projection": { }, "sort": { "name": 1 } }');
SELECT document FROM helio_api_catalog.bson_aggregation_find('config', '{ "find": "collections", "projection": { }, "sort": { "name": 1 } }');
SELECT document FROM helio_api_catalog.bson_aggregation_find('config', '{ "find": "chunks", "projection": { } }');

-- add second db
SELECT helio_api.create_collection('test2', 'my_coll1');
SELECT document FROM helio_api_catalog.bson_aggregation_find('config', '{ "find": "databases", "projection": { }, "sort": { "name": 1 } }');
SELECT document FROM helio_api_catalog.bson_aggregation_find('config', '{ "find": "collections", "projection": { }, "sort": { "name": 1 } }');
SELECT document FROM helio_api_catalog.bson_aggregation_find('config', '{ "find": "chunks", "projection": { } }');

-- now shard mycoll1
SELECT helio_api.shard_collection('test2', 'my_coll1', '{ "nameValue": "hashed" }', false);
SELECT document FROM helio_api_catalog.bson_aggregation_find('config', '{ "find": "databases", "projection": { }, "sort": { "name": 1 } }');
SELECT document FROM helio_api_catalog.bson_aggregation_find('config', '{ "find": "collections", "projection": { }, "sort": { "name": 1 } }');
SELECT document FROM helio_api_catalog.bson_aggregation_find('config', '{ "find": "chunks", "projection": { } }');
