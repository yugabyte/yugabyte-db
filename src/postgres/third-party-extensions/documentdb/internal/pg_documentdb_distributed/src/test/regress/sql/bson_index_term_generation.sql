SET search_path TO documentdb_core,documentdb_api,documentdb_api_catalog,documentdb_api_internal;
SET citus.next_shard_id TO 370000;
SET documentdb.next_collection_id TO 3700;
SET documentdb.next_collection_index_id TO 3700;

-- now get terms for a simple document ( { a: { b : 1 }})
-- test root, 'a', 'a.b', 'a.b.1' with wildcard/non-wildcard
SELECT documentdb_distributed_test_helpers.gin_bson_get_single_path_generated_terms('{"_id": 6, "a" : { "b": 1, "c": 2 }, "d": 1 }', '', true);
SELECT documentdb_distributed_test_helpers.gin_bson_get_single_path_generated_terms('{"_id": 6, "a" : { "b": 1, "c": 2 }, "d": 1 }', '', false);
SELECT documentdb_distributed_test_helpers.gin_bson_get_single_path_generated_terms('{"_id": 6, "a" : { "b": 1, "c": 2 }, "d": 1 }', 'a', true);
SELECT documentdb_distributed_test_helpers.gin_bson_get_single_path_generated_terms('{"_id": 6, "a" : { "b": 1, "c": 2 }, "d": 1 }', 'a', false);
SELECT documentdb_distributed_test_helpers.gin_bson_get_single_path_generated_terms('{"_id": 6, "a" : { "b": 1, "c": 2 }, "d": 1 }', 'a.b', true);
SELECT documentdb_distributed_test_helpers.gin_bson_get_single_path_generated_terms('{"_id": 6, "a" : { "b": 1, "c": 2 }, "d": 1 }', 'a.b', false);
SELECT documentdb_distributed_test_helpers.gin_bson_get_single_path_generated_terms('{"_id": 6, "a" : { "b": 1, "c": 2 }, "d": 1 }', 'a.b.1', true);
SELECT documentdb_distributed_test_helpers.gin_bson_get_single_path_generated_terms('{"_id": 6, "a" : { "b": 1, "c": 2 }, "d": 1 }', 'a.b.1', false);


SELECT documentdb_distributed_test_helpers.gin_bson_get_single_path_generated_terms('{"_id": 8, "a" : [0, 1, 2]}', '', true);
SELECT documentdb_distributed_test_helpers.gin_bson_get_single_path_generated_terms('{"_id": 8, "a" : [0, 1, 2]}', '', false);
SELECT documentdb_distributed_test_helpers.gin_bson_get_single_path_generated_terms('{"_id": 8, "a" : [0, 1, 2]}', 'a', true);
SELECT documentdb_distributed_test_helpers.gin_bson_get_single_path_generated_terms('{"_id": 8, "a" : [0, 1, 2]}', 'a', false);
SELECT documentdb_distributed_test_helpers.gin_bson_get_single_path_generated_terms('{"_id": 8, "a" : [0, 1, 2]}', 'a.b', true);
SELECT documentdb_distributed_test_helpers.gin_bson_get_single_path_generated_terms('{"_id": 8, "a" : [0, 1, 2]}', 'a.b', false);
SELECT documentdb_distributed_test_helpers.gin_bson_get_single_path_generated_terms('{"_id": 8, "a" : [0, 1, 2]}', 'a.b.1', true);
SELECT documentdb_distributed_test_helpers.gin_bson_get_single_path_generated_terms('{"_id": 8, "a" : [0, 1, 2]}', 'a.b.1', false);

SELECT documentdb_distributed_test_helpers.gin_bson_get_single_path_generated_terms('{"_id": 10, "a" : { "b" : [ 0, 1, 2 ] } }', '', true);
SELECT documentdb_distributed_test_helpers.gin_bson_get_single_path_generated_terms('{"_id": 10, "a" : { "b" : [ 0, 1, 2 ] } }', '', false);
SELECT documentdb_distributed_test_helpers.gin_bson_get_single_path_generated_terms('{"_id": 10, "a" : { "b" : [ 0, 1, 2 ] } }', 'a', true);
SELECT documentdb_distributed_test_helpers.gin_bson_get_single_path_generated_terms('{"_id": 10, "a" : { "b" : [ 0, 1, 2 ] } }', 'a', false);
SELECT documentdb_distributed_test_helpers.gin_bson_get_single_path_generated_terms('{"_id": 10, "a" : { "b" : [ 0, 1, 2 ] } }', 'a.b', true);
SELECT documentdb_distributed_test_helpers.gin_bson_get_single_path_generated_terms('{"_id": 10, "a" : { "b" : [ 0, 1, 2 ] } }', 'a.b', false);
SELECT documentdb_distributed_test_helpers.gin_bson_get_single_path_generated_terms('{"_id": 10, "a" : { "b" : [ 0, 1, 2 ] } }', 'a.b.1', true);
SELECT documentdb_distributed_test_helpers.gin_bson_get_single_path_generated_terms('{"_id": 10, "a" : { "b" : [ 0, 1, 2 ] } }', 'a.b.1', false);

SELECT documentdb_distributed_test_helpers.gin_bson_get_single_path_generated_terms('{"_id": 11, "a" : [ { "b": 0 }, { "b": 1 }, { "b": 3.0 }] }', '', true);
SELECT documentdb_distributed_test_helpers.gin_bson_get_single_path_generated_terms('{"_id": 11, "a" : [ { "b": 0 }, { "b": 1 }, { "b": 3.0 }] }', '', false);
SELECT documentdb_distributed_test_helpers.gin_bson_get_single_path_generated_terms('{"_id": 11, "a" : [ { "b": 0 }, { "b": 1 }, { "b": 3.0 }] }', 'a', true);
SELECT documentdb_distributed_test_helpers.gin_bson_get_single_path_generated_terms('{"_id": 11, "a" : [ { "b": 0 }, { "b": 1 }, { "b": 3.0 }] }', 'a', false);
SELECT documentdb_distributed_test_helpers.gin_bson_get_single_path_generated_terms('{"_id": 11, "a" : [ { "b": 0 }, { "b": 1 }, { "b": 3.0 }] }', 'a.b', true);
SELECT documentdb_distributed_test_helpers.gin_bson_get_single_path_generated_terms('{"_id": 11, "a" : [ { "b": 0 }, { "b": 1 }, { "b": 3.0 }] }', 'a.b', false);
SELECT documentdb_distributed_test_helpers.gin_bson_get_single_path_generated_terms('{"_id": 11, "a" : [ { "b": 0 }, { "b": 1 }, { "b": 3.0 }] }', 'a.b.1', true);
SELECT documentdb_distributed_test_helpers.gin_bson_get_single_path_generated_terms('{"_id": 11, "a" : [ { "b": 0 }, { "b": 1 }, { "b": 3.0 }] }', 'a.b.1', false);

