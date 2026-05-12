SET search_path TO documentdb_api,documentdb_core;

SET documentdb.next_collection_id TO 101400;
SET documentdb.next_collection_index_id TO 101400;

SET search_path TO documentdb_api,documentdb_core;

select * from documentdb_api.insert_one('sortdb', 'sortcoll', '{ "_id": 1, "a": { "b": 1 } }');
select * from documentdb_api.insert_one('sortdb', 'sortcoll', '{ "_id": 2, "a": { "b": null } }');
select * from documentdb_api.insert_one('sortdb', 'sortcoll', '{ "_id": 3, "a": { "b": "string value" } }');
select * from documentdb_api.insert_one('sortdb', 'sortcoll', '{ "_id": 4, "a": { "b": true } }');
select * from documentdb_api.insert_one('sortdb', 'sortcoll', '{ "_id": 5, "a": { "b": false } }');
select * from documentdb_api.insert_one('sortdb', 'sortcoll', '{ "_id": 6, "a": { "b": [] } }');
select * from documentdb_api.insert_one('sortdb', 'sortcoll', '{ "_id": 7, "a": { "b": [1, 2, 3] } }');
select * from documentdb_api.insert_one('sortdb', 'sortcoll', '{ "_id": 8, "a": { "b": [1, { "$minKey": 1 }, 3, true] } }');
select * from documentdb_api.insert_one('sortdb', 'sortcoll', '{ "_id": 9, "a": { "b": [1, { "$maxKey": 1 }, 3, true] } }');
select * from documentdb_api.insert_one('sortdb', 'sortcoll', '{ "_id": 10, "a": { "b": { "c": 1 } } }');
select * from documentdb_api.insert_one('sortdb', 'sortcoll', '{ "_id": 11, "a": { "b": { "$maxKey": 1 } } }');

-- now some more esoteric values
select * from documentdb_api.insert_one('sortdb', 'sortcoll', '{ "_id": 12, "a": null }');
select * from documentdb_api.insert_one('sortdb', 'sortcoll', '{ "_id": 13, "a": [ {} ] }');
select * from documentdb_api.insert_one('sortdb', 'sortcoll', '{ "_id": 14, "a": [ 1 ] }');
select * from documentdb_api.insert_one('sortdb', 'sortcoll', '{ "_id": 15, "a": [ 1, { "b": 3 } ] }');
select * from documentdb_api.insert_one('sortdb', 'sortcoll', '{ "_id": 16, "a": [ null, { "b": 4 } ] }');
select * from documentdb_api.insert_one('sortdb', 'sortcoll', '{ "_id": 17, "a": [ {}, { "b": 3 } ] }');
select * from documentdb_api.insert_one('sortdb', 'sortcoll', '{ "_id": 18, "a": [ { "c": 1 } ] }');
select * from documentdb_api.insert_one('sortdb', 'sortcoll', '{ "_id": 19, "a": [ { "c": 1 }, { "b": 3 } ] }');

-- baseline 
select * from documentdb_api.insert_one('sortdb', 'sortcoll', '{ "_id": 20, "a": { "b": 0 } }');

SELECT * FROM documentdb_api_catalog.bson_aggregation_find('sortdb', '{ "find": "sortcoll", "filter": {}, "sort": { "a.b": 1, "_id": 1 } }');

-- test exists
SELECT * FROM documentdb_api_catalog.bson_aggregation_find('sortdb', '{ "find": "sortcoll", "filter": { "a.b": { "$exists": true } }, "sort": { "a.b": 1, "_id": 1 } }');
SELECT * FROM documentdb_api_catalog.bson_aggregation_find('sortdb', '{ "find": "sortcoll", "filter": { "a.b": { "$exists": false } }, "sort": { "a.b": 1, "_id": 1 } }');

-- test null
SELECT * FROM documentdb_api_catalog.bson_aggregation_find('sortdb', '{ "find": "sortcoll", "filter": { "a.b": null }, "sort": { "a.b": 1, "_id": 1 } }');
SELECT * FROM documentdb_api_catalog.bson_aggregation_find('sortdb', '{ "find": "sortcoll", "filter": { "a.b": { "$ne": null } }, "sort": { "a.b": 1, "_id": 1 } }');

-- test with composite index
set documentdb.enableExtendedExplainPlans to on;

SELECT documentdb_api_internal.create_indexes_non_concurrently('sortdb', '{ "createIndexes": "sortcoll2", "indexes": [ { "key": { "a.b": 1 }, "enableCompositeTerm": true, "name": "a.b_1" }] }', true);
SELECT COUNT(documentdb_api.insert_one('sortdb', 'sortcoll2', document)) FROM (SELECT document FROM documentdb_api.collection('sortdb', 'sortcoll')) coll;

-- test exists
set documentdb.forceDisableSeqScan to on;
SELECT * FROM documentdb_api_catalog.bson_aggregation_find('sortdb', '{ "find": "sortcoll2", "filter": { "a.b": { "$exists": true } }, "sort": { "a.b": 1, "_id": 1 } }');
SELECT * FROM documentdb_api_catalog.bson_aggregation_find('sortdb', '{ "find": "sortcoll2", "filter": { "a.b": { "$exists": false } }, "sort": { "a.b": 1, "_id": 1 } }');

