SET search_path TO documentdb_api_catalog, documentdb_api, documentdb_core, documentdb_api_internal, public;
SET citus.next_shard_id TO 6700000;
SET documentdb.next_collection_id TO 6700;
SET documentdb.next_collection_index_id TO 6700;

-- create a collection
SELECT documentdb_api.create_collection('db', 'cursors_basic');

-- insert 20 documents
WITH r1 AS (SELECT FORMAT('{"_id": %I, "a": { "b": { "$numberInt": %I }, "c": { "$numberInt" : %I }, "d": [ { "$numberInt" : %I }, { "$numberInt" : %I } ] }}', g.Id, g.Id, g.Id, g.Id, g.Id)::bson AS formatDoc FROM generate_series(1, 20) AS g (id) ORDER BY g.Id desc) 
SELECT documentdb_api.insert_one('db', 'cursors_basic', r1.formatDoc) FROM r1;

-- run the default test.
-- now query them with varying page sizes using cursors.
SELECT document FROM documentdb_api.collection('db', 'cursors_basic') WHERE documentdb_api_internal.cursor_state(document, '{ "getpage_batchCount": 1 }') AND document @@ '{ "a.b": { "$gt": 12 }}';
SELECT document FROM documentdb_api.collection('db', 'cursors_basic') WHERE documentdb_api_internal.cursor_state(document, '{ "getpage_batchCount": 3 }') AND document @@ '{ "a.b": { "$gt": 12 }}';
SELECT document FROM documentdb_api.collection('db', 'cursors_basic') WHERE documentdb_api_internal.cursor_state(document, '{ "getpage_batchCount": 6 }') AND document @@ '{ "a.b": { "$gt": 12 }}';
SELECT document FROM documentdb_api.collection('db', 'cursors_basic') WHERE documentdb_api_internal.cursor_state(document, '{ "getpage_batchCount": 10 }') AND document @@ '{ "a.b": { "$gt": 12 }}';


-- try more complex queries that should work.
SELECT document FROM documentdb_api.collection('db', 'cursors_basic') WHERE documentdb_api_internal.cursor_state(document, '{ "getpage_batchCount": 1 }') AND document @@ '{ "a.d": { "$elemMatch": { "$gt": 2 } }}';
SELECT document FROM documentdb_api.collection('db', 'cursors_basic') WHERE documentdb_api_internal.cursor_state(document, '{ "getpage_batchCount": 5 }') AND document @@ '{ "a.d": { "$elemMatch": { "$gt": 2 } }}';
SELECT document FROM documentdb_api.collection('db', 'cursors_basic') WHERE documentdb_api_internal.cursor_state(document, '{ "getpage_batchCount": 10 }') AND document @@ '{ "a.d": { "$elemMatch": { "$gt": 2 } }}';

-- add projections to this.
SELECT document, bson_dollar_project(document, '{ "a.b": 1 }') FROM documentdb_api.collection('db', 'cursors_basic') WHERE documentdb_api_internal.cursor_state(document, '{ "getpage_batchCount": 6 }') AND document @@ '{ "a.b": { "$gt": 2 }}';

-- try on tables that do not exist
SELECT document FROM documentdb_api.collection('db', 'cursors_basic_table_dne') WHERE documentdb_api_internal.cursor_state(document, '{ "getpage_batchCount": 1 }') AND document @@ '{ "a.b": { "$gt": 12 }}';
SELECT document FROM documentdb_api.collection('db', 'cursors_basic_table_dne') WHERE documentdb_api_internal.cursor_state(document, '{ "getpage_batchCount": 3 }') AND document @@ '{ "a.b": { "$gt": 12 }}';


-- try prepared queries
PREPARE q1(text, text, bson, bson) AS SELECT document FROM documentdb_api.collection($1, $2) WHERE documentdb_api_internal.cursor_state(document, $3) AND document @@ $4;