SELECT documentdb_distributed_test_helpers.gin_bson_get_single_path_generated_terms('{"_id": 12, "a" : [ { "b": [-1, 1, 2] }, { "b": [0, 1, 2] }, { "b": [0, 1, 7] }] }', '', true);
SELECT documentdb_distributed_test_helpers.gin_bson_get_single_path_generated_terms('{"_id": 12, "a" : [ { "b": [-1, 1, 2] }, { "b": [0, 1, 2] }, { "b": [0, 1, 7] }] }', '', false);
SELECT documentdb_distributed_test_helpers.gin_bson_get_single_path_generated_terms('{"_id": 12, "a" : [ { "b": [-1, 1, 2] }, { "b": [0, 1, 2] }, { "b": [0, 1, 7] }] }', 'a', true);
SELECT documentdb_distributed_test_helpers.gin_bson_get_single_path_generated_terms('{"_id": 12, "a" : [ { "b": [-1, 1, 2] }, { "b": [0, 1, 2] }, { "b": [0, 1, 7] }] }', 'a', false);
SELECT documentdb_distributed_test_helpers.gin_bson_get_single_path_generated_terms('{"_id": 12, "a" : [ { "b": [-1, 1, 2] }, { "b": [0, 1, 2] }, { "b": [0, 1, 7] }] }', 'a.b', true);
SELECT documentdb_distributed_test_helpers.gin_bson_get_single_path_generated_terms('{"_id": 12, "a" : [ { "b": [-1, 1, 2] }, { "b": [0, 1, 2] }, { "b": [0, 1, 7] }] }', 'a.b', false);
SELECT documentdb_distributed_test_helpers.gin_bson_get_single_path_generated_terms('{"_id": 12, "a" : [ { "b": [-1, 1, 2] }, { "b": [0, 1, 2] }, { "b": [0, 1, 7] }] }', 'a.b.1', true);
SELECT documentdb_distributed_test_helpers.gin_bson_get_single_path_generated_terms('{"_id": 12, "a" : [ { "b": [-1, 1, 2] }, { "b": [0, 1, 2] }, { "b": [0, 1, 7] }] }', 'a.b.1', false);


SELECT documentdb_distributed_test_helpers.gin_bson_get_single_path_generated_terms('{"_id": 13, "a" : [ { "b": [[-1, 1, 2]] }, { "b": [[0, 1, 2]] }, { "b": [[0, 1, 7]] }] }', '', true);
SELECT documentdb_distributed_test_helpers.gin_bson_get_single_path_generated_terms('{"_id": 13, "a" : [ { "b": [[-1, 1, 2]] }, { "b": [[0, 1, 2]] }, { "b": [[0, 1, 7]] }] }', '', false);
SELECT documentdb_distributed_test_helpers.gin_bson_get_single_path_generated_terms('{"_id": 13, "a" : [ { "b": [[-1, 1, 2]] }, { "b": [[0, 1, 2]] }, { "b": [[0, 1, 7]] }] }', 'a', true);
SELECT documentdb_distributed_test_helpers.gin_bson_get_single_path_generated_terms('{"_id": 13, "a" : [ { "b": [[-1, 1, 2]] }, { "b": [[0, 1, 2]] }, { "b": [[0, 1, 7]] }] }', 'a', false);
SELECT documentdb_distributed_test_helpers.gin_bson_get_single_path_generated_terms('{"_id": 13, "a" : [ { "b": [[-1, 1, 2]] }, { "b": [[0, 1, 2]] }, { "b": [[0, 1, 7]] }] }', 'a.b', true);
SELECT documentdb_distributed_test_helpers.gin_bson_get_single_path_generated_terms('{"_id": 13, "a" : [ { "b": [[-1, 1, 2]] }, { "b": [[0, 1, 2]] }, { "b": [[0, 1, 7]] }] }', 'a.b', false);
SELECT documentdb_distributed_test_helpers.gin_bson_get_single_path_generated_terms('{"_id": 13, "a" : [ { "b": [[-1, 1, 2]] }, { "b": [[0, 1, 2]] }, { "b": [[0, 1, 7]] }] }', 'a.b.1', true);
SELECT documentdb_distributed_test_helpers.gin_bson_get_single_path_generated_terms('{"_id": 13, "a" : [ { "b": [[-1, 1, 2]] }, { "b": [[0, 1, 2]] }, { "b": [[0, 1, 7]] }] }', 'a.b.1', false);

SELECT documentdb_distributed_test_helpers.gin_bson_get_single_path_generated_terms('{"_id": 14, "a" : { "b" : [ { "1" : [1, 2, 3 ] } ] } }', '', true);
SELECT documentdb_distributed_test_helpers.gin_bson_get_single_path_generated_terms('{"_id": 14, "a" : { "b" : [ { "1" : [1, 2, 3 ] } ] } }', '', false);
SELECT documentdb_distributed_test_helpers.gin_bson_get_single_path_generated_terms('{"_id": 14, "a" : { "b" : [ { "1" : [1, 2, 3 ] } ] } }', 'a', true);
SELECT documentdb_distributed_test_helpers.gin_bson_get_single_path_generated_terms('{"_id": 14, "a" : { "b" : [ { "1" : [1, 2, 3 ] } ] } }', 'a', false);
SELECT documentdb_distributed_test_helpers.gin_bson_get_single_path_generated_terms('{"_id": 14, "a" : { "b" : [ { "1" : [1, 2, 3 ] } ] } }', 'a.b', true);
SELECT documentdb_distributed_test_helpers.gin_bson_get_single_path_generated_terms('{"_id": 14, "a" : { "b" : [ { "1" : [1, 2, 3 ] } ] } }', 'a.b', false);
SELECT documentdb_distributed_test_helpers.gin_bson_get_single_path_generated_terms('{"_id": 14, "a" : { "b" : [ { "1" : [1, 2, 3 ] } ] } }', 'a.b.1', true);
SELECT documentdb_distributed_test_helpers.gin_bson_get_single_path_generated_terms('{"_id": 14, "a" : { "b" : [ { "1" : [1, 2, 3 ] } ] } }', 'a.b.1', false);

SELECT documentdb_distributed_test_helpers.gin_bson_get_single_path_generated_terms('{"_id":"7", "a" : [ { "c" : 0 }, { "c" : 1 }, { "b" : 2 } ] }', '', true);
SELECT documentdb_distributed_test_helpers.gin_bson_get_single_path_generated_terms('{"_id":"7", "a" : [ { "c" : 0 }, { "c" : 1 }, { "b" : 2 } ] }', '', false);
SELECT documentdb_distributed_test_helpers.gin_bson_get_single_path_generated_terms('{"_id":"7", "a" : [ { "c" : 0 }, { "c" : 1 }, { "b" : 2 } ] }', 'a', true);
SELECT documentdb_distributed_test_helpers.gin_bson_get_single_path_generated_terms('{"_id":"7", "a" : [ { "c" : 0 }, { "c" : 1 }, { "b" : 2 } ] }', 'a', false);
SELECT documentdb_distributed_test_helpers.gin_bson_get_single_path_generated_terms('{"_id":"7", "a" : [ { "c" : 0 }, { "c" : 1 }, { "b" : 2 } ] }', 'a.b', true);
SELECT documentdb_distributed_test_helpers.gin_bson_get_single_path_generated_terms('{"_id":"7", "a" : [ { "c" : 0 }, { "c" : 1 }, { "b" : 2 } ] }', 'a.b', false);
SELECT documentdb_distributed_test_helpers.gin_bson_get_single_path_generated_terms('{"_id":"7", "a" : [ { "c" : 0 }, { "c" : 1 }, { "b" : 2 } ] }', 'a.b.1', true);
SELECT documentdb_distributed_test_helpers.gin_bson_get_single_path_generated_terms('{"_id":"7", "a" : [ { "c" : 0 }, { "c" : 1 }, { "b" : 2 } ] }', 'a.b.1', false);


