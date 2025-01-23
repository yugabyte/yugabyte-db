
set search_path to documentdb_core,documentdb_api,documentdb_api_catalog,pg_catalog;
SET citus.next_shard_id TO 512000;
SET documentdb.next_collection_id TO 5120;
SET documentdb.next_collection_index_id TO 5120;

SET client_min_messages=WARNING;
SELECT documentdb_api.drop_collection('db', 'negation_tests') IS NULL;
SELECT documentdb_api.create_collection('db', 'negation_tests') IS NULL;
\o /dev/null
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "negation_tests", "indexes": [{ "key": { "$**": 1 }, "name": "myIdx1" }] }', TRUE);
\o

BEGIN;
set local enable_seqscan to off;
set local documentdb.forceUseIndexIfAvailable to on;
\i sql/bson_dollar_operators_negation_tests_core.sql
ROLLBACK;

SELECT documentdb_api.shard_collection('db', 'negation_tests', '{ "_id": "hashed" }', false);
BEGIN;
set local enable_seqscan to off;
set local documentdb.forceUseIndexIfAvailable to on;
\i sql/bson_dollar_operators_negation_tests_core.sql
ROLLBACK;