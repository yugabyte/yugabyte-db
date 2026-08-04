
set search_path to documentdb_core,documentdb_api,documentdb_api_catalog,pg_catalog;
SET citus.next_shard_id TO 510000;
SET documentdb.next_collection_id TO 5100;
SET documentdb.next_collection_index_id TO 5100;

BEGIN;
set local enable_seqscan to off;
set local documentdb.forceUseIndexIfAvailable to on;
\i sql/bson_dollar_ops_basic_compare_tests_core.sql
ROLLBACK;