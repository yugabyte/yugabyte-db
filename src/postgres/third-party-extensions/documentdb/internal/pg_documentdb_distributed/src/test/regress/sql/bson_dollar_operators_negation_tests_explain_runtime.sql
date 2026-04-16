
set search_path to documentdb_core,documentdb_api,documentdb_api_catalog,pg_catalog;
SET citus.next_shard_id TO 513000;
SET documentdb.next_collection_id TO 5130;
SET documentdb.next_collection_index_id TO 5130;

SET client_min_messages=WARNING;
SELECT documentdb_api.drop_collection('db', 'negation_tests_explain');
SELECT documentdb_api.create_collection('db', 'negation_tests_explain');


BEGIN;
SET local search_path to documentdb_core,documentdb_api,documentdb_api_catalog,pg_catalog;
\o /dev/null
\o
set local enable_seqscan to off;
set local documentdb.forceUseIndexIfAvailable to on;
\i sql/bson_dollar_operators_negation_tests_explain_core.sql
ROLLBACK;