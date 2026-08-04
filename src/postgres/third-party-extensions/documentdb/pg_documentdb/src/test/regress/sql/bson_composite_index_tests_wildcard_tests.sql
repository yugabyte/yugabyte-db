SET search_path TO documentdb_api,documentdb_core,documentdb_api_catalog;

SET documentdb.next_collection_id TO 5400;
SET documentdb.next_collection_index_id TO 5400;


CREATE OR REPLACE FUNCTION documentdb_test_helpers.gin_bson_get_composite_path_wildcard_generated_terms(document documentdb_core.bson, pathSpec text, termLimit int4, addMetadata bool, wildcardIndex int4)
    RETURNS SETOF documentdb_core.bson LANGUAGE C IMMUTABLE PARALLEL SAFE STRICT AS '$libdir/pg_documentdb',
$$gin_bson_get_composite_path_generated_terms$$;

SELECT documentdb_api.create_collection('compdb', 'compwildcard');

-- Simple failure paths:
SELECT documentdb_api_internal.create_indexes_non_concurrently('compdb', '{ "createIndexes": "compwildcard", "indexes": [ { "key": { "a.$**": 1 }, "name": "a_1", "enableOrderedIndex": true }]}', TRUE);
SELECT documentdb_api_internal.create_indexes_non_concurrently('compdb', '{ "createIndexes": "compwildcard", "indexes": [ { "key": { "$**": 1 }, "name": "$**_1", "enableOrderedIndex": true }]}', TRUE);
SELECT documentdb_api_internal.create_indexes_non_concurrently('compdb', '{ "createIndexes": "compwildcard", "indexes": [ { "key": { "$**": -1 }, "name": "$**_-1", "enableOrderedIndex": true }]}', TRUE);
SELECT documentdb_api_internal.create_indexes_non_concurrently('compdb', '{ "createIndexes": "compwildcard", "indexes": [ { "key": { "a.$**": -1 }, "name": "a_-1", "enableOrderedIndex": true }]}', TRUE);

-- with unique
SELECT documentdb_api_internal.create_indexes_non_concurrently('compdb', '{ "createIndexes": "compwildcard", "indexes": [ { "key": { "a.$**": 1 }, "name": "a_1", "enableOrderedIndex": true, "unique": true }]}', TRUE);
SELECT documentdb_api_internal.create_indexes_non_concurrently('compdb', '{ "createIndexes": "compwildcard", "indexes": [ { "key": { "$**": 1 }, "name": "$**_1", "enableOrderedIndex": true, "unique": true }]}', TRUE);
SELECT documentdb_api_internal.create_indexes_non_concurrently('compdb', '{ "createIndexes": "compwildcard", "indexes": [ { "key": { "$**": -1 }, "name": "$**_-1", "enableOrderedIndex": true, "unique": true }]}', TRUE);
SELECT documentdb_api_internal.create_indexes_non_concurrently('compdb', '{ "createIndexes": "compwildcard", "indexes": [ { "key": { "a.$**": -1 }, "name": "a_-1", "enableOrderedIndex": true, "unique": true }]}', TRUE);

-- with composite
SELECT documentdb_api_internal.create_indexes_non_concurrently('compdb', '{ "createIndexes": "compwildcard", "indexes": [ { "key": { "a.$**": 1, "b": 1 }, "name": "a_1", "enableOrderedIndex": true }]}', TRUE);
SELECT documentdb_api_internal.create_indexes_non_concurrently('compdb', '{ "createIndexes": "compwildcard", "indexes": [ { "key": { "a.$**": 1, "b.$**": 1 }, "name": "a_1", "enableOrderedIndex": true }]}', TRUE);
SELECT documentdb_api_internal.create_indexes_non_concurrently('compdb', '{ "createIndexes": "compwildcard", "indexes": [ { "key": { "$**": 1, "b": 1 }, "name": "a_1", "enableOrderedIndex": true }]}', TRUE);
SELECT documentdb_api_internal.create_indexes_non_concurrently('compdb', '{ "createIndexes": "compwildcard", "indexes": [ { "key": { "$**": 1, "b.$**": 1 }, "name": "a_1", "enableOrderedIndex": true }]}', TRUE);

set documentdb.enableCompositeWildcardIndex to on;
set documentdb.enableExtendedExplainPlans to on;

-- now we can create the ones that are valid - invalid cases still fail
SELECT documentdb_api_internal.create_indexes_non_concurrently('compdb', '{ "createIndexes": "compwildcard", "indexes": [ { "key": { "a.$**": 1 }, "name": "a_1", "enableOrderedIndex": true }]}', TRUE);
SELECT documentdb_api_internal.create_indexes_non_concurrently('compdb', '{ "createIndexes": "compwildcard", "indexes": [ { "key": { "$**": 1 }, "name": "$**_1", "enableOrderedIndex": true }]}', TRUE);
SELECT documentdb_api_internal.create_indexes_non_concurrently('compdb', '{ "createIndexes": "compwildcard", "indexes": [ { "key": { "$**": -1 }, "name": "$**_-1", "enableOrderedIndex": true }]}', TRUE);
SELECT documentdb_api_internal.create_indexes_non_concurrently('compdb', '{ "createIndexes": "compwildcard", "indexes": [ { "key": { "a.$**": -1 }, "name": "a_-1", "enableOrderedIndex": true }]}', TRUE);

