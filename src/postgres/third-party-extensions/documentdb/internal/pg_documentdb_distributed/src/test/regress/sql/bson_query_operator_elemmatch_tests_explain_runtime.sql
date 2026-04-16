
SET search_path TO documentdb_core,documentdb_api,documentdb_api_catalog,documentdb_api_internal;
SET citus.next_shard_id TO 720000;
SET documentdb.next_collection_id TO 7200;
SET documentdb.next_collection_index_id TO 7200;

SELECT documentdb_api.drop_collection('db', 'elemmatchtest') IS NOT NULL;
SELECT documentdb_api.create_collection('db', 'elemmatchtest') IS NOT NULL;


-- avoid plans that use the primary key index
SELECT documentdb_distributed_test_helpers.drop_primary_key('db','elemmatchtest');

BEGIN;
set local enable_seqscan TO on;
set local citus.enable_local_execution TO OFF;
set local documentdb.useLocalExecutionShardQueries to off;
\i sql/bson_query_operator_elemmatch_tests_explain_core.sql
ROLLBACK;

-- Shard the collection and run an explain analyze
SELECT documentdb_api.shard_collection('db','elemmatchtest', '{"_id":"hashed"}', false);

BEGIN;
set local enable_seqscan TO on;
SELECT documentdb_distributed_test_helpers.mask_plan_id_from_distributed_subplan($Q$
EXPLAIN (COSTS OFF) SELECT object_id, document FROM documentdb_api.collection('db', 'elemmatchtest') where document @@ '{"a.b" : { "$elemMatch": {"$gte" : 10, "$lte" : 15} }}';
$Q$);

SELECT documentdb_distributed_test_helpers.mask_plan_id_from_distributed_subplan($Q$
EXPLAIN (COSTS OFF) SELECT object_id, document FROM documentdb_api.collection('db', 'elemmatchtest') where document @@ '{"a" : { "$elemMatch": { "$or": [{ "b": {"$gte": 1} }, { "b": { "$lt": 2 }}] } } }';
$Q$);
ROLLBACK;
