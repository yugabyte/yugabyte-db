SET search_path TO documentdb_api_catalog, documentdb_api, documentdb_core, documentdb_api_internal, public;
SET citus.next_shard_id TO 6720000;
SET documentdb.next_collection_id TO 6720;
SET documentdb.next_collection_index_id TO 6720;

-- create a collection
SELECT documentdb_api.create_collection('db', 'cursors_seqscan_sharded');

SELECT documentdb_api.shard_collection('db', 'cursors_seqscan_sharded', '{ "sh": "hashed" }', false);

-- insert 20 documents in shard key 1
WITH r1 AS (SELECT FORMAT('{"_id": %I, "sh": 1, "a": { "b": { "$numberInt": %I }, "c": { "$numberInt" : %I }, "d": [ { "$numberInt" : %I }, { "$numberInt" : %I } ] }}', g.Id, g.Id, g.Id, g.Id, g.Id)::bson AS formatDoc FROM generate_series(1, 20) AS g (id) ORDER BY g.Id desc) 
SELECT documentdb_api.insert_one('db', 'cursors_seqscan_sharded', r1.formatDoc) FROM r1;

-- insert 20 documents in shard key 2
WITH r1 AS (SELECT FORMAT('{"_id": %I, "sh": 2, "a": { "b": { "$numberInt": %I }, "c": { "$numberInt" : %I }, "d": [ { "$numberInt" : %I }, { "$numberInt" : %I } ] }}', g.Id, g.Id, g.Id, g.Id, g.Id)::bson AS formatDoc FROM generate_series(1, 20) AS g (id) ORDER BY g.Id desc) 
SELECT documentdb_api.insert_one('db', 'cursors_seqscan_sharded', r1.formatDoc) FROM r1;

\d documentdb_data.documents_6720

SELECT documentdb_distributed_test_helpers.drop_primary_key('db', 'cursors_seqscan_sharded');

-- run the default test.
-- now query them with varying page sizes using cursors.
SELECT * FROM execute_and_sort($$SELECT  object_id, document::text FROM documentdb_api.collection('db', 'cursors_seqscan_sharded') WHERE documentdb_api_internal.cursor_state(document, '{ "getpage_batchCount": 1 }') AND document @@ '{ "a.b": { "$gt": 12 }}'$$);
SELECT * FROM execute_and_sort($$SELECT  object_id, document::text FROM documentdb_api.collection('db', 'cursors_seqscan_sharded') WHERE documentdb_api_internal.cursor_state(document, '{ "getpage_batchCount": 3 }') AND document @@ '{ "a.b": { "$gt": 12 }}'$$);

-- query with page sizes and get cursor state.
SELECT * FROM execute_and_sort($$SELECT  object_id, document::text || ', cursurState:' || current_cursor_state(document)::text as document FROM documentdb_api.collection('db', 'cursors_seqscan_sharded') WHERE documentdb_api_internal.cursor_state(document, '{ "getpage_batchCount": 3 }') AND document @@ '{ "a.b": { "$gt": 12 }}'$$);
SELECT * FROM execute_and_sort($$SELECT  object_id, document::text || ', cursurState:' || current_cursor_state(document)::text as document FROM documentdb_api.collection('db', 'cursors_seqscan_sharded') WHERE documentdb_api_internal.cursor_state(document, '{ "getpage_batchCount": 7 }') AND document @@ '{ "a.b": { "$gt": 12 }}'$$);

-- query with page sizes, projection and get cursor state.
SELECT * FROM execute_and_sort($$SELECT  object_id, document::text || ', dollarProject:' || bson_dollar_project(document, '{ "a.b": 1 }')::text as document, current_cursor_state(document) FROM documentdb_api.collection('db', 'cursors_seqscan_sharded') WHERE documentdb_api_internal.cursor_state(document, '{ "getpage_batchCount": 5 }') AND document @@ '{ "a.b": { "$gt": 12 }}'$$);
SELECT * FROM execute_and_sort($$SELECT  object_id, document::text || ', dollarProject:' || bson_dollar_project(document, '{ "a.b": 1 }')::text as document, current_cursor_state(document) FROM documentdb_api.collection('db', 'cursors_seqscan_sharded') WHERE documentdb_api_internal.cursor_state(document, '{ "getpage_batchCount": 3 }') AND document @@ '{ "a.b": { "$gt": 12 }}'$$);