-- test null
SELECT * FROM documentdb_api_catalog.bson_aggregation_find('sortdb', '{ "find": "sortcoll2", "filter": { "a.b": null }, "sort": { "a.b": 1, "_id": 1 } }');
SELECT * FROM documentdb_api_catalog.bson_aggregation_find('sortdb', '{ "find": "sortcoll2", "filter": { "a.b": { "$ne": null } }, "sort": { "a.b": 1, "_id": 1 } }');

reset documentdb.forceDisableSeqScan;

-- now repeat with 3 dotted paths
SELECT * FROM documentdb_api.insert_one('sortdb', 'sortcoll3', '{ "_id": 1, "a": { "b": { "c": 1 } } }');
SELECT * FROM documentdb_api.insert_one('sortdb', 'sortcoll3', '{ "_id": 2, "a": { "b": [ { "c": 2 } ] } }');
SELECT * FROM documentdb_api.insert_one('sortdb', 'sortcoll3', '{ "_id": 3, "a": [ { "b": [ { "c": 1 } ] } ] }');

-- combinations of those paths going missing
SELECT * FROM documentdb_api.insert_one('sortdb', 'sortcoll3', '{ "_id": 4, "a": { "b": { "d": 1 } } }');
SELECT * FROM documentdb_api.insert_one('sortdb', 'sortcoll3', '{ "_id": 5, "a": { "b": [ { "c": 2 }, {} ] } }');
SELECT * FROM documentdb_api.insert_one('sortdb', 'sortcoll3', '{ "_id": 6, "a": { "b": [ { "c": 2 }, 2 ] } }');
SELECT * FROM documentdb_api.insert_one('sortdb', 'sortcoll3', '{ "_id": 7, "a": { "b": [ 2 ] } }');
SELECT * FROM documentdb_api.insert_one('sortdb', 'sortcoll3', '{ "_id": 8, "a": { "b": [ {} ] } }');

SELECT * FROM documentdb_api.insert_one('sortdb', 'sortcoll3', '{ "_id": 9, "a": [ { "b": { "c": 3 } }, { "b": { "d": 1 } } ] }');
SELECT * FROM documentdb_api.insert_one('sortdb', 'sortcoll3', '{ "_id": 10, "a": [ { "b": { "c": 3 } }, { "b": 2 } ] }');
SELECT * FROM documentdb_api.insert_one('sortdb', 'sortcoll3', '{ "_id": 11, "a": [ { "b": { "c": 3 } }, {  } ] }');
SELECT * FROM documentdb_api.insert_one('sortdb', 'sortcoll3', '{ "_id": 12, "a": [ { "b": { "c": 3 } }, 1 ] }');

-- baseline
SELECT * FROM documentdb_api.insert_one('sortdb', 'sortcoll3', '{ "_id": 13, "a": { "b": { "c": 0 } } }');

SELECT * FROM documentdb_api_catalog.bson_aggregation_find('sortdb', '{ "find": "sortcoll3", "filter": {}, "sort": { "a.b.c": 1, "_id": 1 } }');

-- test exists
SELECT * FROM documentdb_api_catalog.bson_aggregation_find('sortdb', '{ "find": "sortcoll3", "filter": { "a.b.c": { "$exists": true } }, "sort": { "a.b.c": 1, "_id": 1 } }');
SELECT * FROM documentdb_api_catalog.bson_aggregation_find('sortdb', '{ "find": "sortcoll3", "filter": { "a.b.c": { "$exists": false } }, "sort": { "a.b.c": 1, "_id": 1 } }');

-- test null
SELECT * FROM documentdb_api_catalog.bson_aggregation_find('sortdb', '{ "find": "sortcoll3", "filter": { "a.b.c": null }, "sort": { "a.b.c": 1, "_id": 1 } }');
SELECT * FROM documentdb_api_catalog.bson_aggregation_find('sortdb', '{ "find": "sortcoll3", "filter": { "a.b.c": { "$ne": null } }, "sort": { "a.b.c": 1, "_id": 1 } }');

-- test again with composite index
SELECT documentdb_api_internal.create_indexes_non_concurrently('sortdb', '{ "createIndexes": "sortcoll4", "indexes": [ { "key": { "a.b.c": 1 }, "enableCompositeTerm": true, "name": "a.b.c_1" }] }', true);

SELECT COUNT(documentdb_api.insert_one('sortdb', 'sortcoll4', document)) FROM (SELECT document FROM documentdb_api.collection('sortdb', 'sortcoll3')) coll;
set documentdb.forceDisableSeqScan to on;

-- test exists
SELECT * FROM documentdb_api_catalog.bson_aggregation_find('sortdb', '{ "find": "sortcoll4", "filter": { "a.b.c": { "$exists": true } }, "sort": { "a.b.c": 1, "_id": 1 } }');
SELECT * FROM documentdb_api_catalog.bson_aggregation_find('sortdb', '{ "find": "sortcoll4", "filter": { "a.b.c": { "$exists": false } }, "sort": { "a.b.c": 1, "_id": 1 } }');

-- test null
SELECT * FROM documentdb_api_catalog.bson_aggregation_find('sortdb', '{ "find": "sortcoll4", "filter": { "a.b.c": null }, "sort": { "a.b.c": 1, "_id": 1 } }');
SELECT * FROM documentdb_api_catalog.bson_aggregation_find('sortdb', '{ "find": "sortcoll4", "filter": { "a.b.c": { "$ne": null } }, "sort": { "a.b.c": 1, "_id": 1 } }');