-- with unique (still fails)
SELECT documentdb_api_internal.create_indexes_non_concurrently('compdb', '{ "createIndexes": "compwildcard", "indexes": [ { "key": { "a.$**": 1 }, "name": "a_1", "enableOrderedIndex": true, "unique": true }]}', TRUE);
SELECT documentdb_api_internal.create_indexes_non_concurrently('compdb', '{ "createIndexes": "compwildcard", "indexes": [ { "key": { "$**": 1 }, "name": "$**_1", "enableOrderedIndex": true, "unique": true }]}', TRUE);
SELECT documentdb_api_internal.create_indexes_non_concurrently('compdb', '{ "createIndexes": "compwildcard", "indexes": [ { "key": { "$**": -1 }, "name": "$**_-1", "enableOrderedIndex": true, "unique": true }]}', TRUE);
SELECT documentdb_api_internal.create_indexes_non_concurrently('compdb', '{ "createIndexes": "compwildcard", "indexes": [ { "key": { "a.$**": -1 }, "name": "a_-1", "enableOrderedIndex": true, "unique": true }]}', TRUE);

-- with composite (still fails)
SELECT documentdb_api_internal.create_indexes_non_concurrently('compdb', '{ "createIndexes": "compwildcard", "indexes": [ { "key": { "a.$**": 1, "b": 1 }, "name": "a_1", "enableOrderedIndex": true }]}', TRUE);
SELECT documentdb_api_internal.create_indexes_non_concurrently('compdb', '{ "createIndexes": "compwildcard", "indexes": [ { "key": { "a.$**": 1, "b.$**": 1 }, "name": "a_1", "enableOrderedIndex": true }]}', TRUE);
SELECT documentdb_api_internal.create_indexes_non_concurrently('compdb', '{ "createIndexes": "compwildcard", "indexes": [ { "key": { "$**": 1, "b": 1 }, "name": "a_1", "enableOrderedIndex": true }]}', TRUE);
SELECT documentdb_api_internal.create_indexes_non_concurrently('compdb', '{ "createIndexes": "compwildcard", "indexes": [ { "key": { "$**": 1, "b.$**": 1 }, "name": "a_1", "enableOrderedIndex": true }]}', TRUE);

-- check the table def
\d documentdb_data.documents_5401
CALL documentdb_api.drop_indexes('compdb', '{ "dropIndexes": "compwildcard", "index": "$**_-1" }');
CALL documentdb_api.drop_indexes('compdb', '{ "dropIndexes": "compwildcard", "index": "a_-1" }');
CALL documentdb_api.drop_indexes('compdb', '{ "dropIndexes": "compwildcard", "index": "$**_1" }');

-- start testing term generation: Path with wildcard
SELECT * FROM documentdb_test_helpers.gin_bson_get_composite_path_wildcard_generated_terms('{ "a": 1, "b": 1 }', '[ "a" ]', 2600, true, 0);
SELECT * FROM documentdb_test_helpers.gin_bson_get_composite_path_wildcard_generated_terms('{ "a": null, "b": "foo" }', '[ "a" ]', 2600, true, 0);
SELECT * FROM documentdb_test_helpers.gin_bson_get_composite_path_wildcard_generated_terms('{ "b": "foo" }', '[ "a" ]', 2600, true, 0);
SELECT * FROM documentdb_test_helpers.gin_bson_get_composite_path_wildcard_generated_terms('{ "a": [1, 2, 3 ], "b": "foo" }', '[ "a" ]', 2600, true, 0);
SELECT * FROM documentdb_test_helpers.gin_bson_get_composite_path_wildcard_generated_terms('{ "a": { "b": 1 }, "b": "foo" }', '[ "a" ]', 2600, true, 0);
SELECT * FROM documentdb_test_helpers.gin_bson_get_composite_path_wildcard_generated_terms('{ "a": { "b": [ 1, 2, 3 ]}, "b": "foo" }', '[ "a" ]', 2600, true, 0);
SELECT * FROM documentdb_test_helpers.gin_bson_get_composite_path_wildcard_generated_terms('{ "a": { }, "b": "foo" }', '[ "a" ]', 2600, true, 0);
SELECT * FROM documentdb_test_helpers.gin_bson_get_composite_path_wildcard_generated_terms('{ "a": [ ], "b": "foo" }', '[ "a" ]', 2600, true, 0);
SELECT * FROM documentdb_test_helpers.gin_bson_get_composite_path_wildcard_generated_terms('{ "a": { "c": { "d": 1 } }, "b": "foo" }', '[ "a" ]', 2600, true, 0);
SELECT * FROM documentdb_test_helpers.gin_bson_get_composite_path_wildcard_generated_terms('{ "a": { "c": [ { "d": 1 }, { "e": 2 } ] }, "b": "foo" }', '[ "a" ]', 2600, true, 0);
SELECT * FROM documentdb_test_helpers.gin_bson_get_composite_path_wildcard_generated_terms('{ "a": [ { "c": { "d": 1, "e": 1 } }, { "f": 1 }], "b": "foo" }', '[ "a" ]', 2600, true, 0);
SELECT * FROM documentdb_test_helpers.gin_bson_get_composite_path_wildcard_generated_terms('{ "a": [ { "c": { "d": [], "e": {} } }, { "f": 1 }], "b": "foo" }', '[ "a" ]', 2600, true, 0);


