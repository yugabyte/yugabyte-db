SET search_path TO documentdb_api_catalog, documentdb_api, documentdb_core, public;
SET documentdb.next_collection_id TO 600;
SET documentdb.next_collection_index_id TO 600;

DO $$
DECLARE i int;
BEGIN
-- each doc is "a": 500KB, "c": 5 MB - ~5.5 MB & there's 10 of them
FOR i IN 1..10 LOOP
PERFORM documentdb_api.insert_one('sb_db', 'sb_get_aggregation_cursor_test', FORMAT('{ "_id": %s, "a": "%s", "c": [ %s "d" ] }',  i, repeat('Sample', 100000), repeat('"' || repeat('a', 1000) || '", ', 5000))::documentdb_core.bson);
END LOOP;
END;
$$;

DO $$
DECLARE i int;
BEGIN
FOR i IN 1..10 LOOP
PERFORM documentdb_api.insert_one('sb_db', 'sb_get_aggregation_cursor_smalldoc_test', FORMAT('{ "_id": %s, "a": "%s", "c": [ %s "d" ] }',  i, repeat('Sample', 10), repeat('"' || repeat('a', 10) || '", ', 5))::documentdb_core.bson);
END LOOP;
END;
$$;

-- turn on cursors on explain to see how the queries are generated.
set documentdb.enableCursorsOnAggregationQueryRewrite to on;

-- simple finds use streaming cursors
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM documentdb_api_catalog.bson_aggregation_find('sb_db', '{ "find": "sb_get_aggregation_cursor_smalldoc_test" }');
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM documentdb_api_catalog.bson_aggregation_find('sb_db', '{ "find": "sb_get_aggregation_cursor_smalldoc_test", "filter": { "a": 1 } }');
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM documentdb_api_catalog.bson_aggregation_find('sb_db', '{ "find": "sb_get_aggregation_cursor_smalldoc_test", "filter": { "a": 1 }, "skip": 0 }');
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM documentdb_api_catalog.bson_aggregation_find('sb_db', '{ "find": "sb_get_aggregation_cursor_smalldoc_test", "filter": { "a": 1 }, "limit": 0 }');
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM documentdb_api_catalog.bson_aggregation_find('sb_db', '{ "find": "sb_get_aggregation_cursor_smalldoc_test", "filter": { "a": 1 }, "skip": 0, "limit": 0 }');

-- projection is applied after the cursor
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM documentdb_api_catalog.bson_aggregation_find('sb_db', '{ "find": "sb_get_aggregation_cursor_smalldoc_test", "filter": { "a": 1 }, "skip": 0, "limit": 0, "projection": { "a": 1 } }');

-- with sort/skip limit it's persisted cursors
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM documentdb_api_catalog.bson_aggregation_find('sb_db', '{ "find": "sb_get_aggregation_cursor_smalldoc_test", "filter": { "a": 1 }, "sort": { "b": 1 } }');
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM documentdb_api_catalog.bson_aggregation_find('sb_db', '{ "find": "sb_get_aggregation_cursor_smalldoc_test", "filter": { "a": 1 }, "sort": { "b": 1 }, "skip": 1 }');
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM documentdb_api_catalog.bson_aggregation_find('sb_db', '{ "find": "sb_get_aggregation_cursor_smalldoc_test", "filter": { "a": 1 }, "sort": { "b": 1 }, "limit": 1 }');

EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM documentdb_api_catalog.bson_aggregation_find('sb_db', '{ "find": "sb_get_aggregation_cursor_smalldoc_test", "filter": { "a": 1 }, "skip": 1 }');
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM documentdb_api_catalog.bson_aggregation_find('sb_db', '{ "find": "sb_get_aggregation_cursor_smalldoc_test", "filter": { "a": 1 }, "limit": 100 }');

-- projection is applied after skip/limit
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM documentdb_api_catalog.bson_aggregation_find('sb_db', '{ "find": "sb_get_aggregation_cursor_smalldoc_test", "filter": { "a": 1 }, "skip": 1, "projection": { "a": 1 }  }');
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM documentdb_api_catalog.bson_aggregation_find('sb_db', '{ "find": "sb_get_aggregation_cursor_smalldoc_test", "filter": { "a": 1 }, "limit": 100, "projection": { "a": 1 }  }');


