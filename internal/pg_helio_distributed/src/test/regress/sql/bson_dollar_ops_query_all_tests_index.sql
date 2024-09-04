
set search_path to helio_core,helio_api,helio_api_catalog,pg_catalog;
SET citus.next_shard_id TO 440000;
SET helio_api.next_collection_id TO 4400;
SET helio_api.next_collection_index_id TO 4400;

\set QUIET on
\set prevEcho :ECHO
\set ECHO none
\o /dev/null

SELECT helio_api.drop_collection('db', 'dollaralltests') IS NOT NULL;
SELECT helio_api.create_collection('db', 'dollaralltests') IS NOT NULL;

-- Create a wildcard index by using CREATE INDEX command instead of
-- using helio_api_internal.create_indexes. This is because, we will use
-- that index to test whether we can use the index via query operators
-- other than "@@".
SELECT helio_api_internal.create_indexes_non_concurrently('db', helio_distributed_test_helpers.generate_create_index_arg('dollaralltests', 'index_2', '{"$**": 1}'), TRUE);

\o
\set ECHO :prevEcho

-- avoid plans that use the primary key index
SELECT helio_distributed_test_helpers.drop_primary_key('db','dollaralltests');

BEGIN;
set local enable_seqscan TO off;
set local helio_api.forceUseIndexIfAvailable to on;
\i sql/bson_dollar_ops_query_all_tests_core.sql

ROLLBACK;

\set QUIET off