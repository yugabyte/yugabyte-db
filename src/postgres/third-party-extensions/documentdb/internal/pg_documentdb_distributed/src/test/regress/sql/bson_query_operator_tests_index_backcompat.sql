
SET search_path TO documentdb_core,documentdb_api,documentdb_api_catalog,documentdb_api_internal;
SET citus.next_shard_id TO 250000;
SET documentdb.next_collection_id TO 2500;
SET documentdb.next_collection_index_id TO 2500;

\set prevEcho :ECHO
\set ECHO none
\o /dev/null

CALL documentdb_api.drop_indexes('db', '{ "dropIndexes": "queryoperator", "index": [ "queryoperator_a_c", "queryoperator_ab", "queryoperator_b_a" ] }');
CALL documentdb_api.drop_indexes('db', '{ "dropIndexes": "nullfield", "index": "nullfield_wildcard" }');
CALL documentdb_api.drop_indexes('db', '{ "dropIndexes": "queryoperatorIn", "index": [ "queryoperatorin_a", "queryoperatorin_ab" ] }');
CALL documentdb_api.drop_indexes('db', '{ "dropIndexes": "singlepathindexexists", "index": "a_index" }');

-- recreate the indexes
BEGIN;
set local documentdb.enableGenerateNonExistsTerm to off;

-- create an index
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "queryoperator", "indexes": [ { "key": { "a": 1, "c": 1 }, "name": "queryoperator_a_c" }] }', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "queryoperator", "indexes": [ { "key": { "a.b": 1, "_id": 1 }, "name": "queryoperator_ab" }] }', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "queryoperator", "indexes": [ { "key": { "b": 1, "a": 1 }, "name": "queryoperator_b_a" }] }', true);

SELECT documentdb_api_internal.create_indexes_non_concurrently('db', documentdb_distributed_test_helpers.generate_create_index_arg('nullfield', 'nullfield_wildcard', '{"$**": 1}'), true);

SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "queryoperatorIn", "indexes": [ { "key": { "a": 1 }, "name": "queryoperatorin_a" }] }', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "queryoperatorIn", "indexes": [ { "key": { "a.b": 1 }, "name": "queryoperatorin_ab" }] }', true);

-- create single path index
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "singlepathindexexists", "indexes": [ { "key": { "a": 1 }, "name": "a_index" }] }', true);

COMMIT;
\o
\set ECHO :prevEcho

BEGIN;
set local documentdb.enableGenerateNonExistsTerm to off;
set local enable_seqscan TO off;
set local documentdb.forceUseIndexIfAvailable to on;
\i sql/bson_query_operator_tests_core.sql
