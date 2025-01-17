
set search_path to helio_core,helio_api,helio_api_catalog,pg_catalog;
SET citus.next_shard_id TO 1500000;
SET helio_api.next_collection_id TO 1500;
SET helio_api.next_collection_index_id TO 1500;

SELECT helio_api.drop_collection('db', 'queryregexopstest') IS NOT NULL;
SELECT helio_api.create_collection('db', 'queryregexopstest');

-- Create a wildcard index by using CREATE INDEX command instead of
-- using helio_api_internal.create_indexes_non_concurrently. This is because, we will use
-- that index to test whether we can use the index via query operators
-- other than "@@".
SELECT helio_api_internal.create_indexes_non_concurrently('db', helio_distributed_test_helpers.generate_create_index_arg('queryregexopstest', 'index_2', '{"$**": 1}'), TRUE);

-- avoid plans that use the primary key index
SELECT helio_distributed_test_helpers.drop_primary_key('db','queryregexopstest');

SELECT helio_api.drop_collection('db', 'querytextopstest') IS NOT NULL;
SELECT helio_api.create_collection('db', 'querytextopstest');

-- avoid plans that use the primary key index
SELECT helio_distributed_test_helpers.drop_primary_key('db','querytextopstest');

BEGIN;
-- avoid sequential scan (likely to be preferred on small tables)
set local enable_seqscan = off;
set local helio_api.forceUseIndexIfAvailable to on;
\i sql/bson_dollar_ops_basic_text_ops_tests_explain_core.sql
END;
