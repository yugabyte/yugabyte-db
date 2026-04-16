SET search_path TO documentdb_core,documentdb_api,documentdb_api_catalog,documentdb_api_internal;
SET citus.next_shard_id TO 1100000;
SET documentdb.next_collection_id TO 1100;
SET documentdb.next_collection_index_id TO 1100;

set documentdb.enableExtendedExplainPlans to on;
set documentdb.enableCompositeWildcardIndex to on;

SELECT documentdb_api.drop_collection('db', 'queryoperator') IS NOT NULL;
SELECT documentdb_api.create_collection('db', 'queryoperator');

SELECT documentdb_api.drop_collection('db', 'queryoperatorIn') IS NOT NULL;
SELECT documentdb_api.create_collection('db', 'queryoperatorIn');

SELECT documentdb_api.drop_collection('db', 'nullfield') IS NOT NULL;
SELECT documentdb_api.create_collection('db', 'nullfield');

SELECT documentdb_api.drop_collection('db', 'singlepathindexexists') IS NOT NULL;
SELECT documentdb_api.create_collection('db', 'singlepathindexexists');

-- create a composite index
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "queryoperator", "indexes": [ { "key": { "a.$**": 1 }, "enableCompositeTerm": true, "name": "queryoperator_a" }] }', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "queryoperator", "indexes": [ { "key": { "b.$**": 1 }, "enableCompositeTerm": true, "name": "queryoperator_b" }] }', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "queryoperator", "indexes": [ { "key": { "c.$**": 1 }, "enableCompositeTerm": true, "name": "queryoperator_c" }] }', true);

SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "nullfield", "indexes": [ { "key": { "$**": 1 }, "enableCompositeTerm": true, "name": "nullfield_root" }] }', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "queryoperatorIn", "indexes": [ { "key": { "$**": 1 }, "enableCompositeTerm": true, "name": "queryoperatorin_root" }] }', true);

-- create single path index
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "singlepathindexexists", "indexes": [ { "key": { "a": 1 }, "enableCompositeTerm": true, "name": "a_index" }] }', true);

-- show the indexes
\d documentdb_data.documents_1100

BEGIN;
SET LOCAL citus.enable_local_execution TO OFF;
set local documentdb.useLocalExecutionShardQueries to off;
set local documentdb.enableCompositeWildcardIndex to on;
-- avoid sequential scan (likely to be preferred on small tables)
set local enable_seqscan TO off;
set local documentdb.forceUseIndexIfAvailable to on;
set local documentdb.enableExtendedExplainPlans to on;
set local documentdb.forceRumIndexScanToBitmapHeapScan to off;

\i sql/bson_query_operator_tests_explain_core.sql
COMMIT;
