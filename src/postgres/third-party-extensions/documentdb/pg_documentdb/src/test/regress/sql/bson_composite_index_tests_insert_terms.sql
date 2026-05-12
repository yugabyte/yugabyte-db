SET search_path TO documentdb_api,documentdb_core,documentdb_api_catalog;

SET documentdb.next_collection_id TO 5600;
SET documentdb.next_collection_index_id TO 5600;


CREATE OR REPLACE FUNCTION documentdb_test_helpers.gin_bson_get_composite_path_generated_terms(document documentdb_core.bson, pathSpec text, termLimit int4, addMetadata bool, wildcardIndex int4 = -1)
    RETURNS SETOF documentdb_core.bson LANGUAGE C IMMUTABLE PARALLEL SAFE STRICT AS '$libdir/pg_documentdb',
$$gin_bson_get_composite_path_generated_terms$$;

-- test scenarios of term generation for composite path
SELECT * FROM documentdb_test_helpers.gin_bson_get_composite_path_generated_terms('{ "a": 1, "b": 2 }', '[ "a", "b" ]', 2000, false);
SELECT * FROM documentdb_test_helpers.gin_bson_get_composite_path_generated_terms('{ "a": [ 1, 2, 3 ], "b": 2 }', '[ "a", "b" ]', 2000, false);
SELECT * FROM documentdb_test_helpers.gin_bson_get_composite_path_generated_terms('{ "a": 1, "b": [ true, false ] }', '[ "a", "b" ]', 2000, false);
SELECT * FROM documentdb_test_helpers.gin_bson_get_composite_path_generated_terms('{ "a": [ 1, 2, 3 ], "b": [ true, false ] }', '[ "a", "b" ]', 2000, false);

-- test when one doesn't exist
SELECT * FROM documentdb_test_helpers.gin_bson_get_composite_path_generated_terms('{ "b": [ true, false ] }', '[ "a", "b" ]', 2000, false);
SELECT * FROM documentdb_test_helpers.gin_bson_get_composite_path_generated_terms('{ "a": [ 1, 2, 3 ] }', '[ "a", "b" ]', 2000, false);

-- test when one gets truncated (a has 29 letters, truncation limit is 50 /2 so 25 per path)
SELECT * FROM documentdb_test_helpers.gin_bson_get_composite_path_generated_terms('{ "a": "aaaaaaaaaaaaaaaaaaaaaaaaaaaa", "b": 1 }', '[ "a", "b" ]', 50, true);

-- nested paths
SELECT * FROM documentdb_test_helpers.gin_bson_get_composite_path_generated_terms('{ "a": { "b": { "c": 1 } } }', '[ "a.b", "a.b.c" ]', 2000, true);

-- create a table and insert some data.

SELECT documentdb_api_internal.create_indexes_non_concurrently(
    'comp_db', '{ "createIndexes": "comp_collection", "indexes": [ { "name": "comp_index", "key": { "a": 1, "b": -1 } } ] }', TRUE);

-- does not work
SELECT documentdb_api_internal.create_indexes_non_concurrently(
    'comp_db', '{ "createIndexes": "comp_collection", "indexes": [ { "name": "comp_index2", "key": { "$**": 1 }, "enableCompositeTerm": true } ] }', TRUE);
SELECT documentdb_api_internal.create_indexes_non_concurrently(
    'comp_db', '{ "createIndexes": "comp_collection", "indexes": [ { "name": "comp_index3", "key": { "a.$**": 1 }, "enableCompositeTerm": true } ] }', TRUE);

-- create an index
SELECT documentdb_api_internal.create_indexes_non_concurrently(
    'comp_db', '{ "createIndexes": "comp_collection", "indexes": [ { "name": "comp_index1", "key": { "a": 1, "b": 1 } } ] }', TRUE);

-- create a non composite index with a different name and same key (works)
SELECT documentdb_api_internal.create_indexes_non_concurrently(
    'comp_db', '{ "createIndexes": "comp_collection", "indexes": [ { "name": "comp_index4", "key": { "a": 1, "b": 1 }, "enableCompositeTerm": false } ] }', TRUE);

