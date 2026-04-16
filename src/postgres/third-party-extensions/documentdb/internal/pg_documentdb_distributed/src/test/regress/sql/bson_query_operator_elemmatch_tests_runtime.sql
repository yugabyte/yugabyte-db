
SET search_path TO documentdb_core,documentdb_api,documentdb_api_catalog,documentdb_api_internal;
SET citus.next_shard_id TO 260000;
SET documentdb.next_collection_id TO 2600;
SET documentdb.next_collection_index_id TO 2600;

SELECT documentdb_api.drop_collection('db', 'elemmatchtest') IS NOT NULL;
SELECT documentdb_api.create_collection('db', 'elemmatchtest') IS NOT NULL;

-- avoid plans that use the primary key index
SELECT documentdb_distributed_test_helpers.drop_primary_key('db','elemmatchtest');

BEGIN;
set local enable_seqscan TO on;
\i sql/bson_query_operator_elemmatch_tests_core.sql
ROLLBACK;

-- Invalid Arguments
SELECT object_id, document FROM documentdb_api.collection('db', 'elemmatchtest') where document @@ '{"a" : { "$elemMatch": [] }}';
