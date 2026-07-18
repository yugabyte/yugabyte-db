
SET search_path TO documentdb_core,documentdb_api,documentdb_api_catalog,documentdb_api_internal;
SET citus.next_shard_id TO 11000;
SET documentdb.next_collection_id TO 1100;
SET documentdb.next_collection_index_id TO 1100;

BEGIN;
set local enable_seqscan TO off;
set local documentdb.forceUseIndexIfAvailable to on;
set local documentdb.enableCompositeWildcardIndex to on;
\i sql/bson_query_operator_tests_core.sql
