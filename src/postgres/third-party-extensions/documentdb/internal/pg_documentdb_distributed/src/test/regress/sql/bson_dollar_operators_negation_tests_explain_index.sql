
set search_path to documentdb_core,documentdb_api,documentdb_api_catalog,pg_catalog;
SET citus.next_shard_id TO 514000;
SET documentdb.next_collection_id TO 5140;
SET documentdb.next_collection_index_id TO 5140;

SET client_min_messages=WARNING;
SELECT documentdb_api.drop_collection('db', 'negation_tests_explain');
SELECT documentdb_api.create_collection('db', 'negation_tests_explain');
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "negation_tests_explain", "indexes": [{ "key": { "$**": 1 }, "name": "myIdx1" }] }'::documentdb_core.bson, true);

BEGIN;
SET local search_path to documentdb_core,documentdb_api,documentdb_api_catalog,pg_catalog;
set local enable_seqscan to off;
set local documentdb.forceUseIndexIfAvailable to on;
\i sql/bson_dollar_operators_negation_tests_explain_core.sql
ROLLBACK;