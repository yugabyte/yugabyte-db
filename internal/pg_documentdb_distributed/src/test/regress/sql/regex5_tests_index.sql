CREATE SCHEMA regex5;
SET search_path TO helio_core,helio_api,helio_api_catalog,helio_api_internal,public,regex5;
SET citus.next_shard_id TO 90000;
SET helio_api.next_collection_id TO 900;
SET helio_api.next_collection_index_id TO 900;

SELECT create_collection('db','regex5');
\set prevEcho :ECHO
\set ECHO none
\o /dev/null
SELECT helio_api_internal.create_indexes_non_concurrently('db', helio_distributed_test_helpers.generate_create_index_arg('regex5', 'index_wc', '{"$**": 1}'), true);
\o
\set ECHO :prevEcho

BEGIN;
set local enable_seqscan TO OFF;
set local helio_api.forceUseIndexIfAvailable to on;
\i sql/regex5_tests_core.sql
ROLLBACK;

SELECT drop_collection('db','regex5');
DROP SCHEMA regex5 CASCADE;