-- start testing term generation: root wildcard
SELECT * FROM documentdb_test_helpers.gin_bson_get_composite_path_wildcard_generated_terms('{ "a": 1, "b": 1 }', '[ "" ]', 2600, true, 0);
SELECT * FROM documentdb_test_helpers.gin_bson_get_composite_path_wildcard_generated_terms('{  }', '[ "" ]', 2600, true, 0);
SELECT * FROM documentdb_test_helpers.gin_bson_get_composite_path_wildcard_generated_terms('{ "a": [1, 2, 3 ], "b": "foo" }', '[ "" ]', 2600, true, 0);
SELECT * FROM documentdb_test_helpers.gin_bson_get_composite_path_wildcard_generated_terms('{ "a": "foo", "b": [ 1, 2, 3 ] }', '[ "" ]', 2600, true, 0);
SELECT * FROM documentdb_test_helpers.gin_bson_get_composite_path_wildcard_generated_terms('{ "a": [ 1, 2, 3 ], "b": [ 1, 2, 3 ] }', '[ "" ]', 2600, true, 0);
SELECT * FROM documentdb_test_helpers.gin_bson_get_composite_path_wildcard_generated_terms('{ "a": { "c": 1 }, "b": { "c": 1 } }', '[ "" ]', 2600, true, 0);
SELECT * FROM documentdb_test_helpers.gin_bson_get_composite_path_wildcard_generated_terms('{ "a": { "c": [ 1, 2 ] }, "b": { "c": [ 1, 2 ] } }', '[ "" ]', 2600, true, 0);
SELECT * FROM documentdb_test_helpers.gin_bson_get_composite_path_wildcard_generated_terms('{ "a": {  }, "b": {  } }', '[ "" ]', 2600, true, 0);
SELECT * FROM documentdb_test_helpers.gin_bson_get_composite_path_wildcard_generated_terms('{ "a": [ ], "b": [ ] }', '[ "" ]', 2600, true, 0);
SELECT * FROM documentdb_test_helpers.gin_bson_get_composite_path_wildcard_generated_terms('{ "a": { "c": { "d": 1 } }, "b": { "c": { "d": 1 } } }', '[ "" ]', 2600, true, 0);
SELECT * FROM documentdb_test_helpers.gin_bson_get_composite_path_wildcard_generated_terms('{ "a": { "c": [ { "d": 1 }, { "e": 2 } ] }, "b": "foo" }', '[ "" ]', 2600, true, 0);
SELECT * FROM documentdb_test_helpers.gin_bson_get_composite_path_wildcard_generated_terms('{ "a": [ { "c": { "d": 1, "e": 1 } }, { "f": 1 }], "b": "foo" }', '[ "" ]', 2600, true, 0);
SELECT * FROM documentdb_test_helpers.gin_bson_get_composite_path_wildcard_generated_terms('{ "a": [ { "c": { "d": [], "e": {} } }, { "f": 1 }], "b": "foo" }', '[ "a" ]', 2600, true, 0);

-- now insert into the table the documents above
SELECT documentdb_api.insert_one('compdb', 'compwildcard', '{ "_id": 1, "a": 1, "b": 1 }');
SELECT documentdb_api.insert_one('compdb', 'compwildcard', '{ "_id": 2, "a": null, "b": "foo" }');
SELECT documentdb_api.insert_one('compdb', 'compwildcard', '{ "_id": 3, "b": "foo" }');
SELECT documentdb_api.insert_one('compdb', 'compwildcard', '{ "_id": 4, "a": [1, 2, 3 ], "b": "foo" }');
SELECT documentdb_api.insert_one('compdb', 'compwildcard', '{ "_id": 5, "a": { "b": 1 }, "b": "foo" }');
SELECT documentdb_api.insert_one('compdb', 'compwildcard', '{ "_id": 6, "a": { "b": [ 1, 2, 3 ]}, "b": "foo" }');
SELECT documentdb_api.insert_one('compdb', 'compwildcard', '{ "_id": 7, "a": { }, "b": "foo" }');
SELECT documentdb_api.insert_one('compdb', 'compwildcard', '{ "_id": 8, "a": [ ], "b": "foo" }');
SELECT documentdb_api.insert_one('compdb', 'compwildcard', '{ "_id": 9, "a": { "c": { "d": 1 } }, "b": "foo" }');
SELECT documentdb_api.insert_one('compdb', 'compwildcard', '{ "_id": 10, "a": { "c": [ { "d": 1 }, { "e": 2 } ] }, "b": "foo" }');
SELECT documentdb_api.insert_one('compdb', 'compwildcard', '{ "_id": 11, "a": [ { "c": { "d": 1, "e": 1 } }, { "f": 1 }], "b": "foo" }');
SELECT documentdb_api.insert_one('compdb', 'compwildcard', '{ "_id": 12, "a": [ { "c": { "d": [], "e": {} } }, { "f": 1 }], "b": "foo" }');
SELECT documentdb_api.insert_one('compdb', 'compwildcard', '{ "_id": 13, "a": { "b": 1 } }');

