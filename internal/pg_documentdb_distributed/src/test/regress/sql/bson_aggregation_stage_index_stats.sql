SET search_path TO documentdb_core,documentdb_api,documentdb_api_catalog,documentdb_api_internal;
SET citus.next_shard_id TO 385000;
SET documentdb.next_collection_id TO 3850;
SET documentdb.next_collection_index_id TO 3850;

CREATE OR REPLACE FUNCTION query_and_flush(field text)
RETURNS bson
set enable_seqscan to false
AS $$
DECLARE
    docText bson;
BEGIN
    SELECT cursorPage INTO docText FROM documentdb_api.aggregate_cursor_first_page('db', 
        FORMAT('{ "aggregate": "indexstats1", "pipeline": [ { "$match": { "%s": { "$gt": 500 } } }, { "$count": "c" } ], "cursor": {} }', field)::documentdb_core.bson);

    IF VERSION() LIKE 'PostgreSQL 14%' THEN
        PERFORM pg_sleep(3);
    ELSE
        PERFORM pg_stat_force_next_flush();
    END IF;
    RETURN docText;
END;
$$
LANGUAGE plpgsql;

SELECT documentdb_api.drop_collection('db', 'indexstats1');

-- fails on non existent collection
SELECT * FROM bson_aggregation_pipeline('db', '{ "aggregate": "indexstats1", "pipeline": [ { "$indexStats": {} }, { "$project": { "accesses.since": 0 }} ]}');

-- Create Collection
SELECT documentdb_api.create_collection('db', 'indexstats1');

SELECT * FROM bson_aggregation_pipeline('db', '{ "aggregate": "indexstats1", "pipeline": [ { "$indexStats": {} }, { "$project": { "accesses.since": 0 }} ]}');

-- Add 1000 docs
SELECT COUNT(*) FROM (SELECT documentdb_api.insert_one('db','indexstats1',FORMAT('{ "a" : %s, "_id": %s }', i, i)::bson, NULL) FROM generate_series(1, 10000) i) ins;

-- create index
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "indexstats1", "indexes": [ { "key": { "a": 1 }, "name": "a_1" } ] }', true);


SELECT * FROM bson_aggregation_pipeline('db', '{ "aggregate": "indexstats1", "pipeline": [ { "$indexStats": {} }, { "$project": { "accesses.since": 0 }} ]}');

-- query using index.
SELECT query_and_flush('_id');
SELECT query_and_flush('_id');
SELECT query_and_flush('a');

SELECT * FROM bson_aggregation_pipeline('db', '{ "aggregate": "indexstats1", "pipeline": [ { "$indexStats": {} }, { "$project": { "accesses.since": 0 }} ]}');

-- shard
SELECT documentdb_api.shard_collection('db', 'indexstats1', '{ "_id": "hashed" }', false);

SELECT * FROM bson_aggregation_pipeline('db', '{ "aggregate": "indexstats1", "pipeline": [ { "$indexStats": {} }, { "$project": { "accesses.since": 0 }} ]}');

-- query using index.
SELECT query_and_flush('_id');
SELECT query_and_flush('_id');
SELECT query_and_flush('a');

SELECT * FROM bson_aggregation_pipeline('db', '{ "aggregate": "indexstats1", "pipeline": [ { "$indexStats": {} }, { "$project": { "accesses.since": 0 }} ]}');


-- invalid cases
SELECT * FROM bson_aggregation_pipeline('db', '{ "aggregate": "indexstats1", "pipeline": [ { "$indexStats": { "a": 1 } }, { "$project": { "accesses.since": 0 }} ]}');
SELECT * FROM bson_aggregation_pipeline('db', '{ "aggregate": "indexstats1", "pipeline": [ { "$match": { "a": 1 } }, { "$indexStats": { } }, { "$project": { "accesses.since": 0 }} ]}');
