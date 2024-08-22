
set search_path to helio_core,helio_api,helio_api_catalog,pg_catalog;
SET citus.next_shard_id TO 510000;
SET helio_api.next_collection_id TO 5100;
SET helio_api.next_collection_index_id TO 5100;

BEGIN;
set local enable_seqscan to off;
set local enable_bitmapscan to off;
set local helio_api.forceUseIndexIfAvailable to on;
\i sql/bson_dollar_ops_basic_compare_tests_core.sql
ROLLBACK;