
SET search_path TO helio_core,helio_api,helio_api_catalog,helio_api_internal;
SET citus.next_shard_id TO 3500000;
SET helio_api.next_collection_id TO 3500;
SET helio_api.next_collection_index_id TO 3500;

SELECT 1 FROM helio_api.drop_collection('db', 'bsonorderby');
SELECT helio_api.create_collection('db', 'bsonorderby');
SELECT helio_distributed_test_helpers.drop_primary_key('db', 'bsonorderby');

\set prevEcho :ECHO
\set ECHO none
\o /dev/null
SELECT helio_api_internal.create_indexes_non_concurrently('db', helio_distributed_test_helpers.generate_create_index_arg('bsonorderby', 'index_2', '{"a.b": 1}'), true);
SELECT helio_api_internal.create_indexes_non_concurrently('db', helio_distributed_test_helpers.generate_create_index_arg('bsonorderby', 'index_3', '{"a.b.1": 1}'), true);
\o
\set ECHO :prevEcho

BEGIN;
set local enable_seqscan to off;
set local enable_bitmapscan to off;
set local helio_api.forceUseIndexIfAvailable to on;
\i sql/bson_query_modifier_orderby_tests_core.sql
ROLLBACK;
