
SET search_path TO documentdb_core,documentdb_api,documentdb_api_catalog,documentdb_api_internal;
SET citus.next_shard_id TO 25000000;
SET documentdb.next_collection_id TO 250000;
SET documentdb.next_collection_index_id TO 250000;

\set prevEcho :ECHO
\set ECHO none
\o /dev/null
SELECT documentdb_api.drop_collection('db', 'queryoperator') IS NOT NULL;
SELECT documentdb_api.create_collection('db', 'queryoperator');

SELECT documentdb_api.drop_collection('db', 'nullfield') IS NOT NULL;
SELECT documentdb_api.create_collection('db', 'nullfield');

SELECT documentdb_api.drop_collection('db', 'singlepathindexexists') IS NOT NULL;
SELECT documentdb_api.create_collection('db', 'singlepathindexexists');

-- create a wildcard index
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', documentdb_distributed_test_helpers.generate_create_index_arg('queryoperator', 'queryoperator_wildcard', '{"$**": 1}'), true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', documentdb_distributed_test_helpers.generate_create_index_arg('nullfield', 'nullfield_wildcard', '{"$**": 1}'), true);

-- create single path index
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', documentdb_distributed_test_helpers.generate_create_index_arg('singlepathindexexists', 'a_index', '{"a": 1}'), true);
\o
\set ECHO :prevEcho

BEGIN;
set local documentdb.enableGenerateNonExistsTerm to off;
set local enable_seqscan TO off;
set local documentdb.forceUseIndexIfAvailable to on;
\i sql/bson_query_operator_tests_core.sql