-- test queries are not pushed to the wildcard index.
\d documentdb_data.documents_5401
SELECT documentdb_test_helpers.run_explain_and_trim($cmd$
    EXPLAIN (COSTS OFF) SELECT document FROM bson_aggregation_find('compdb', '{ "find": "compwildcard", "filter": { "b": 1 }}') $cmd$);
SELECT documentdb_test_helpers.run_explain_and_trim($cmd$
    EXPLAIN (COSTS OFF) SELECT document FROM bson_aggregation_find('compdb', '{ "find": "compwildcard", "filter": { "ab": 1 }}') $cmd$);

SELECT documentdb_test_helpers.run_explain_and_trim($cmd$
    EXPLAIN (COSTS OFF) SELECT document FROM bson_aggregation_find('compdb', '{ "find": "compwildcard", "filter": { "a/b": 1 }}') $cmd$);

SELECT documentdb_test_helpers.run_explain_and_trim($cmd$
    EXPLAIN (COSTS OFF) SELECT document FROM bson_aggregation_find('compdb', '{ "find": "compwildcard", "filter": { "aa": 1 }}') $cmd$);

set documentdb.forceDisableSeqScan to on;
SELECT document FROM bson_aggregation_find('compdb', '{ "find": "compwildcard", "filter": { "b": 1 }}');

-- these queries get pushed to the wildcard index.
SELECT document FROM bson_aggregation_find('compdb', '{ "find": "compwildcard", "filter": { "a": 1 }}');
SELECT document FROM bson_aggregation_find('compdb', '{ "find": "compwildcard", "filter": { "a.b": 1 }}');

-- queries on arrays don't get pushed for negation
SELECT document FROM bson_aggregation_find('compdb', '{ "find": "compwildcard", "filter": { "a": { "$gt": 1 } }}');
SELECT document FROM bson_aggregation_find('compdb', '{ "find": "compwildcard", "filter": { "a": { "$in": [ 1, 3 ] } }}');
SELECT document FROM bson_aggregation_find('compdb', '{ "find": "compwildcard", "filter": { "a.b": { "$in": [ 1, 3 ] } }}');

-- shows documents and arrays
SELECT document FROM bson_aggregation_find('compdb', '{ "find": "compwildcard", "filter": { "a": { "$exists": true } }}');
SELECT document FROM bson_aggregation_find('compdb', '{ "find": "compwildcard", "filter": { "a.b": { "$exists": true } }}');

-- cannot push down
SELECT document FROM bson_aggregation_find('compdb', '{ "find": "compwildcard", "filter": { "a": { "$eq": { "b": 1 } } }}');
SELECT document FROM bson_aggregation_find('compdb', '{ "find": "compwildcard", "filter": { "a": { "$gt": { "b": 1 } } }}');
SELECT document FROM bson_aggregation_find('compdb', '{ "find": "compwildcard", "filter": { "a": { "$exists": false } }}');
SELECT document FROM bson_aggregation_find('compdb', '{ "find": "compwildcard", "filter": { "a": { "$in": [ 1, null ] } }}');
SELECT document FROM bson_aggregation_find('compdb', '{ "find": "compwildcard", "filter": { "a": { "$in": [ 1, { "b": 1 } ] } }}');
SELECT document FROM bson_aggregation_find('compdb', '{ "find": "compwildcard", "filter": { "a.b": { "$eq": { "b": 1 } } }}');
SELECT document FROM bson_aggregation_find('compdb', '{ "find": "compwildcard", "filter": { "a.b": { "$gt": { "b": 1 } } }}');
SELECT document FROM bson_aggregation_find('compdb', '{ "find": "compwildcard", "filter": { "a.b": { "$exists": false } }}');
SELECT document FROM bson_aggregation_find('compdb', '{ "find": "compwildcard", "filter": { "a.b": { "$in": [ 1, null ] } }}');
SELECT document FROM bson_aggregation_find('compdb', '{ "find": "compwildcard", "filter": { "a.b": { "$in": [ 1, { "b": 1 } ] } }}');

SELECT documentdb_test_helpers.run_explain_and_trim($cmd$
    EXPLAIN (COSTS OFF, ANALYZE ON, SUMMARY OFF, BUFFERS OFF, TIMING OFF) SELECT document FROM bson_aggregation_find('compdb', '{ "find": "compwildcard", "filter": { "a": { "$exists": true } }}') $cmd$);

SELECT documentdb_test_helpers.run_explain_and_trim($cmd$
    EXPLAIN (COSTS OFF, ANALYZE ON, SUMMARY OFF, BUFFERS OFF, TIMING OFF) SELECT document FROM bson_aggregation_find('compdb', '{ "find": "compwildcard", "filter": { "a.b": { "$exists": true } }}') $cmd$);