SELECT documentdb_distributed_test_helpers.gin_bson_get_wildcard_project_generated_terms('{"_id": 6, "a" : { "b": 1, "c": 2 }, "d": 1 }', '[ "a" ]', false, true);
SELECT documentdb_distributed_test_helpers.gin_bson_get_wildcard_project_generated_terms('{"_id": 6, "a" : { "b": 1, "c": 2 }, "d": 1 }', '[ "a" ]', true, true);
SELECT documentdb_distributed_test_helpers.gin_bson_get_wildcard_project_generated_terms('{"_id": 6, "a" : { "b": 1, "c": 2 }, "d": 1 }', '[ "a" ]', true, false);
SELECT documentdb_distributed_test_helpers.gin_bson_get_wildcard_project_generated_terms('{"_id": 6, "a" : { "b": 1, "c": 2 }, "d": 1 }', '[ "a", "d" ]', false, true);
SELECT documentdb_distributed_test_helpers.gin_bson_get_wildcard_project_generated_terms('{"_id": 6, "a" : { "b": 1, "c": 2 }, "d": 1 }', '[ "a", "d" ]', true, true);
SELECT documentdb_distributed_test_helpers.gin_bson_get_wildcard_project_generated_terms('{"_id": 6, "a" : { "b": 1, "c": 2 }, "d": 1 }', '[ "a", "d" ]', true, false);


SELECT documentdb_distributed_test_helpers.gin_bson_get_wildcard_project_generated_terms('{"_id": 8, "a" : [0, 1, 2]}', '[ "a" ]', false, true);
SELECT documentdb_distributed_test_helpers.gin_bson_get_wildcard_project_generated_terms('{"_id": 8, "a" : [0, 1, 2]}', '[ "a" ]', true, true);
SELECT documentdb_distributed_test_helpers.gin_bson_get_wildcard_project_generated_terms('{"_id": 8, "a" : [0, 1, 2]}', '[ "a" ]', true, false);

SELECT documentdb_distributed_test_helpers.gin_bson_get_wildcard_project_generated_terms('{"_id": 10, "a" : { "b" : [ 0, 1, 2 ] } }', '[ "a" ]', false, true);
SELECT documentdb_distributed_test_helpers.gin_bson_get_wildcard_project_generated_terms('{"_id": 10, "a" : { "b" : [ 0, 1, 2 ] } }', '[ "a" ]', true, true);
SELECT documentdb_distributed_test_helpers.gin_bson_get_wildcard_project_generated_terms('{"_id": 10, "a" : { "b" : [ 0, 1, 2 ] } }', '[ "a" ]', true, false);
SELECT documentdb_distributed_test_helpers.gin_bson_get_wildcard_project_generated_terms('{"_id": 10, "a" : { "b" : [ 0, 1, 2 ] } }', '[ "a.b" ]', false, true);
SELECT documentdb_distributed_test_helpers.gin_bson_get_wildcard_project_generated_terms('{"_id": 10, "a" : { "b" : [ 0, 1, 2 ] } }', '[ "a.b" ]', true, true);
SELECT documentdb_distributed_test_helpers.gin_bson_get_wildcard_project_generated_terms('{"_id": 10, "a" : { "b" : [ 0, 1, 2 ] } }', '[ "a.b" ]', true, false);

SELECT documentdb_distributed_test_helpers.gin_bson_get_wildcard_project_generated_terms('{"_id": 11, "a" : [ { "b": 0 }, { "b": 1 }, { "b": 3.0 }] }', '[ "a" ]', false, true);
SELECT documentdb_distributed_test_helpers.gin_bson_get_wildcard_project_generated_terms('{"_id": 11, "a" : [ { "b": 0 }, { "b": 1 }, { "b": 3.0 }] }', '[ "a" ]', true, true);
SELECT documentdb_distributed_test_helpers.gin_bson_get_wildcard_project_generated_terms('{"_id": 11, "a" : [ { "b": 0 }, { "b": 1 }, { "b": 3.0 }] }', '[ "a" ]', true, false);
SELECT documentdb_distributed_test_helpers.gin_bson_get_wildcard_project_generated_terms('{"_id": 11, "a" : [ { "b": 0 }, { "b": 1 }, { "b": 3.0 }] }', '[ "a.b" ]', false, true);
SELECT documentdb_distributed_test_helpers.gin_bson_get_wildcard_project_generated_terms('{"_id": 11, "a" : [ { "b": 0 }, { "b": 1 }, { "b": 3.0 }] }', '[ "a.b" ]', true, true);
SELECT documentdb_distributed_test_helpers.gin_bson_get_wildcard_project_generated_terms('{"_id": 11, "a" : [ { "b": 0 }, { "b": 1 }, { "b": 3.0 }] }', '[ "a.b" ]', true, false);

SELECT documentdb_distributed_test_helpers.gin_bson_get_wildcard_project_generated_terms('{"_id": 12, "a" : [ { "b": [-1, 1, 2] }, { "b": [0, 1, 2] }, { "b": [0, 1, 7] }] }', '[ "a" ]', false, true);
SELECT documentdb_distributed_test_helpers.gin_bson_get_wildcard_project_generated_terms('{"_id": 12, "a" : [ { "b": [-1, 1, 2] }, { "b": [0, 1, 2] }, { "b": [0, 1, 7] }] }', '[ "a" ]', true, true);
SELECT documentdb_distributed_test_helpers.gin_bson_get_wildcard_project_generated_terms('{"_id": 12, "a" : [ { "b": [-1, 1, 2] }, { "b": [0, 1, 2] }, { "b": [0, 1, 7] }] }', '[ "a" ]', true, false);
SELECT documentdb_distributed_test_helpers.gin_bson_get_wildcard_project_generated_terms('{"_id": 12, "a" : [ { "b": [-1, 1, 2] }, { "b": [0, 1, 2] }, { "b": [0, 1, 7] }] }', '[ "a.b" ]', false, true);
SELECT documentdb_distributed_test_helpers.gin_bson_get_wildcard_project_generated_terms('{"_id": 12, "a" : [ { "b": [-1, 1, 2] }, { "b": [0, 1, 2] }, { "b": [0, 1, 7] }] }', '[ "a.b" ]', true, true);
SELECT documentdb_distributed_test_helpers.gin_bson_get_wildcard_project_generated_terms('{"_id": 12, "a" : [ { "b": [-1, 1, 2] }, { "b": [0, 1, 2] }, { "b": [0, 1, 7] }] }', '[ "a.b" ]', true, false);
SELECT documentdb_distributed_test_helpers.gin_bson_get_wildcard_project_generated_terms('{"_id": 12, "a" : [ { "b": [-1, 1, 2] }, { "b": [0, 1, 2] }, { "b": [0, 1, 7] }] }', '[ "a.b.0", "a.b.1" ]', false, true);
SELECT documentdb_distributed_test_helpers.gin_bson_get_wildcard_project_generated_terms('{"_id": 12, "a" : [ { "b": [-1, 1, 2] }, { "b": [0, 1, 2] }, { "b": [0, 1, 7] }] }', '[ "a.b.0", "a.b.1" ]', true, true);
SELECT documentdb_distributed_test_helpers.gin_bson_get_wildcard_project_generated_terms('{"_id": 12, "a" : [ { "b": [-1, 1, 2] }, { "b": [0, 1, 2] }, { "b": [0, 1, 7] }] }', '[ "a.b.0", "a.b.1" ]', true, false);