-- now test resume from continuation
SELECT document, current_cursor_state(document) AS cursor1 INTO TEMPORARY d1 FROM documentdb_api.collection('db', 'cursors_seqscan_sharded') WHERE documentdb_api_internal.cursor_state(document, '{ "getpage_batchCount": 3 }') AND document @@ '{ "a.b": { "$gt": 12 }}';

SELECT r2 FROM (SELECT $$'$$ || row_get_bson(rec) || $$'$$ AS r2 FROM (SELECT 3 AS "getpage_batchCount", array_append('{}'::bson[], cursor1) AS "continuation" FROM d1 ORDER BY document -> 'sh', document-> '_id' OFFSET 2 LIMIT 1 ) rec) r2 \gset
-- print the continuation
\echo :r2

-- now run the query with the continuation.
SELECT document, bson_dollar_project(document, '{ "a.b": 1 }'), current_cursor_state(document) INTO TEMPORARY d2 FROM documentdb_api.collection('db', 'cursors_seqscan_sharded') WHERE documentdb_api_internal.cursor_state(document, :r2) AND document @@ '{ "a.b": { "$gt": 12 }}';
SELECT document,  bson_dollar_project, bson_dollar_project(current_cursor_state, '{ "table_name": 0 }') FROM d2 order by document -> '_id';
EXPLAIN (VERBOSE ON, COSTS OFF ) SELECT document, bson_dollar_project(document, '{ "a.b": 1 }'), current_cursor_state(document) FROM documentdb_api.collection('db', 'cursors_seqscan_sharded') WHERE documentdb_api_internal.cursor_state(document, :r2) AND document @@ '{ "a.b": { "$gt": 12 }}';


-- now try with multi-continuation with a different table
SELECT r3 FROM (SELECT $$'$$ || row_get_bson(rec) || $$'$$ AS r3 FROM (SELECT 3 AS "getpage_batchCount", array_append('{}'::bson[], '{ "table_name": "someOtherTable" }'::bson) AS "continuation" FROM d1 OFFSET 2 LIMIT 1) rec) r3 \gset
-- print the continuation
\echo :r3

-- now run the query with the continuation (Should have no continuation).
SELECT document, bson_dollar_project(document, '{ "a.b": 1 }'), current_cursor_state(document) INTO TEMPORARY d3 FROM documentdb_api.collection('db', 'cursors_seqscan_sharded') WHERE documentdb_api_internal.cursor_state(document, :r3) AND document @@ '{ "a.b": { "$gt": 12 }}';
SELECT document,  bson_dollar_project, bson_dollar_project(current_cursor_state, '{ "table_name": 0 }') FROM d3 order by document -> '_id';
EXPLAIN (VERBOSE ON, COSTS OFF ) SELECT document, bson_dollar_project(document, '{ "a.b": 1 }'), current_cursor_state(document) FROM documentdb_api.collection('db', 'cursors_seqscan_sharded') WHERE documentdb_api_internal.cursor_state(document, :r3) AND document @@ '{ "a.b": { "$gt": 12 }}';

-- run with remote execution
set citus.enable_local_execution to off;
SELECT * FROM execute_and_sort($$SELECT  object_id, document::text || ', cursurState:' ||current_cursor_state(document)::text as document FROM documentdb_api.collection('db', 'cursors_seqscan_sharded') WHERE documentdb_api_internal.cursor_state(document, '{ "getpage_batchCount": 3 }') AND document @@ '{ "a.b": { "$gt": 12 }}'$$);
SELECT document, bson_dollar_project(document, '{ "a.b": 1 }'), current_cursor_state(document) INTO TEMPORARY d4 FROM documentdb_api.collection('db', 'cursors_seqscan_sharded') WHERE documentdb_api_internal.cursor_state(document, :r2) AND document @@ '{ "a.b": { "$gt": 12 }}';
SELECT document,  bson_dollar_project, bson_dollar_project(current_cursor_state, '{ "table_name": 0 }') FROM d4 order by document -> '_id';
