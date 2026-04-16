
set search_path to documentdb_core,documentdb_api,documentdb_api_catalog,pg_catalog;
SET citus.next_shard_id TO 510000;
SET documentdb.next_collection_id TO 5100;
SET documentdb.next_collection_index_id TO 5100;

SELECT documentdb_api.drop_collection('db', 'querydollartest') IS NOT NULL;
SELECT documentdb_api.create_collection('db', 'querydollartest');

SET client_min_messages=WARNING;

-- Create a wildcard index by using CREATE INDEX command instead of
-- using documentdb_api.create_indexes. This is because, we will use
-- that index to test whether we can use the index via query operators
-- other than "@@".
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', documentdb_distributed_test_helpers.generate_create_index_arg('querydollartest', 'index_2', '{"$**": 1}'), TRUE);

BEGIN;
-- avoid sequential scan (likely to be preferred on small tables)
set local enable_seqscan to off;
set local documentdb.forceUseIndexIfAvailable to on;

\i sql/bson_dollar_ops_basic_compare_tests_explain_core.sql
ROLLBACK;
