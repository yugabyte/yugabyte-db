SET search_path TO documentdb_core,documentdb_api,documentdb_api_catalog,documentdb_api_internal;

-- CREATE EXTENSION IF NOT EXISTS tsm_system_rows;

SET citus.next_shard_id TO 70000;
SET documentdb.next_collection_id TO 7000;
SET documentdb.next_collection_index_id TO 7000;

-- Insert data
SELECT documentdb_api.insert_one('db','sample',' { "_id" : 1, "item" : "almonds", "price" : 1, "quantity" : 1 }', NULL);
SELECT documentdb_api.insert_one('db','sample',' { "_id" : 2, "item" : "almonds", "price" : 1, "quantity" : 1 }', NULL);
SELECT documentdb_api.insert_one('db','sample',' { "_id" : 3, "item" : "almonds", "price" : 1, "quantity" : 1 }', NULL);
SELECT documentdb_api.insert_one('db','sample',' { "_id" : 4, "item" : "almonds", "price" : 1, "quantity" : 1 }', NULL);
SELECT documentdb_api.insert_one('db','sample',' { "_id" : 5, "item" : "almonds", "price" : 1, "quantity" : 1 }', NULL);
SELECT documentdb_api.insert_one('db','sample',' { "_id" : 6, "item" : "almonds", "price" : 1, "quantity" : 1 }', NULL);

-- Tests and explain for collection with data
-- SYSTEM sampling method, SYSTEM_ROWS performs block-level sampling,
-- so that the sample is not completely random but may be subject to clustering effects.
-- especially if only a small number of rows are requested.
-- https://www.postgresql.org/docs/current/tsm-system-rows.html

-- Sample with cursor for unsharded collection not supported - use persisted cursor
SELECT * FROM documentdb_api.aggregate_cursor_first_page(database => 'db', commandSpec => '{ "aggregate": "sample", "pipeline": [ { "$sample": { "size": 3 } }, { "$project": { "_id": 0 } } ], "cursor": { "batchSize": 1 } }', cursorId => 4294967294);

-- Shard orders collection on item 
SELECT documentdb_api.shard_collection('db','sample', '{"item":"hashed"}', false);

-- If the collection is sharded, have to call TABLESAMPLE SYSTEM_ROWS(n) LIMIT n
-- SYSTEM_ROWS(n) may always be optimal, but important, as one but all shards may be 
-- emptty. If we use SYSTEM_ROWS(<n), we might have to go back to get more data.
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "sample", "pipeline": [ { "$sample": { "size": 3 } }, { "$project": { "_id": 0 } } ] }');
SELECT documentdb_distributed_test_helpers.mask_plan_id_from_distributed_subplan($Q$
EXPLAIN(costs off) SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "sample", "pipeline": [ { "$sample": { "size": 3 } }, { "$project": { "_id": 0 } } ] }');
$Q$);
