
set search_path to helio_core,helio_api,helio_api_catalog,pg_catalog;
SET citus.next_shard_id TO 1400000;
SET helio_api.next_collection_id TO 1400;
SET helio_api.next_collection_index_id TO 1400;

SELECT helio_api.create_collection('db', 'queryregexopstest');
SELECT helio_api.create_collection('db', 'querytextopstest');

\i sql/bson_dollar_ops_basic_text_ops_tests_explain_core.sql
