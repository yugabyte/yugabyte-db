
set search_path to helio_core,helio_api,helio_api_catalog,pg_catalog;
SET citus.next_shard_id TO 510000;
SET helio_api.next_collection_id TO 5100;
SET helio_api.next_collection_index_id TO 5100;

SELECT helio_api.drop_collection('db', 'querydollartest') IS NOT NULL;
SELECT helio_api.create_collection('db', 'querydollartest');

SET client_min_messages=WARNING;

-- Create a wildcard index by using CREATE INDEX command instead of
-- using helio_api.create_indexes. This is because, we will use
-- that index to test whether we can use the index via query operators
-- other than "@@".
SELECT helio_api_internal.create_indexes_non_concurrently('db', helio_distributed_test_helpers.generate_create_index_arg('querydollartest', 'index_2', '{"$**": 1}'), TRUE);

BEGIN;
-- avoid sequential scan (likely to be preferred on small tables)
set local enable_seqscan to off;
set local helio_api.forceUseIndexIfAvailable to on;

\i sql/bson_dollar_ops_basic_compare_tests_explain_core.sql
ROLLBACK;