-- check the index
\d documentdb_data.documents_5601

-- now drop the extra indexes
CALL documentdb_api.drop_indexes('comp_db', '{ "dropIndexes": "comp_collection", "index": "comp_index" }');
CALL documentdb_api.drop_indexes('comp_db', '{ "dropIndexes": "comp_collection", "index": "comp_index4" }');

\d documentdb_data.documents_5601

SELECT documentdb_api.insert_one('comp_db', 'comp_collection', '{ "_id": 1, "a": 1, "b": true }');
SELECT documentdb_api.insert_one('comp_db', 'comp_collection', '{ "_id": 2, "a": [ 1, 2 ], "b": true }');
SELECT documentdb_api.insert_one('comp_db', 'comp_collection', '{ "_id": 3, "a": 1, "b": [ true, false ] }');
SELECT documentdb_api.insert_one('comp_db', 'comp_collection', '{ "_id": 4, "a": [ 1, 2 ], "b": [ true, false ] }');

-- pushes to the composite index
SELECT document FROM documentdb_api_catalog.bson_aggregation_find('comp_db', '{ "find": "comp_collection", "filter": { "a": 1, "b": true } }');
SELECT document FROM documentdb_api_catalog.bson_aggregation_find('comp_db', '{ "find": "comp_collection", "filter": { "a": 2, "b": true } }');
SELECT document FROM documentdb_api_catalog.bson_aggregation_find('comp_db', '{ "find": "comp_collection", "filter": { "a": 2, "b": false } }');

-- validate specifying just one path
SELECT document FROM documentdb_api_catalog.bson_aggregation_find('comp_db', '{ "find": "comp_collection", "filter": { "a": 2 } }');
SELECT document FROM documentdb_api_catalog.bson_aggregation_find('comp_db', '{ "find": "comp_collection", "filter": { "b": false } }');

-- prefix inequality
SELECT document FROM documentdb_api_catalog.bson_aggregation_find('comp_db', '{ "find": "comp_collection", "filter": { "a": { "$gt": 0 }, "b": false } }');
SELECT document FROM documentdb_api_catalog.bson_aggregation_find('comp_db', '{ "find": "comp_collection", "filter": { "a": { "$gt": 1 }, "b": false } }');

-- suffix inequality
SELECT document FROM documentdb_api_catalog.bson_aggregation_find('comp_db', '{ "find": "comp_collection", "filter": { "a": 1, "b":  { "$gt": false } } }');
SELECT document FROM documentdb_api_catalog.bson_aggregation_find('comp_db', '{ "find": "comp_collection", "filter": { "a": 2, "b":  { "$gt": false } } }');
SELECT document FROM documentdb_api_catalog.bson_aggregation_find('comp_db', '{ "find": "comp_collection", "filter": { "a": 1, "b":  { "$gt": true } } }');

-- now add some cross-type members
SELECT documentdb_api.insert_one('comp_db', 'comp_collection', '{ "_id": 5, "a": "string1", "b": true }');
SELECT documentdb_api.insert_one('comp_db', 'comp_collection', '{ "_id": 6, "a": "string2", "b": true }');

SELECT documentdb_api.insert_one('comp_db', 'comp_collection', '{ "_id": 7, "a": { "key": "string2" }, "b": true }');

-- has cross type values
SELECT document FROM documentdb_api_catalog.bson_aggregation_find('comp_db', '{ "find": "comp_collection", "filter": { "a": { "$exists": true }, "b": true } }');
SELECT document FROM documentdb_api_catalog.bson_aggregation_find('comp_db', '{ "find": "comp_collection", "filter": { "a": { "$gte": { "$minKey": 1 } }, "b": true } }');

-- applies type bracketing
SELECT document FROM documentdb_api_catalog.bson_aggregation_find('comp_db', '{ "find": "comp_collection", "filter": { "a": { "$gt": 0 }, "b": true } }');
SELECT document FROM documentdb_api_catalog.bson_aggregation_find('comp_db', '{ "find": "comp_collection", "filter": { "a": { "$gte": "string0" }, "b": true } }');

