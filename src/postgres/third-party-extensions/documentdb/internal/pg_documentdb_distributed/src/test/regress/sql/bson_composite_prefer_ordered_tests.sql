SET search_path TO documentdb_api,documentdb_api_catalog,documentdb_api_internal,documentdb_core;

SET citus.next_shard_id TO 685000;
SET documentdb.next_collection_id TO 68500;
SET documentdb.next_collection_index_id TO 68500;

set documentdb.enableExtendedExplainPlans to on;
set documentdb.enableIndexOrderbyPushdown to on;

-- if documentdb_extended_rum exists, set alternate index handler
SELECT pg_catalog.set_config('documentdb.alternate_index_handler_name', 'extended_rum', false), extname FROM pg_extension WHERE extname = 'documentdb_extended_rum';


SELECT documentdb_api.drop_collection('comp_db2', 'query_ordered_pref') IS NOT NULL;
SELECT documentdb_api.create_collection('comp_db2', 'query_ordered_pref');

SELECT documentdb_api_internal.create_indexes_non_concurrently('comp_db2', '{ "createIndexes": "query_ordered_pref", "indexes": [ { "key": { "a": 1 }, "enableCompositeTerm": true, "name": "a_1" }] }', true);

\d documentdb_data.documents_68501

SELECT COUNT(documentdb_api.insert_one('comp_db2', 'query_ordered_pref', FORMAT('{ "_id": %s, "a": %s }', i, i)::bson)) FROM generate_series(1, 10000) AS i;

ANALYZE documentdb_data.documents_68501;

set enable_bitmapscan to off;
set documentdb.forceDisableSeqScan to on;
set documentdb_rum.preferOrderedIndexScan to off;
EXPLAIN (ANALYZE ON, COSTS OFF, VERBOSE ON, BUFFERS OFF, TIMING OFF, SUMMARY OFF) SELECT document FROM bson_aggregation_find('comp_db2',
    '{ "find": "query_ordered_pref", "filter": { "a": { "$gt": 50 } }, "projection": { "_id": 1 }, "limit": 5 }');
EXPLAIN (ANALYZE ON, COSTS OFF, VERBOSE ON, BUFFERS OFF, TIMING OFF, SUMMARY OFF) SELECT document FROM bson_aggregation_find('comp_db2',
    '{ "find": "query_ordered_pref", "filter": { "a": { "$gt": 50, "$lt": 900 } }, "projection": { "_id": 1 }, "limit": 5 }');

set documentdb_rum.preferOrderedIndexScan to on;
EXPLAIN (ANALYZE ON, COSTS OFF, VERBOSE ON, BUFFERS OFF, TIMING OFF, SUMMARY OFF) SELECT document FROM bson_aggregation_find('comp_db2',
    '{ "find": "query_ordered_pref", "filter": { "a": { "$gt": 50 } }, "projection": { "_id": 1 }, "limit": 5 }');
EXPLAIN (ANALYZE ON, COSTS OFF, VERBOSE ON, BUFFERS OFF, TIMING OFF, SUMMARY OFF) SELECT document FROM bson_aggregation_find('comp_db2',
    '{ "find": "query_ordered_pref", "filter": { "a": { "$gt": 50, "$lt": 900 } }, "projection": { "_id": 1 }, "limit": 5 }');


-- test ordered scan in the presence of deletes
set documentdb.enableExtendedExplainPlans to off;
reset documentdb.forceDisableSeqScan;
SELECT documentdb_api_internal.create_indexes_non_concurrently('comp_db2', '{ "createIndexes": "ordered_delete", "indexes": [ { "key": { "a": 1 }, "name": "a_1", "enableOrderedIndex": true } ] }');
SELECT COUNT(documentdb_api.insert_one('comp_db2', 'ordered_delete', FORMAT('{ "_id": %s, "a": %s }', i, i % 50)::bson)) FROM generate_series(1, 100) i;

ANALYZE documentdb_data.documents_68502;

set documentdb.forceDisableSeqScan to on;
EXPLAIN (ANALYZE ON, COSTS OFF, VERBOSE ON, BUFFERS OFF, TIMING OFF, SUMMARY OFF) SELECT document FROM bson_aggregation_find('comp_db2', '{ "find": "ordered_delete", "filter": { "a": { "$lt": 4 } } }');

