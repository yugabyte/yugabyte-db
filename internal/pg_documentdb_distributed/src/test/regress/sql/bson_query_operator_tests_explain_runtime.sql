SET search_path TO helio_core,helio_api,helio_api_catalog,helio_api_internal;
SET citus.next_shard_id TO 1300000;
SET helio_api.next_collection_id TO 1300;
SET helio_api.next_collection_index_id TO 1300;

SELECT helio_api.drop_collection('db', 'queryoperator');
SELECT helio_api.create_collection('db', 'queryoperator');

SELECT helio_api.drop_collection('db', 'nullfield');
SELECT helio_api.create_collection('db', 'nullfield');

SELECT helio_api.drop_collection('db', 'singlepathindexexists');
SELECT helio_api.create_collection('db', 'singlepathindexexists');

\i sql/bson_query_operator_tests_explain_core.sql
