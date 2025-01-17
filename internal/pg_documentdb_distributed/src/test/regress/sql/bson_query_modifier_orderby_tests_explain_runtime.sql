
set search_path to helio_api_catalog;
SET citus.next_shard_id TO 3400000;
SET helio_api.next_collection_id TO 3400;
SET helio_api.next_collection_index_id TO 3400;

SELECT 1 FROM helio_api.drop_collection('db', 'bsoexplainnorderby');
SELECT helio_api.create_collection('db', 'bsoexplainnorderby');

BEGIN;
set enable_seqscan to on;
set local citus.enable_local_execution TO OFF;
\i sql/bson_query_modifier_orderby_tests_explain_core.sql
ROLLBACK;