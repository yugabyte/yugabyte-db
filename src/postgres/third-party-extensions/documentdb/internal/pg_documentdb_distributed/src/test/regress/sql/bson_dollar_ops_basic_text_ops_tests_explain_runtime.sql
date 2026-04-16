
set search_path to documentdb_core,documentdb_api,documentdb_api_catalog,pg_catalog;
SET citus.next_shard_id TO 1400000;
SET documentdb.next_collection_id TO 1400;
SET documentdb.next_collection_index_id TO 1400;

SELECT documentdb_api.create_collection('db', 'queryregexopstest');
SELECT documentdb_api.create_collection('db', 'querytextopstest');

\i sql/bson_dollar_ops_basic_text_ops_tests_explain_core.sql
