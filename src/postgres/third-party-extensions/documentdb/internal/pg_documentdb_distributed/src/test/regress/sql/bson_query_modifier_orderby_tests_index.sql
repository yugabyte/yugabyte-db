
SET search_path TO documentdb_core,documentdb_api,documentdb_api_catalog,documentdb_api_internal;
SET citus.next_shard_id TO 3500000;
SET documentdb.next_collection_id TO 3500;
SET documentdb.next_collection_index_id TO 3500;

SELECT 1 FROM documentdb_api.drop_collection('db', 'bsonorderby');
SELECT documentdb_api.create_collection('db', 'bsonorderby');
SELECT documentdb_distributed_test_helpers.drop_primary_key('db', 'bsonorderby');

\set prevEcho :ECHO
\set ECHO none
\o /dev/null
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', documentdb_distributed_test_helpers.generate_create_index_arg('bsonorderby', 'index_2', '{"a.b": 1}'), true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', documentdb_distributed_test_helpers.generate_create_index_arg('bsonorderby', 'index_3', '{"a.b.1": 1}'), true);
\o
\set ECHO :prevEcho

BEGIN;
set local enable_seqscan to off;
set local enable_bitmapscan to off;
set local documentdb.forceUseIndexIfAvailable to on;
\i sql/bson_query_modifier_orderby_tests_core.sql
ROLLBACK;
