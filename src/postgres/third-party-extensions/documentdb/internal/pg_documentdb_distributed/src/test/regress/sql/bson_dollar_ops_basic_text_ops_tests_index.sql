set search_path to documentdb_core,documentdb_api,documentdb_api_catalog,pg_catalog;
SET citus.next_shard_id TO 450000;
SET documentdb.next_collection_id TO 4500;
SET documentdb.next_collection_index_id TO 4500;

BEGIN;
set local enable_seqscan = off;
\i sql/bson_dollar_ops_basic_text_ops_tests_core.sql
ROLLBACK;