-- operator validation: $eq
SELECT document FROM bson_aggregation_find('compdb', '{ "find": "compwildcard", "filter": { "a": { "$eq": 1 } }}');
SELECT document FROM bson_aggregation_find('compdb', '{ "find": "compwildcard", "filter": { "a": { "$eq": 2 } }}');
SELECT document FROM bson_aggregation_find('compdb', '{ "find": "compwildcard", "filter": { "a.b": { "$eq": 1 } }}');
SELECT document FROM bson_aggregation_find('compdb', '{ "find": "compwildcard", "filter": { "a.b": { "$eq": 2 } }}');
SELECT document FROM bson_aggregation_find('compdb', '{ "find": "compwildcard", "filter": { "a.c.d": { "$eq": 1 } }}');
SELECT document FROM bson_aggregation_find('compdb', '{ "find": "compwildcard", "filter": { "a.c.e": { "$eq": 2 } }}');
SELECT document FROM bson_aggregation_find('compdb', '{ "find": "compwildcard", "filter": { "a.f": { "$eq": 1 } }}');

-- pushes down with runtime recheck (becomes "a": { "$eq": 1 } )
SELECT document FROM bson_aggregation_find('compdb', '{ "find": "compwildcard", "filter": { "a": { "$eq": [1, 2, 3 ] } }}');

-- pushes down with runtime recheck (becomes "a.b": { "$eq": 1 } )
SELECT document FROM bson_aggregation_find('compdb', '{ "find": "compwildcard", "filter": { "a.b": { "$eq": [ 1, 2, 3 ] } }}');

-- this one won't push down since we don't index documents.
SELECT document FROM bson_aggregation_find('compdb', '{ "find": "compwildcard", "filter": { "a": { "$eq": [{ "b": 1 }, 2, 3 ] } }}');

-- can't push down
SELECT document FROM bson_aggregation_find('compdb', '{ "find": "compwildcard", "filter": { "a": { "$eq": null } }}');
SELECT document FROM bson_aggregation_find('compdb', '{ "find": "compwildcard", "filter": { "a.0": { "$eq": 1 } }}');
SELECT document FROM bson_aggregation_find('compdb', '{ "find": "compwildcard", "filter": { "a.0.b": { "$eq": 1 } }}');
SELECT document FROM bson_aggregation_find('compdb', '{ "find": "compwildcard", "filter": { "a.b.0": { "$eq": 1 } }}');
SELECT document FROM bson_aggregation_find('compdb', '{ "find": "compwildcard", "filter": { "a.c": { "$eq": { "d": 1 } } }}');
SELECT document FROM bson_aggregation_find('compdb', '{ "find": "compwildcard", "filter": { "a.c": { "$eq": [ { "d": 1 }, { "e": 1 }] } }}');
SELECT document FROM bson_aggregation_find('compdb', '{ "find": "compwildcard", "filter": { "b": { "$eq": "foo" } }}');

-- similar to the above, but for $in
SELECT document FROM bson_aggregation_find('compdb', '{ "find": "compwildcard", "filter": { "a": { "$in": [ 1, 2 ] } }}');
SELECT document FROM bson_aggregation_find('compdb', '{ "find": "compwildcard", "filter": { "a.b": { "$in": [ 1, 2 ] } }}');
SELECT document FROM bson_aggregation_find('compdb', '{ "find": "compwildcard", "filter": { "a.c.d": { "$in": [ 1, 5 ] } }}');
SELECT document FROM bson_aggregation_find('compdb', '{ "find": "compwildcard", "filter": { "a.c.e": { "$in": [ 2, 6 ] } }}');
SELECT document FROM bson_aggregation_find('compdb', '{ "find": "compwildcard", "filter": { "a.f": { "$in": [ 1, -1 ] } }}');

-- these are valid
SELECT document FROM bson_aggregation_find('compdb', '{ "find": "compwildcard", "filter": { "a": { "$in": [ [1, 2, 3 ], 99 ] } }}');
SELECT document FROM bson_aggregation_find('compdb', '{ "find": "compwildcard", "filter": { "a.b": { "$in": [ [1, 2, 3 ], 99 ] } }}');

-- this is not valid
SELECT document FROM bson_aggregation_find('compdb', '{ "find": "compwildcard", "filter": { "a": { "$in": [ [{ "b": 1 }, 2, 3 ], 99 ] } }}');

SELECT document FROM bson_aggregation_find('compdb', '{ "find": "compwildcard", "filter": { "a": { "$in": [ null, 2 ] } }}');
SELECT document FROM bson_aggregation_find('compdb', '{ "find": "compwildcard", "filter": { "a.0": { "$in": [ 1, 2 ] } }}');
SELECT document FROM bson_aggregation_find('compdb', '{ "find": "compwildcard", "filter": { "a.0.b": { "$in": [ 1, 2 ] } }}');
SELECT document FROM bson_aggregation_find('compdb', '{ "find": "compwildcard", "filter": { "a.b.0": { "$in": [ 1, 3 ] } }}');
SELECT document FROM bson_aggregation_find('compdb', '{ "find": "compwildcard", "filter": { "a.c": { "$in": [ { "d": 1 }, { "e": 1 } ] } }}');
SELECT document FROM bson_aggregation_find('compdb', '{ "find": "compwildcard", "filter": { "a.c": { "$in": [ [ { "d": 1 }, { "e": 1 }], 2 ]} }}');
SELECT document FROM bson_aggregation_find('compdb', '{ "find": "compwildcard", "filter": { "b": { "$in": [ "foo", "bar" ] } }}');