SELECT documentdb_distributed_test_helpers.gin_bson_get_wildcard_project_generated_terms('{"_id": 13, "a" : [ { "b": [[-1, 1, 2]] }, { "b": [[0, 1, 2]] }, { "b": [[0, 1, 7]] }] }', '[ "a" ]', false, true);
SELECT documentdb_distributed_test_helpers.gin_bson_get_wildcard_project_generated_terms('{"_id": 13, "a" : [ { "b": [[-1, 1, 2]] }, { "b": [[0, 1, 2]] }, { "b": [[0, 1, 7]] }] }', '[ "a" ]', true, true);
SELECT documentdb_distributed_test_helpers.gin_bson_get_wildcard_project_generated_terms('{"_id": 13, "a" : [ { "b": [[-1, 1, 2]] }, { "b": [[0, 1, 2]] }, { "b": [[0, 1, 7]] }] }', '[ "a" ]', true, false);
SELECT documentdb_distributed_test_helpers.gin_bson_get_wildcard_project_generated_terms('{"_id": 13, "a" : [ { "b": [[-1, 1, 2]] }, { "b": [[0, 1, 2]] }, { "b": [[0, 1, 7]] }] }', '[ "a.b" ]', false, true);
SELECT documentdb_distributed_test_helpers.gin_bson_get_wildcard_project_generated_terms('{"_id": 13, "a" : [ { "b": [[-1, 1, 2]] }, { "b": [[0, 1, 2]] }, { "b": [[0, 1, 7]] }] }', '[ "a.b" ]', true, true);
SELECT documentdb_distributed_test_helpers.gin_bson_get_wildcard_project_generated_terms('{"_id": 13, "a" : [ { "b": [[-1, 1, 2]] }, { "b": [[0, 1, 2]] }, { "b": [[0, 1, 7]] }] }', '[ "a.b" ]', true, false);
SELECT documentdb_distributed_test_helpers.gin_bson_get_wildcard_project_generated_terms('{"_id": 13, "a" : [ { "b": [[-1, 1, 2]] }, { "b": [[0, 1, 2]] }, { "b": [[0, 1, 7]] }] }', '[ "a.b.0", "a.b.1" ]', false, true);
SELECT documentdb_distributed_test_helpers.gin_bson_get_wildcard_project_generated_terms('{"_id": 13, "a" : [ { "b": [[-1, 1, 2]] }, { "b": [[0, 1, 2]] }, { "b": [[0, 1, 7]] }] }', '[ "a.b.0", "a.b.1" ]', true, true);
SELECT documentdb_distributed_test_helpers.gin_bson_get_wildcard_project_generated_terms('{"_id": 13, "a" : [ { "b": [[-1, 1, 2]] }, { "b": [[0, 1, 2]] }, { "b": [[0, 1, 7]] }] }', '[ "a.b.0", "a.b.1" ]', true, false);

SELECT documentdb_distributed_test_helpers.gin_bson_get_wildcard_project_generated_terms('{"_id": 14, "a" : { "b" : [ { "1" : [1, 2, 3 ] } ] } }', '[ "a" ]', false, true);
SELECT documentdb_distributed_test_helpers.gin_bson_get_wildcard_project_generated_terms('{"_id": 14, "a" : { "b" : [ { "1" : [1, 2, 3 ] } ] } }', '[ "a" ]', true, true);
SELECT documentdb_distributed_test_helpers.gin_bson_get_wildcard_project_generated_terms('{"_id": 14, "a" : { "b" : [ { "1" : [1, 2, 3 ] } ] } }', '[ "a" ]', true, false);
SELECT documentdb_distributed_test_helpers.gin_bson_get_wildcard_project_generated_terms('{"_id": 14, "a" : { "b" : [ { "1" : [1, 2, 3 ] } ] } }', '[ "a.b" ]', false, true);
SELECT documentdb_distributed_test_helpers.gin_bson_get_wildcard_project_generated_terms('{"_id": 14, "a" : { "b" : [ { "1" : [1, 2, 3 ] } ] } }', '[ "a.b" ]', true, true);
SELECT documentdb_distributed_test_helpers.gin_bson_get_wildcard_project_generated_terms('{"_id": 14, "a" : { "b" : [ { "1" : [1, 2, 3 ] } ] } }', '[ "a.b" ]', true, false);
SELECT documentdb_distributed_test_helpers.gin_bson_get_wildcard_project_generated_terms('{"_id": 14, "a" : { "b" : [ { "1" : [1, 2, 3 ] } ] } }', '[ "a.b.0", "a.b.1" ]', false, true);
SELECT documentdb_distributed_test_helpers.gin_bson_get_wildcard_project_generated_terms('{"_id": 14, "a" : { "b" : [ { "1" : [1, 2, 3 ] } ] } }', '[ "a.b.0", "a.b.1" ]', true, true);
SELECT documentdb_distributed_test_helpers.gin_bson_get_wildcard_project_generated_terms('{"_id": 14, "a" : { "b" : [ { "1" : [1, 2, 3 ] } ] } }', '[ "a.b.0", "a.b.1" ]', true, false);

SELECT documentdb_distributed_test_helpers.gin_bson_get_wildcard_project_generated_terms('{"_id":"7", "a" : [ { "c" : 0 }, { "c" : 1 }, { "b" : 2 } ] }', '[ "a" ]', false, true);
SELECT documentdb_distributed_test_helpers.gin_bson_get_wildcard_project_generated_terms('{"_id":"7", "a" : [ { "c" : 0 }, { "c" : 1 }, { "b" : 2 } ] }', '[ "a" ]', true, true);
SELECT documentdb_distributed_test_helpers.gin_bson_get_wildcard_project_generated_terms('{"_id":"7", "a" : [ { "c" : 0 }, { "c" : 1 }, { "b" : 2 } ] }', '[ "a" ]', true, false);
SELECT documentdb_distributed_test_helpers.gin_bson_get_wildcard_project_generated_terms('{"_id":"7", "a" : [ { "c" : 0 }, { "c" : 1 }, { "b" : 2 } ] }', '[ "a.b" ]', false, true);
SELECT documentdb_distributed_test_helpers.gin_bson_get_wildcard_project_generated_terms('{"_id":"7", "a" : [ { "c" : 0 }, { "c" : 1 }, { "b" : 2 } ] }', '[ "a.b" ]', true, true);
SELECT documentdb_distributed_test_helpers.gin_bson_get_wildcard_project_generated_terms('{"_id":"7", "a" : [ { "c" : 0 }, { "c" : 1 }, { "b" : 2 } ] }', '[ "a.b" ]', true, false);

--Nested Array or Array has document
SELECT documentdb_distributed_test_helpers.gin_bson_get_single_path_generated_terms('{"_id": 10, "a" : [{"b" : "old"}]  }', '', true);
SELECT documentdb_distributed_test_helpers.gin_bson_get_single_path_generated_terms('{"_id": 10, "a" : [[{"b" : "old"}]]  }', '', true);
SELECT documentdb_distributed_test_helpers.gin_bson_get_single_path_generated_terms('{"_id": 10, "a" : [[1,{"b" : "old"},3]]  }', '', true);

