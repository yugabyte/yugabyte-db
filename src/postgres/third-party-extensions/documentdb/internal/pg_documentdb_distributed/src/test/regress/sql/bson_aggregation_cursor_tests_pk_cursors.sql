SET search_path TO documentdb_api_catalog, documentdb_api, documentdb_core, public;
SET documentdb.next_collection_id TO 400;
SET documentdb.next_collection_index_id TO 400;
SET citus.next_shard_id TO 40000;

set documentdb.enablePrimaryKeyCursorScan to on;

-- insert 10 documents - but insert the _id as reverse order of insertion order (so that TID and insert order do not match)
DO $$
DECLARE i int;
BEGIN
FOR i IN 1..10 LOOP
PERFORM documentdb_api.insert_one('pkcursordb', 'aggregation_cursor_pk', FORMAT('{ "_id": %s, "sk": %s, "a": "%s", "c": [ %s "d" ] }',  10-i , mod(i, 2), repeat('Sample', 10), repeat('"' || repeat('a', 10) || '", ', 5))::documentdb_core.bson);
END LOOP;
END;
$$;

PREPARE drain_find_query(bson, bson) AS
    (WITH RECURSIVE cte AS (
        SELECT cursorPage, continuation FROM find_cursor_first_page(database => 'pkcursordb', commandSpec => $1, cursorId => 534)
        UNION ALL
        SELECT gm.cursorPage, gm.continuation FROM cte, cursor_get_more(database => 'pkcursordb', getMoreSpec => $2, continuationSpec => cte.continuation) gm
            WHERE cte.continuation IS NOT NULL
    )
    SELECT * FROM cte);

PREPARE drain_find_query_continuation(bson, bson) AS
    (WITH RECURSIVE cte AS (
        SELECT cursorPage, continuation FROM find_cursor_first_page(database => 'pkcursordb', commandSpec => $1, cursorId => 534)
        UNION ALL
        SELECT gm.cursorPage, gm.continuation FROM cte, cursor_get_more(database => 'pkcursordb', getMoreSpec => $2, continuationSpec => cte.continuation) gm
            WHERE cte.continuation IS NOT NULL
    )
    SELECT bson_dollar_project(cursorPage, '{"firstBatchLength": { "$size": { "$ifNull": ["$cursor.firstBatch", []]}}, "nextBatchLength": { "$size": { "$ifNull": ["$cursor.nextBatch", []]}}}'), continuation FROM cte);

-- create an index to force non HOT path
SELECT documentdb_api_internal.create_indexes_non_concurrently('pkcursordb', '{"createIndexes": "aggregation_cursor_pk", "indexes": [{"key": {"a": 1}, "name": "a_1" }]}', TRUE);

