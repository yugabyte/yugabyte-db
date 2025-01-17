
set search_path to helio_core,helio_api,helio_api_catalog,pg_catalog;
SET citus.next_shard_id TO 512000;
SET helio_api.next_collection_id TO 5120;
SET helio_api.next_collection_index_id TO 5120;

SET client_min_messages=WARNING;
SELECT helio_api.drop_collection('db', 'negation_tests') IS NULL;
SELECT helio_api.create_collection('db', 'negation_tests') IS NULL;
\o /dev/null
SELECT helio_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "negation_tests", "indexes": [{ "key": { "$**": 1 }, "name": "myIdx1" }] }', TRUE);
\o

BEGIN;
set local enable_seqscan to off;
set local helio_api.forceUseIndexIfAvailable to on;
\i sql/bson_dollar_operators_negation_tests_core.sql
ROLLBACK;

SELECT helio_api.shard_collection('db', 'negation_tests', '{ "_id": "hashed" }', false);
BEGIN;
set local enable_seqscan to off;
set local helio_api.forceUseIndexIfAvailable to on;
\i sql/bson_dollar_operators_negation_tests_core.sql
ROLLBACK;