-- for a case of limit 1 we use singleBatch cursors and not streaming
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM documentdb_api_catalog.bson_aggregation_find('sb_db', '{ "find": "sb_get_aggregation_cursor_smalldoc_test", "filter": { "a": 1 }, "limit": 1 }');
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM documentdb_api_catalog.bson_aggregation_find('sb_db', '{ "find": "sb_get_aggregation_cursor_smalldoc_test", "filter": { "a": 1 }, "skip": 0, "limit": 1 }');
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM documentdb_api_catalog.bson_aggregation_find('sb_db', '{ "find": "sb_get_aggregation_cursor_smalldoc_test", "filter": { "a": 1 }, "limit": 1 }');
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM documentdb_api_catalog.bson_aggregation_find('sb_db', '{ "find": "sb_get_aggregation_cursor_smalldoc_test", "filter": { "a": 1 }, "skip": 2, "limit": 1 }');
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM documentdb_api_catalog.bson_aggregation_find('sb_db', '{ "find": "sb_get_aggregation_cursor_smalldoc_test", "filter": { "a": 1 }, "skip": 2, "limit": 1, "projection": { "a": 1 } }');


-- do the same checks with aggregates
-- these are streamable
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('sb_db', '{ "aggregate": "sb_get_aggregation_cursor_smalldoc_test", "pipeline": [] }');
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('sb_db', '{ "aggregate": "sb_get_aggregation_cursor_smalldoc_test", "pipeline": [{ "$match": { "a": 1 }}] }');
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('sb_db', '{ "aggregate": "sb_get_aggregation_cursor_smalldoc_test", "pipeline": [{ "$match": { "a": 1 }}, { "$skip": 0 }] }');
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('sb_db', '{ "aggregate": "sb_get_aggregation_cursor_smalldoc_test", "pipeline": [ { "$skip": 0 }, { "$match": { "a": 1 }}] }');

-- these are persistent
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('sb_db', '{ "aggregate": "sb_get_aggregation_cursor_smalldoc_test", "pipeline": [{ "$match": { "a": 1 }},  { "$limit": 2 }] }');
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('sb_db', '{ "aggregate": "sb_get_aggregation_cursor_smalldoc_test", "pipeline": [{ "$match": { "a": 1 }}, { "$skip": 1 }, { "$limit": 4 }] }');

-- with limit 1 is singleBatch (not streaming)
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('sb_db', '{ "aggregate": "sb_get_aggregation_cursor_smalldoc_test", "pipeline": [ { "$limit": 1 }, { "$match": { "a": 1 }}] }');
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('sb_db', '{ "aggregate": "sb_get_aggregation_cursor_smalldoc_test", "pipeline": [ { "$match": { "a": 1 }}, { "$limit": 1 } ] }');
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('sb_db', '{ "aggregate": "sb_get_aggregation_cursor_smalldoc_test", "pipeline": [ { "$match": { "a": 1 }}, { "$limit": 1 }, { "$limit": 2 } ] }');

-- streamable with batchSize 0
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('sb_db', '{ "aggregate": "sb_get_aggregation_cursor_smalldoc_test", "pipeline": [ { "$match": { "a": 1 }}, { "$limit": 1 } ], "cursor": { "batchSize": 0 } }');

-- becomes streamable with guc off
set documentdb.enableConversionStreamableToSingleBatch to off;
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('sb_db', '{ "aggregate": "sb_get_aggregation_cursor_smalldoc_test", "pipeline": [ { "$match": { "a": 1 }}, { "$limit": 1 } ] }');

-- still not streamable
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('sb_db', '{ "aggregate": "sb_get_aggregation_cursor_smalldoc_test", "pipeline": [ { "$limit": 1 }, { "$match": { "a": 1 }}] }');
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('sb_db', '{ "aggregate": "sb_get_aggregation_cursor_smalldoc_test", "pipeline": [ { "$match": { "a": 1 }}, { "$limit": 1 }, { "$limit": 2 } ] }');

reset documentdb.enableConversionStreamableToSingleBatch;
-- should be singlebatch
set client_min_messages to DEBUG1;
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('sb_db', '{ "aggregate": "sb_get_aggregation_cursor_smalldoc_test", "pipeline": [ { "$match": { "a": 1 }}, { "$limit": 1 } ] }');

-- TODO: Should make this streamable
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('sb_db', '{ "aggregate": "sb_get_aggregation_cursor_smalldoc_test", "pipeline": [ { "$match": { "a": 1 }}, { "$limit": 1 }, { "$limit": 2 } ] }');

-- not streamable even with limit 1
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('sb_db', '{ "aggregate": "sb_get_aggregation_cursor_smalldoc_test", "pipeline": [ { "$match": { "a": 1 }}, { "$limit": 1 }, { "$unwind": "$a" } ] }');

