
set search_path to helio_api_catalog;
SET citus.next_shard_id TO 3400000;
SET helio_api.next_collection_id TO 3400;
SET helio_api.next_collection_index_id TO 3400;

SELECT 1 FROM helio_api.drop_collection('db', 'bsoexplainnorderby');
SELECT helio_api.create_collection('db', 'bsoexplainnorderby');
SELECT helio_distributed_test_helpers.drop_primary_key('db', 'bsoexplainnorderby');
SELECT helio_api_internal.create_indexes_non_concurrently('db', helio_distributed_test_helpers.generate_create_index_arg('bsoexplainnorderby', 'index_2', '{"a.b": 1}'), true);
SELECT helio_api_internal.create_indexes_non_concurrently('db', helio_distributed_test_helpers.generate_create_index_arg('bsoexplainnorderby', 'index_3', '{"a.b.1": 1}'), true);

BEGIN;
set local enable_seqscan to off;
set local enable_bitmapscan to off;
set local helio_api.forceRumIndexScantoBitmapHeapScan TO OFF;
set local citus.enable_local_execution TO OFF;
set local helio_api.forceUseIndexIfAvailable to on;
\i sql/bson_query_modifier_orderby_tests_explain_core.sql
ROLLBACK;