EXECUTE q1('db', 'cursors_basic', '{ "getpage_batchCount": 1 }', '{ "a.b": { "$gt": 12 }}');
EXECUTE q1('db', 'cursors_basic', '{ "getpage_batchCount": 3 }', '{ "a.b": { "$gt": 12 }}');
EXECUTE q1('db', 'cursors_basic', '{ "getpage_batchCount": 7 }', '{ "a.b": { "$gt": 12 }}');

-- try on tables that do not exist.
EXECUTE q1('db', 'cursors_basic_table_dne', '{ "getpage_batchCount": 3 }', '{ "a.b": { "$gt": 12 }}');
EXECUTE q1('db', 'cursors_basic_table_dne', '{ "getpage_batchCount": 7 }', '{ "a.b": { "$gt": 12 }}');


-- queries that return no rows.
SELECT document FROM documentdb_api.collection('db', 'cursors_basic') WHERE documentdb_api_internal.cursor_state(document, '{ "getpage_batchCount": 6 }') AND document @@ '{ "a.b": { "$gt": 25 }}';
SELECT document FROM documentdb_api.collection('db', 'cursors_basic') WHERE documentdb_api_internal.cursor_state(document, '{ "getpage_batchCount": 11 }') AND document @@ '{ "a.b": { "$gt": 25 }}';
EXECUTE q1('db', 'cursors_basic', '{ "getpage_batchCount": 7 }', '{ "a.b": { "$gt": 25 }}');

-- Explain the queries.
EXPLAIN (VERBOSE ON, COSTS OFF) SELECT document FROM documentdb_api.collection('db', 'cursors_basic') WHERE documentdb_api_internal.cursor_state(document, '{ "getpage_batchCount": 1 }') AND document @@ '{ "a.b": { "$gt": 2 }}';
EXPLAIN (VERBOSE ON, COSTS OFF) SELECT document FROM documentdb_api.collection('db', 'cursors_basic') WHERE documentdb_api_internal.cursor_state(document, '{ "getpage_batchCount": 5 }') AND document @@ '{ "a.d": { "$elemMatch": { "$gt": 2 } }}';
EXPLAIN (VERBOSE ON, COSTS OFF) EXECUTE q1('db', 'cursors_basic', '{ "getpage_batchCount": 7 }', '{ "a.b": { "$gt": 2 }}');

EXPLAIN (VERBOSE ON, ANALYZE ON, COSTS OFF, SUMMARY OFF, TIMING OFF) EXECUTE q1('db', 'cursors_basic', '{ "getpage_batchCount": 7 }', '{ "a.b": { "$gt": 2 }}');

-- explain with projection (projection happens in the custom scan.)
EXPLAIN (VERBOSE ON, COSTS OFF) SELECT document, bson_dollar_project(document, '{ "a.b": 1 }') FROM documentdb_api.collection('db', 'cursors_basic') WHERE documentdb_api_internal.cursor_state(document, '{ "getpage_batchCount": 5 }') AND document @@ '{ "a.d": { "$elemMatch": { "$gt": 2 } }}';

-- do a primary key scan.
SELECT document FROM documentdb_api.collection('db', 'cursors_basic') WHERE documentdb_api_internal.cursor_state(document, '{ "getpage_batchCount": 5 }') AND document @@ '{ "_id": { "$gt": "10" } }';
EXPLAIN (VERBOSE ON, COSTS OFF) SELECT document FROM documentdb_api.collection('db', 'cursors_basic') WHERE documentdb_api_internal.cursor_state(document, '{ "getpage_batchCount": 5 }') AND document @@ '{ "_id": { "$gt": "10" } }';

-- do a primary key lookup
SELECT document FROM documentdb_api.collection('db', 'cursors_basic') WHERE documentdb_api_internal.cursor_state(document, '{ "getpage_batchCount": 5 }') AND document @@ '{ "_id": { "$eq": "10" } }';
EXPLAIN (VERBOSE ON, COSTS OFF) SELECT document FROM documentdb_api.collection('db', 'cursors_basic') WHERE documentdb_api_internal.cursor_state(document, '{ "getpage_batchCount": 5 }') AND document @@ '{ "_id": { "$eq": "10" } }';

