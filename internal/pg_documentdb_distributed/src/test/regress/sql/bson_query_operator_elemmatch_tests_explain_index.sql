
SET search_path TO helio_core,helio_api,helio_api_catalog,helio_api_internal;
SET citus.next_shard_id TO 720000;
SET helio_api.next_collection_id TO 7200;
SET helio_api.next_collection_index_id TO 7200;

SELECT helio_api.drop_collection('db', 'elemmatchtest') IS NOT NULL;
SELECT helio_api.create_collection('db', 'elemmatchtest') IS NOT NULL;

-- Create a wildcard index by using CREATE INDEX command instead of
-- using helio_api.create_indexes. This is because, we will use
-- that index to test whether we can use the index via query operators
-- other than "@@".
SELECT helio_api_internal.create_indexes_non_concurrently('db', helio_distributed_test_helpers.generate_create_index_arg('elemmatchtest', 'index_2', '{"$**": 1}'), true);

-- avoid plans that use the primary key index
SELECT helio_distributed_test_helpers.drop_primary_key('db','elemmatchtest');

BEGIN;
set local enable_seqscan TO off;
set local citus.enable_local_execution TO OFF;
set local helio_api.useLocalExecutionShardQueries to off;
set local helio_api.forceUseIndexIfAvailable to on;
\i sql/bson_query_operator_elemmatch_tests_explain_core.sql
ROLLBACK;

-- Shard the collection and run an explain analyze
SELECT helio_api.shard_collection('db','elemmatchtest', '{"_id":"hashed"}', false);

BEGIN;
set local enable_seqscan TO off;
set local helio_api.forceUseIndexIfAvailable to on;
SELECT helio_distributed_test_helpers.mask_plan_id_from_distributed_subplan($Q$
EXPLAIN (COSTS OFF) SELECT object_id, document FROM helio_api.collection('db', 'elemmatchtest') where document @@ '{"a.b" : { "$elemMatch": {"$gte" : 10, "$lte" : 15} }}';
$Q$);

SELECT helio_distributed_test_helpers.mask_plan_id_from_distributed_subplan($Q$
EXPLAIN (COSTS OFF) SELECT object_id, document FROM helio_api.collection('db', 'elemmatchtest') where document @@ '{"a" : { "$elemMatch": { "$or": [{ "b": {"$gte": 1} }, { "b": { "$lt": 2 }}] } } }';
$Q$);
ROLLBACK;
