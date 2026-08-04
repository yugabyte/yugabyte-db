SET search_path TO documentdb_core,documentdb_api,documentdb_api_catalog,documentdb_api_internal;

SET citus.next_shard_id TO 8400000;
SET documentdb.next_collection_id TO 8400;
SET documentdb.next_collection_index_id TO 8400;

CREATE OR REPLACE FUNCTION batch_insert_testing_vector_documents_hnsw_planner(collectionName text, beginId integer, numDocuments integer, docPerBatch integer)
  RETURNS void
  LANGUAGE plpgsql
AS $fn$
DECLARE
    endId integer := beginId + numDocuments - 1;
    batchCnt integer := 0;
    batchIdx integer := 0;
    batchBeginId integer := 0;
    batchEndId integer := 0;
    v_insertSpec bson;
    v_resultDocs bson;
BEGIN
    RAISE NOTICE 'Inserting % documents into %', numDocuments, collectionName;
    if numDocuments%docPerBatch = 0 then
        batchCnt := numDocuments/docPerBatch;
    else
        batchCnt := numDocuments/docPerBatch + 1;
    end if;
    RAISE NOTICE 'Begin id: %, Batch size: %, batch count: %', beginId, docPerBatch, batchCnt;

    WHILE batchIdx < batchCnt LOOP
        batchBeginId := beginId + batchIdx * docPerBatch;
        batchEndId := beginId + (batchIdx + 1) * docPerBatch - 1;
        if endId < batchEndId then
            batchEndId := endId;
        end if;
        WITH r1 AS (SELECT counter from generate_series(batchBeginId, batchEndId) AS counter),
             r2 AS ( SELECT ('{ "_id": ' || counter || ', "a": "some sentence", "v": [ ' || 10+counter || ', ' || 15+counter || ', ' || 1.1+counter || ' ] }') AS documentValue FROM r1),
             r3 AS ( SELECT collectionName as insert, array_agg(r2.documentValue::bson) AS documents FROM r2)

        SELECT row_get_bson(r3) INTO v_insertSpec FROM r3;
        SELECT p_result INTO v_resultDocs FROM documentdb_api.insert('vector_db', v_insertSpec);
        batchIdx := batchIdx + 1;
    END LOOP;
END;
$fn$;

---------------------------------------------------------------------------------------------------------------------------
-- HNSW
-- HNSW create index, error cases
SELECT documentdb_api.insert_one('vector_db', 'agg_vector_hnsw_planner', '{ "_id":"1", "int": 10, "a" : { "b" : [ "x", 1, 2.0, true ] } }', NULL);
SELECT documentdb_api.insert_one('vector_db', 'agg_vector_hnsw_planner', '{ "_id":"2", "double": 2.0, "a" : { "b" : {"c": 3} } }', NULL);
SELECT documentdb_api.insert_one('vector_db', 'agg_vector_hnsw_planner', '{ "_id":"3", "boolean": false, "a" : "no", "b": "yes", "c": true }', NULL);
SELECT documentdb_api.insert_one('vector_db', 'agg_vector_hnsw_planner', '{ "_id": 6,  "a": "some sentence", "v": [3.0, 5.0, 1.1 ] }');
SELECT documentdb_api.insert_one('vector_db', 'agg_vector_hnsw_planner', '{ "_id": 7,  "a": "some other sentence", "v": [8.0, 5.0, 0.1 ] }');

ANALYZE;

-- HNSW search, success cases
SELECT documentdb_api_internal.create_indexes_non_concurrently('vector_db', '{ "createIndexes": "agg_vector_hnsw_planner", "indexes": [ { "key": { "v": "cosmosSearch" }, "name": "foo_1", "cosmosSearchOptions": { "kind": "vector-hnsw", "m": 4, "efConstruction": 16, "similarity": "COS", "dimensions": 3 } } ] }', true);
ANALYZE;

-- should not go via Citus planner (with pushdown to index)
set documentdb.enable_force_push_vector_index to on;
EXPLAIN (COSTS OFF, ANALYZE ON, SUMMARY OFF, TIMING OFF) SELECT document FROM bson_aggregation_pipeline('vector_db', '{ "aggregate": "agg_vector_hnsw_planner", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3.0, 4.9, 1.0 ], "k": 2, "path": "v", "efSearch": 1 }  } } ], "cursor": {} }');

SELECT batch_insert_testing_vector_documents_hnsw_planner('agg_vector_hnsw_planner', 10, 100, 10);
EXPLAIN (COSTS OFF, ANALYZE ON, SUMMARY OFF, TIMING OFF) SELECT document FROM bson_aggregation_pipeline('vector_db', '{ "aggregate": "agg_vector_hnsw_planner", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3.0, 4.9, 1.0 ], "k": 2, "path": "v", "efSearch": 1 }  } } ], "cursor": {} }');

-- create indexes for vector filter
SELECT documentdb_api_internal.create_indexes_non_concurrently('vector_db', '{ "createIndexes": "agg_vector_hnsw_planner", "indexes": [ { "key": { "a": 1 }, "name": "a_1" } ] }', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('vector_db', '{ "createIndexes": "agg_vector_hnsw_planner", "indexes": [ { "key": { "b": 1 }, "name": "b_1" } ] }', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('vector_db', '{ "createIndexes": "agg_vector_hnsw_planner", "indexes": [ { "key": { "c": 1 }, "name": "c_1" } ] }', true);

set documentdb.enableVectorPreFilter to on;
EXPLAIN (COSTS OFF, ANALYZE ON, SUMMARY OFF, TIMING OFF, VERBOSE ON) SELECT document FROM bson_aggregation_pipeline('vector_db', '{ "aggregate": "agg_vector_hnsw_planner", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3.0, 4.9, 1.0 ], "k": 2, "path": "v", "efSearch": 1, "filter": { "a": { "$lt": "s" }} }  } } ], "cursor": {} }');

set documentdb.enableVectorPreFilterV2 to on;
-- This is currently a post-filter and will not use the filter index.
EXPLAIN (COSTS OFF, ANALYZE ON, SUMMARY OFF, TIMING OFF) SELECT document FROM bson_aggregation_pipeline('vector_db', '{ "aggregate": "agg_vector_hnsw_planner", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3.0, 4.9, 1.0 ], "k": 2, "path": "v", "efSearch": 1, "filter": { "a": { "$lt": "s" }} }  } } ], "cursor": {} }');
EXPLAIN (COSTS OFF, ANALYZE ON, SUMMARY OFF, TIMING OFF) SELECT document FROM bson_aggregation_pipeline('vector_db', '{ "aggregate": "agg_vector_hnsw_planner", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3.0, 4.9, 1.0 ], "k": 2, "path": "v", "efSearch": 1, "filter": { "_id": { "$lt": 5 }} }  } } ], "cursor": {} }');
