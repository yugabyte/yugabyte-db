SET search_path TO documentdb_core,documentdb_api,documentdb_api_catalog,documentdb_api_internal;
SET citus.next_shard_id TO 1200000;
SET documentdb.next_collection_id TO 1200;
SET documentdb.next_collection_index_id TO 1200;

SELECT documentdb_api.drop_collection('db', 'queryoperator') IS NOT NULL;
SELECT documentdb_api.create_collection('db', 'queryoperator');

SELECT documentdb_api.drop_collection('db', 'queryoperatorIn') IS NOT NULL;
SELECT documentdb_api.create_collection('db', 'queryoperatorIn');

SELECT documentdb_api.drop_collection('db', 'nullfield') IS NOT NULL;
SELECT documentdb_api.create_collection('db', 'nullfield');

SELECT documentdb_api.drop_collection('db', 'singlepathindexexists') IS NOT NULL;
SELECT documentdb_api.create_collection('db', 'singlepathindexexists');

-- create an index
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "queryoperator", "indexes": [ { "key": { "a": 1, "c": 1 }, "name": "queryoperator_a_c" }] }', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "queryoperator", "indexes": [ { "key": { "a.b": 1, "_id": 1 }, "name": "queryoperator_ab" }] }', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "queryoperator", "indexes": [ { "key": { "b": 1, "a": 1 }, "name": "queryoperator_b_a" }] }', true);

SELECT documentdb_api_internal.create_indexes_non_concurrently('db', documentdb_distributed_test_helpers.generate_create_index_arg('nullfield', 'nullfield_wildcard', '{"$**": 1}'), true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "queryoperatorIn", "indexes": [ { "key": { "a": 1 }, "name": "queryoperatorin_a" }] }', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "queryoperatorIn", "indexes": [ { "key": { "a.b": 1 }, "name": "queryoperatorin_ab" }] }', true);

-- create single path index
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "singlepathindexexists", "indexes": [ { "key": { "a": 1 }, "name": "a_index" }] }', true);

-- show the indexes
\d documentdb_data.documents_1200

BEGIN;
SET LOCAL citus.enable_local_execution TO OFF;
set local documentdb.useLocalExecutionShardQueries to off;
-- avoid sequential scan (likely to be preferred on small tables)
set local enable_seqscan TO off;
set local documentdb.forceUseIndexIfAvailable to on;

\i sql/bson_query_operator_tests_explain_core.sql
COMMIT;