-- repeat these for root wildcard
CALL documentdb_api.drop_indexes('compdb', '{ "dropIndexes": "compwildcard", "index": "a_1" }');
SELECT documentdb_api_internal.create_indexes_non_concurrently('compdb', '{ "createIndexes": "compwildcard", "indexes": [ { "key": { "$**": 1 }, "name": "$**_1", "enableOrderedIndex": true }]}', TRUE);

-- can push this down now:
SELECT document FROM bson_aggregation_find('compdb', '{ "find": "compwildcard", "filter": { "b": 1 }}');

SELECT document FROM bson_aggregation_find('compdb', '{ "find": "compwildcard", "filter": { "a": 1 }}');
SELECT document FROM bson_aggregation_find('compdb', '{ "find": "compwildcard", "filter": { "a.b": 1 }}');

-- queries on arrays don't get pushed for negation
SELECT document FROM bson_aggregation_find('compdb', '{ "find": "compwildcard", "filter": { "a": { "$gt": 1 } }}');
SELECT document FROM bson_aggregation_find('compdb', '{ "find": "compwildcard", "filter": { "a": { "$in": [ 1, 3 ] } }}');
SELECT document FROM bson_aggregation_find('compdb', '{ "find": "compwildcard", "filter": { "a.b": { "$in": [ 1, 3 ] } }}');

-- shows documents and arrays
SELECT document FROM bson_aggregation_find('compdb', '{ "find": "compwildcard", "filter": { "a": { "$exists": true } }}');
SELECT document FROM bson_aggregation_find('compdb', '{ "find": "compwildcard", "filter": { "a.b": { "$exists": true } }}');

-- cannot push down
SELECT document FROM bson_aggregation_find('compdb', '{ "find": "compwildcard", "filter": { "a": { "$eq": { "b": 1 } } }}');
SELECT document FROM bson_aggregation_find('compdb', '{ "find": "compwildcard", "filter": { "a": { "$gt": { "b": 1 } } }}');
SELECT document FROM bson_aggregation_find('compdb', '{ "find": "compwildcard", "filter": { "a": { "$exists": false } }}');
SELECT document FROM bson_aggregation_find('compdb', '{ "find": "compwildcard", "filter": { "a": { "$in": [ 1, null ] } }}');
SELECT document FROM bson_aggregation_find('compdb', '{ "find": "compwildcard", "filter": { "a": { "$in": [ 1, { "b": 1 } ] } }}');
SELECT document FROM bson_aggregation_find('compdb', '{ "find": "compwildcard", "filter": { "a.b": { "$eq": { "b": 1 } } }}');
SELECT document FROM bson_aggregation_find('compdb', '{ "find": "compwildcard", "filter": { "a.b": { "$gt": { "b": 1 } } }}');
SELECT document FROM bson_aggregation_find('compdb', '{ "find": "compwildcard", "filter": { "a.b": { "$exists": false } }}');
SELECT document FROM bson_aggregation_find('compdb', '{ "find": "compwildcard", "filter": { "a.b": { "$in": [ 1, null ] } }}');
SELECT document FROM bson_aggregation_find('compdb', '{ "find": "compwildcard", "filter": { "a.b": { "$in": [ 1, { "b": 1 } ] } }}');

SELECT documentdb_test_helpers.run_explain_and_trim($cmd$
    EXPLAIN (COSTS OFF, ANALYZE ON, SUMMARY OFF, BUFFERS OFF, TIMING OFF) SELECT document FROM bson_aggregation_find('compdb', '{ "find": "compwildcard", "filter": { "a": { "$exists": true } }}') $cmd$);

SELECT documentdb_test_helpers.run_explain_and_trim($cmd$
    EXPLAIN (COSTS OFF, ANALYZE ON, SUMMARY OFF, BUFFERS OFF, TIMING OFF) SELECT document FROM bson_aggregation_find('compdb', '{ "find": "compwildcard", "filter": { "a.b": { "$exists": true } }}') $cmd$);

-- operator validation: $eq
SELECT document FROM bson_aggregation_find('compdb', '{ "find": "compwildcard", "filter": { "a": { "$eq": 1 } }}');
SELECT document FROM bson_aggregation_find('compdb', '{ "find": "compwildcard", "filter": { "a": { "$eq": 2 } }}');
SELECT document FROM bson_aggregation_find('compdb', '{ "find": "compwildcard", "filter": { "a.b": { "$eq": 1 } }}');
SELECT document FROM bson_aggregation_find('compdb', '{ "find": "compwildcard", "filter": { "a.b": { "$eq": 2 } }}');
SELECT document FROM bson_aggregation_find('compdb', '{ "find": "compwildcard", "filter": { "a.c.d": { "$eq": 1 } }}');
SELECT document FROM bson_aggregation_find('compdb', '{ "find": "compwildcard", "filter": { "a.c.e": { "$eq": 2 } }}');
SELECT document FROM bson_aggregation_find('compdb', '{ "find": "compwildcard", "filter": { "a.f": { "$eq": 1 } }}');

