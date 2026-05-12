
SET search_path TO documentdb_core,documentdb_api,documentdb_api_catalog,documentdb_api_internal;

SET citus.next_shard_id TO 5600000;
SET documentdb.next_collection_id TO 56000;
SET documentdb.next_collection_index_id TO 56000;

-- test term generation
SELECT documentdb_distributed_test_helpers.gin_bson_get_single_path_generated_terms('{"_id": 6, "a" : { "b": 1, "c": 2 }, "d": 1 }', '', true, enableReducedWildcardTerms => true);
SELECT documentdb_distributed_test_helpers.gin_bson_get_single_path_generated_terms('{"_id": 6, "a" : { "b": [ 1, 2 ], "c": 2 }, "d": 1 }', '', true, enableReducedWildcardTerms => true);
SELECT documentdb_distributed_test_helpers.gin_bson_get_single_path_generated_terms('{"_id": 6, "a" : [ { "b": 1, "c": 2 }, { "d": 1 } ] }', '', true, enableReducedWildcardTerms => true);
SELECT documentdb_distributed_test_helpers.gin_bson_get_single_path_generated_terms('{"_id": 6, "a" : [ { "b": [ 1, 2 ], "c": 2 }, { "d": 1 }, 3 ] }', '', true, enableReducedWildcardTerms => true);
SELECT documentdb_distributed_test_helpers.gin_bson_get_single_path_generated_terms('{"_id": 6, "a" : [ { "b": [ 1, 2 ], "c": 2 }, [ { "d": 1 }, 5 ], 3 ] }', '', true, enableReducedWildcardTerms => true);

SELECT documentdb_distributed_test_helpers.gin_bson_get_single_path_generated_terms('{"_id": 6, "a" : [ [ 1, 2 ] ] }', '', true, enableReducedWildcardTerms => true);
SELECT documentdb_distributed_test_helpers.gin_bson_get_single_path_generated_terms('{"_id": 6, "a" : [ [ { "b": 1 } ] ] }', '', true, enableReducedWildcardTerms => true);

-- objects with fields with numeric paths don't get skipped
SELECT documentdb_distributed_test_helpers.gin_bson_get_single_path_generated_terms('{"_id": 6, "0" : { "b": { "1": { "f": true }, "2": 2 }, "c": 2 }, "d": 1 }', '', true, enableReducedWildcardTerms => true);

-- array paths *before* matches are fully considered
SELECT documentdb_distributed_test_helpers.gin_bson_get_single_path_generated_terms('{"_id": 6, "a" : [ { "b": [ { "c": 1 }]}] }', 'a.0.b', true, enableReducedWildcardTerms => true);

-- create a root wildcard index with reduced terms
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "wildcardreducedterms", "indexes": [ { "key": { "$**": 1 }, "name": "my_idx", "enableReducedWildcardTerm": true }] }');

\d documentdb_data.documents_56000

-- now insert some convoluted document
SELECT documentdb_api.insert_one('db','wildcardreducedterms', '{"_id": 15, "c" : [ { "d": [[-1, 1, 2]] }, { "d": [[0, 1, 2]] }, { "d": [[0, 1, 7]] }] }', NULL);

-- these return the above document
SELECT object_id, document FROM documentdb_api.collection('db','wildcardreducedterms') WHERE document @@ '{ "c" : { "$eq" : [ { "d": [[-1, 1, 2]] }, { "d": [[0, 1, 2]] }, { "d": [[0, 1, 7]] }] }}';
SELECT object_id, document FROM documentdb_api.collection('db','wildcardreducedterms') WHERE document @@ '{ "c" : { "$eq" : { "d" : [ [ -1, 1, 2 ] ] } }}';
SELECT object_id, document FROM documentdb_api.collection('db','wildcardreducedterms') WHERE document @@ '{ "c.0" : { "$eq" : { "d" : [ [ -1, 1, 2 ] ] } }}';
SELECT object_id, document FROM documentdb_api.collection('db','wildcardreducedterms') WHERE document @@ '{ "c.0.d" : { "$eq" : [ [ -1, 1, 2 ] ] }}';
SELECT object_id, document FROM documentdb_api.collection('db','wildcardreducedterms') WHERE document @@ '{ "c.0.d" : { "$eq" : [ -1, 1, 2 ] }}';
SELECT object_id, document FROM documentdb_api.collection('db','wildcardreducedterms') WHERE document @@ '{ "c.0.d.0" : { "$eq" : [ -1, 1, 2 ] }}';
SELECT object_id, document FROM documentdb_api.collection('db','wildcardreducedterms') WHERE document @@ '{ "c.0.d.0.0" : { "$eq" : -1 }}';
SELECT object_id, document FROM documentdb_api.collection('db','wildcardreducedterms') WHERE document @@ '{ "c.0.d.0.1" : { "$eq" : 1 }}';
SELECT object_id, document FROM documentdb_api.collection('db','wildcardreducedterms') WHERE document @@ '{ "c.0.d.0.2" : { "$eq" : 2 }}';
SELECT object_id, document FROM documentdb_api.collection('db','wildcardreducedterms') WHERE document @@ '{ "c.d" : { "$eq" : [ [ -1, 1, 2 ] ] }}';
SELECT object_id, document FROM documentdb_api.collection('db','wildcardreducedterms') WHERE document @@ '{ "c.d" : { "$eq" : [ -1, 1, 2 ] }}';
SELECT object_id, document FROM documentdb_api.collection('db','wildcardreducedterms') WHERE document @@ '{ "c.d.0" : { "$eq" : [ -1, 1, 2 ] }}';
SELECT object_id, document FROM documentdb_api.collection('db','wildcardreducedterms') WHERE document @@ '{ "c.d.0.0" : { "$eq" : -1 }}';
SELECT object_id, document FROM documentdb_api.collection('db','wildcardreducedterms') WHERE document @@ '{ "c.d.0.1" : { "$eq" : 1 }}';
SELECT object_id, document FROM documentdb_api.collection('db','wildcardreducedterms') WHERE document @@ '{ "c.d.0.2" : { "$eq" : 2 }}';