-- generate terms with and without truncation
-- string
SELECT documentdb_distributed_test_helpers.gin_bson_get_single_path_generated_terms('{"_id": 13, "a" : "p86PXqGpykgF4prF9X0ouGVG7mkRGg9ZyXZwt25L49trG5IQXgrHdkR01ZKSBlZv8SH04XUych6c0HNvS6Dn17Rx3JgKYMFqxSTyhRZLdlmMgxCMBE7wYDrvtfCPRgeDPGOR4FTVP0521ulBAnAdoxub6WndMqLhGZKymTmmjdCJ28ka0fnN47BeMPy9jqllCfuWh6MUg4bSRrvkopOHgkXx0Jdg7KwLsvfozsoVzKVI1BYlV6ruz1QipKZTRgAJVUYEzG9qA28ZBFu6ZLN0vzeXpPlxNbPu4pwLcPISxCNmgIeDm397Bs9krq0OCqhqhVJzYaLLmcTipuqi0mmPYNBF5RYfiEWeLmarJzW4oirL1s6d8EMGqvlRW5M6GY5OCUo5BfvADGpa8VQ3dGF1GKs05U7IWax6am9TOuq6hXWuhw3r8pmv0OvhqNmh0Wm9rAoK2zEHnuwoUqh57McwWx5gvouiZQdgDrRlU0IAvv4wpPwtgoAZpNje9ZPNywbrdYkKdy3CPGDf4bhreNvnOVx2aIfY3ZlQCL8RWXjIN1HWhb8nRTZuaqJVDh8lnB8kessHCrS0tTLEcnjZIRVPXge5F3AD2x4PYrP1jthursnY6XqqzZVvN2PEia9AXyqvAi8447AONDn27AqUEDRCVBg8l4DzbZ7O2OUOyG3nBE78xDdQMbpkhmPF0MPiihgZtcPLxB4E36I5Kr1g0ecmX6XsFN2FFDDHg0R8oi120fnm33UWWwpfM13czkJKzkGucSDv0NO0nrmd0yxlTwLCwYg3IOP62pyUFfZNj755sbXigEajKcypSgNdCcVJ8fak9xhe575FmcA1LSr8qOKKCyy3bZdyFKuwDtAtCOrlp2Ay5qtqIJhovZp3ek6U1ZKlLAXPf5Xk661XHOLuFNExn7vyMxeFKb9v2EWmdrO622ylfc0xnGnOc2yT7lAE7w1x4AGxBdgjI3q052o4gWfALRDjbhEK39sM2rpTIwMrvsSo35rsv5p9mQ2zQCL1OUBHQHmDFEzfH7zanSgISWGYjdbpGoyo3JxhcypqUxx7jB4DIe20i3fGbdpFOKVirFjZ4LyfaDgGWGaW6eD7XyAigTuajLe5NpXBJOAQr31C8YYl1XlWZSBv8jdOjpLE8BixgHsXTldxzh7QxPQH2eKO74FeDySOZCNlsHENbQHbwfQJRxz33oHpr0dsNRn2AYCP6mOFJ8G9AASMx6jSP5j2ZRZGFb6GKhG2FyiIklRTPvtu14aMPTgD0tPBlXEWwi6IuynzrXXzOY3BodDRk2EwkruuuuEwCuSd82HJDuChSWf2A9duqv9J0UrbYuXN9NWOVypBq4tlY6joGUPe509Z4EQEKAmFIUV0GBQixzt1tPVgBs9esNvNFSftzKgYBS4FJe489UWwaX3njm5uQYW7wFJuqcI4A0MrfOYJSgJwzvtfaKwu95yAtjW7QgLPG355RkKsjZDLZdjuNvjN17yYC0xchIaBGaK6cIGDjRV2mnmKHaaLRrEwjKqS1F1FCH19JnXSB1OoDdcoEYAbQwK2Kd0KWKh6tslohWoPWvFlOXBzZOEnPNENpU8xxD8mHxYDr8siiaknwRMvqf4yP2Oe4v7GFwOrMmR6UJknUu7xp2HvsjqceO5g7nkuUyiec4lk1sPraMygpBAboLCB3S4qSfNqtfJj0vwZVXmmK3lJyrh2dgJ7botypjDE1ENq6vovXKtZ7OYfE81V2ic5qnSKakbwmsiS3uywVjuvFtDBwgZQBhEMqcG6txa9qNINGA5Xbt1xnhJbFuxaykJpBTGtYAem00AX5ZRTVPy57WRue1aIvopDx92yL2Lw3eWSebATumO91PYlVDUhHzaQ232MS2hrHbKZYWguCb9UU3Wrsu9f5dybJDhJhE7jOnDb4hYJvdxQH8Ni7cELn4bf0AxXTQ74RJM2ZiPCAG8CYtpDcaHnU4BEfs5stjBOX1rWgQihsz5fbCEZOcDYJ5om5Gwk3R12Q49Ly3IARzsVTZnswxfpOfq6bIY9oKLpYanNDaf9ZhZEaa2MwOA9Ruiy4RmquYoJ92gBxS1l1FC8jYlCEfvSAkylhSA54TsWVVIwsOZd6Hj3RQQZtQcJEIMIHPxdTNUx1sSMCItVbTikTw8gviNtI1UM6VOJxiqBsdOmfy8WbfP7djRbRUBwCnCFoaEIFrAQfebevG6hxQ6MbeVP17ythvVUlDSJL2Bn7KmwNBj7Aido8RmBUPXuTxZzMpuLjn1q0Wm4FMz232XddybeBnELMMDkEWUqPL94xo1AdfhtXQ2WhpIJKHsSqj6vv51PnmfzmHZaqXkWIY6WhCf7SsBMojqBUEEI3VYKxcQ5IBzvX284CrH5x2M6AoGpANFp036l6cor9VBVYHyXODCBO1ACMDe9YENwatNgWhpiu2W6Ao1jE5vs00Vk9j4gY9NFPNrjpw5gFgRdinELZAyd2akBSoYdXxBt9NtfVJEYl2OiblplIOgB7fi4HEho4JtNhmyS3P3BdlQkRAciVHfHKAOdi2dBZxxVFJjqBuVW4Svv6XJuYYLPMJiPGrgV3rvlzlUdUAn0LKsga4BEn4cPoRnRPPYgj7L5bkqMxonzRiCBkMU4HTYBTrVfNqu7zHLcMQwc9lIEHYHDN2JyqEr0emG5B8NMqDJFwUHIILvA5pUuZQT5PV87QpLs8n24vV5YHqFDFm7KlGxan3Hdy5Zw4PaQsROlwIxFGFuHUXi6B4nn8KYZlULfAQ7stk4DDukrPmOXlbbDOhNHu2pXqejS12MTOYZ33"}', '', true);
SELECT documentdb_distributed_test_helpers.gin_bson_get_single_path_generated_terms('{"_id": 13, "a" : "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"}', '', true, true, false, 500);
SELECT documentdb_distributed_test_helpers.gin_bson_get_single_path_generated_terms('{"_id": 13, "a" : "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"}', '', true, true, false, 1000);
SELECT documentdb_distributed_test_helpers.gin_bson_get_single_path_generated_terms('{"_id": 13, "a" : "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"}', '', true, true, false, 2500);
SELECT documentdb_distributed_test_helpers.gin_bson_get_single_path_generated_terms('{"_id": 13, "a" : "p86PXqGpykgF4prF9X0ouGVG7mkRGg9ZyXZwt25L49trG5IQXgrHdkR01ZKSBlZv8SH04XUych6c0HNvS6Dn17Rx3JgKYMFqxSTyhRZLdlmMgxCMBE7wYDrvtfCPRgeDPGOR4FTVP0521ulBAnAdLdlmMgxCMBE7wYDrvtfhGZymTmmjdCJ28ka0fnN47BeKymTmmjdCJ28ka0CPRgeDPGOR4FTVP0521ulBAnAdoxuboxub63"}', '', true, true, false, 45);