SELECT document FROM documentdb_api_catalog.bson_aggregation_find('comp_db', '{ "find": "comp_collection", "filter": { "a": { "$type": "string" }, "b": true } }');
SELECT document FROM documentdb_api_catalog.bson_aggregation_find('comp_db', '{ "find": "comp_collection", "filter": { "a": { "$type": "object" }, "b": true } }');
SELECT document FROM documentdb_api_catalog.bson_aggregation_find('comp_db', '{ "find": "comp_collection", "filter": { "a": { "$type": "number" }, "b": true } }');

-- runtime recheck
SELECT document FROM documentdb_api_catalog.bson_aggregation_find('comp_db', '{ "find": "comp_collection", "filter": { "a": { "$regex": ".+2$" }, "b": true } }');

-- add large keys
SELECT documentdb_api.insert_one('comp_db', 'comp_collection', FORMAT('{ "_id": 8, "a": { "key": "%s" }, "b": "%s" }', repeat('a', 10000), repeat('a', 10000))::bson);

SELECT FORMAT('{ "find": "comp_collection", "filter": { "a": { "key": "%s" }, "b": "%s" }, "projection": { "_id": 1 } }', repeat('a', 5000), repeat('a', 5000)) AS q1 \gset
SELECT document FROM documentdb_api_catalog.bson_aggregation_find('comp_db', :'q1'::bson);

SELECT FORMAT('{ "find": "comp_collection", "filter": { "a": { "key": "%s" }, "b": "%s" }, "projection": { "_id": 1 } }', repeat('a', 8000), repeat('a', 8000)) AS q1 \gset
SELECT document FROM documentdb_api_catalog.bson_aggregation_find('comp_db', :'q1'::bson);

SELECT FORMAT('{ "find": "comp_collection", "filter": { "a": { "key": "%s" }, "b": "%s" }, "projection": { "_id": 1 } }', repeat('a', 10000), repeat('a', 10000)) AS q1 \gset
SELECT document FROM documentdb_api_catalog.bson_aggregation_find('comp_db', :'q1'::bson);

SELECT FORMAT('{ "find": "comp_collection", "filter": { "a": { "key": "%s" } }, "projection": { "_id": 1 } }', repeat('a', 10000)) AS q1 \gset
SELECT document FROM documentdb_api_catalog.bson_aggregation_find('comp_db', :'q1'::bson);

SELECT FORMAT('{ "find": "comp_collection", "filter": { "b": "%s" }, "projection": { "_id": 1 } }', repeat('a', 10000)) AS q1 \gset
SELECT document FROM documentdb_api_catalog.bson_aggregation_find('comp_db', :'q1'::bson);

-- multi-bound queries
SELECT document FROM documentdb_api_catalog.bson_aggregation_find('comp_db', '{ "find": "comp_collection", "filter": { "a": { "$in": [ 1, 2 ] }, "b": true } }');
SELECT document FROM documentdb_api_catalog.bson_aggregation_find('comp_db', '{ "find": "comp_collection", "filter": { "a": { "$in": [ 1, 2 ] }, "b": false } }');

SELECT document FROM documentdb_api_catalog.bson_aggregation_find('comp_db', '{ "find": "comp_collection", "filter": { "a": { "$in": [ 2, "string1" ] }, "b": { "$in": [ true, false ] } } }');
SELECT document FROM documentdb_api_catalog.bson_aggregation_find('comp_db', '{ "find": "comp_collection", "filter": { "a": { "$in": [ 1, 2 ] }, "b": { "$in": [ true, false ] } } }');

SELECT document FROM documentdb_api_catalog.bson_aggregation_find('comp_db', '{ "find": "comp_collection", "filter": { "a": { "$in": [ 1, 2 ] }, "a": { "$lt": 2 }, "b": { "$in": [ true, false ] } } }');

