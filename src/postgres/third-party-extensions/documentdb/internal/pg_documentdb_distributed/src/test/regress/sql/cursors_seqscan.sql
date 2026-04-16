SET search_path TO documentdb_api_catalog, documentdb_api, documentdb_core, documentdb_api_internal, public;
SET citus.next_shard_id TO 6710000;
SET documentdb.next_collection_id TO 6710;
SET documentdb.next_collection_index_id TO 6710;

-- create a collection
SELECT documentdb_api.create_collection('db', 'cursors_seqscan');

-- insert 20 documents
WITH r1 AS (SELECT FORMAT('{"_id": %I, "a": { "b": { "$numberInt": %I }, "c": { "$numberInt" : %I }, "d": [ { "$numberInt" : %I }, { "$numberInt" : %I } ] }}', g.Id, g.Id, g.Id, g.Id, g.Id)::bson AS formatDoc FROM generate_series(1, 20) AS g (id) ORDER BY g.Id desc) 
SELECT documentdb_api.insert_one('db', 'cursors_seqscan', r1.formatDoc) FROM r1;

SELECT documentdb_distributed_test_helpers.drop_primary_key('db', 'cursors_seqscan');

-- limit it by batchSize
SELECT document, length(document::bytea) FROM documentdb_api.collection('db', 'cursors_seqscan') WHERE documentdb_api_internal.cursor_state(document, '{ "getpage_batchSizeHint": 1 }') AND document @@ '{ "a.b": { "$gt": 12 }}';
SELECT document, length(document::bytea) FROM documentdb_api.collection('db', 'cursors_seqscan') WHERE documentdb_api_internal.cursor_state(document, '{ "getpage_batchSizeHint": 100 }') AND document @@ '{ "a.b": { "$gt": 12 }}';

-- specify getpage_batchSizeAttr
SELECT document, length(document::bytea) FROM documentdb_api.collection('db', 'cursors_seqscan') WHERE documentdb_api_internal.cursor_state(document, '{ "getpage_batchSizeHint": 1, "getpage_batchSizeAttr": 1 }') AND document @@ '{ "a.b": { "$gt": 12 }}';
SELECT document, length(document::bytea) FROM documentdb_api.collection('db', 'cursors_seqscan') WHERE documentdb_api_internal.cursor_state(document, '{ "getpage_batchSizeHint": 100, "getpage_batchSizeAttr": 1 }') AND document @@ '{ "a.b": { "$gt": 12 }}';

-- run the default test.
-- now query them with varying page sizes using cursors.
SELECT document FROM documentdb_api.collection('db', 'cursors_seqscan') WHERE documentdb_api_internal.cursor_state(document, '{ "getpage_batchCount": 1 }') AND document @@ '{ "a.b": { "$gt": 12 }}';
SELECT document FROM documentdb_api.collection('db', 'cursors_seqscan') WHERE documentdb_api_internal.cursor_state(document, '{ "getpage_batchCount": 3 }') AND document @@ '{ "a.b": { "$gt": 12 }}';

-- query with page sizes and get cursor state.
SELECT document, current_cursor_state(document) FROM documentdb_api.collection('db', 'cursors_seqscan') WHERE documentdb_api_internal.cursor_state(document, '{ "getpage_batchCount": 3 }') AND document @@ '{ "a.b": { "$gt": 12 }}';
SELECT document, current_cursor_state(document) FROM documentdb_api.collection('db', 'cursors_seqscan') WHERE documentdb_api_internal.cursor_state(document, '{ "getpage_batchCount": 7 }') AND document @@ '{ "a.b": { "$gt": 12 }}';

-- query with page sizes, projection and get cursor state.
SELECT document, bson_dollar_project(document, '{ "a.b": 1 }'), current_cursor_state(document) FROM documentdb_api.collection('db', 'cursors_seqscan') WHERE documentdb_api_internal.cursor_state(document, '{ "getpage_batchCount": 5 }') AND document @@ '{ "a.b": { "$gt": 12 }}';
SELECT document, bson_dollar_project(document, '{ "a.b": 1 }'), current_cursor_state(document) FROM documentdb_api.collection('db', 'cursors_seqscan') WHERE documentdb_api_internal.cursor_state(document, '{ "getpage_batchCount": 3 }') AND document @@ '{ "a.b": { "$gt": 12 }}';

-- now test resume from continuation
SELECT document, current_cursor_state(document) AS cursor1 INTO TEMPORARY d1 FROM documentdb_api.collection('db', 'cursors_seqscan') WHERE documentdb_api_internal.cursor_state(document, '{ "getpage_batchCount": 3 }') AND document @@ '{ "a.b": { "$gt": 12 }}';

SELECT r2 FROM (SELECT $$'$$ || row_get_bson(rec) || $$'$$ AS r2 FROM (SELECT 3 AS "getpage_batchCount", array_append('{}'::bson[], cursor1) AS "continuation" FROM d1 OFFSET 2 LIMIT 1) rec) r2 \gset
-- print the continuation
\echo :r2

-- now run the query with the continuation.
SELECT document, bson_dollar_project(document, '{ "a.b": 1 }'), current_cursor_state(document) FROM documentdb_api.collection('db', 'cursors_seqscan') WHERE documentdb_api_internal.cursor_state(document, :r2) AND document @@ '{ "a.b": { "$gt": 12 }}';

EXPLAIN (VERBOSE ON, COSTS OFF ) SELECT document, bson_dollar_project(document, '{ "a.b": 1 }'), current_cursor_state(document) FROM documentdb_api.collection('db', 'cursors_seqscan') WHERE documentdb_api_internal.cursor_state(document, :r2) AND document @@ '{ "a.b": { "$gt": 12 }}';


-- now try with multi-continuation with a different table
SELECT r3 FROM (SELECT $$'$$ || row_get_bson(rec) || $$'$$ AS r3 FROM (SELECT 3 AS "getpage_batchCount", array_append('{}'::bson[], '{ "table_name": "someOtherTable" }'::bson) AS "continuation" FROM d1 OFFSET 2 LIMIT 1) rec) r3 \gset
-- print the continuation
\echo :r3

-- now run the query with the continuation (Should have no continuation).
SELECT document, bson_dollar_project(document, '{ "a.b": 1 }'), current_cursor_state(document) FROM documentdb_api.collection('db', 'cursors_seqscan') WHERE documentdb_api_internal.cursor_state(document, :r3) AND document @@ '{ "a.b": { "$gt": 12 }}';
EXPLAIN (VERBOSE ON, COSTS OFF ) SELECT document, bson_dollar_project(document, '{ "a.b": 1 }'), current_cursor_state(document) FROM documentdb_api.collection('db', 'cursors_seqscan') WHERE documentdb_api_internal.cursor_state(document, :r3) AND document @@ '{ "a.b": { "$gt": 12 }}';

-- run with remote execution
set citus.enable_local_execution to off;
SELECT document, current_cursor_state(document) FROM documentdb_api.collection('db', 'cursors_seqscan') WHERE documentdb_api_internal.cursor_state(document, '{ "getpage_batchCount": 3 }') AND document @@ '{ "a.b": { "$gt": 12 }}';
SELECT document, bson_dollar_project(document, '{ "a.b": 1 }'), current_cursor_state(document) FROM documentdb_api.collection('db', 'cursors_seqscan') WHERE documentdb_api_internal.cursor_state(document, :r2) AND document @@ '{ "a.b": { "$gt": 12 }}';