SELECT documentdb_distributed_test_helpers.drop_primary_key('db', 'cursors_basic');

-- validate seq scan works.
EXECUTE q1('db', 'cursors_basic', '{ "getpage_batchCount": 1 }', '{ "a.b": { "$gt": 12 }}');
EXECUTE q1('db', 'cursors_basic', '{ "getpage_batchCount": 3 }', '{ "a.b": { "$gt": 12 }}');
EXECUTE q1('db', 'cursors_basic', '{ "getpage_batchCount": 11 }', '{ "a.b": { "$gt": 12 }}');

-- queries that return no rows.
SELECT document FROM documentdb_api.collection('db', 'cursors_basic') WHERE documentdb_api_internal.cursor_state(document, '{ "getpage_batchCount": 6 }') AND document @@ '{ "a.b": { "$gt": 25 }}';
SELECT document FROM documentdb_api.collection('db', 'cursors_basic') WHERE documentdb_api_internal.cursor_state(document, '{ "getpage_batchCount": 11 }') AND document @@ '{ "a.b": { "$gt": 25 }}';
EXECUTE q1('db', 'cursors_basic', '{ "getpage_batchCount": 7 }', '{ "a.b": { "$gt": 25 }}');

-- Explain the queries.
EXPLAIN (VERBOSE ON, COSTS OFF) SELECT document FROM documentdb_api.collection('db', 'cursors_basic') WHERE documentdb_api_internal.cursor_state(document, '{ "getpage_batchCount": 1 }') AND document @@ '{ "a.b": { "$gt": 2 }}';
EXPLAIN (VERBOSE ON, COSTS OFF) SELECT document FROM documentdb_api.collection('db', 'cursors_basic') WHERE documentdb_api_internal.cursor_state(document, '{ "getpage_batchCount": 5 }') AND document @@ '{ "a.d": { "$elemMatch": { "$gt": 2 } }}';
EXPLAIN (VERBOSE ON, COSTS OFF) EXECUTE q1('db', 'cursors_basic', '{ "getpage_batchCount": 7 }', '{ "a.b": { "$gt": 2 }}');

-- now create an index on a.b and a.d
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', documentdb_distributed_test_helpers.generate_create_index_arg('cursors_basic', 'index_1', '{"a.b": 1}'), true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', documentdb_distributed_test_helpers.generate_create_index_arg('cursors_basic', 'index_2', '{"a.d": 1}'), true);