-- binary
SELECT documentdb_distributed_test_helpers.gin_bson_get_single_path_generated_terms('{"_id": 13, "a" : { "$binary": { "base64": "cDg2UFhxR3B5a2dGNHByRjlYMG91R1ZHN21rUkdnOVp5WFp3dDI1TDQ5dHJHNUlRWGdySGRrUjAxWktTQmxadjhTSDA0WFV5Y2g2YzBITnZTNkRuMTdSeDNKZ0tZTUZxeFNUeWhSWkxkbG1NZ3hDTUJFN3dZRHJ2dGZDUFJnZURQR09SNEZUVlAwNTIxdWxCQW5BZExkbG1NZ3hDTUJFN3dZRHJ2dGZoR1p5bVRtbWpkQ0oyOGthMGZuTjQ3QmVLeW1UbW1qZENKMjhrYTBDUFJnZURQR09SNEZUVlAwNTIxdWxCQW5BZG94dWJveHViNjM=", "subType": "00" } } }', '', true, true, false, 45);
SELECT documentdb_distributed_test_helpers.gin_bson_get_single_path_generated_terms('{"_id": 13, "a" : { "$binary": { "base64": "cDg2UFhxR3B5a2dGNHByRjlYMG91R1ZHN21rUkdnOVp5WFp3dDI1TDQ5dHJHNUlRWGdySGRrUjAxWktTQmxadjhTSDA0WFV5Y2g2YzBITnZTNkRuMTdSeDNKZ0tZTUZxeFNUeWhSWkxkbG1NZ3hDTUJFN3dZRHJ2dGZDUFJnZURQR09SNEZUVlAwNTIxdWxCQW5BZExkbG1NZ3hDTUJFN3dZRHJ2dGZoR1p5bVRtbWpkQ0oyOGthMGZuTjQ3QmVLeW1UbW1qZENKMjhrYTBDUFJnZURQR09SNEZUVlAwNTIxdWxCQW5BZG94dWJveHViNjM=", "subType": "00" } } }', '', true, true, false, 100);
SELECT documentdb_distributed_test_helpers.gin_bson_get_single_path_generated_terms('{"_id": 13, "a" : { "$binary": { "base64": "cDg2UFhxR3B5a2dGNHByRjlYMG91R1ZHN21rUkdnOVp5WFp3dDI1TDQ5dHJHNUlRWGdySGRrUjAxWktTQmxadjhTSDA0WFV5Y2g2YzBITnZTNkRuMTdSeDNKZ0tZTUZxeFNUeWhSWkxkbG1NZ3hDTUJFN3dZRHJ2dGZDUFJnZURQR09SNEZUVlAwNTIxdWxCQW5BZExkbG1NZ3hDTUJFN3dZRHJ2dGZoR1p5bVRtbWpkQ0oyOGthMGZuTjQ3QmVLeW1UbW1qZENKMjhrYTBDUFJnZURQR09SNEZUVlAwNTIxdWxCQW5BZG94dWJveHViNjM=", "subType": "00" } } }', '', true, true, false, 20);
SELECT documentdb_distributed_test_helpers.gin_bson_get_single_path_generated_terms('{"_id": 13, "a" : { "$binary": { "base64": "cDg2UFhxR3B5a2dGNHByRjlYMG91R1ZHN21rUkdnOVp5WFp3dDI1TDQ5dHJHNUlRWGdySGRrUjAxWktTQmxadjhTSDA0WFV5Y2g2YzBITnZTNkRuMTdSeDNKZ0tZTUZxeFNUeWhSWkxkbG1NZ3hDTUJFN3dZRHJ2dGZDUFJnZURQR09SNEZUVlAwNTIxdWxCQW5BZExkbG1NZ3hDTUJFN3dZRHJ2dGZoR1p5bVRtbWpkQ0oyOGthMGZuTjQ3QmVLeW1UbW1qZENKMjhrYTBDUFJnZURQR09SNEZUVlAwNTIxdWxCQW5BZG94dWJveHViNjM=", "subType": "00" } } }', '', true, true, false, 500);

-- code
SELECT documentdb_distributed_test_helpers.gin_bson_get_single_path_generated_terms('{"_id": 13, "a" : { "$code": "cDg2UFhxR3B5a2dGNHByRjlYMG91R1ZHN21rUkdnOVp5WFp3dDI1TDQ5dHJHNUlRWGdySGRrUjAxWktTQmxadjhTSDA0WFV5Y2g2YzBITnZTNkRuMTdSeDNKZ0tZTUZxeFNUeWhSWkxkbG1NZ3hDTUJFN3dZRHJ2dGZDUFJnZURQR09SNEZUVlAwNTIxdWxCQW5BZExkbG1NZ3hDTUJFN3dZRHJ2dGZoR1p5bVRtbWpkQ0oyOGthMGZuTjQ3QmVLeW1UbW1qZENKMjhrYTBDUFJnZURQR09SNEZUVlAwNTIxdWxCQW5BZG94dWJveHViNjM=" } }', '', true, true, false, 500);
SELECT documentdb_distributed_test_helpers.gin_bson_get_single_path_generated_terms('{"_id": 13, "a" : { "$code": "cDg2UFhxR3B5a2dGNHByRjlYMG91R1ZHN21rUkdnOVp5WFp3dDI1TDQ5dHJHNUlRWGdySGRrUjAxWktTQmxadjhTSDA0WFV5Y2g2YzBITnZTNkRuMTdSeDNKZ0tZTUZxeFNUeWhSWkxkbG1NZ3hDTUJFN3dZRHJ2dGZDUFJnZURQR09SNEZUVlAwNTIxdWxCQW5BZExkbG1NZ3hDTUJFN3dZRHJ2dGZoR1p5bVRtbWpkQ0oyOGthMGZuTjQ3QmVLeW1UbW1qZENKMjhrYTBDUFJnZURQR09SNEZUVlAwNTIxdWxCQW5BZG94dWJveHViNjM=" } }', '', true, true, false, 40);
SELECT documentdb_distributed_test_helpers.gin_bson_get_single_path_generated_terms('{"_id": 13, "a" : { "$code": "cDg2UFhxR3B5a2dGNHByRjlYMG91R1ZHN21rUkdnOVp5WFp3dDI1TDQ5dHJHNUlRWGdySGRrUjAxWktTQmxadjhTSDA0WFV5Y2g2YzBITnZTNkRuMTdSeDNKZ0tZTUZxeFNUeWhSWkxkbG1NZ3hDTUJFN3dZRHJ2dGZDUFJnZURQR09SNEZUVlAwNTIxdWxCQW5BZExkbG1NZ3hDTUJFN3dZRHJ2dGZoR1p5bVRtbWpkQ0oyOGthMGZuTjQ3QmVLeW1UbW1qZENKMjhrYTBDUFJnZURQR09SNEZUVlAwNTIxdWxCQW5BZG94dWJveHViNjM=" } }', '', true, true, false, 20);

