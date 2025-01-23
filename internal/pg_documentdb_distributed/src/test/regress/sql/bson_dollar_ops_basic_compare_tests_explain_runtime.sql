
set search_path to helio_core,helio_api,helio_api_catalog,pg_catalog;
SET citus.next_shard_id TO 52000;
SET helio_api.next_collection_id TO 5200;
SET helio_api.next_collection_index_id TO 5200;

SELECT helio_api.drop_collection('db', 'querydollartest');
SELECT helio_api.create_collection('db', 'querydollartest');

SET client_min_messages=WARNING;

-- avoid plans that use primary key index
\i sql/bson_dollar_ops_basic_compare_tests_explain_core.sql
