SET search_path TO documentdb_core,documentdb_api,documentdb_api_catalog,documentdb_api_internal;

SET citus.next_shard_id TO 416000;
SET documentdb.next_collection_id TO 4160;
SET documentdb.next_collection_index_id TO 4160;


-- create entries with accid == 1, 2, val == 3, 4
with r1 AS (SELECT FORMAT('{ "accid": %s, "val": %s, "text": "%s" }', ((s % 2) + 1), ((s % 2) + 3), repeat(md5(random()::text), 50))::json as textVal from generate_series(1, 25000) s),
r2 AS (SELECT json_build_object('insert', 'agg_pipeline_index_pushdown', 'documents', json_agg(r1.textVal)) AS jsonObj FROM r1)
SELECT documentdb_api.insert('db', (r2.jsonObj)::text::bson) FROM r2;

SELECT documentdb_api.insert_one('db', 'agg_pipeline_index_pushdown', '{ "accid": 1, "val": 5 }');
DO $$
DECLARE v_output record;
BEGIN
    FOR i IN 1..5 LOOP        
        with r1 AS (SELECT FORMAT('{ "accid": %s, "val": %s, "text": "%s" }', ((s % 2) + 1), ((s % 2) + 3), repeat(md5(random()::text), 50))::json from generate_series(1, 15000) s),
        r2 AS (SELECT json_build_object('insert', 'agg_pipeline_index_pushdown', 'documents', json_agg(r1)) AS jsonObj FROM r1)
        SELECT documentdb_api.insert('db', (r2.jsonObj)::text::bson) INTO v_output FROM r2;
    END LOOP;
END;
$$ LANGUAGE plpgsql;

SELECT documentdb_api.insert_one('db', 'agg_pipeline_index_pushdown', '{ "accid": 1, "val": 5 }');

-- create entries with accid == 3, 4, val == 5, 6

SELECT documentdb_api.insert_one('db', 'agg_pipeline_index_pushdown', '{ "accid": 2, "val": 6 }');

DO $$
DECLARE v_output record;
BEGIN
    FOR i IN 1..5 LOOP        
        with r1 AS (SELECT FORMAT('{ "accid": %s, "val": %s, "text": "%s" }', ((s % 2) + 3), ((s % 2) + 5), repeat(md5(random()::text), 50))::json from generate_series(1, 15000) s),
        r2 AS (SELECT json_build_object('insert', 'agg_pipeline_index_pushdown', 'documents', json_agg(r1)) AS jsonObj FROM r1)
        SELECT documentdb_api.insert('db', (r2.jsonObj)::text::bson) INTO v_output FROM r2;
    END LOOP;
END;
$$ LANGUAGE plpgsql;

SELECT documentdb_api.insert_one('db', 'agg_pipeline_index_pushdown', '{ "accid": 2, "val": 6 }');

SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "agg_pipeline_index_pushdown", "indexes": [ { "key": { "accid": 1, "val": 1 }, "name": "myIdx1" }]}', true);

ANALYZE documentdb_data.documents_4160;

-- First scenario with enable_indexscan to off: This technically loads 25000 rows on the bitmap scan
BEGIN;
set local seq_page_cost to 5;
set local rum.enable_semifast_gettuple to on;
set local documentdb.enableNewSelectivityMode to on;
EXPLAIN (COSTS OFF, BUFFERS OFF, ANALYZE ON, TIMING OFF, SUMMARY OFF) SELECT document FROM bson_aggregation_find('db', '{ "find": "agg_pipeline_index_pushdown", "filter": { "accid": 1 }, "skip": 100, "limit": 100 }');
ROLLBACK;

-- now turn on the flag - we should only load as many rows as the skip/limit
BEGIN;
set local seq_page_cost to 5;
set local rum.enable_semifast_gettuple to on;
set local documentdb.enableNewSelectivityMode to on;
EXPLAIN (COSTS OFF, BUFFERS OFF, ANALYZE ON, TIMING OFF, SUMMARY OFF) SELECT document FROM bson_aggregation_find('db', '{ "find": "agg_pipeline_index_pushdown", "filter": { "accid": 1 }, "skip": 100, "limit": 100 }');
ROLLBACK;