-- test that we can create side by side non composite and composite indexes with the same key when forcing composite op class.
set documentdb.defaultUseCompositeOpClass to off;
select documentdb_api.drop_database('comp_db');

SELECT documentdb_api_internal.create_indexes_non_concurrently(
    'comp_db', '{ "createIndexes": "comp_collection", "indexes": [ { "name": "a_1", "key": { "a": 1 } } ] }');
SELECT documentdb_api_internal.create_indexes_non_concurrently(
    'comp_db', '{ "createIndexes": "comp_collection", "indexes": [ { "name": "a_-1", "key": { "a": -1} } ] }', TRUE);

set documentdb.defaultUseCompositeOpClass to on;

-- name collision still fails
SELECT documentdb_api_internal.create_indexes_non_concurrently(
    'comp_db', '{ "createIndexes": "comp_collection", "indexes": [ { "name": "a_1", "key": { "a": 1 } } ] }', TRUE);
SELECT documentdb_api_internal.create_indexes_non_concurrently(
    'comp_db', '{ "createIndexes": "comp_collection", "indexes": [ { "name": "a_-1", "key": { "a": -1 } } ] }', TRUE);

SELECT documentdb_api_internal.create_indexes_non_concurrently(
    'comp_db', '{ "createIndexes": "comp_collection", "indexes": [ { "name": "a_1_comp", "key": { "a": 1 } } ] }', TRUE);
SELECT documentdb_api_internal.create_indexes_non_concurrently(
    'comp_db', '{ "createIndexes": "comp_collection", "indexes": [ { "name": "a_-1_comp", "key": { "a": -1} } ] }', TRUE);
SELECT documentdb_api_internal.create_indexes_non_concurrently(
    'comp_db', '{ "createIndexes": "comp_collection", "indexes": [ { "name": "_id_1_comp", "key": { "_id": 1} } ] }', TRUE);

SELECT collection_id as collid FROM documentdb_api_catalog.collections where database_name = 'comp_db' and collection_name = 'comp_collection' \gset 
SELECT index_spec FROM documentdb_api_catalog.collection_indexes where collection_id = :'collid'::int4;

-- creating two with composite and different names fails
SELECT documentdb_api_internal.create_indexes_non_concurrently(
    'comp_db', '{ "createIndexes": "comp_collection", "indexes": [ { "name": "a_1_comp_2", "key": { "a": 1 } } ] }', TRUE);
SELECT documentdb_api_internal.create_indexes_non_concurrently(
    'comp_db', '{ "createIndexes": "comp_collection", "indexes": [ { "name": "a_-1_comp_2", "key": { "a": -1 } } ] }', TRUE);


-- test that having side by side indexes we prefer composite index
SELECT documentdb_api.insert_one('comp_db', 'comp_collection', '{ "_id": 1, "a": 1, "b": true }');
SELECT documentdb_api.insert_one('comp_db', 'comp_collection', '{ "_id": 2, "a": [ 1, 2 ], "b": true }');
SELECT documentdb_api.insert_one('comp_db', 'comp_collection', '{ "_id": 3, "a": 1, "b": [ true, false ] }');
SELECT documentdb_api.insert_one('comp_db', 'comp_collection', '{ "_id": 4, "a": [ 1, 2 ], "b": [ true, false ] }');

SELECT documentdb_api.insert_one('comp_db', 'comp_collection', '{ "_id": 5, "a": "string1", "b": true }');
SELECT documentdb_api.insert_one('comp_db', 'comp_collection', '{ "_id": 6, "a": "string2", "b": true }');
SELECT documentdb_api.insert_one('comp_db', 'comp_collection', '{ "_id": 7, "a": { "key": "string2" }, "b": true }');

SELECT documentdb_api.insert_one('comp_db', 'comp_collection', FORMAT('{ "_id": 8, "a": { "key": "%s" }, "b": "%s" }', repeat('a', 10000), repeat('a', 10000))::bson);

