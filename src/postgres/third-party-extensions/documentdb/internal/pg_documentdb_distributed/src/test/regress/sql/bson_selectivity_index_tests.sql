SET search_path TO documentdb_core,documentdb_api,documentdb_api_catalog,documentdb_api_internal;

SET citus.next_shard_id TO 1014000;
SET documentdb.next_collection_id TO 10140;
SET documentdb.next_collection_index_id TO 10140;

-- Test Scenarios that cover the Bitmap Or selectivity - more selecitivity tests will be added here
with r1 AS (SELECT FORMAT('{ "a": 1, "b": 2, "c": %s, "d": %s }', ((s % 2) + 1), s)::json as textVal from generate_series(1, 25000) s),
r2 AS (SELECT json_build_object('insert', 'selectivity_index_tests', 'documents', json_agg(r1.textVal)) AS jsonObj FROM r1)
SELECT documentdb_api.insert('db', (r2.jsonObj)::text::bson) FROM r2;

with r1 AS (SELECT FORMAT('{ "a": 1, "b": 3, "c": %s, "d": %s }', ((s % 2) + 1), s)::json as textVal from generate_series(25001, 50000) s),
r2 AS (SELECT json_build_object('insert', 'selectivity_index_tests', 'documents', json_agg(r1.textVal)) AS jsonObj FROM r1)
SELECT documentdb_api.insert('db', (r2.jsonObj)::text::bson) FROM r2;

with r1 AS (SELECT FORMAT('{ "a": 1, "b": 3, "c": %s, "d": %s }', ((s % 2) + 1), s)::json as textVal from generate_series(50001, 75000) s),
r2 AS (SELECT json_build_object('insert', 'selectivity_index_tests', 'documents', json_agg(r1.textVal)) AS jsonObj FROM r1)
SELECT documentdb_api.insert('db', (r2.jsonObj)::text::bson) FROM r2;


SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "selectivity_index_tests", "indexes": [ { "key": { "a": 1, "b": 1, "c": 1, "d": 1 }, "name": "idx_1" } ]}', true);

ANALYZE documentdb_data.documents_10140;

EXPLAIN (COSTS OFF) SELECT document FROM documentdb_api.collection('db', 'selectivity_index_tests') WHERE document @@ '{ "a": 1, "b": { "$in": [ 2, 3, 4, 5 ] }, "a": { "$in": [ 1, 5, 6, 7 ] }, "$or": [ { "c": 3, "d": { "$gt": 500 } }, { "c": { "$gt": 4 } }] }';

BEGIN;
set local documentdb.enableNewSelectivityMode to on;
EXPLAIN (COSTS OFF) SELECT document FROM documentdb_api.collection('db', 'selectivity_index_tests') WHERE document @@ '{ "a": 1, "b": { "$in": [ 2, 3, 4, 5 ] }, "a": { "$in": [ 1, 5, 6, 7 ] }, "$or": [ { "c": 3, "d": { "$gt": 500 } }, { "c": { "$gt": 4 } }] }';
ROLLBACK;

-- test bitmap or pushdown
SELECT COUNT(documentdb_api.insert_one('seldb', 'bimap_or_selectivity', '{ "plane": "A380", "noticeDate": "2008-01-01", "stoppingDate": "2025-01-01", "productionDate": "2005-01-01" }')) FROM generate_series(1, 40);
SELECT COUNT(documentdb_api.insert_one('seldb', 'bimap_or_selectivity', '{ "plane": "A350", "noticeDate": "2008-01-01", "stoppingDate": "2025-01-01", "productionDate": "2005-01-01" }')) FROM generate_series(1, 50);
SELECT COUNT(documentdb_api.insert_one('seldb', 'bimap_or_selectivity', '{ "plane": "A330", "noticeDate": "2008-01-01", "stoppingDate": "2025-01-01", "productionDate": "2005-01-01" }')) FROM generate_series(1, 30);
SELECT COUNT(documentdb_api.insert_one('seldb', 'bimap_or_selectivity', '{ "plane": "A340", "noticeDate": "2008-01-01", "stoppingDate": "2025-01-01", "productionDate": "2005-01-01" }')) FROM generate_series(1, 30);

SELECT documentdb_api_internal.create_indexes_non_concurrently('seldb', '{ "createIndexes": "bimap_or_selectivity", "indexes": [ { "key": { "productionDate": 1, "plane": 1 }, "name": "plane_productionDate_index" } ] }', TRUE);
SELECT documentdb_api_internal.create_indexes_non_concurrently('seldb', '{ "createIndexes": "bimap_or_selectivity", "indexes": [ { "key": { "stoppingDate": 1, "plane": 1 }, "name": "plane_stoppingDate_index" } ] }', TRUE);
SELECT documentdb_api_internal.create_indexes_non_concurrently('seldb', '{ "createIndexes": "bimap_or_selectivity", "indexes": [ { "key": { "noticeDate": 1, "plane": 1 }, "name": "plane_noticeDate_index" } ] }', TRUE);

ANALYZE documentdb_data.documents_10142;

set documentdb.enableNewSelectivityMode to off;
EXPLAIN (COSTS OFF, ANALYZE ON, SUMMARY OFF, TIMING OFF) SELECT document FROM bson_aggregation_find('seldb', '{ "find" : "bimap_or_selectivity", "filter" : { "plane" : { "$regularExpression" : { "pattern" : "A.+", "options" : "i" } }, "$and" : [ { "$or" : [ { "productionDate" : { "$exists" : false } }, { "stoppingDate" : { "$exists" : false } }, { "noticeDate" : { "$exists" : false } } ] } ] }, "limit" : { "$numberInt" : "10" } }');

set documentdb.enableNewSelectivityMode to on;
EXPLAIN (COSTS OFF, ANALYZE ON, SUMMARY OFF, TIMING OFF) SELECT document FROM bson_aggregation_find('seldb', '{ "find" : "bimap_or_selectivity", "filter" : { "plane" : { "$regularExpression" : { "pattern" : "A.+", "options" : "i" } }, "$and" : [ { "$or" : [ { "productionDate" : { "$exists" : false } }, { "stoppingDate" : { "$exists" : false } }, { "noticeDate" : { "$exists" : false } } ] } ] }, "limit" : { "$numberInt" : "10" } }');

set documentdb.enableNewSelectivityMode to off;
EXPLAIN (COSTS OFF, ANALYZE ON, SUMMARY OFF, TIMING OFF) SELECT document FROM bson_aggregation_find('seldb', '{ "find" : "bimap_or_selectivity", "filter" : { "plane" : { "$regularExpression" : { "pattern" : "A.+", "options" : "i" } }, "$and" : [ { "$or" : [ { "productionDate" : { "$exists" : false } }, { "stoppingDate" : { "$exists" : false } }, { "noticeDate" : { "$exists" : false } } ] } ] }, "limit" : { "$numberInt" : "10" } }');