-- now delete everthing
reset documentdb.forceDisableSeqScan;
DELETE FROM documentdb_data.documents_68502;

-- vacuum the table
VACUUM documentdb_data.documents_68502;

-- query the data
set documentdb.forceDisableSeqScan to on;
EXPLAIN (ANALYZE ON, COSTS OFF, VERBOSE ON, BUFFERS OFF, TIMING OFF, SUMMARY OFF) SELECT document FROM bson_aggregation_find('comp_db2', '{ "find": "ordered_delete", "filter": { "a": { "$lt": 4 } } }');

-- now try with a posting tree at the end.
reset documentdb.forceDisableSeqScan;
CALL documentdb_api.drop_indexes('comp_db2', '{ "dropIndexes": "ordered_delete", "index": "a_1" }');
SELECT documentdb_api_internal.create_indexes_non_concurrently('comp_db2', '{ "createIndexes": "ordered_delete", "indexes": [ { "key": { "a": 1 }, "name": "a_1", "enableOrderedIndex": true } ] }', TRUE);
SELECT COUNT(documentdb_api.insert_one('comp_db2', 'ordered_delete', FORMAT('{ "_id": %s, "a": 1 }', i)::bson)) FROM generate_series(1, 5000) i;
SELECT COUNT(documentdb_api.insert_one('comp_db2', 'ordered_delete', FORMAT('{ "_id": %s, "a": 2 }', i)::bson)) FROM generate_series(10001, 15000) i;
SELECT COUNT(documentdb_api.insert_one('comp_db2', 'ordered_delete', FORMAT('{ "_id": %s, "a": 3 }', i)::bson)) FROM generate_series(20001, 25000) i;
SELECT COUNT(documentdb_api.insert_one('comp_db2', 'ordered_delete', FORMAT('{ "_id": %s, "a": 4 }', i)::bson)) FROM generate_series(30001, 35000) i;

ANALYZE documentdb_data.documents_68502;

set documentdb.forceDisableSeqScan to on;
EXPLAIN (ANALYZE ON, COSTS OFF, VERBOSE ON, BUFFERS OFF, TIMING OFF, SUMMARY OFF) SELECT document FROM bson_aggregation_find('comp_db2', '{ "find": "ordered_delete", "filter": { "a": { "$lte": 4 } } }');

-- delete the first half of TIDs from some of the posting trees
DELETE FROM documentdb_data.documents_68502 WHERE object_id >= '{ "": 10001 }' AND object_id < '{ "": 12000 }';
DELETE FROM documentdb_data.documents_68502 WHERE object_id >= '{ "": 20001 }' AND object_id < '{ "": 22500 }';

VACUUM documentdb_data.documents_68502;

reset documentdb.forceDisableSeqScan;
set enable_indexscan to off;
set enable_bitmapscan to off; 
EXPLAIN (ANALYZE ON, COSTS OFF, VERBOSE ON, BUFFERS OFF, TIMING OFF, SUMMARY OFF) SELECT document FROM bson_aggregation_find('comp_db2', '{ "find": "ordered_delete", "filter": { "a": { "$lte": 4 } } }');
SELECT document FROM bson_aggregation_pipeline('comp_db2', '{ "aggregate": "ordered_delete", "pipeline": [ { "$match": { "a": { "$lte": 4 } } }, { "$group": { "_id": "$a", "c": { "$count": 1 } }} ] }');

reset enable_indexscan;
reset enable_bitmapscan;
set documentdb.forceDisableSeqScan to on;
EXPLAIN (ANALYZE OFF, COSTS OFF, VERBOSE ON, BUFFERS OFF, TIMING OFF, SUMMARY OFF) SELECT document FROM bson_aggregation_pipeline('comp_db2', '{ "aggregate": "ordered_delete", "pipeline": [ { "$match": { "a": { "$lte": 4 } } }, { "$group": { "_id": "$a", "c": { "$count": 1 } }} ] }');
SELECT document FROM bson_aggregation_pipeline('comp_db2', '{ "aggregate": "ordered_delete", "pipeline": [ { "$match": { "a": { "$lte": 4 } } }, { "$group": { "_id": "$a", "c": { "$count": 1 } }} ] }');