-- symbol
SELECT documentdb_distributed_test_helpers.gin_bson_get_single_path_generated_terms('{"_id": 13, "a" : { "$symbol": "cDg2UFhxR3B5a2dGNHByRjlYMG91R1ZHN21rUkdnOVp5WFp3dDI1TDQ5dHJHNUlRWGdySGRrUjAxWktTQmxadjhTSDA0WFV5Y2g2YzBITnZTNkRuMTdSeDNKZ0tZTUZxeFNUeWhSWkxkbG1NZ3hDTUJFN3dZRHJ2dGZDUFJnZURQR09SNEZUVlAwNTIxdWxCQW5BZExkbG1NZ3hDTUJFN3dZRHJ2dGZoR1p5bVRtbWpkQ0oyOGthMGZuTjQ3QmVLeW1UbW1qZENKMjhrYTBDUFJnZURQR09SNEZUVlAwNTIxdWxCQW5BZG94dWJveHViNjM=" } }', '', true, true, false, 500);
SELECT documentdb_distributed_test_helpers.gin_bson_get_single_path_generated_terms('{"_id": 13, "a" : { "$symbol": "cDg2UFhxR3B5a2dGNHByRjlYMG91R1ZHN21rUkdnOVp5WFp3dDI1TDQ5dHJHNUlRWGdySGRrUjAxWktTQmxadjhTSDA0WFV5Y2g2YzBITnZTNkRuMTdSeDNKZ0tZTUZxeFNUeWhSWkxkbG1NZ3hDTUJFN3dZRHJ2dGZDUFJnZURQR09SNEZUVlAwNTIxdWxCQW5BZExkbG1NZ3hDTUJFN3dZRHJ2dGZoR1p5bVRtbWpkQ0oyOGthMGZuTjQ3QmVLeW1UbW1qZENKMjhrYTBDUFJnZURQR09SNEZUVlAwNTIxdWxCQW5BZG94dWJveHViNjM=" } }', '', true, true, false, 40);
SELECT documentdb_distributed_test_helpers.gin_bson_get_single_path_generated_terms('{"_id": 13, "a" : { "$symbol": "cDg2UFhxR3B5a2dGNHByRjlYMG91R1ZHN21rUkdnOVp5WFp3dDI1TDQ5dHJHNUlRWGdySGRrUjAxWktTQmxadjhTSDA0WFV5Y2g2YzBITnZTNkRuMTdSeDNKZ0tZTUZxeFNUeWhSWkxkbG1NZ3hDTUJFN3dZRHJ2dGZDUFJnZURQR09SNEZUVlAwNTIxdWxCQW5BZExkbG1NZ3hDTUJFN3dZRHJ2dGZoR1p5bVRtbWpkQ0oyOGthMGZuTjQ3QmVLeW1UbW1qZENKMjhrYTBDUFJnZURQR09SNEZUVlAwNTIxdWxCQW5BZG94dWJveHViNjM=" } }', '', true, true, false, 20);

-- regex
SELECT documentdb_distributed_test_helpers.gin_bson_get_single_path_generated_terms('{"_id": 13, "a" : { "$regularExpression": { "pattern": "cDg2UFhxR3B5a2dGNHByRjlYMG91R1ZHN21rUkdnOVp5WFp3dDI1TDQ5dHJHNUlRWGdySGRrUjAxWktTQmxadjhTSDA0WFV5Y2g2YzBITnZTNkRuMTdSeDNKZ0tZTUZxeFNUeWhSWkxkbG1NZ3hDTUJFN3dZRHJ2dGZDUFJnZURQR09SNEZUVlAwNTIxdWxCQW5BZExkbG1NZ3hDTUJFN3dZRHJ2dGZoR1p5bVRtbWpkQ0oyOGthMGZuTjQ3QmVLeW1UbW1qZENKMjhrYTBDUFJnZURQR09SNEZUVlAwNTIxdWxCQW5BZG94dWJveHViNjM=", "options": "" } } }', '', true, true, false, 500);
SELECT documentdb_distributed_test_helpers.gin_bson_get_single_path_generated_terms('{"_id": 13, "a" : { "$regularExpression": { "pattern": "cDg2UFhxR3B5a2dGNHByRjlYMG91R1ZHN21rUkdnOVp5WFp3dDI1TDQ5dHJHNUlRWGdySGRrUjAxWktTQmxadjhTSDA0WFV5Y2g2YzBITnZTNkRuMTdSeDNKZ0tZTUZxeFNUeWhSWkxkbG1NZ3hDTUJFN3dZRHJ2dGZDUFJnZURQR09SNEZUVlAwNTIxdWxCQW5BZExkbG1NZ3hDTUJFN3dZRHJ2dGZoR1p5bVRtbWpkQ0oyOGthMGZuTjQ3QmVLeW1UbW1qZENKMjhrYTBDUFJnZURQR09SNEZUVlAwNTIxdWxCQW5BZG94dWJveHViNjM=", "options": "" } } }', '', true, true, false, 50);
SELECT documentdb_distributed_test_helpers.gin_bson_get_single_path_generated_terms('{"_id": 13, "a" : { "$regularExpression": { "pattern": "cDg2UFhxR3B5a2dGNHByRjlYMG91R1ZHN21rUkdnOVp5WFp3dDI1TDQ5dHJHNUlRWGdySGRrUjAxWktTQmxadjhTSDA0WFV5Y2g2YzBITnZTNkRuMTdSeDNKZ0tZTUZxeFNUeWhSWkxkbG1NZ3hDTUJFN3dZRHJ2dGZDUFJnZURQR09SNEZUVlAwNTIxdWxCQW5BZExkbG1NZ3hDTUJFN3dZRHJ2dGZoR1p5bVRtbWpkQ0oyOGthMGZuTjQ3QmVLeW1UbW1qZENKMjhrYTBDUFJnZURQR09SNEZUVlAwNTIxdWxCQW5BZG94dWJveHViNjM=", "options": "" } } }', '', true, true, false, 20);

-- regex with options
SELECT documentdb_distributed_test_helpers.gin_bson_get_single_path_generated_terms('{"_id": 13, "a" : { "$regularExpression": { "pattern": "cDg2UFhxR3B5a2dGNHByRjlYMG91R1ZHN21rUkdnOVp5WFp3dDI1TDQ5dHJHNUlRWGdySGRrUjAxWktTQmxadjhTSDA0WFV5Y2g2YzBITnZTNkRuMTdSeDNKZ0tZTUZxeFNUeWhSWkxkbG1NZ3hDTUJFN3dZRHJ2dGZDUFJnZURQR09SNEZUVlAwNTIxdWxCQW5BZExkbG1NZ3hDTUJFN3dZRHJ2dGZoR1p5bVRtbWpkQ0oyOGthMGZuTjQ3QmVLeW1UbW1qZENKMjhrYTBDUFJnZURQR09SNEZUVlAwNTIxdWxCQW5BZG94dWJveHViNjM=", "options": "imxs" } } }', '', true, true, false, 500);
SELECT documentdb_distributed_test_helpers.gin_bson_get_single_path_generated_terms('{"_id": 13, "a" : { "$regularExpression": { "pattern": "cDg2UFhxR3B5a2dGNHByRjlYMG91R1ZHN21rUkdnOVp5WFp3dDI1TDQ5dHJHNUlRWGdySGRrUjAxWktTQmxadjhTSDA0WFV5Y2g2YzBITnZTNkRuMTdSeDNKZ0tZTUZxeFNUeWhSWkxkbG1NZ3hDTUJFN3dZRHJ2dGZDUFJnZURQR09SNEZUVlAwNTIxdWxCQW5BZExkbG1NZ3hDTUJFN3dZRHJ2dGZoR1p5bVRtbWpkQ0oyOGthMGZuTjQ3QmVLeW1UbW1qZENKMjhrYTBDUFJnZURQR09SNEZUVlAwNTIxdWxCQW5BZG94dWJveHViNjM=", "options": "imxs" } } }', '', true, true, false, 50);

-- codewscope
SELECT documentdb_distributed_test_helpers.gin_bson_get_single_path_generated_terms('{"_id": 13, "a" : { "$code": "cDg2UFhxR3B5a2dGNHByRjlYMG91R1ZHN21rUkdnOVp5WFp3dDI1TDQ5dHJHNUlRWGdySGRrUjAxWktTQmxadjhTSDA0WFV5Y2g2YzBITnZTNkRuMTdSeDNKZ0tZTUZxeFNUeWhSWkxkbG1NZ3hDTUJFN3dZRHJ2dGZDUFJnZURQR09SNEZUVlAwNTIxdWxCQW5BZExkbG1NZ3hDTUJFN3dZRHJ2dGZoR1p5bVRtbWpkQ0oyOGthMGZuTjQ3QmVLeW1UbW1qZENKMjhrYTBDUFJnZURQR09SNEZUVlAwNTIxdWxCQW5BZG94dWJveHViNjM=", "$scope": { "a": "b" } } }', '', true, true, false, 50);

