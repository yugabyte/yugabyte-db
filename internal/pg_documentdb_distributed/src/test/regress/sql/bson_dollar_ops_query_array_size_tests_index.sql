
set search_path to helio_core,helio_api,helio_api_catalog,pg_catalog;
SET citus.next_shard_id TO 430000;
SET helio_api.next_collection_id TO 4300;
SET helio_api.next_collection_index_id TO 4300;

\set QUIET on
\set prevEcho :ECHO
\set ECHO none

SELECT helio_api.drop_collection('db', 'arraysize') IS NOT NULL;
SELECT helio_api.create_collection('db', 'arraysize') IS NOT NULL;
 
\o /dev/null
SELECT helio_api_internal.create_indexes_non_concurrently('db', helio_distributed_test_helpers.generate_create_index_arg('arraysize', 'testgin_arraysize', '{"$**": 1}'), TRUE);
\o
\set ECHO :prevEcho

-- avoid plans that use the primary key index
SELECT helio_distributed_test_helpers.drop_primary_key('db','arraysize');

BEGIN;
set local enable_seqscan TO off;
set local helio_api.forceUseIndexIfAvailable to on;

\i sql/bson_dollar_ops_query_array_size_tests_core.sql

ROLLBACK;

\set QUIET off