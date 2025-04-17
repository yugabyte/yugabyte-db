
SET search_path TO documentdb_core,documentdb_api,documentdb_api_catalog,documentdb_api_internal;
SET citus.next_shard_id TO 260000;
SET documentdb.next_collection_id TO 2600;
SET documentdb.next_collection_index_id TO 2600;

SELECT documentdb_api.drop_collection('db', 'elemmatchtest') IS NOT NULL;
SELECT documentdb_api.create_collection('db', 'elemmatchtest') IS NOT NULL;

\set prevEcho :ECHO
\set ECHO none
\o /dev/null
-- Create a wildcard index by using CREATE INDEX command instead of
-- using documentdb_api.create_indexes. This is because, we will use
-- that index to test whether we can use the index via query operators
-- other than "@@".
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', documentdb_distributed_test_helpers.generate_create_index_arg('elemmatchtest', 'index_2', '{"$**": 1}'), true);
\o
\set ECHO :prevEcho


-- avoid plans that use the primary key index
SELECT documentdb_distributed_test_helpers.drop_primary_key('db','elemmatchtest');

BEGIN;
set local enable_seqscan TO off;
set local documentdb.forceUseIndexIfAvailable to on;
\i sql/bson_query_operator_elemmatch_tests_core.sql
ROLLBACK;

-- Invalid Arguments
SELECT object_id, document FROM documentdb_api.collection('db', 'elemmatchtest') where document @@ '{"a" : { "$elemMatch": [] }}';