-- dbref
SELECT documentdb_distributed_test_helpers.gin_bson_get_single_path_generated_terms('{"_id": 13, "a" : { "$ref": "cDg2UFhxR3B5a2dGNHByRjlYMG91R1ZHN21rUkdnOVp5WFp3dDI1TDQ5dHJHNUlRWGdySGRrUjAxWktTQmxadjhTSDA0WFV5Y2g2YzBITnZTNkRuMTdSeDNKZ0tZTUZxeFNUeWhSWkxkbG1NZ3hDTUJFN3dZRHJ2dGZDUFJnZURQR09SNEZUVlAwNTIxdWxCQW5BZExkbG1NZ3hDTUJFN3dZRHJ2dGZoR1p5bVRtbWpkQ0oyOGthMGZuTjQ3QmVLeW1UbW1qZENKMjhrYTBDUFJnZURQR09SNEZUVlAwNTIxdWxCQW5BZG94dWJveHViNjM=", "$id": { "a": "b" } } }', '', true, true, false, 50);

-- object
SELECT documentdb_distributed_test_helpers.gin_bson_get_single_path_generated_terms('{"_id": 13, "a" : { "field": { "pattern": "cDg2UFhxR3B5a2dGNHByRjlYMG91R1ZHN21rUkdnOVp5WFp3dDI1TDQ5dHJHNUlRWGdySGRrUjAxWktTQmxadjhTSDA0WFV5Y2g2YzBITnZTNkRuMTdSeDNKZ0tZTUZxeFNUeWhSWkxkbG1NZ3hDTUJFN3dZRHJ2dGZDUFJnZURQR09SNEZUVlAwNTIxdWxCQW5BZExkbG1NZ3hDTUJFN3dZRHJ2dGZoR1p5bVRtbWpkQ0oyOGthMGZuTjQ3QmVLeW1UbW1qZENKMjhrYTBDUFJnZURQR09SNEZUVlAwNTIxdWxCQW5BZG94dWJveHViNjM=", "field2": "short field" } } }', '', true, true, false, 50);

-- array
SELECT documentdb_distributed_test_helpers.gin_bson_get_single_path_generated_terms('{"_id": 13, "a" : [ "cDg2UFhxR3B5a2dGNHByRjlYMG91R1ZHN21rUkdnOVp5WFp3dDI1TDQ5dHJHNUlRWGdySGRrUjAxWktTQmxadjhTSDA0WFV5Y2g2YzBITnZTNkRuMTdSeDNKZ0tZTUZxeFNUeWhSWkxkbG1NZ3hDTUJFN3dZRHJ2dGZDUFJnZURQR09SNEZUVlAwNTIxdWxCQW5BZExkbG1NZ3hDTUJFN3dZRHJ2dGZoR1p5bVRtbWpkQ0oyOGthMGZuTjQ3QmVLeW1UbW1qZENKMjhrYTBDUFJnZURQR09SNEZUVlAwNTIxdWxCQW5BZG94dWJveHViNjM=", "short field" ] }', '', true, true, false, 50);

-- Section for unique index terms
SELECT documentdb_distributed_test_helpers.gin_bson_get_single_path_generated_terms('{"_id":"7", "a" : [ { "c" : 0 }, { "c" : 1 }, { "b" : 2 } ] }', 'a.b', false, true);
SELECT documentdb_distributed_test_helpers.gin_bson_get_single_path_generated_terms('{"_id":"7", "a" : [ { "c" : 0 }, { "c" : 1 }, { "c" : 2 } ] }', 'a.b', false, true);
SELECT documentdb_distributed_test_helpers.gin_bson_get_single_path_generated_terms('{"_id":"7", "a" : [ { "c" : 0 }, { "c" : 1 }, { "b" : null } ] }', 'a.b', false, true);

-- addMetadata true
SELECT documentdb_distributed_test_helpers.gin_bson_get_single_path_generated_terms('{"_id":"7", "a" : [ { "c" : 0 }, { "c" : 1 }, { "b" : 2 } ] }', '', true, false, true);
SELECT documentdb_distributed_test_helpers.gin_bson_get_wildcard_project_generated_terms('{"_id":"7", "a" : [ { "c" : 0 }, { "c" : 1 }, { "b" : 2 } ] }', '[ "a.b" ]', true, false, true);

-- add array ancestor for wildcard
SELECT documentdb_distributed_test_helpers.gin_bson_get_single_path_generated_terms('{"_id":"7", "a" : [ { "b": { "c": 1 } } ] }', 'a', true);

-- test when non exists term generation is off should generate min key root term
BEGIN;
set local documentdb.enableGenerateNonExistsTerm to off;
SELECT documentdb_distributed_test_helpers.gin_bson_get_single_path_generated_terms('{"_id": 1, "a" : 1 }', 'b', true);
SELECT documentdb_distributed_test_helpers.gin_bson_get_single_path_generated_terms('{"_id": 1, "a" : 1 }', 'b', false);
COMMIT;

SELECT documentdb_api.create_collection('db', 'indextermgeneration');


-- create all of these indexes
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', documentdb_distributed_test_helpers.generate_create_index_arg('indextermgeneration', 'indexgen_path_1', '{"$**": 1}'), true);

SELECT documentdb_api_internal.create_indexes_non_concurrently('db', documentdb_distributed_test_helpers.generate_create_index_arg('indextermgeneration', 'indexgen_path_2', '{"a.b.$**": 1}'), true);

SELECT documentdb_api_internal.create_indexes_non_concurrently('db', documentdb_distributed_test_helpers.generate_create_index_arg('indextermgeneration', 'indexgen_path_3', '{"a.b": 1}'), true);

-- create a compound index
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', documentdb_distributed_test_helpers.generate_create_index_arg('indextermgeneration', 'indexgen_comp_path_1', '{"a.b": 1, "a.c": 1}'), true);

SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "indextermgeneration", "indexes": [ { "key": { "$**": 1 }, "name": "indexgen_wildcard_1", "wildcardProjection": { "a.b": 1 } }]}', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "indextermgeneration", "indexes": [ { "key": { "$**": 1 }, "name": "indexgen_wildcard_2", "wildcardProjection": { "a.b": 0, "a.c": 0 }  }]}', true);

\d documentdb_data.documents_3700

-- now insert some documents - we still can't query it at this point but this is a sanity validation.
SELECT documentdb_api.insert_one('db','indextermgeneration', '{"_id": 6, "a" : { "b": 1, "c": 2 }, "d": 1 }', NULL);
SELECT documentdb_api.insert_one('db','indextermgeneration', '{"_id": 10, "a" : { "b" : [ 0, 1, 2 ] } }', NULL);
SELECT documentdb_api.insert_one('db','indextermgeneration', '{"_id":"7", "a" : [ { "c" : 0 }, { "c" : 1 }, { "b" : 2 } ] }', NULL);
SELECT documentdb_api.insert_one('db','indextermgeneration', '{"_id": 13, "a" : [ { "b": [[-1, 1, 2]] }, { "b": [[0, 1, 2]] }, { "b": [[0, 1, 7]] }] }', NULL);