-- these queries do not return the above document
SELECT object_id, document FROM documentdb_api.collection('db','wildcardreducedterms') WHERE document @@ '{ "c.0.d" : { "$eq" : -1 }}';
SELECT object_id, document FROM documentdb_api.collection('db','wildcardreducedterms') WHERE document @@ '{ "c.0.d" : { "$eq" : 1 }}';
SELECT object_id, document FROM documentdb_api.collection('db','wildcardreducedterms') WHERE document @@ '{ "c.0.d" : { "$eq" : 2 }}';
SELECT object_id, document FROM documentdb_api.collection('db','wildcardreducedterms') WHERE document @@ '{ "c.0.d.0" : { "$eq" : -1 }}';
SELECT object_id, document FROM documentdb_api.collection('db','wildcardreducedterms') WHERE document @@ '{ "c.0.d.0" : { "$eq" : 1 }}';
SELECT object_id, document FROM documentdb_api.collection('db','wildcardreducedterms') WHERE document @@ '{ "c.0.d.0" : { "$eq" : 2 }}';
SELECT object_id, document FROM documentdb_api.collection('db','wildcardreducedterms') WHERE document @@ '{ "c.0.d.1" : { "$eq" : 1 }}';
SELECT object_id, document FROM documentdb_api.collection('db','wildcardreducedterms') WHERE document @@ '{ "c.0.d.2" : { "$eq" : 2 }}';
SELECT object_id, document FROM documentdb_api.collection('db','wildcardreducedterms') WHERE document @@ '{ "c.0.d.0.2" : { "$eq" : [ 2 ] }}';
SELECT object_id, document FROM documentdb_api.collection('db','wildcardreducedterms') WHERE document @@ '{ "c.d" : { "$eq" : -1 }}';
SELECT object_id, document FROM documentdb_api.collection('db','wildcardreducedterms') WHERE document @@ '{ "c.d" : { "$eq" : 1 }}';
SELECT object_id, document FROM documentdb_api.collection('db','wildcardreducedterms') WHERE document @@ '{ "c.d" : { "$eq" : 2 }}';
SELECT object_id, document FROM documentdb_api.collection('db','wildcardreducedterms') WHERE document @@ '{ "c.d.0" : { "$eq" : -1 }}';
SELECT object_id, document FROM documentdb_api.collection('db','wildcardreducedterms') WHERE document @@ '{ "c.d.0" : { "$eq" : 1 }}';
SELECT object_id, document FROM documentdb_api.collection('db','wildcardreducedterms') WHERE document @@ '{ "c.d.0" : { "$eq" : 2 }}';
SELECT object_id, document FROM documentdb_api.collection('db','wildcardreducedterms') WHERE document @@ '{ "c.d.1" : { "$eq" : 1 }}';
SELECT object_id, document FROM documentdb_api.collection('db','wildcardreducedterms') WHERE document @@ '{ "c.d.2" : { "$eq" : 2 }}';


-- EXPLAIN to show the plan with the paths
EXPLAIN (COSTS OFF) SELECT object_id, document FROM documentdb_api.collection('db','wildcardreducedterms') WHERE document @@ '{ "c" : { "$eq" : [ { "d": [[-1, 1, 2]] }, { "d": [[0, 1, 2]] }, { "d": [[0, 1, 7]] }] }}';

-- having numbers in fields is okay as long as it also has letters
EXPLAIN (COSTS OFF) SELECT object_id, document FROM documentdb_api.collection('db','wildcardreducedterms') WHERE document @@ '{ "c01" : { "$eq" : [ { "d": [[-1, 1, 2]] }, { "d": [[0, 1, 2]] }, { "d": [[0, 1, 7]] }] }}';
EXPLAIN (COSTS OFF) SELECT object_id, document FROM documentdb_api.collection('db','wildcardreducedterms') WHERE document @@ '{ "c.d0af.e" : { "$eq" : [ { "d": [[-1, 1, 2]] }, { "d": [[0, 1, 2]] }, { "d": [[0, 1, 7]] }] }}';

-- these won't be pushed
EXPLAIN (COSTS OFF) SELECT object_id, document FROM documentdb_api.collection('db','wildcardreducedterms') WHERE document @@ '{ "c.0.d" : { "$eq" : [ [ -1, 1, 2 ] ] }}';
EXPLAIN (COSTS OFF) SELECT object_id, document FROM documentdb_api.collection('db','wildcardreducedterms') WHERE document @@ '{ "c.0" : { "$eq" : { "d" : [ [ -1, 1, 2 ] ] } }}';