-- create a streaming cursor (that doesn't drain)
CREATE TEMP TABLE firstPageResponse AS
SELECT bson_dollar_project(cursorpage, '{ "cursor.firstBatch._id": 1, "cursor.id": 1 }'), continuation, persistconnection, cursorid FROM
    find_cursor_first_page(database => 'pkcursordb', commandSpec => '{ "find": "aggregation_cursor_pk", "batchSize": 5 }', cursorId => 4294967294);

SELECT * FROM firstPageResponse;

-- now drain it
SELECT continuation AS r1_continuation FROM firstPageResponse \gset
SELECT bson_dollar_project(cursorpage, '{ "cursor.nextBatch._id": 1, "cursor.id": 1 }'), continuation FROM
    cursor_get_more(database => 'pkcursordb', getMoreSpec => '{ "collection": "aggregation_cursor_pk", "getMore": 4294967294, "batchSize": 6 }', continuationSpec => :'r1_continuation');

-- drain with batchsize of 1 - continuation _ids should increase until it drains: uses pk scan continuation tokens
EXECUTE drain_find_query('{ "find": "aggregation_cursor_pk", "projection": { "_id": 1 }, "batchSize": 2 }', '{ "getMore": { "$numberLong": "534" }, "collection": "aggregation_cursor_pk", "batchSize": 1 }');

-- drain with batchsize of 1 - with the GUC disabled, it should be returned in reverse (TID) order
set documentdb.enablePrimaryKeyCursorScan to off;
EXECUTE drain_find_query('{ "find": "aggregation_cursor_pk", "projection": { "_id": 1 }, "batchSize": 2 }', '{ "getMore": { "$numberLong": "534" }, "collection": "aggregation_cursor_pk", "batchSize": 1 }');

set documentdb.enablePrimaryKeyCursorScan to on;

-- the query honors filters in continuations.
EXECUTE drain_find_query('{ "find": "aggregation_cursor_pk", "projection": { "_id": 1 }, "filter": { "_id": { "$gt": 3, "$lt": 8 }}, "batchSize": 1 }', '{ "getMore": { "$numberLong": "534" }, "collection": "aggregation_cursor_pk", "batchSize": 1 }');

-- if a query picks a pk index scan on the first page, the second page is guaranteed to pick it:
DROP TABLE firstPageResponse;
CREATE TEMP TABLE firstPageResponse AS
SELECT bson_dollar_project(cursorpage, '{ "cursor.firstBatch._id": 1, "cursor.id": 1 }'), continuation, persistconnection, cursorid FROM
    find_cursor_first_page(database => 'pkcursordb', commandSpec => '{ "find": "aggregation_cursor_pk",  "projection": { "_id": 1 }, "filter": { "_id": { "$gt": 3, "$lt": 8 }}, "batchSize": 2 }', cursorId => 4294967294);

SELECT * FROM firstPageResponse;

-- disable these to ensure we pick seqscan as the default path.
set enable_indexscan to off;
set enable_bitmapscan to off;

-- now drain it partially
SELECT continuation AS r1_continuation FROM firstPageResponse \gset
SELECT bson_dollar_project(cursorpage, '{ "cursor.nextBatch._id": 1, "cursor.id": 1 }'), continuation FROM
    cursor_get_more(database => 'pkcursordb', getMoreSpec => '{ "collection": "aggregation_cursor_pk", "getMore": 4294967294, "batchSize": 1 }', continuationSpec => :'r1_continuation');

-- drain it fully
SELECT bson_dollar_project(cursorpage, '{ "cursor.nextBatch._id": 1, "cursor.id": 1 }'), continuation FROM
    cursor_get_more(database => 'pkcursordb', getMoreSpec => '{ "collection": "aggregation_cursor_pk", "getMore": 4294967294, "batchSize": 5 }', continuationSpec => :'r1_continuation');

-- explain the first query (should be an index scan on the pk index)
set documentdb.enableCursorsOnAggregationQueryRewrite to on;
EXPLAIN (VERBOSE ON, COSTS OFF) SELECT document FROM bson_aggregation_find('pkcursordb', '{ "find": "aggregation_cursor_pk", "projection": { "_id": 1 }, "filter": { "_id": { "$gt": 3, "$lt": 8 }}, "batchSize": 1 }');
EXPLAIN (VERBOSE ON, COSTS OFF) SELECT document FROM bson_aggregation_find('pkcursordb', '{ "find": "aggregation_cursor_pk", "projection": { "_id": 1 }, "batchSize": 1 }');

-- the getmore should still work and use the _id index
EXPLAIN (VERBOSE ON, COSTS OFF) SELECT document FROM bson_aggregation_getmore('pkcursordb',
    '{ "getMore": { "$numberLong": "4294967294" }, "collection": "aggregation_cursor_pk", "batchSize": 1 }', :'r1_continuation');

set enable_indexscan to on;
set enable_bitmapscan to on;
set enable_seqscan to off;

EXPLAIN (VERBOSE ON, COSTS OFF) SELECT document FROM bson_aggregation_find('pkcursordb', '{ "find": "aggregation_cursor_pk", "projection": { "_id": 1 }, "filter": { "_id": { "$gt": 3, "$lt": 8 }}, "batchSize": 1 }');
EXPLAIN (VERBOSE ON, COSTS OFF) SELECT document FROM bson_aggregation_find('pkcursordb', '{ "find": "aggregation_cursor_pk", "projection": { "_id": 1 }, "batchSize": 1 }');

-- the getmore should still work and use the _id index
EXPLAIN (VERBOSE ON, COSTS OFF) SELECT document FROM bson_aggregation_getmore('pkcursordb',
    '{ "getMore": { "$numberLong": "4294967294" }, "collection": "aggregation_cursor_pk", "batchSize": 1 }', :'r1_continuation');


-- shard the collection
SELECT documentdb_api.shard_collection('{ "shardCollection": "pkcursordb.aggregation_cursor_pk", "key": { "_id": "hashed" }, "numInitialChunks": 3 }');

-- now this walks in order of shard-key THEN _id.
BEGIN;
set local enable_seqscan to on;
set local enable_indexscan to off;
set local enable_bitmapscan to off;
set local documentdb.enablePrimaryKeyCursorScan to on;
set local citus.max_adaptive_executor_pool_size to 1;
EXECUTE drain_find_query('{ "find": "aggregation_cursor_pk", "projection": { "_id": 1 }, "batchSize": 2 }', '{ "getMore": { "$numberLong": "534" }, "collection": "aggregation_cursor_pk", "batchSize": 2 }');

-- continues to work with filters
EXECUTE drain_find_query('{ "find": "aggregation_cursor_pk", "projection": { "_id": 1 }, "filter": { "_id": { "$gt": 2, "$lt": 8 } }, "batchSize": 2 }', '{ "getMore": { "$numberLong": "534" }, "collection": "aggregation_cursor_pk", "batchSize": 2 }');
ROLLBACK;

BEGIN;
set local documentdb.enablePrimaryKeyCursorScan to on;
set local citus.max_adaptive_executor_pool_size to 1;
EXECUTE drain_find_query('{ "find": "aggregation_cursor_pk", "projection": { "_id": 1 }, "batchSize": 2 }', '{ "getMore": { "$numberLong": "534" }, "collection": "aggregation_cursor_pk", "batchSize": 2 }');

-- continues to work with filters
EXECUTE drain_find_query('{ "find": "aggregation_cursor_pk", "projection": { "_id": 1 }, "filter": { "_id": { "$gt": 2, "$lt": 8 } }, "batchSize": 2 }', '{ "getMore": { "$numberLong": "534" }, "collection": "aggregation_cursor_pk", "batchSize": 2 }');
EXECUTE drain_find_query('{ "find": "aggregation_cursor_pk", "projection": { "_id": 1 }, "filter": {"sk": {"$exists": true}}, "batchSize": 2 }', '{ "getMore": { "$numberLong": "534" }, "collection": "aggregation_cursor_pk", "batchSize": 2 }');
ROLLBACK;


-- we create a contrived scenario where we create _ids that are ~100b each and insert 1000 docs in there. This will ensure that
-- we have many pages to scan for the _id.
DO $$
DECLARE i int;
BEGIN
FOR i IN 1..1000 LOOP
PERFORM documentdb_api.insert_one('pkcursordb', 'aggregation_cursor_pk_sk2', FORMAT('{ "_id": "%s%s%s", "sk": "skval", "a": "aval", "c": [ "c", "d" ] }', CASE WHEN i >= 10 AND i < 100 THEN '0' WHEN i < 10 THEN '00' ELSE '' END, i, repeat('a', 100))::documentdb_core.bson);
END LOOP;
END;
$$;
DROP TABLE firstPageResponse;

CREATE TEMP TABLE firstPageResponse AS
SELECT bson_dollar_project(cursorpage, '{ "cursor.firstBatch._id": 1, "cursor.id": 1 }'), continuation, persistconnection, cursorid FROM
    find_cursor_first_page(database => 'pkcursordb', commandSpec => '{ "find": "aggregation_cursor_pk_sk2", "projection": { "_id": 1 }, "filter": { "_id": { "$gt": "002", "$lt": "015" } }, "batchSize": 2  }', cursorId => 4294967294);

SELECT continuation AS r1_continuation FROM firstPageResponse \gset

-- run the query once, this also fills the buffers and validates the index qual
EXPLAIN (VERBOSE OFF, COSTS OFF, BUFFERS OFF, ANALYZE ON, TIMING OFF, SUMMARY OFF) SELECT document FROM bson_aggregation_getmore('pkcursordb',
    '{ "getMore": { "$numberLong": "4294967294" }, "collection": "aggregation_cursor_pk_sk2", "batchSize": 2 }', :'r1_continuation');

-- now rerun with buffers on (there should be no I/O but it should only load as many index pages as we want rows in the shared hit)
EXPLAIN (VERBOSE OFF, COSTS OFF, BUFFERS ON, ANALYZE ON, TIMING OFF, SUMMARY OFF) SELECT document FROM bson_aggregation_getmore('pkcursordb',
    '{ "getMore": { "$numberLong": "4294967294" }, "collection": "aggregation_cursor_pk_sk2", "batchSize": 2 }', :'r1_continuation');


-- let's shard and now test with different combinations where we the shard key is on the primary key index and where it is not, or a compound shard key with _id + another field.
SELECT documentdb_api.shard_collection('{ "shardCollection": "pkcursordb.aggregation_cursor_pk_sk2", "key": { "_id": "hashed" }, "numInitialChunks": 5 }');

BEGIN;
set citus.propagate_set_commands to 'local';
set local citus.max_adaptive_executor_pool_size to 1;
set local citus.enable_local_execution to off;
set local citus.explain_analyze_sort_method to taskId;
set local documentdb.enablePrimaryKeyCursorScan to on;

-- shard key is _id so range queries should still do bitmap heap scan on the RUM index since we don't have a shard_key filter.
DROP TABLE firstPageResponse;
CREATE TEMP TABLE firstPageResponse AS
SELECT bson_dollar_project(cursorpage, '{ "cursor.firstBatch._id": 1, "cursor.id": 1 }'), continuation, persistconnection, cursorid FROM
    find_cursor_first_page(database => 'pkcursordb', commandSpec => '{ "find": "aggregation_cursor_pk_sk2", "projection": { "_id": 1 }, "filter": { "_id": { "$gt": "002", "$lt": "015" } }, "batchSize": 2  }', cursorId => 4294967294);

SELECT continuation AS r1_continuation FROM firstPageResponse \gset

SHOW documentdb.enablePrimaryKeyCursorScan;

EXPLAIN (VERBOSE OFF, COSTS OFF, BUFFERS OFF, ANALYZE ON, TIMING OFF, SUMMARY OFF) SELECT document FROM bson_aggregation_find('pkcursordb', '{ "find": "aggregation_cursor_pk_sk2", "projection": { "_id": 1 }, "filter": { "_id": { "$gt": "002", "$lt": "015" }}, "batchSize": 2 }');

EXPLAIN (VERBOSE OFF, COSTS OFF, BUFFERS OFF, ANALYZE ON, TIMING OFF, SUMMARY OFF) SELECT document FROM bson_aggregation_getmore('pkcursordb',
    '{ "getMore": { "$numberLong": "4294967294" }, "collection": "aggregation_cursor_pk_sk2", "batchSize": 2 }', :'r1_continuation');

-- equality on another field that doesn't have an index should use pk index scan
EXECUTE drain_find_query_continuation('{ "find": "aggregation_cursor_pk_sk2", "projection": { "_id": 1 }, "filter": { "sk": "skval" }, "batchSize": 1 }', '{ "getMore": { "$numberLong": "534" }, "collection": "aggregation_cursor_pk_sk2", "batchSize": 500 }');

DROP TABLE firstPageResponse;
CREATE TEMP TABLE firstPageResponse AS
SELECT bson_dollar_project(cursorpage, '{ "cursor.firstBatch._id": 1, "cursor.id": 1 }') as cp, continuation, persistconnection, cursorid FROM
    find_cursor_first_page(database => 'pkcursordb', commandSpec => '{ "find": "aggregation_cursor_pk_sk2", "projection": { "_id": 1 }, "filter": { "sk": "skval" }, "batchSize": 3  }', cursorId => 4294967294);

SELECT continuation AS r1_continuation FROM firstPageResponse \gset

SELECT cp, continuation FROM firstPageResponse;

EXPLAIN (VERBOSE OFF, COSTS OFF, BUFFERS OFF, ANALYZE ON, TIMING OFF, SUMMARY OFF) SELECT document FROM bson_aggregation_find('pkcursordb', '{ "find": "aggregation_cursor_pk_sk2", "projection": { "_id": 1 }, "filter": { "sk": "skval" },  "batchSize": 1 }');

EXPLAIN (VERBOSE OFF, COSTS OFF, BUFFERS OFF, ANALYZE ON, TIMING OFF, SUMMARY OFF) SELECT document FROM bson_aggregation_getmore('pkcursordb',
    '{ "getMore": { "$numberLong": "4294967294" }, "collection": "aggregation_cursor_pk_sk2", "batchSize": 2 }', :'r1_continuation');

-- TODO: optimization, $in on shard_key could also use primary key index.
DROP TABLE firstPageResponse;
CREATE TEMP TABLE firstPageResponse AS
SELECT bson_dollar_project(cursorpage, '{ "cursor.firstBatch._id": 1, "cursor.id": 1 }'), continuation, persistconnection, cursorid FROM
    find_cursor_first_page(database => 'pkcursordb', commandSpec => '{ "find": "aggregation_cursor_pk_sk2", "projection": { "_id": 1 }, "filter": { "_id": { "$in":  [ "003aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", "002aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa" ]} }, "batchSize": 1  }', cursorId => 4294967294);

SELECT continuation AS r1_continuation FROM firstPageResponse \gset

EXPLAIN (VERBOSE OFF, COSTS OFF, BUFFERS OFF, ANALYZE ON, TIMING OFF, SUMMARY OFF) SELECT document FROM bson_aggregation_find('pkcursordb', '{ "find": "aggregation_cursor_pk_sk2", "projection": { "_id": 1 }, "filter": { "_id": { "$in":  [ "003aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", "002aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa" ]}}, "batchSize": 1 }');

EXPLAIN (VERBOSE OFF, COSTS OFF, BUFFERS OFF, ANALYZE ON, TIMING OFF, SUMMARY OFF) SELECT document FROM bson_aggregation_getmore('pkcursordb',
    '{ "getMore": { "$numberLong": "4294967294" }, "collection": "aggregation_cursor_pk_sk2", "batchSize": 2 }', :'r1_continuation');
END;

-- unshard and shard with a shard key that is not the primary key
SELECT documentdb_api.unshard_collection('{"unshardCollection": "pkcursordb.aggregation_cursor_pk_sk2" }');
SELECT documentdb_api.shard_collection('{ "shardCollection": "pkcursordb.aggregation_cursor_pk_sk2", "key": { "sk": "hashed" }, "numInitialChunks": 5 }');

BEGIN;
set citus.propagate_set_commands to 'local';
set local citus.max_adaptive_executor_pool_size to 1;
set local citus.enable_local_execution to off;
set local citus.explain_analyze_sort_method to taskId;
set local documentdb.enablePrimaryKeyCursorScan to on;

-- range queries on _id with shard_key equality should use pk index scan
DROP TABLE firstPageResponse;
CREATE TEMP TABLE firstPageResponse AS
SELECT bson_dollar_project(cursorpage, '{ "cursor.firstBatch._id": 1, "cursor.id": 1 }'), continuation, persistconnection, cursorid FROM
    find_cursor_first_page(database => 'pkcursordb', commandSpec => '{ "find": "aggregation_cursor_pk_sk2", "projection": { "_id": 1 }, "filter": { "_id": { "$gt": "002" }, "sk": "skval" }, "batchSize": 2  }', cursorId => 4294967294);

SELECT continuation AS r1_continuation FROM firstPageResponse \gset

EXPLAIN (VERBOSE OFF, COSTS OFF, BUFFERS OFF, ANALYZE ON, TIMING OFF, SUMMARY OFF) SELECT document FROM bson_aggregation_find('pkcursordb', '{ "find": "aggregation_cursor_pk_sk2", "projection": { "_id": 1 }, "filter": { "_id": { "$gt": "002" }, "sk": "skval" }, "batchSize": 2 }');

EXPLAIN (VERBOSE OFF, COSTS OFF, BUFFERS OFF, ANALYZE ON, TIMING OFF, SUMMARY OFF) SELECT document FROM bson_aggregation_getmore('pkcursordb',
    '{ "getMore": { "$numberLong": "4294967294" }, "collection": "aggregation_cursor_pk_sk2", "batchSize": 2 }', :'r1_continuation');

-- test draining the query
EXECUTE drain_find_query_continuation('{ "find": "aggregation_cursor_pk_sk2", "projection": { "_id": 1 }, "filter": { "_id": { "$gt": "002" }, "sk": "skval" }, "batchSize": 1 }', '{ "getMore": { "$numberLong": "534" }, "collection": "aggregation_cursor_pk_sk2", "batchSize": 500 }');

-- TODO: optimization, range queries on _id + shard_key_filter should also use primary key index, it currently uses the RUM _id index with the @<> range operator.
DROP TABLE firstPageResponse;
CREATE TEMP TABLE firstPageResponse AS
SELECT bson_dollar_project(cursorpage, '{ "cursor.firstBatch._id": 1, "cursor.id": 1 }'), continuation, persistconnection, cursorid FROM
    find_cursor_first_page(database => 'pkcursordb', commandSpec => '{ "find": "aggregation_cursor_pk_sk2", "projection": { "_id": 1 }, "filter": { "_id": { "$gt": "002", "$lt": "015" }, "sk": "skval" }, "batchSize": 2  }', cursorId => 4294967294);

SELECT continuation AS r1_continuation FROM firstPageResponse \gset

EXPLAIN (VERBOSE OFF, COSTS OFF, BUFFERS OFF, ANALYZE ON, TIMING OFF, SUMMARY OFF) SELECT document FROM bson_aggregation_find('pkcursordb', '{ "find": "aggregation_cursor_pk_sk2", "projection": { "_id": 1 }, "filter": { "_id": { "$gt": "002", "$lt": "015" }, "sk": "skval" }, "batchSize": 2 }');

EXPLAIN (VERBOSE OFF, COSTS OFF, BUFFERS OFF, ANALYZE ON, TIMING OFF, SUMMARY OFF) SELECT document FROM bson_aggregation_getmore('pkcursordb',
    '{ "getMore": { "$numberLong": "4294967294" }, "collection": "aggregation_cursor_pk_sk2", "batchSize": 2 }', :'r1_continuation');

-- equality on the shard key since we don't have an index on it should do primary key scan
DROP TABLE firstPageResponse;
CREATE TEMP TABLE firstPageResponse AS
SELECT bson_dollar_project(cursorpage, '{ "cursor.firstBatch._id": 1, "cursor.id": 1 }'), continuation, persistconnection, cursorid FROM
    find_cursor_first_page(database => 'pkcursordb', commandSpec => '{ "find": "aggregation_cursor_pk_sk2", "projection": { "_id": 1 }, "filter": { "sk": "skval" }, "batchSize": 2  }', cursorId => 4294967294);

SELECT continuation AS r1_continuation FROM firstPageResponse \gset

EXPLAIN (VERBOSE OFF, COSTS OFF, BUFFERS OFF, ANALYZE ON, TIMING OFF, SUMMARY OFF) SELECT document FROM bson_aggregation_find('pkcursordb', '{ "find": "aggregation_cursor_pk_sk2", "projection": { "_id": 1 }, "filter": { "sk": "skval" }, "batchSize": 2 }');

EXPLAIN (VERBOSE OFF, COSTS OFF, BUFFERS OFF, ANALYZE ON, TIMING OFF, SUMMARY OFF) SELECT document FROM bson_aggregation_getmore('pkcursordb',
    '{ "getMore": { "$numberLong": "4294967294" }, "collection": "aggregation_cursor_pk_sk2", "batchSize": 2 }', :'r1_continuation');

-- TODO: optimization, $in on shard_key could also use primary key index instead of bitmap scan on RUM.
DROP TABLE firstPageResponse;
CREATE TEMP TABLE firstPageResponse AS
SELECT bson_dollar_project(cursorpage, '{ "cursor.firstBatch._id": 1, "cursor.id": 1 }'), continuation, persistconnection, cursorid FROM
    find_cursor_first_page(database => 'pkcursordb', commandSpec => '{ "find": "aggregation_cursor_pk_sk2", "projection": { "_id": 1 }, "filter": { "sk": { "$in": [ "skval", "skval2", "skval3" ] } }, "batchSize": 2  }', cursorId => 4294967294);

SELECT continuation AS r1_continuation FROM firstPageResponse \gset

EXPLAIN (VERBOSE OFF, COSTS OFF, BUFFERS OFF, ANALYZE ON, TIMING OFF, SUMMARY OFF) SELECT document FROM bson_aggregation_find('pkcursordb', '{ "find": "aggregation_cursor_pk_sk2", "projection": { "_id": 1 }, "filter": { "sk": { "$in": [ "skval", "skval2", "skval3" ] } }, "batchSize": 2 }');

EXPLAIN (VERBOSE OFF, COSTS OFF, BUFFERS OFF, ANALYZE ON, TIMING OFF, SUMMARY OFF) SELECT document FROM bson_aggregation_getmore('pkcursordb',
    '{ "getMore": { "$numberLong": "4294967294" }, "collection": "aggregation_cursor_pk_sk2", "batchSize": 2 }', :'r1_continuation');
END;