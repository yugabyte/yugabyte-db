
set search_path to documentdb_core,documentdb_api,documentdb_api_catalog,pg_catalog;
SET citus.next_shard_id TO 440000;
SET documentdb.next_collection_id TO 4400;
SET documentdb.next_collection_index_id TO 4400;

\set QUIET on
\set prevEcho :ECHO
\set ECHO none
\o /dev/null

SELECT documentdb_api.drop_collection('db', 'dollaralltests') IS NOT NULL;
SELECT documentdb_api.create_collection('db', 'dollaralltests') IS NOT NULL;

\o
\set ECHO :prevEcho

-- avoid plans that use the primary key index
SELECT documentdb_distributed_test_helpers.drop_primary_key('db','dollaralltests');

BEGIN;
set local enable_seqscan TO on;

\i sql/bson_dollar_ops_query_all_tests_core.sql

ROLLBACK;

\set QUIET off