-- pushes down with runtime recheck (becomes "a": { "$eq": 1 } )
SELECT document FROM bson_aggregation_find('compdb', '{ "find": "compwildcard", "filter": { "a": { "$eq": [1, 2, 3 ] } }}');

-- pushes down with runtime recheck (becomes "a.b": { "$eq": 1 } )
SELECT document FROM bson_aggregation_find('compdb', '{ "find": "compwildcard", "filter": { "a.b": { "$eq": [ 1, 2, 3 ] } }}');

-- this one won't push down since we don't index documents.
SELECT document FROM bson_aggregation_find('compdb', '{ "find": "compwildcard", "filter": { "a": { "$eq": [{ "b": 1 }, 2, 3 ] } }}');

-- can't push down
SELECT document FROM bson_aggregation_find('compdb', '{ "find": "compwildcard", "filter": { "a": { "$eq": null } }}');
SELECT document FROM bson_aggregation_find('compdb', '{ "find": "compwildcard", "filter": { "a.0": { "$eq": 1 } }}');
SELECT document FROM bson_aggregation_find('compdb', '{ "find": "compwildcard", "filter": { "a.0.b": { "$eq": 1 } }}');
SELECT document FROM bson_aggregation_find('compdb', '{ "find": "compwildcard", "filter": { "a.b.0": { "$eq": 1 } }}');
SELECT document FROM bson_aggregation_find('compdb', '{ "find": "compwildcard", "filter": { "a.c": { "$eq": { "d": 1 } } }}');
SELECT document FROM bson_aggregation_find('compdb', '{ "find": "compwildcard", "filter": { "a.c": { "$eq": [ { "d": 1 }, { "e": 1 }] } }}');
SELECT document FROM bson_aggregation_find('compdb', '{ "find": "compwildcard", "filter": { "b": { "$eq": "foo" } }}');

-- similar to the above, but for $in
SELECT document FROM bson_aggregation_find('compdb', '{ "find": "compwildcard", "filter": { "a": { "$in": [ 1, 2 ] } }}');
SELECT document FROM bson_aggregation_find('compdb', '{ "find": "compwildcard", "filter": { "a.b": { "$in": [ 1, 2 ] } }}');
SELECT document FROM bson_aggregation_find('compdb', '{ "find": "compwildcard", "filter": { "a.c.d": { "$in": [ 1, 5 ] } }}');
SELECT document FROM bson_aggregation_find('compdb', '{ "find": "compwildcard", "filter": { "a.c.e": { "$in": [ 2, 6 ] } }}');
SELECT document FROM bson_aggregation_find('compdb', '{ "find": "compwildcard", "filter": { "a.f": { "$in": [ 1, -1 ] } }}');

-- these are valid
SELECT document FROM bson_aggregation_find('compdb', '{ "find": "compwildcard", "filter": { "a": { "$in": [ [1, 2, 3 ], 99 ] } }}');
SELECT document FROM bson_aggregation_find('compdb', '{ "find": "compwildcard", "filter": { "a.b": { "$in": [ [1, 2, 3 ], 99 ] } }}');
SELECT document FROM bson_aggregation_find('compdb', '{ "find": "compwildcard", "filter": { "b": { "$in": [ "foo", "bar" ] } }}');

-- this is not valid
SELECT document FROM bson_aggregation_find('compdb', '{ "find": "compwildcard", "filter": { "a": { "$in": [ [{ "b": 1 }, 2, 3 ], 99 ] } }}');

SELECT document FROM bson_aggregation_find('compdb', '{ "find": "compwildcard", "filter": { "a": { "$in": [ null, 2 ] } }}');
SELECT document FROM bson_aggregation_find('compdb', '{ "find": "compwildcard", "filter": { "a.0": { "$in": [ 1, 2 ] } }}');
SELECT document FROM bson_aggregation_find('compdb', '{ "find": "compwildcard", "filter": { "a.0.b": { "$in": [ 1, 2 ] } }}');
SELECT document FROM bson_aggregation_find('compdb', '{ "find": "compwildcard", "filter": { "a.b.0": { "$in": [ 1, 3 ] } }}');
SELECT document FROM bson_aggregation_find('compdb', '{ "find": "compwildcard", "filter": { "a.c": { "$in": [ { "d": 1 }, { "e": 1 } ] } }}');
SELECT document FROM bson_aggregation_find('compdb', '{ "find": "compwildcard", "filter": { "a.c": { "$in": [ [ { "d": 1 }, { "e": 1 }], 2 ]} }}');

