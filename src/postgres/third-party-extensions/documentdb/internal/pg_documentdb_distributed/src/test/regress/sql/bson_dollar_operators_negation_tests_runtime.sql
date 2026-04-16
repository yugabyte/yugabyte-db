
set search_path to documentdb_core,documentdb_api,documentdb_api_catalog,pg_catalog;
SET citus.next_shard_id TO 512000;
SET documentdb.next_collection_id TO 5120;
SET documentdb.next_collection_index_id TO 5120;

SET client_min_messages=WARNING;
SELECT documentdb_api.drop_collection('db', 'simple_negation_tests') IS NULL;
SELECT documentdb_api.create_collection('db', 'simple_negation_tests') IS NULL;
\o /dev/null
\o

BEGIN;
set local enable_seqscan to off;
set local documentdb.forceUseIndexIfAvailable to on;
\i sql/bson_dollar_operators_negation_tests_core.sql
ROLLBACK;

SELECT documentdb_api.shard_collection('db', 'simple_negation_tests', '{ "_id": "hashed" }', false);
BEGIN;
set local enable_seqscan to off;
set local documentdb.forceUseIndexIfAvailable to on;
\i sql/bson_dollar_operators_negation_tests_core.sql
ROLLBACK;