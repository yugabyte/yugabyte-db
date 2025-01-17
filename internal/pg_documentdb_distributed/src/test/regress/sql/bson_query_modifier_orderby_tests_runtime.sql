
SET search_path TO helio_core,helio_api,helio_api_catalog,helio_api_internal;
SET citus.next_shard_id TO 3500000;
SET helio_api.next_collection_id TO 3500;
SET helio_api.next_collection_index_id TO 3500;

SELECT 1 FROM helio_api.drop_collection('db', 'bsonorderby');
SELECT helio_api.create_collection('db', 'bsonorderby');
SELECT helio_distributed_test_helpers.drop_primary_key('db', 'bsonorderby');

BEGIN;
set enable_seqscan to off;
\i sql/bson_query_modifier_orderby_tests_core.sql
ROLLBACK;
