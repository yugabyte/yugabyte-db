CREATE SCHEMA regex5;
SET search_path TO documentdb_core,documentdb_api,documentdb_api_catalog,documentdb_api_internal,public,regex5;
SET citus.next_shard_id TO 90000;
SET documentdb.next_collection_id TO 900;
SET documentdb.next_collection_index_id TO 900;

SELECT create_collection('db','regex5');
\set prevEcho :ECHO
\set ECHO none
\o /dev/null
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', documentdb_distributed_test_helpers.generate_create_index_arg('regex5', 'index_wc', '{"$**": 1}'), true);
\o
\set ECHO :prevEcho

BEGIN;
set local enable_seqscan TO OFF;
set local documentdb.forceUseIndexIfAvailable to on;
\i sql/regex5_tests_core.sql
ROLLBACK;

SELECT drop_collection('db','regex5');
DROP SCHEMA regex5 CASCADE;