BEGIN;
set local enable_seqscan TO OFF;
    -- now query them with varying page sizes using cursors - this will convert to a bitmap scan (as it's an index scan).
    SELECT document FROM documentdb_api.collection('db', 'cursors_basic') WHERE documentdb_api_internal.cursor_state(document, '{ "getpage_batchCount": 1 }') AND document @@ '{ "a.b": { "$gt": 2 }}';
    EXPLAIN (VERBOSE ON, COSTS OFF) SELECT document FROM documentdb_api.collection('db', 'cursors_basic') WHERE documentdb_api_internal.cursor_state(document, '{ "getpage_batchCount": 1 }') AND document @@ '{ "a.b": { "$gt": 2 }}';
ROLLBACK;

BEGIN;
set local enable_seqscan TO OFF;
set local enable_indexscan TO OFF;
    -- now query them with varying page sizes using cursors - this should work as a normal bitmap heap scan.
    SELECT document FROM documentdb_api.collection('db', 'cursors_basic') WHERE documentdb_api_internal.cursor_state(document, '{ "getpage_batchCount": 1 }') AND document @@ '{ "a.b": { "$gt": 2 }}';
    EXPLAIN (VERBOSE ON, COSTS OFF) SELECT document FROM documentdb_api.collection('db', 'cursors_basic') WHERE documentdb_api_internal.cursor_state(document, '{ "getpage_batchCount": 1 }') AND document @@ '{ "a.b": { "$gt": 2 }}';
ROLLBACK;

-- ensure parallel scans don't impact this.
DO $$
DECLARE
  v_collection_id bigint;
BEGIN
    SELECT collection_id INTO v_collection_id FROM documentdb_api_catalog.collections
  	    WHERE database_name = 'db' AND collection_name = 'cursors_basic';
    EXECUTE format('ALTER TABLE documentdb_data.documents_%s SET (parallel_workers = 1)',
                    v_collection_id);
END
$$;

BEGIN;
set local parallel_tuple_cost TO 0;
set local parallel_setup_cost TO 0;
set local enable_seqscan TO ON;
set local enable_indexscan TO OFF;
EXPLAIN (VERBOSE ON, COSTS OFF) SELECT document FROM documentdb_api.collection('db', 'cursors_basic') WHERE documentdb_api_internal.cursor_state(document, '{ "getpage_batchCount": 1 }') AND document @@ '{ "a.b": { "$gt": 2 }}';
SELECT document FROM documentdb_api.collection('db', 'cursors_basic') WHERE documentdb_api_internal.cursor_state(document, '{ "getpage_batchCount": 1 }') AND document @@ '{ "a.b": { "$gt": 2 }}';
ROLLBACK;

-- invalid cursor scans
PREPARE q2(text, text, bson, bson) AS SELECT document FROM documentdb_api.collection($1, $2) WHERE documentdb_api_internal.cursor_state(document, $3) AND document @@ $4 ORDER BY bson_orderby(document, '{ "a": 1 }');
EXECUTE q2('db', 'cursors_basic', '{}', '{}');
DEALLOCATE q2;

-- creates projectSet - not allowed.
SET client_min_messages TO ERROR;
PREPARE q2(text, text, bson, bson) AS SELECT bson_dollar_unwind(document, '{ "$a": 1 }') FROM documentdb_api.collection($1, $2) WHERE documentdb_api_internal.cursor_state(document, $3) AND document @@ $4;
EXECUTE q2('db', 'cursors_basic', '{}', '{}');
RESET client_min_messages;
DEALLOCATE q2;

PREPARE q2(text, text, bson, bson) AS SELECT bson_dollar_project(document, '{ "a": 1 }') FROM documentdb_api.collection($1, $2) WHERE documentdb_api_internal.cursor_state(document, $3) AND document @@ $4 OFFSET 1;
EXECUTE q2('db', 'cursors_basic', '{}', '{}');
DEALLOCATE q2;

-- now create many CTEs of Project/Filter/Project
PREPARE q2(text, text, bson) AS 
    WITH r1 AS (SELECT bson_dollar_project(document, '{ "a": 1, "d": "$a.b" }') AS document, current_cursor_state(document) AS cursorState FROM documentdb_api.collection($1, $2) WHERE documentdb_api_internal.cursor_state(document, $3) AND document @@ '{ "_id": { "$exists": true } }'),
         r2 AS (SELECT bson_dollar_add_fields(document, '{ "c": 4 }') AS document, cursorState FROM r1 WHERE document @@ '{ "d": 1 }'),
         r3 AS (SELECT bson_dollar_project(document, '{ "_id": 0 }') AS document, cursorState FROM r2 WHERE document @@ '{ "c": 4 }'),
         r4 AS (SELECT bson_dollar_add_fields(document, '{ "e.f": "foo" }') AS document, cursorState FROM r3),
         r5 AS (SELECT document, cursorState FROM r4 WHERE document @@ '{ "e.f" : "foo" }')
         SELECT * FROM r5;

EXECUTE q2('db', 'cursors_basic', '{}');

EXECUTE q2('db', 'cursors_non_existent', '{}');