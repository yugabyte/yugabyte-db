
set search_path to helio_core,helio_api,helio_api_catalog,pg_catalog;
SET citus.next_shard_id TO 440000;
SET helio_api.next_collection_id TO 4400;
SET helio_api.next_collection_index_id TO 4400;

\set QUIET on
\set prevEcho :ECHO
\set ECHO none
\o /dev/null

SELECT helio_api.drop_collection('db', 'dollaralltests') IS NOT NULL;
SELECT helio_api.create_collection('db', 'dollaralltests') IS NOT NULL;

\o
\set ECHO :prevEcho

-- avoid plans that use the primary key index
SELECT helio_distributed_test_helpers.drop_primary_key('db','dollaralltests');

BEGIN;
set local enable_seqscan TO on;

\i sql/bson_dollar_ops_query_all_tests_core.sql

ROLLBACK;

\set QUIET off