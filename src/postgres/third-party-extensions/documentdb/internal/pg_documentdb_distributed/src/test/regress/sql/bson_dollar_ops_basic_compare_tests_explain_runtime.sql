
set search_path to documentdb_core,documentdb_api,documentdb_api_catalog,pg_catalog;
SET citus.next_shard_id TO 52000;
SET documentdb.next_collection_id TO 5200;
SET documentdb.next_collection_index_id TO 5200;

SELECT documentdb_api.drop_collection('db', 'querydollartest');
SELECT documentdb_api.create_collection('db', 'querydollartest');

SET client_min_messages=WARNING;

-- avoid plans that use primary key index
\i sql/bson_dollar_ops_basic_compare_tests_explain_core.sql
