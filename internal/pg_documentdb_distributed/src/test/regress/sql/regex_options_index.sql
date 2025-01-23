CREATE SCHEMA regex_options;
SET search_path TO helio_core,helio_api,helio_api_catalog,helio_api_internal,public,regex_options;
SET citus.next_shard_id TO 80000;
SET helio_api.next_collection_id TO 800;
SET helio_api.next_collection_index_id TO 800;

SELECT create_collection('db','regex_options');
\set prevEcho :ECHO
\set ECHO none
\o /dev/null
SELECT helio_api_internal.create_indexes_non_concurrently('db', helio_distributed_test_helpers.generate_create_index_arg('regex_options', 'index_wc', '{"$**": 1}'), true);
\o
\set ECHO :prevEcho
BEGIN;
set local enable_seqscan TO OFF;
set local helio_api.forceUseIndexIfAvailable to on;
\i sql/regex_options_core.sql
ROLLBACK;

SELECT drop_collection('db','regex_options');
DROP SCHEMA regex_options CASCADE;