-- with a root wildcard, generate a term that *only* has sub-terms (this only generates a.b.c = 1)
SELECT * FROM documentdb_test_helpers.gin_bson_get_composite_path_wildcard_generated_terms('{ "h": { "b": { "c": 1 } } }', '[ "" ]', 2600, true, 0);
SELECT documentdb_api.insert_one('compdb', 'compwildcard', '{ "_id": 14, "h": { "b": { "c": 1 } } }');
SELECT documentdb_api.insert_one('compdb', 'compwildcard', '{ "_id": 15, "h": { "c": { "c": 1 } } }');
SELECT documentdb_api.insert_one('compdb', 'compwildcard', '{ "_id": 16, "h": { "c": { "e": 1 } } }');
SELECT documentdb_api.insert_one('compdb', 'compwildcard', '{ "_id": 17, "i": { "c": { "e": 1 } } }');

SELECT document FROM bson_aggregation_find('compdb', '{ "find": "compwildcard", "filter": { "h": { "$exists": true } }}');
SELECT document FROM bson_aggregation_find('compdb', '{ "find": "compwildcard", "filter": { "h.b": { "$exists": true } }}');
SELECT document FROM bson_aggregation_find('compdb', '{ "find": "compwildcard", "filter": { "h.b.c": { "$exists": true } }}');

SELECT document FROM bson_aggregation_find('compdb', '{ "find": "compwildcard", "filter": { "h": { "$gt": { "$minKey": 1 } } }}');
SELECT document FROM bson_aggregation_find('compdb', '{ "find": "compwildcard", "filter": { "h.b": { "$gt": { "$minKey": 1 } } }}');
SELECT document FROM bson_aggregation_find('compdb', '{ "find": "compwildcard", "filter": { "h.b.c": { "$gt": { "$minKey": 1 } } }}');

-- but this doesn't apply for inequality operators
SELECT document FROM bson_aggregation_find('compdb', '{ "find": "compwildcard", "filter": { "h.b": { "$gte": 1 } }}');
SELECT document FROM bson_aggregation_find('compdb', '{ "find": "compwildcard", "filter": { "h.b.c": { "$gte": 0 } }}');

-- since the index orders by path "string" - inserting a path that comes before the dotted path shouldn't stop traversal.
SELECT documentdb_test_helpers.run_explain_and_trim($cmd$
    EXPLAIN (COSTS OFF, ANALYZE ON, SUMMARY OFF, BUFFERS OFF, TIMING OFF) SELECT document FROM bson_aggregation_find('compdb', '{ "find": "compwildcard", "filter": { "h.b": { "$exists": true } }}') $cmd$);

SELECT documentdb_api.insert_one('compdb', 'compwildcard', '{ "_id": 18, "h": { "b!c": 1 } }');
SELECT documentdb_api.insert_one('compdb', 'compwildcard', '{ "_id": 19, "h": { "b": { "d": 1 } } }');
SELECT document FROM bson_aggregation_find('compdb', '{ "find": "compwildcard", "filter": { "h": { "$exists": true } }}');
SELECT document FROM bson_aggregation_find('compdb', '{ "find": "compwildcard", "filter": { "h.b": { "$exists": true } }}');
SELECT document FROM bson_aggregation_find('compdb', '{ "find": "compwildcard", "filter": { "h.b.c": { "$exists": true } }}');

SELECT document FROM bson_aggregation_find('compdb', '{ "find": "compwildcard", "filter": { "h.b": { "$lte": { "$maxKey": 1 } } }}');

-- testing non-array merge of paths
SELECT documentdb_api.insert_one('compdb', 'compwildcard2', '{ "a": { "b": 1, "c": 5 }}');
SELECT documentdb_api.insert_one('compdb', 'compwildcard2', '{ "a": { "b": 1, "c": 6 }}');
SELECT documentdb_api.insert_one('compdb', 'compwildcard2', '{ "a": { "b": 2, "c": 6 }}');
SELECT documentdb_api.insert_one('compdb', 'compwildcard2', '{ "a": { "b": 2, "c": 7 }}');
SELECT documentdb_api.insert_one('compdb', 'compwildcard2', '{ "a": { "b": 3, "c": 7 }}');
SELECT documentdb_api_internal.create_indexes_non_concurrently('compdb', '{ "createIndexes": "compwildcard2", "indexes": [ { "key": { "$**": 1 }, "name": "$**_1", "enableOrderedIndex": true }]}', TRUE);

SELECT * FROM documentdb_test_helpers.gin_bson_get_composite_path_wildcard_generated_terms('{ "a": { "b": 1, "c": 5 }}', '[ "" ]', 2600, true, 0);

SELECT documentdb_test_helpers.run_explain_and_trim($cmd$
   EXPLAIN (COSTS OFF, ANALYZE ON, SUMMARY OFF, BUFFERS OFF, TIMING OFF) SELECT document FROM bson_aggregation_find('compdb', '{ "find": "compwildcard2", "filter": { "a.b": { "$gte": 2, "$lt": 4 }, "a.c": { "$lt": 9 } }}') $cmd$);

SELECT documentdb_test_helpers.run_explain_and_trim($cmd$
   EXPLAIN (COSTS OFF, ANALYZE ON, SUMMARY OFF, BUFFERS OFF, TIMING OFF) SELECT document FROM bson_aggregation_find('compdb', '{ "find": "compwildcard2", "filter": { "a.b": { "$gte": 2, "$lt": 4 } }}') $cmd$);