set documentdb.logRelationIndexesOrder to on;
set client_min_messages to log;
EXPLAIN VERBOSE SELECT document FROM documentdb_api_catalog.bson_aggregation_find('comp_db', '{ "find": "comp_collection", "filter": { "a": 1 } }');

EXPLAIN VERBOSE SELECT document FROM documentdb_api_catalog.bson_aggregation_find('comp_db', '{ "find": "comp_collection", "filter": { "a": 1, "b": {"$exists": true} } }');
reset client_min_messages;

SELECT documentdb_api_internal.create_indexes_non_concurrently(
    'comp_db', '{ "createIndexes": "comp_collection", "indexes": [ { "name": "a_1_b_1_c_1", "key": { "a": 1, "b": 1, "c": 1}, "enableOrderedIndex": false } ] }', TRUE);

SELECT documentdb_api.insert_one('comp_db', 'comp_collection', '{ "_id": 7, "a": { "key": "string2" }, "b": true, "c": 1 }');

set documentdb.forceDisableSeqScan to on;
set client_min_messages to log;
EXPLAIN VERBOSE SELECT document FROM documentdb_api_catalog.bson_aggregation_find('comp_db', '{ "find": "comp_collection", "filter": { "a": 1, "b": {"$exists": true}, "c": {"$exists": true} } }');

set documentdb.enableIndexPriorityOrdering to off;
EXPLAIN VERBOSE SELECT document FROM documentdb_api_catalog.bson_aggregation_find('comp_db', '{ "find": "comp_collection", "filter": { "a": 1, "b": {"$exists": true}, "c": {"$exists": true} } }');

SELECT documentdb_api_internal.create_indexes_non_concurrently(
    'comp_db', '{ "createIndexes": "comp_collection", "indexes": [ { "name": "a_1_b_1_c_1_comp", "key": { "a": 1, "b": 1, "c": 1} } ] }', TRUE);

set documentdb.enableIndexPriorityOrdering to on;
EXPLAIN VERBOSE SELECT document FROM documentdb_api_catalog.bson_aggregation_find('comp_db', '{ "find": "comp_collection", "filter": { "a": 1, "b": {"$exists": true}, "c": {"$exists": true} } }');

reset client_min_messages;
reset documentdb.forceDisableSeqScan;

set documentdb.logRelationIndexesOrder to off;
set documentdb.defaultUseCompositeOpClass to off;

-- test index limits
SELECT documentdb_api_internal.create_indexes_non_concurrently(
    'comp_db', '{ "createIndexes": "cmpcollcreate", "indexes": [ { "name": "comp_index", "key": { "a1": 1, "a2": 1, "a3": 1, "a4": 1, "a5": 1, "a6": 1, "a7": 1, "a8": 1, "a9": 1, "a10": 1, "a11": 1, "a12": 1, "a13": 1, "a14": 1, "a15": 1, "a16": 1, "a17": 1, "a18": 1, "a19": 1, "a20": 1, "a21": 1, "a22": 1, "a23": 1, "a24": 1, "a25": 1, "a26": 1, "a27": 1, "a28": 1, "a29": 1, "a30": 1, "a31": 1, "a32": 1 }, "enableCompositeTerm": true } ] }', TRUE);

-- fails
SELECT documentdb_api_internal.create_indexes_non_concurrently(
    'comp_db', '{ "createIndexes": "cmpcollcreate", "indexes": [ { "name": "comp_index22", "key": { "a1": 1, "a2": 1, "a3": 1, "a4": 1, "a5": 1, "a6": 1, "a7": 1, "a8": 1, "a9": 1, "a10": 1, "a11": 1, "a12": 1, "a13": 1, "a14": 1, "a15": 1, "a16": 1, "a17": 1, "a18": 1, "a19": 1, "a20": 1, "a21": 1, "a22": 1, "a23": 1, "a24": 1, "a25": 1, "a26": 1, "a27": 1, "a28": 1, "a29": 1, "a30": 1, "a31": 1, "a32": 1, "a33": 1 }, "enableCompositeTerm": true } ] }', TRUE);