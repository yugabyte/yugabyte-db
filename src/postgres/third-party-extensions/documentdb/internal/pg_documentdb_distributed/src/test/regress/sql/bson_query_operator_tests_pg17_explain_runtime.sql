SET search_path TO documentdb_core,documentdb_api,documentdb_api_catalog,documentdb_api_internal;
SET citus.next_shard_id TO 1300000;
SET documentdb.next_collection_id TO 1300;
SET documentdb.next_collection_index_id TO 1300;

SELECT documentdb_api.drop_collection('db', 'queryoperator');
SELECT documentdb_api.create_collection('db', 'queryoperator');

SELECT documentdb_api.drop_collection('db', 'nullfield');
SELECT documentdb_api.create_collection('db', 'nullfield');

SELECT documentdb_api.drop_collection('db', 'singlepathindexexists');
SELECT documentdb_api.create_collection('db', 'singlepathindexexists');

\i sql/bson_query_operator_tests_explain_core.sql