-- now delete everthing
reset documentdb.forceDisableSeqScan;
DELETE FROM documentdb_data.documents_68502;

-- vacuum the table
VACUUM documentdb_data.documents_68502;

-- query the data
set documentdb.forceDisableSeqScan to on;
EXPLAIN (ANALYZE ON, COSTS OFF, VERBOSE ON, BUFFERS OFF, TIMING OFF, SUMMARY OFF) SELECT document FROM bson_aggregation_find('comp_db2', '{ "find": "ordered_delete", "filter": { "a": { "$lt": 4 } } }');


-- test scan skipping behavior.
reset documentdb.forceDisableSeqScan;
SELECT COUNT(documentdb_api.insert_one('comp_db2', 'skip_entry_asc', FORMAT('{ "_id": %s, "a": %s, "b": %s, "c": %s }', ((i * 100) + (j * 10) + k), i, j, k)::bson)) FROM generate_series(1, 5) i, generate_series(1, 10) j, generate_series(1, 100) k;
SELECT documentdb_api_internal.create_indexes_non_concurrently('comp_db2', '{ "createIndexes": "skip_entry_asc", "indexes": [ { "key": { "a": 1, "b": 1, "c": 1 }, "name": "a_1", "enableOrderedIndex": true } ] }', TRUE);

SELECT COUNT(documentdb_api.insert_one('comp_db2', 'skip_entry_desc', FORMAT('{ "_id": %s, "a": %s, "b": %s, "c": %s }', ((i * 100) + (j * 10) + k), i, j, k)::bson)) FROM generate_series(1, 5) i, generate_series(1, 10) j, generate_series(1, 100) k;
SELECT documentdb_api_internal.create_indexes_non_concurrently('comp_db2', '{ "createIndexes": "skip_entry_desc", "indexes": [ { "key": { "a": -1, "b": -1, "c": -1 }, "name": "a_1", "enableOrderedIndex": true } ] }', TRUE);

ANALYZE documentdb_data.documents_68503;

-- now test the behavior of partial skipping
set documentdb.forceDisableSeqScan to on;
set documentdb.enableExtendedExplainPlans to on;
set documentdb_rum.enableSkipIntermediateEntry to off;
EXPLAIN (ANALYZE ON, COSTS OFF, VERBOSE ON, BUFFERS OFF, TIMING OFF, SUMMARY OFF) SELECT document FROM bson_aggregation_find('comp_db2',
    '{ "find": "skip_entry_asc", "filter": { "a": { "$gte": 2, "$lte": 4 }, "c": { "$gte": 3, "$lte": 5 } } }');
EXPLAIN (ANALYZE ON, COSTS OFF, VERBOSE ON, BUFFERS OFF, TIMING OFF, SUMMARY OFF) SELECT document FROM bson_aggregation_find('comp_db2',
    '{ "find": "skip_entry_desc", "filter": { "a": { "$gte": 2, "$lte": 4 }, "c": { "$gte": 3, "$lte": 5 } } }');
EXPLAIN (ANALYZE ON, COSTS OFF, VERBOSE ON, BUFFERS OFF, TIMING OFF, SUMMARY OFF) SELECT document FROM bson_aggregation_find('comp_db2',
    '{ "find": "skip_entry_asc", "filter": { "a": { "$in": [ 2, 3 ] }, "c": { "$gte": 3, "$lte": 5 } } }');

-- with skip entries on we skip relevant portions of the tree
set documentdb_rum.enableSkipIntermediateEntry to on;
EXPLAIN (ANALYZE ON, COSTS OFF, VERBOSE ON, BUFFERS OFF, TIMING OFF, SUMMARY OFF) SELECT document FROM bson_aggregation_find('comp_db2',
    '{ "find": "skip_entry_asc", "filter": { "a": { "$gte": 2, "$lte": 4 }, "c": { "$gte": 3, "$lte": 5 } } }');
EXPLAIN (ANALYZE ON, COSTS OFF, VERBOSE ON, BUFFERS OFF, TIMING OFF, SUMMARY OFF) SELECT document FROM bson_aggregation_find('comp_db2',
    '{ "find": "skip_entry_desc", "filter": { "a": { "$gte": 2, "$lte": 4 }, "c": { "$gte": 3, "$lte": 5 } } }');
