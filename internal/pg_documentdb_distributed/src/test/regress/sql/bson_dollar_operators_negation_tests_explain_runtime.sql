
set search_path to helio_core,helio_api,helio_api_catalog,pg_catalog;
SET citus.next_shard_id TO 513000;
SET helio_api.next_collection_id TO 5130;
SET helio_api.next_collection_index_id TO 5130;

SET client_min_messages=WARNING;
SELECT helio_api.drop_collection('db', 'negation_tests_explain');
SELECT helio_api.create_collection('db', 'negation_tests_explain');


BEGIN;
SET local search_path to helio_core,helio_api,helio_api_catalog,pg_catalog;
\o /dev/null
\o
set local enable_seqscan to off;
set local helio_api.forceUseIndexIfAvailable to on;
\i sql/bson_dollar_operators_negation_tests_explain_core.sql
ROLLBACK;