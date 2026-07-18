
SET search_path TO documentdb_core,documentdb_api,documentdb_api_catalog,documentdb_api_internal;
SET citus.next_shard_id TO 3500000;
SET documentdb.next_collection_id TO 3500;
SET documentdb.next_collection_index_id TO 3500;

SELECT 1 FROM documentdb_api.drop_collection('db', 'bsonorderby');
SELECT documentdb_api.create_collection('db', 'bsonorderby');
SELECT documentdb_distributed_test_helpers.drop_primary_key('db', 'bsonorderby');

BEGIN;
set enable_seqscan to off;
\i sql/bson_query_modifier_orderby_tests_core.sql
ROLLBACK;