EXPLAIN (ANALYZE ON, COSTS OFF, VERBOSE ON, BUFFERS OFF, TIMING OFF, SUMMARY OFF) SELECT document FROM bson_aggregation_find('comp_db2',
    '{ "find": "skip_entry_asc", "filter": { "a": { "$in": [ 2, 3 ] }, "c": { "$gte": 3, "$lte": 5 } } }');

-- try with hint to do secondary/tertiary docs
SELECT documentdb_api.insert_one('comp_db2', 'skip_entry_mixed', '{ "_id": 0 }');
SELECT documentdb_api.insert_one('comp_db2', 'skip_entry_mixed', '{ "_id": 1, "a": 1 }');
SELECT documentdb_api.insert_one('comp_db2', 'skip_entry_mixed', '{ "_id": 2, "a": 1, "b": 1 }');
SELECT documentdb_api.insert_one('comp_db2', 'skip_entry_mixed', '{ "_id": 3, "a": 1, "b": 1, "c": 1 }');
SELECT documentdb_api.insert_one('comp_db2', 'skip_entry_mixed', '{ "_id": 4, "a": 1, "b": 1, "c": 1, "d": 1 }');
SELECT documentdb_api_internal.create_indexes_non_concurrently('comp_db2', '{ "createIndexes": "skip_entry_mixed", "indexes": [ { "key": { "a": 1, "b": -1, "c": 1, "d": -1 }, "name": "a_1", "enableOrderedIndex": true } ] }', TRUE);

set documentdb.forceDisableSeqScan to off;
EXPLAIN (ANALYZE ON, COSTS OFF, VERBOSE ON, BUFFERS OFF, TIMING OFF, SUMMARY OFF) SELECT document FROM bson_aggregation_find('comp_db2', '{ "find": "skip_entry_mixed", "filter": { "a": 1 }, "hint": "a_1" }');
EXPLAIN (ANALYZE ON, COSTS OFF, VERBOSE ON, BUFFERS OFF, TIMING OFF, SUMMARY OFF) SELECT document FROM bson_aggregation_find('comp_db2', '{ "find": "skip_entry_mixed", "filter": { "b": 1 }, "hint": "a_1" }');
EXPLAIN (ANALYZE ON, COSTS OFF, VERBOSE ON, BUFFERS OFF, TIMING OFF, SUMMARY OFF) SELECT document FROM bson_aggregation_find('comp_db2', '{ "find": "skip_entry_mixed", "filter": { "c": 1 }, "hint": "a_1" }');
EXPLAIN (ANALYZE ON, COSTS OFF, VERBOSE ON, BUFFERS OFF, TIMING OFF, SUMMARY OFF) SELECT document FROM bson_aggregation_find('comp_db2', '{ "find": "skip_entry_mixed", "filter": { "d": 1 }, "hint": "a_1" }');

EXPLAIN (ANALYZE ON, COSTS OFF, VERBOSE ON, BUFFERS OFF, TIMING OFF, SUMMARY OFF) SELECT document FROM bson_aggregation_find('comp_db2', '{ "find": "skip_entry_mixed", "filter": { "a": null }, "hint": "a_1" }');
EXPLAIN (ANALYZE ON, COSTS OFF, VERBOSE ON, BUFFERS OFF, TIMING OFF, SUMMARY OFF) SELECT document FROM bson_aggregation_find('comp_db2', '{ "find": "skip_entry_mixed", "filter": { "b": null }, "hint": "a_1" }');
EXPLAIN (ANALYZE ON, COSTS OFF, VERBOSE ON, BUFFERS OFF, TIMING OFF, SUMMARY OFF) SELECT document FROM bson_aggregation_find('comp_db2', '{ "find": "skip_entry_mixed", "filter": { "c": null }, "hint": "a_1" }');
EXPLAIN (ANALYZE ON, COSTS OFF, VERBOSE ON, BUFFERS OFF, TIMING OFF, SUMMARY OFF) SELECT document FROM bson_aggregation_find('comp_db2', '{ "find": "skip_entry_mixed", "filter": { "d": null }, "hint": "a_1" }');