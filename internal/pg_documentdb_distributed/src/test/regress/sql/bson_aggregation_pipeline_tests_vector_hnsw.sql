-- init a test user and set vector pre-filtering on
SELECT current_user as original_test_user \gset
CREATE ROLE test_filter_user WITH LOGIN INHERIT SUPERUSER CREATEDB CREATEROLE IN ROLE :original_test_user;

SET search_path TO documentdb_core,documentdb_api,documentdb_api_catalog,documentdb_api_internal;

SET citus.next_shard_id TO 8200000;
SET documentdb.next_collection_id TO 8200;
SET documentdb.next_collection_index_id TO 8200;

CREATE OR REPLACE FUNCTION batch_insert_testing_vector_documents(collectionName text, beginId integer, numDocuments integer, docPerBatch integer)
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
        SELECT p_result INTO v_resultDocs FROM documentdb_api.insert('db', v_insertSpec);
        batchIdx := batchIdx + 1;
    END LOOP;
END;
$fn$;

---------------------------------------------------------------------------------------------------------------------------
-- HNSW
-- HNSW create index, error cases
SELECT documentdb_api.insert_one('db', 'aggregation_pipeline_vector', '{ "_id":"1", "int": 10, "a" : { "b" : [ "x", 1, 2.0, true ] } }', NULL);
SELECT documentdb_api.insert_one('db', 'aggregation_pipeline_vector', '{ "_id":"2", "double": 2.0, "a" : { "b" : {"c": 3} } }', NULL);
SELECT documentdb_api.insert_one('db', 'aggregation_pipeline_vector', '{ "_id":"3", "boolean": false, "a" : "no", "b": "yes", "c": true }', NULL);
SELECT documentdb_api.insert_one('db', 'aggregation_pipeline_vector', '{ "_id": 6,  "a": "some sentence", "v": [3.0, 5.0, 1.1 ] }');
SELECT documentdb_api.insert_one('db', 'aggregation_pipeline_vector', '{ "_id": 7,  "a": "some other sentence", "v": [8.0, 5.0, 0.1 ] }');

SELECT documentdb_distributed_test_helpers.drop_primary_key('db','aggregation_pipeline_vector');
ANALYZE;

-- by default, HNSW index is enabled
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "aggregation_pipeline_vector", "indexes": [ { "key": { "v": "cosmosSearch" }, "name": "foo_1", "cosmosSearchOptions": { "kind": "unknown", "similarity": "IP", "dimensions": 3 } } ] }', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "aggregation_pipeline_vector", "indexes": [ { "key": { "v": "cosmosSearch" }, "name": "foo_1", "cosmosSearchOptions": { "kind": "vector-hnsw", "m": -4, "efConstruction": 16, "similarity": "IP", "dimensions": 3 } } ] }', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "aggregation_pipeline_vector", "indexes": [ { "key": { "v": "cosmosSearch" }, "name": "foo_1", "cosmosSearchOptions": { "kind": "vector-hnsw", "m": 101, "efConstruction": 160, "similarity": "IP", "dimensions": 3 } } ] }', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "aggregation_pipeline_vector", "indexes": [ { "key": { "v": "cosmosSearch" }, "name": "foo_1", "cosmosSearchOptions": { "kind": "vector-hnsw", "m": 4, "efConstruction": -16, "similarity": "COS", "dimensions": 3 } } ] }', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "aggregation_pipeline_vector", "indexes": [ { "key": { "v": "cosmosSearch" }, "name": "foo_1", "cosmosSearchOptions": { "kind": "vector-hnsw", "m": 40, "efConstruction": 1001, "similarity": "COS", "dimensions": 3 } } ] }', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "aggregation_pipeline_vector", "indexes": [ { "key": { "v": "cosmosSearch" }, "name": "foo_1", "cosmosSearchOptions": { "kind": "vector-hnsw", "m": "40", "efConstruction": 16, "similarity": "L2", "dimensions": 3 } } ] }', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "aggregation_pipeline_vector", "indexes": [ { "key": { "v": "cosmosSearch" }, "name": "foo_1", "cosmosSearchOptions": { "kind": "vector-hnsw", "m": 40, "efConstruction": "16", "similarity": "L2", "dimensions": 3 } } ] }', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "aggregation_pipeline_vector", "indexes": [ { "key": { "v": "cosmosSearch" }, "name": "foo_1", "cosmosSearchOptions": { "kind": "vector-hnsw", "m": 4, "efConstruction": 16, "similarity": "unknown", "dimensions": 3 } } ] }', true);
-- efConstruction is less than 2*m
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "aggregation_pipeline_vector", "indexes": [ { "key": { "v": "cosmosSearch" }, "name": "foo_1", "cosmosSearchOptions": { "kind": "vector-hnsw", "m": 4, "efConstruction": 4, "similarity": "IP", "dimensions": 3 } } ] }', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "aggregation_pipeline_vector", "indexes": [ { "key": { "v": "cosmosSearch" }, "name": "foo_1", "cosmosSearchOptions": { "kind": "vector-hnsw", "m": 64, "similarity": "COS", "dimensions": 3 } } ] }', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "aggregation_pipeline_vector", "indexes": [ { "key": { "v": "cosmosSearch" }, "name": "foo_1", "cosmosSearchOptions": { "kind": "vector-hnsw", "efConstruction": 4, "similarity": "COS", "dimensions": 3 } } ] }', true);

SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "aggregation_pipeline_vector", "indexes": [ { "key": { "v": "cosmosSearch" }, "name": "foo_1", "cosmosSearchOptions": { "kind": "unknown", "similarity": "IP", "dimensions": 3 } } ] }', true);

-- check dimensions exceeds 2000
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "aggregation_pipeline_vector", "indexes": [ { "key": { "v": "cosmosSearch" }, "name": "foo_1", "cosmosSearchOptions": { "kind": "vector-hnsw", "similarity": "COS", "dimensions": 2001 } } ] }', true);

SET documentdb.enableVectorHNSWIndex = off;
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "aggregation_pipeline_vector", "indexes": [ { "key": { "v": "cosmosSearch" }, "name": "foo_1", "cosmosSearchOptions": { "kind": "vector-hnsw", "m": -4, "efConstruction": 16, "similarity": "IP", "dimensions": 3 } } ] }', true);
SET documentdb.enableVectorHNSWIndex = on;

-- HNSW create index, success cases
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "aggregation_pipeline_vector", "indexes": [ { "key": { "v": "cosmosSearch" }, "name": "foo_1", "cosmosSearchOptions": { "kind": "vector-hnsw", "similarity": "COS", "dimensions": 3 } } ] }', true);
CALL documentdb_api.drop_indexes('db', '{ "dropIndexes": "aggregation_pipeline_vector", "index": "foo_1"}');
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "aggregation_pipeline_vector", "indexes": [ { "key": { "v": "cosmosSearch" }, "name": "foo_1", "cosmosSearchOptions": { "kind": "vector-hnsw", "m": 4, "similarity": "L2", "dimensions": 3 } } ] }', true);
CALL documentdb_api.drop_indexes('db', '{ "dropIndexes": "aggregation_pipeline_vector", "index": "foo_1"}');
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "aggregation_pipeline_vector", "indexes": [ { "key": { "v": "cosmosSearch" }, "name": "foo_1", "cosmosSearchOptions": { "kind": "vector-hnsw", "efConstruction": 32, "similarity": "IP", "dimensions": 3 } } ] }', true);

-- HNSW search, success cases
CALL documentdb_api.drop_indexes('db', '{ "dropIndexes": "aggregation_pipeline_vector", "index": "foo_1"}');
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "aggregation_pipeline_vector", "indexes": [ { "key": { "v": "cosmosSearch" }, "name": "foo_1", "cosmosSearchOptions": { "kind": "vector-hnsw", "m": 4, "efConstruction": 16, "similarity": "COS", "dimensions": 3 } } ] }', true);
ANALYZE;
BEGIN;
SET LOCAL enable_seqscan = off;
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_vector", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3.0, 4.9, 1.0 ], "k": 2, "path": "v", "efSearch": 1 }  } } ], "cursor": {} }');
COMMIT;

CALL documentdb_api.drop_indexes('db', '{ "dropIndexes": "aggregation_pipeline_vector", "index": "foo_1"}');
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "aggregation_pipeline_vector", "indexes": [ { "key": { "v": "cosmosSearch" }, "name": "foo_1", "cosmosSearchOptions": { "kind": "vector-hnsw", "m": 4, "efConstruction": 16, "similarity": "IP", "dimensions": 3 } } ] }', true);
ANALYZE;
BEGIN;
SET LOCAL enable_seqscan = off;
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_vector", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3.0, 4.9, 1.0 ], "k": 2, "path": "v" }  } } ], "cursor": {} }');
COMMIT;

CALL documentdb_api.drop_indexes('db', '{ "dropIndexes": "aggregation_pipeline_vector", "index": "foo_1"}');
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "aggregation_pipeline_vector", "indexes": [ { "key": { "v": "cosmosSearch" }, "name": "foo_1", "cosmosSearchOptions": { "kind": "vector-hnsw", "m": 4, "efConstruction": 16, "similarity": "L2", "dimensions": 3 } } ] }', true);
ANALYZE;
BEGIN;
SET LOCAL enable_seqscan = off;
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_vector", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3.0, 4.9, 1.0 ], "k": 2, "path": "v", "efSearch": 2 }  } } ], "cursor": {} }');
COMMIT;

BEGIN;
SET LOCAL enable_seqscan = off;
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_vector", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3.0, 4.9, 1.0 ], "k": 2, "path": "v" }  } } ], "cursor": {} }');
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_vector", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3.0, 4.9, 1.0 ], "k": 2, "path": "v", "efSearch": 2 }  } } ], "cursor": {} }');
ROLLBACK;

-- HNSW search, error cases
SET documentdb.enableVectorHNSWIndex = off;
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_vector", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3.0, 4.9, 1.0 ], "k": 2, "path": "v", "efSearch": 10000000 }  } } ], "cursor": {} }');
SET documentdb.enableVectorHNSWIndex = on;
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_vector", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3.0, 4.9, 1.0 ], "k": 2, "path": "v", "efSearch": 10000000 }  } } ], "cursor": {} }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_vector", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3.0, 4.9, 1.0 ], "k": 2, "path": "v", "efSearch": -5 }  } } ], "cursor": {} }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_vector", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3.0, 4.9, 1.0 ], "k": 2, "path": "v", "efSearch": "5" }  } } ], "cursor": {} }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_vector", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3.0, 4.9, 1.0 ], "k": 2, "path": "v", "efSearch": 5.5 }  } } ], "cursor": {} }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_vector", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3.0, 4.9, 1.0 ], "k": 2, "path": "v", "efSearch": 0 }  } } ], "cursor": {} }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_vector", "pipeline": [ { "$search": { "cosmosSearch": { "k": 2, "path": "v", "efSearch": 1 }  } } ], "cursor": {} }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_vector", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3.0, 4.9, 1.0 ], "k": -1, "path": "v", "efSearch": 1 }  } } ], "cursor": {} }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_vector", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3.0, 4.9, 1.0 ], "k": "1", "path": "v", "efSearch": 1 }  } } ], "cursor": {} }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_vector", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3.0, 4.9, 1.0 ], "k": 0, "path": "v", "efSearch": 1 }  } } ], "cursor": {} }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_vector", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3.0, 4.9, 1.0 ], "path": "v", "efSearch": 1 }  } } ], "cursor": {} }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_vector", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3.0, 4.9, 1.0 ], "k": 1 }  } } ], "cursor": {} }');

-- check dimension of query vector exceeds 2000
DO $$  
DECLARE  
    dim_num integer := 2001;  
	vect_text text;
    pipeine_text text;  
BEGIN  
	vect_text := (SELECT array_agg(n)::public.vector::text FROM generate_series(1, dim_num) AS n) ;
	pipeine_text := (SELECT '{ "aggregate": "aggregation_pipeline_vector", "pipeline": [ { "$search": { "cosmosSearch": { "vector": ' || vect_text || ', "k": 2, "path": "v" } } } ], "cursor": {} }');
	SELECT document FROM bson_aggregation_pipeline('db'::text, pipeine_text::bson);
END;  
$$;

DO $$  
DECLARE  
    dim_num integer := 2000;  
	vect_text text;
    pipeine_text text;  
BEGIN  
	vect_text := (SELECT array_agg(n)::public.vector::text FROM generate_series(1, dim_num) AS n) ;
	pipeine_text := (SELECT '{ "aggregate": "aggregation_pipeline_vector", "pipeline": [ { "$search": { "cosmosSearch": { "vector": ' || vect_text || ', "k": 2, "path": "v" } } } ], "cursor": {} }');
	SELECT document FROM bson_aggregation_pipeline('db'::text, pipeine_text::bson);
END;  
$$;

SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_vector", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3.0, 4.9, 1.0 ], "k": 1.1, "path": "v", "efSearch": 1 }  } } ], "cursor": {} }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_vector", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3.0, 4.9, 1.0 ], "k": 1.8, "path": "v", "efSearch": 1 }  } } ], "cursor": {} }');

-- efSearch = 1, get 1 document
BEGIN;
SET LOCAL enable_seqscan = off;
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_vector", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3.0, 4.9, 1.0 ], "k": 5, "path": "v", "efSearch": 1 }  } } ], "cursor": {} }');
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_vector", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3.0, 4.9, 1.0 ], "k": 5, "path": "v", "efSearch": 1 }  } } ], "cursor": {} }');
COMMIT;

-- efSearch = 3, get 3 documents
BEGIN;
SET LOCAL enable_seqscan = off;
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_vector", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3.0, 4.9, 1.0 ], "k": 5, "path": "v", "efSearch": 3 }  } } ], "cursor": {} }');
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_vector", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3.0, 4.9, 1.0 ], "k": 5, "path": "v", "efSearch": 3 }  } } ], "cursor": {} }');
COMMIT;

CALL documentdb_api.drop_indexes('db', '{ "dropIndexes": "aggregation_pipeline_vector", "index": "foo_1"}');

----------------------------------------------------------------------------------------------------
-- turn on vector pre-filtering, check dynamic efSearch
select batch_insert_testing_vector_documents('aggregation_pipeline_hnsw_efsearch', 1, 150, 2000);
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "aggregation_pipeline_hnsw_efsearch", "indexes": [ { "key": { "v": "cosmosSearch" }, "name": "foo_1", "cosmosSearchOptions": { "kind": "vector-hnsw", "m": 4, "efConstruction": 16, "similarity": "L2", "dimensions": 3 } } ] }', true);
SELECT documentdb_distributed_test_helpers.drop_primary_key('db','aggregation_pipeline_hnsw_efsearch');
ANALYZE;

-- 150 less than 10000 documents, use efConstruction as efSearch
BEGIN;
SET LOCAL enable_seqscan to off;
SET LOCAL documentdb.enableVectorPreFilter = on;
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_hnsw_efsearch", "pipeline": [ { "$search": { "cosmosSearch": { "k": 5, "path": "v", "vector": [ 3.0, 4.9, 1.0 ] }  } } ], "cursor": {} }');
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_hnsw_efsearch", "pipeline": [ { "$search": { "cosmosSearch": { "k": 5, "path": "v", "vector": [ 3.0, 4.9, 1.0 ] }  } } ], "cursor": {} }');
ROLLBACK;

-- check efSeache with score projection
BEGIN;
SET LOCAL enable_seqscan to off;
SET LOCAL documentdb.enableVectorPreFilter = on;
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_hnsw_efsearch", "pipeline": [ { "$search": { "cosmosSearch": { "k": 5, "path": "v", "vector": [ 3.0, 4.9, 1.0 ] }}}, { "$project": { "rank": {"$round":[{"$multiply": [{"$meta": "searchScore" }, 100000]}]} } } ], "cursor": {} }');
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_hnsw_efsearch", "pipeline": [ { "$search": { "cosmosSearch": { "k": 5, "path": "v", "vector": [ 3.0, 4.9, 1.0 ] }}}, { "$project": { "rank": {"$round":[{"$multiply": [{"$meta": "searchScore" }, 100000]}]} } } ], "cursor": {} }');
ROLLBACK;

-- generate 1000 documents and insert into collection
select batch_insert_testing_vector_documents('aggregation_pipeline_hnsw_efsearch', 151, 1000, 2000);
ANALYZE;

BEGIN;
SET LOCAL enable_seqscan to off;
SET LOCAL documentdb.enableVectorPreFilter = on;
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_hnsw_efsearch", "pipeline": [ { "$search": { "cosmosSearch": { "k": 5, "path": "v", "vector": [ 3.0, 4.9, 1.0 ] }  } } ], "cursor": {} }');
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_hnsw_efsearch", "pipeline": [ { "$search": { "cosmosSearch": { "k": 5, "path": "v", "vector": [ 3.0, 4.9, 1.0 ] }  } } ], "cursor": {} }');
ROLLBACK;

-- more than 10000 and less than 1M documents, use default efSearch 40
-- generate 9000 documents and insert into collection
select batch_insert_testing_vector_documents('aggregation_pipeline_hnsw_efsearch', 1151, 9000, 2000);
ANALYZE;

BEGIN;
SET LOCAL enable_seqscan to off;
SET LOCAL documentdb.enableVectorPreFilter = on;
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_hnsw_efsearch", "pipeline": [ { "$search": { "cosmosSearch": { "k": 5, "path": "v", "vector": [ 3.0, 4.9, 1.0 ] }  } } ], "cursor": {} }');
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_hnsw_efsearch", "pipeline": [ { "$search": { "cosmosSearch": { "k": 5, "path": "v", "vector": [ 3.0, 4.9, 1.0 ] }  } } ], "cursor": {} }');
ROLLBACK;

----------------------------------------------------------------------------------------------------
-- hnsw search with filter
SELECT documentdb_api.insert_one('db', 'aggregation_pipeline_hnsw_filter', '{ "_id": 6, "meta":{ "a": "some sentence", "b": 1 }, "c": true , "v": [3.0, 5.0, 1.1 ] }');
SELECT documentdb_api.insert_one('db', 'aggregation_pipeline_hnsw_filter', '{ "_id": 7, "meta":{ "a": "some other sentence", "b": 2}, "c": true , "v": [8.0, 5.0, 0.1 ] }');
SELECT documentdb_api.insert_one('db', 'aggregation_pipeline_hnsw_filter', '{ "_id": 8, "meta":{ "a": "other sentence", "b": 5 }, "c": false, "v": [13.0, 5.0, 0.1 ] }');
SELECT documentdb_api.insert_one('db', 'aggregation_pipeline_hnsw_filter', '{ "_id": 9, "meta":{ "a" : [ { "b" : 3 } ] }, "c": false, "v": [15.0, 5.0, 0.1 ] }');
SELECT documentdb_api.insert_one('db', 'aggregation_pipeline_hnsw_filter', '{ "_id": 10, "meta":{ "a" : [ { "b" : 5 } ] }, "c": false }');
SELECT documentdb_distributed_test_helpers.drop_primary_key('db','aggregation_pipeline_hnsw_filter');

SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "aggregation_pipeline_hnsw_filter", "indexes": [ { "key": { "v": "cosmosSearch" }, "name": "hnsw_index", "cosmosSearchOptions": { "kind": "vector-hnsw", "m": 4, "efConstruction": 16, "similarity": "L2", "dimensions": 3 } } ] }', true);
ANALYZE;

SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_hnsw_filter", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3.0, 4.9, 1.0 ], "k": 2, "path": "v", "filter": {"a": "some sentence"} }  } } ], "cursor": {} }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_hnsw_filter", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3.0, 4.9, 1.0 ], "k": 1, "path": "v", "filter": "some sentence" }  } } ], "cursor": {} }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_hnsw_filter", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3.0, 4.9, 1.0 ], "k": 1, "path": "v", "filter": {} }  } } ], "cursor": {} }');
SET documentdb.enableVectorPreFilter = on;

-- filter without index
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_hnsw_filter", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3.0, 4.9, 1.0 ], "k": 2, "path": "v", "filter": {"a": "some sentence"} }  } } ], "cursor": {} }');
BEGIN;
SET LOCAL enable_seqscan to off;
SET LOCAL documentdb.enableVectorPreFilter = on;
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_hnsw_filter", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3.0, 4.9, 1.0 ], "k": 2, "path": "v", "filter": {"a": "some sentence"} }  } } ], "cursor": {} }');
ROLLBACK;

SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_hnsw_filter", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3.0, 4.9, 1.0 ], "k": 2, "path": "v", "filter": {"meta.a": "some sentence"} }  } } ], "cursor": {} }');
BEGIN;
SET LOCAL enable_seqscan to off;
SET LOCAL documentdb.enableVectorPreFilter = on;
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_hnsw_filter", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3.0, 4.9, 1.0 ], "k": 2, "path": "v", "filter": {"meta.a": "some sentence"} }  } } ], "cursor": {} }');
ROLLBACK;

SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_hnsw_filter", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3.0, 4.9, 1.0 ], "k": 2, "path": "v", "filter": {"meta.a.b": 3} }  } } ], "cursor": {} }');
BEGIN;
SET LOCAL enable_seqscan to off;
SET LOCAL documentdb.enableVectorPreFilter = on;
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_hnsw_filter", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3.0, 4.9, 1.0 ], "k": 2, "path": "v", "filter": {"meta.a.b": 3} }  } } ], "cursor": {} }');
ROLLBACK;

SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_hnsw_filter", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3.0, 4.9, 1.0 ], "k": 1, "path": "v", "filter": "some sentence" }  } } ], "cursor": {} }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_hnsw_filter", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3.0, 4.9, 1.0 ], "k": 1, "path": "v", "filter": {} }  } } ], "cursor": {} }');
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "aggregation_pipeline_hnsw_filter", "indexes": [ { "key": { "$**" : 1 }, "name": "wildcardIndex" } ] }', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "aggregation_pipeline_hnsw_filter", "indexes": [ { "key": { "meta.a": 1 }, "name": "idx_meta.a" } ] }', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "aggregation_pipeline_hnsw_filter", "indexes": [ { "key": { "meta.b": 1 }, "name": "numberIndex_meta.b" } ] }', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "aggregation_pipeline_hnsw_filter", "indexes": [ { "key": { "meta.a.b": 1 }, "name": "idx_meta.a.b" } ] }', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "aggregation_pipeline_hnsw_filter", "indexes": [ { "key": { "meta": 1 }, "name": "documentIndex_meta" } ] }', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "aggregation_pipeline_hnsw_filter", "indexes": [ { "key": { "c": 1 }, "name": "boolIndex_c" } ] }', true);
ANALYZE;

--------------------------------------------------
-- no match index path
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_hnsw_filter", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3.0, 4.9, 1.0 ], "k": 2, "path": "v", "filter": {"unknownPath": "some sentence"} }  } } ], "cursor": {} }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_hnsw_filter", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3.0, 4.9, 1.0 ], "k": 2, "path": "v", "filter": {"meta.c": "some sentence"} }  } } ], "cursor": {} }');

--------------------------------------------------
-- multiple index path
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_hnsw_filter", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3.0, 4.9, 1.0 ], "k": 10, "path": "v", "filter": {"$or": [ { "meta.a": { "$eq": "some sentence" } }, { "meta.b": { "$gt": 2 } }, {"c":  { "$eq": false } } ] }, "efSearch": 100 }  } }, { "$project": {"searchScore": {"$round": [ {"$multiply": ["$__cosmos_meta__.score", 100000]}]} } }  ], "cursor": {} }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_hnsw_filter", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3.0, 4.9, 1.0 ], "k": 4, "path": "v", "filter": {"$and": [ { "$or": [{ "meta.b": { "$eq": 2 } }, { "meta.b": { "$eq": 5 } } ] }, { "c": { "$eq": false } } ] } }  } }, { "$project": {"searchScore": {"$round": [ {"$multiply": ["$__cosmos_meta__.score", 100000]}]} } } ], "cursor": {} }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_hnsw_filter", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3.0, 4.9, 1.0 ], "k": 4, "path": "v", "filter": {"$and": [ { "$or": [{ "meta.b": { "$eq": 2 } }, { "c": { "$eq": false } } ] }, { "meta.b": { "$lt": 5 } } ] } }  } }, { "$project": {"searchScore": {"$round": [ {"$multiply": ["$__cosmos_meta__.score", 100000]}]} } } ], "cursor": {} }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_hnsw_filter", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3.0, 4.9, 1.0 ], "k": 4, "path": "v", "filter": {"$and": [ { "$or": [{ "meta.a": { "$regex": "^some", "$options" : "i" } }, { "meta.b": { "$eq": 5 } } ] }, { "meta.b": { "$lt": 5 } } ] } }  } }, { "$project": {"searchScore": {"$round": [ {"$multiply": ["$__cosmos_meta__.score", 100000]}]} } } ], "cursor": {} }');

-- check the vector index is forced to be used
ALTER ROLE test_filter_user SET documentdb.enableVectorPreFilter = "True";
SELECT current_setting('citus' || '.next_shard_id') as vector_citus__next_shard_id \gset
SELECT current_setting('documentdb' || '.next_collection_id') as vector__next_collection_id \gset
SELECT current_setting('documentdb' || '.next_collection_index_id') as vector__next_collection_index_id \gset
\c - test_filter_user
SET search_path TO documentdb_core,documentdb_api,documentdb_api_catalog,documentdb_api_internal;

SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_hnsw_filter", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3.0, 4.9, 1.0 ], "k": 4, "path": "v", "filter": {"meta.a": [ { "b" : 3 } ]}, "efSearch": 100 }  } }, { "$project": {"searchScore": {"$round": [ {"$multiply": ["$__cosmos_meta__.score", 100000]}]} } } ], "cursor": {} }');
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_hnsw_filter", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3.0, 4.9, 1.0 ], "k": 4, "path": "v", "filter": {"meta.a": [ { "b" : 3 } ]}, "efSearch": 100 }  } } ], "cursor": {} }');

-- default efSearch = efConstruction(16)
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_hnsw_filter", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3.0, 4.9, 1.0 ], "k": 4, "path": "v", "filter": {"meta": { "a" : [ { "b" : 3 } ] } } }  } }, { "$project": {"searchScore": {"$round": [ {"$multiply": ["$__cosmos_meta__.score", 100000]}]} } } ], "cursor": {} }');
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_hnsw_filter", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3.0, 4.9, 1.0 ], "k": 4, "path": "v", "filter": {"meta": { "a" : [ { "b" : 3 } ] } } }  } } ], "cursor": {} }');

SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_hnsw_filter", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3.0, 4.9, 1.0 ], "k": 4, "path": "v", "filter": {"meta": { "a" : [ { "b" : 3 } ] } }, "efSearch": 100 }  } }, { "$project": {"searchScore": {"$round": [ {"$multiply": ["$__cosmos_meta__.score", 100000]}]} } } ], "cursor": {} }');
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_hnsw_filter", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3.0, 4.9, 1.0 ], "k": 4, "path": "v", "filter": {"meta": { "a" : [ { "b" : 3 } ] } }, "efSearch": 100 }  } } ], "cursor": {} }');

\c - :original_test_user
SET search_path TO documentdb_core,documentdb_api,documentdb_api_catalog,documentdb_api_internal;
SELECT set_config('citus' || '.next_shard_id', '' || :vector_citus__next_shard_id, FALSE);
SELECT set_config('documentdb' || '.next_collection_id', '' || :vector__next_collection_id, FALSE);
SELECT set_config('documentdb' || '.next_collection_index_id', '' || :vector__next_collection_index_id, FALSE);

--------------------------------------------------
-- hnsw search: pre-filtering match with different indexes
-- "filter": {"meta.a": [ { "b" : 3 } ]}, match with idx_meta.a
BEGIN;
SET LOCAL enable_seqscan to off;
SET LOCAL documentdb.enableVectorPreFilter = on;
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_hnsw_filter", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3.0, 4.9, 1.0 ], "k": 4, "path": "v", "filter": {"meta.a": [ { "b" : 3 } ]}, "efSearch": 100 }  } }, { "$project": {"searchScore": {"$round": [ {"$multiply": ["$__cosmos_meta__.score", 100000]}]} } } ], "cursor": {} }');
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_hnsw_filter", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3.0, 4.9, 1.0 ], "k": 4, "path": "v", "filter": {"meta.a": [ { "b" : 3 } ]}, "efSearch": 100 }  } } ], "cursor": {} }');
ROLLBACK;

-- "filter": {"meta": { "a" : [ { "b" : 3 } ] }, match with documentIndex_meta
BEGIN;
SET LOCAL enable_seqscan to off;
SET LOCAL documentdb.enableVectorPreFilter = on;
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_hnsw_filter", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3.0, 4.9, 1.0 ], "k": 4, "path": "v", "filter": {"meta": { "a" : [ { "b" : 3 } ] } }, "efSearch": 100 }  } }, { "$project": {"searchScore": {"$round": [ {"$multiply": ["$__cosmos_meta__.score", 100000]}]} } } ], "cursor": {} }');
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_hnsw_filter", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3.0, 4.9, 1.0 ], "k": 4, "path": "v", "filter": {"meta": { "a" : [ { "b" : 3 } ] } }, "efSearch": 100 }  } } ], "cursor": {} }');
ROLLBACK;

-- "filter": {"meta.a.b": 3 }, match with idx_meta.a.b
BEGIN;
SET LOCAL enable_seqscan to off;
SET LOCAL documentdb.enableVectorPreFilter = on;
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_hnsw_filter", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3.0, 4.9, 1.0 ], "k": 4, "path": "v", "filter": {"meta.a.b": 3 }, "efSearch": 100 }  } }, { "$project": {"searchScore": {"$round": [ {"$multiply": ["$__cosmos_meta__.score", 100000]}]} } } ], "cursor": {} }');
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_hnsw_filter", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3.0, 4.9, 1.0 ], "k": 4, "path": "v", "filter": {"meta.a.b": 3 }, "efSearch": 100 }  } } ], "cursor": {} }');
ROLLBACK;

--------------------------------------------------
-- hnsw filter string: default efSearch
BEGIN;
SET LOCAL enable_seqscan to off;
SET LOCAL documentdb.enableVectorPreFilter = on;
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_hnsw_filter", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3.0, 4.9, 1.0 ], "k": 4, "path": "v", "filter": {"meta.a": "some sentence"} }  } }, { "$project": {"searchScore": {"$round": [ {"$multiply": ["$__cosmos_meta__.score", 100000]}]} } } ], "cursor": {} }');
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_hnsw_filter", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3.0, 4.9, 1.0 ], "k": 4, "path": "v", "filter": {"meta.a": "some sentence"} }  } } ], "cursor": {} }');
ROLLBACK;

-- hnsw filter string: with $and, efSearch = 3, match 1 document
BEGIN;
SET LOCAL enable_seqscan to off;
SET LOCAL documentdb.enableVectorPreFilter = on;
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_hnsw_filter", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3.0, 4.9, 1.0 ], "k": 4, "path": "v", "filter": {"$and": [ { "meta.a": { "$gt": "other sentence" } }, { "meta.a": { "$lt": "some sentence" } } ] }, "efSearch": 3 }  } }, { "$project": {"searchScore": {"$round": [ {"$multiply": ["$__cosmos_meta__.score", 100000]}]} } } ], "cursor": {} }');
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_hnsw_filter", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3.0, 4.9, 1.0 ], "k": 4, "path": "v", "filter": {"$and": [ { "meta.a": { "$gt": "other sentence" } }, { "meta.a": { "$lt": "some sentence" } } ] }, "efSearch": 3 }  } } ], "cursor": {} }');
ROLLBACK;

-- hnsw filter string: with $and, efSearch = 1, no match document
BEGIN;
SET LOCAL enable_seqscan to off;
SET LOCAL documentdb.enableVectorPreFilter = on;
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_hnsw_filter", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3.0, 4.9, 1.0 ], "k": 4, "path": "v", "filter": {"$and": [ { "meta.a": { "$gt": "other sentence" } }, { "meta.a": { "$lt": "some sentence" } } ] }, "efSearch": 1 }  } }, { "$project": {"searchScore": {"$round": [ {"$multiply": ["$__cosmos_meta__.score", 100000]}]} } } ], "cursor": {} }');
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_hnsw_filter", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3.0, 4.9, 1.0 ], "k": 4, "path": "v", "filter": {"$and": [ { "meta.a": { "$gt": "other sentence" } }, { "meta.a": { "$lt": "some sentence" } } ] }, "efSearch": 1 }  } } ], "cursor": {} }');
ROLLBACK;

-- hnsw filter string: with $or, efSearch = 3, match 3 documents
BEGIN;
SET LOCAL enable_seqscan to off;
SET LOCAL documentdb.enableVectorPreFilter = on;
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_hnsw_filter", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3.0, 4.9, 1.0 ], "k": 4, "path": "v", "filter": {"$or": [ { "meta.a": { "$gt": "other sentence" } }, { "meta.a": { "$lt": "some sentence" } } ] }, "efSearch": 3 }  } }, { "$project": {"searchScore": {"$round": [ {"$multiply": ["$__cosmos_meta__.score", 100000]}]} } } ], "cursor": {} }');
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_hnsw_filter", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3.0, 4.9, 1.0 ], "k": 4, "path": "v", "filter": {"$or": [ { "meta.a": { "$gt": "other sentence" } }, { "meta.a": { "$lt": "some sentence" } } ] }, "efSearch": 3 }  } } ], "cursor": {} }');
ROLLBACK;

-- hnsw filter string: with $or, efSearch = 1, match 1 document
BEGIN;
SET LOCAL enable_seqscan to off;
SET LOCAL documentdb.enableVectorPreFilter = on;
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_hnsw_filter", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3.0, 4.9, 1.0 ], "k": 4, "path": "v", "filter": {"$or": [ { "meta.a": { "$gt": "other sentence" } }, { "meta.a": { "$lt": "some sentence" } } ] }, "efSearch": 1 }  } }, { "$project": {"searchScore": {"$round": [ {"$multiply": ["$__cosmos_meta__.score", 100000]}]} } } ], "cursor": {} }');
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_hnsw_filter", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3.0, 4.9, 1.0 ], "k": 4, "path": "v", "filter": {"$or": [ { "meta.a": { "$gt": "other sentence" } }, { "meta.a": { "$lt": "some sentence" } } ] }, "efSearch": 1 }  } } ], "cursor": {} }');
ROLLBACK;

-- hnsw filter string: with $or, $and, efSearch = 10, match 2 documents
BEGIN;
SET LOCAL enable_seqscan to off;
SET LOCAL documentdb.enableVectorPreFilter = on;
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_hnsw_filter", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3.0, 4.9, 1.0 ], "k": 10, "path": "v", "filter": {"$or":[{"$and":[{"meta.a":{"$gt":"other sentence"}},{"meta.a":{"$lt":"some sentence"}}]},{"meta.a":{"$in":[{"b":3},{"b":5}]}}]}, "efSearch": 10 }  } }, { "$project": {"searchScore": {"$round": [ {"$multiply": ["$__cosmos_meta__.score", 100000]}]} } } ], "cursor": {} }');
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_hnsw_filter", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3.0, 4.9, 1.0 ], "k": 10, "path": "v", "filter": {"$or":[{"$and":[{"meta.a":{"$gt":"other sentence"}},{"meta.a":{"$lt":"some sentence"}}]},{"meta.a":{"$in":[{"b":3},{"b":5}]}}]}, "efSearch": 10 }  } } ], "cursor": {} }');
ROLLBACK;

-- hnsw filter string:, with $eq, $gt, $lt, $gte, $lte, $ne, $in, $and, $or, $regex
BEGIN;
SET LOCAL enable_seqscan to off;
SET LOCAL documentdb.enableVectorPreFilter = on;
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_hnsw_filter", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3.0, 4.9, 1.0 ], "k": 4, "path": "v", "filter": {"meta.a":  { "$eq": "some sentence"}}, "efSearch": 100 }  } }, { "$project": {"searchScore": {"$round": [ {"$multiply": ["$__cosmos_meta__.score", 100000]}]} } } ], "cursor": {} }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_hnsw_filter", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3.0, 4.9, 1.0 ], "k": 4, "path": "v", "filter": {"meta.a":  { "$gt": "some sentence"}}, "efSearch": 100 }  } }, { "$project": {"searchScore": {"$round": [ {"$multiply": ["$__cosmos_meta__.score", 100000]}]} } } ], "cursor": {} }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_hnsw_filter", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3.0, 4.9, 1.0 ], "k": 4, "path": "v", "filter": {"meta.a":  { "$lt": "some sentence"}}, "efSearch": 100 }  } }, { "$project": {"searchScore": {"$round": [ {"$multiply": ["$__cosmos_meta__.score", 100000]}]} } } ], "cursor": {} }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_hnsw_filter", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3.0, 4.9, 1.0 ], "k": 4, "path": "v", "filter": {"meta.a":  { "$ne": "some sentence"}}, "efSearch": 100 }  } }, { "$project": {"searchScore": {"$round": [ {"$multiply": ["$__cosmos_meta__.score", 100000]}]} } } ], "cursor": {} }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_hnsw_filter", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3.0, 4.9, 1.0 ], "k": 4, "path": "v", "filter": {"meta.a":  { "$in": ["some sentence", "other sentence"]}}, "efSearch": 100 }  } }, { "$project": {"searchScore": {"$round": [ {"$multiply": ["$__cosmos_meta__.score", 100000]}]} } } ], "cursor": {} }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_hnsw_filter", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3.0, 4.9, 1.0 ], "k": 4, "path": "v", "filter": {"meta.a":  { "$regex": "^some", "$options" : "i"}}, "efSearch": 100 }  } }, { "$project": {"searchScore": {"$round": [ {"$multiply": ["$__cosmos_meta__.score", 100000]}]} } } ], "cursor": {} }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_hnsw_filter", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3.0, 4.9, 1.0 ], "k": 4, "path": "v", "filter": {"meta.a":  [ { "b" : 3 } ] }, "efSearch": 100 }  } }, { "$project": {"searchScore": {"$round": [ {"$multiply": ["$__cosmos_meta__.score", 100000]}]} } } ], "cursor": {} }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_hnsw_filter", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3.0, 4.9, 1.0 ], "k": 4, "path": "v", "filter": {"$and": [ { "meta.a": { "$gt": "other sentence" } }, { "meta.a": { "$lt": "some sentence" } } ] }, "efSearch": 100 }  } }, { "$project": {"searchScore": {"$round": [ {"$multiply": ["$__cosmos_meta__.score", 100000]}]} } } ], "cursor": {} }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_hnsw_filter", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3.0, 4.9, 1.0 ], "k": 4, "path": "v", "filter": {"$or": [ { "meta.a": { "$gt": "other sentence" } }, { "meta.a": { "$lt": "some sentence" } } ] }, "efSearch": 100 }  } }, { "$project": {"searchScore": {"$round": [ {"$multiply": ["$__cosmos_meta__.score", 100000]}]} } } ], "cursor": {} }');
ROLLBACK;

--------------------------------------------------
-- hnsw filter number, with default efSearch
BEGIN;
SET LOCAL enable_seqscan to off;
SET LOCAL documentdb.enableVectorPreFilter = on;
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_hnsw_filter", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3.0, 4.9, 1.0 ], "k": 4, "path": "v", "filter": {"meta.b": 2 } }  } }, { "$project": {"searchScore": {"$round": [ {"$multiply": ["$__cosmos_meta__.score", 100000]}]} } } ], "cursor": {} }');
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_hnsw_filter", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3.0, 4.9, 1.0 ], "k": 4, "path": "v", "filter": {"meta.b": 2 } }  } } ], "cursor": {} }');
ROLLBACK;

-- hnsw filter number, with $and, efSearch = 1, match 1 document 
BEGIN;
SET LOCAL enable_seqscan to off;
SET LOCAL documentdb.enableVectorPreFilter = on;
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_hnsw_filter", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3.0, 4.9, 1.0 ], "k": 4, "path": "v", "filter": {"$or": [ { "meta.b": { "$gt": 1 } }, { "meta.b": { "$lt": 5 } } ] }, "efSearch": 1 }  } }, { "$project": {"searchScore": {"$round": [ {"$multiply": ["$__cosmos_meta__.score", 100000]}]} } } ], "cursor": {} }');
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_hnsw_filter", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3.0, 4.9, 1.0 ], "k": 4, "path": "v", "filter": {"$or": [ { "meta.b": { "$gt": 1 } }, { "meta.b": { "$lt": 5 } } ] }, "efSearch": 1 }  } } ], "cursor": {} }');
ROLLBACK;

-- hnsw filter number, with $and, default efSearch, match 1 document
BEGIN;
SET LOCAL enable_seqscan to off;
SET LOCAL documentdb.enableVectorPreFilter = on;
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_hnsw_filter", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3.0, 4.9, 1.0 ], "k": 4, "path": "v", "filter": {"$and": [ { "$or": [{ "meta.b": { "$eq": 2 } }, { "meta.b": { "$eq": 5 } } ] }, { "meta.b": { "$lt": 5 } } ] } }  } }, { "$project": {"searchScore": {"$round": [ {"$multiply": ["$__cosmos_meta__.score", 100000]}]} } } ], "cursor": {} }');
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_hnsw_filter", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3.0, 4.9, 1.0 ], "k": 4, "path": "v", "filter": {"$and": [ { "$or": [{ "meta.b": { "$eq": 2 } }, { "meta.b": { "$eq": 5 } } ] }, { "meta.b": { "$lt": 5 } } ] } }  } } ], "cursor": {} }');
ROLLBACK;

-- hnsw filter number, with $or, efSearch = 1, match 1 document
BEGIN;
SET LOCAL enable_seqscan to off;
SET LOCAL documentdb.enableVectorPreFilter = on;
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_hnsw_filter", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3.0, 4.9, 1.0 ], "k": 4, "path": "v", "filter": {"$or": [ { "meta.b": { "$gt": 1 } }, { "meta.b": { "$lt": 5 } } ] }, "efSearch": 1 }  } }, { "$project": {"searchScore": {"$round": [ {"$multiply": ["$__cosmos_meta__.score", 100000]}]} } } ], "cursor": {} }');
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_hnsw_filter", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3.0, 4.9, 1.0 ], "k": 4, "path": "v", "filter": {"$or": [ { "meta.b": { "$gt": 1 } }, { "meta.b": { "$lt": 5 } } ] }, "efSearch": 1 }  } } ], "cursor": {} }');
ROLLBACK;

-- hnsw filter number, with $or, default efSearch, match 3 documents
BEGIN;
SET LOCAL enable_seqscan to off;
SET LOCAL documentdb.enableVectorPreFilter = on;
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_hnsw_filter", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3.0, 4.9, 1.0 ], "k": 4, "path": "v", "filter": {"$or": [ { "meta.b": { "$eq": 1 } }, { "meta.b": { "$eq": 2 } } , { "meta.b": { "$eq": 5 } } ] } }  } }, { "$project": {"searchScore": {"$round": [ {"$multiply": ["$__cosmos_meta__.score", 100000]}]} } } ], "cursor": {} }');
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_hnsw_filter", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3.0, 4.9, 1.0 ], "k": 4, "path": "v", "filter": {"$or": [ { "meta.b": { "$eq": 1 } }, { "meta.b": { "$eq": 2 } } , { "meta.b": { "$eq": 5 } } ] } }  } } ], "cursor": {} }');
ROLLBACK;

-- hnsw filter number: with $eq, $gt, $lt, $gte, $lte, $ne, $in, $and, $or
BEGIN;
SET LOCAL enable_seqscan to off;
SET LOCAL documentdb.enableVectorPreFilter = on;
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_hnsw_filter", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3.0, 4.9, 1.0 ], "k": 4, "path": "v", "filter": {"meta.b": { "$eq": 2 } }, "efSearch": 100 }  } }, { "$project": {"searchScore": {"$round": [ {"$multiply": ["$__cosmos_meta__.score", 100000]}]} } } ], "cursor": {} }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_hnsw_filter", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3.0, 4.9, 1.0 ], "k": 4, "path": "v", "filter": {"meta.b": { "$gte": 2 } }, "efSearch": 100 }  } }, { "$project": {"searchScore": {"$round": [ {"$multiply": ["$__cosmos_meta__.score", 100000]}]} } } ], "cursor": {} }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_hnsw_filter", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3.0, 4.9, 1.0 ], "k": 4, "path": "v", "filter": {"meta.b": { "$lte": 2 } }, "efSearch": 100 }  } }, { "$project": {"searchScore": {"$round": [ {"$multiply": ["$__cosmos_meta__.score", 100000]}]} } } ], "cursor": {} }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_hnsw_filter", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3.0, 4.9, 1.0 ], "k": 4, "path": "v", "filter": {"meta.b": { "$ne": 2 } }, "efSearch": 100 }  } }, { "$project": {"searchScore": {"$round": [ {"$multiply": ["$__cosmos_meta__.score", 100000]}]} } } ], "cursor": {} }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_hnsw_filter", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3.0, 4.9, 1.0 ], "k": 4, "path": "v", "filter": {"meta.b": { "$in": [ 2,3 ] } }, "efSearch": 100 }  } }, { "$project": {"searchScore": {"$round": [ {"$multiply": ["$__cosmos_meta__.score", 100000]}]} } } ], "cursor": {} }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_hnsw_filter", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3.0, 4.9, 1.0 ], "k": 4, "path": "v", "filter": {"meta.b": { "$nin": [ 2 ] } }, "efSearch": 100 }  } }, { "$project": {"searchScore": {"$round": [ {"$multiply": ["$__cosmos_meta__.score", 100000]}]} } } ], "cursor": {} }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_hnsw_filter", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3.0, 4.9, 1.0 ], "k": 4, "path": "v", "filter": {"meta.b": { "$gt": 2 } }, "efSearch": 100 }  } }, { "$project": {"searchScore": {"$round": [ {"$multiply": ["$__cosmos_meta__.score", 100000]}]} } } ], "cursor": {} }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_hnsw_filter", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3.0, 4.9, 1.0 ], "k": 4, "path": "v", "filter": {"meta.b": { "$lt": 2 } }, "efSearch": 100 }  } }, { "$project": {"searchScore": {"$round": [ {"$multiply": ["$__cosmos_meta__.score", 100000]}]} } } ], "cursor": {} }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_hnsw_filter", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3.0, 4.9, 1.0 ], "k": 4, "path": "v", "filter": {"$and": [ { "meta.b": { "$gt": 1 } }, { "meta.b": { "$lt": 5 } } ] }, "efSearch": 100 }  } }, { "$project": {"searchScore": {"$round": [ {"$multiply": ["$__cosmos_meta__.score", 100000]}]} } } ], "cursor": {} }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_hnsw_filter", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3.0, 4.9, 1.0 ], "k": 4, "path": "v", "filter": {"$or": [ { "meta.b": { "$gt": 1 } }, { "meta.b": { "$lt": 5 } } ] }, "efSearch": 100 }  } }, { "$project": {"searchScore": {"$round": [ {"$multiply": ["$__cosmos_meta__.score", 100000]}]} } } ], "cursor": {} }');
ROLLBACK;

--------------------------------------------------
-- hnsw filter boolean: with default efSearch
BEGIN;
SET LOCAL enable_seqscan to off;
SET LOCAL documentdb.enableVectorPreFilter = on;
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_hnsw_filter", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3.0, 4.9, 1.0 ], "k": 4, "path": "v", "filter": { "c": true } }  } }, { "$project": {"searchScore": {"$round": [ {"$multiply": ["$__cosmos_meta__.score", 100000]}]} } } ], "cursor": {} }');
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_hnsw_filter", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3.0, 4.9, 1.0 ], "k": 4, "path": "v", "filter": { "c": true } }  } } ], "cursor": {} }');
ROLLBACK;

-- hnsw filter boolean, with efSearch = 1, match 1 document
BEGIN;
SET LOCAL enable_seqscan to off;
SET LOCAL documentdb.enableVectorPreFilter = on;
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_hnsw_filter", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3.0, 4.9, 1.0 ], "k": 4, "path": "v", "filter": { "c": true }, "efSearch": 1 }  } }, { "$project": {"searchScore": {"$round": [ {"$multiply": ["$__cosmos_meta__.score", 100000]}]} } } ], "cursor": {} }');
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_hnsw_filter", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3.0, 4.9, 1.0 ], "k": 4, "path": "v", "filter": { "c": true }, "efSearch": 1 }  } } ], "cursor": {} }');
ROLLBACK;

-- TODO, current implementation is post-filtering, need to fix
-- hnsw filter boolean, with c = false, efSearch = 1, match 0 document
BEGIN;
SET LOCAL enable_seqscan to off;
SET LOCAL documentdb.enableVectorPreFilter = on;
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_hnsw_filter", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3.0, 4.9, 1.0 ], "k": 4, "path": "v", "filter": { "c": false }, "efSearch": 1 }  } }, { "$project": {"searchScore": {"$round": [ {"$multiply": ["$__cosmos_meta__.score", 100000]}]} } } ], "cursor": {} }');
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_hnsw_filter", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3.0, 4.9, 1.0 ], "k": 4, "path": "v", "filter": { "c": false }, "efSearch": 1 }  } } ], "cursor": {} }');
ROLLBACK;

-- hnsw filter boolean, with c = false, efSearch = 3, match 1 document
BEGIN;
SET LOCAL enable_seqscan to off;
SET LOCAL documentdb.enableVectorPreFilter = on;
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_hnsw_filter", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3.0, 4.9, 1.0 ], "k": 4, "path": "v", "filter": { "c": false }, "efSearch": 3 }  } }, { "$project": {"searchScore": {"$round": [ {"$multiply": ["$__cosmos_meta__.score", 100000]}]} } } ], "cursor": {} }');
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_hnsw_filter", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3.0, 4.9, 1.0 ], "k": 4, "path": "v", "filter": { "c": false }, "efSearch": 3 }  } } ], "cursor": {} }');
ROLLBACK;

-- hnsw search: filter boolean, with $eq, $ne, $in, $nin, $and, $or
BEGIN;
SET LOCAL enable_seqscan to off;
SET LOCAL documentdb.enableVectorPreFilter = on;
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_hnsw_filter", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3.0, 4.9, 1.0 ], "k": 4, "path": "v", "filter": {"c":  { "$eq": true}}, "efSearch": 100 }  } }, { "$project": {"searchScore": {"$round": [ {"$multiply": ["$__cosmos_meta__.score", 100000]}]} } } ], "cursor": {} }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_hnsw_filter", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3.0, 4.9, 1.0 ], "k": 4, "path": "v", "filter": {"c":  { "$ne": true}}, "efSearch": 100 }  } }, { "$project": {"searchScore": {"$round": [ {"$multiply": ["$__cosmos_meta__.score", 100000]}]} } } ], "cursor": {} }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_hnsw_filter", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3.0, 4.9, 1.0 ], "k": 4, "path": "v", "filter": {"c":  { "$in": [true]}}, "efSearch": 100 }  } }, { "$project": {"searchScore": {"$round": [ {"$multiply": ["$__cosmos_meta__.score", 100000]}]} } } ], "cursor": {} }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_hnsw_filter", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3.0, 4.9, 1.0 ], "k": 4, "path": "v", "filter": {"c":  { "$nin": [true]}}, "efSearch": 100 }  } }, { "$project": {"searchScore": {"$round": [ {"$multiply": ["$__cosmos_meta__.score", 100000]}]} } } ], "cursor": {} }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_hnsw_filter", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3.0, 4.9, 1.0 ], "k": 4, "path": "v", "filter": {"$and": [ { "c": { "$eq": true } }, { "c": { "$eq": false } } ] }, "efSearch": 100 }  } }, { "$project": {"searchScore": {"$round": [ {"$multiply": ["$__cosmos_meta__.score", 100000]}]} } } ], "cursor": {} }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_hnsw_filter", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3.0, 4.9, 1.0 ], "k": 4, "path": "v", "filter": {"$or": [ { "c": { "$eq": true } }, { "c": { "$eq": false } } ] }, "efSearch": 100 }  } }, { "$project": {"searchScore": {"$round": [ {"$multiply": ["$__cosmos_meta__.score", 100000]}]} } } ], "cursor": {} }');
ROLLBACK;

--------------------------------------------------
-- hnsw search: with filter, different distance metric

-- hnsw search: cosine similarity
CALL documentdb_api.drop_indexes('db', '{ "dropIndexes": "aggregation_pipeline_hnsw_filter", "index": "hnsw_index"}');
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "aggregation_pipeline_hnsw_filter", "indexes": [ { "key": { "v": "cosmosSearch" }, "name": "hnsw_index_cos", "cosmosSearchOptions": { "kind": "vector-hnsw", "m": 4, "efConstruction": 16, "similarity": "COS", "dimensions": 3 } } ] }', true);
ANALYZE;

BEGIN;
SET LOCAL documentdb.enableVectorPreFilter = on;
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_hnsw_filter", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3.0, 4.9, 1.0 ], "k": 4, "path": "v", "filter": {"meta.a":  { "$regex": "^some", "$options" : "i"}}, "efSearch": 100 }  } }, { "$project": {"searchScore": {"$round": [ {"$multiply": ["$__cosmos_meta__.score", 100000]}]} } } ], "cursor": {} }');
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_hnsw_filter", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3.0, 4.9, 1.0 ], "k": 4, "path": "v", "filter": {"meta.a":  { "$regex": "^some", "$options" : "i"}}, "efSearch": 100 }  } } ], "cursor": {} }');
ROLLBACK;

-- hnsw search: inner product
CALL documentdb_api.drop_indexes('db', '{ "dropIndexes": "aggregation_pipeline_hnsw_filter", "index": "hnsw_index_cos"}');
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "aggregation_pipeline_hnsw_filter", "indexes": [ { "key": { "v": "cosmosSearch" }, "name": "hnsw_index_ip", "cosmosSearchOptions": { "kind": "vector-hnsw", "m": 4, "efConstruction": 16, "similarity": "IP", "dimensions": 3 } } ] }', true);

BEGIN;
SET LOCAL documentdb.enableVectorPreFilter = on;
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_hnsw_filter", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3.0, 4.9, 1.0 ], "k": 4, "path": "v", "filter": {"meta.a":  { "$regex": "^some", "$options" : "i"}}, "efSearch": 100 }  } }, { "$project": {"searchScore": {"$round": [ {"$multiply": ["$__cosmos_meta__.score", 100000]}]} } } ], "cursor": {} }');
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_hnsw_filter", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3.0, 4.9, 1.0 ], "k": 4, "path": "v", "filter": {"meta.a":  { "$regex": "^some", "$options" : "i"}}, "efSearch": 100 }  } } ], "cursor": {} }');
ROLLBACK;

-- hnsw search: restore to euclidean distance
CALL documentdb_api.drop_indexes('db', '{ "dropIndexes": "aggregation_pipeline_hnsw_filter", "index": "hnsw_index_ip"}');
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "aggregation_pipeline_hnsw_filter", "indexes": [ { "key": { "v": "cosmosSearch" }, "name": "hnsw_index", "cosmosSearchOptions": { "kind": "vector-hnsw", "m": 4, "efConstruction": 16, "similarity": "L2", "dimensions": 3 } } ] }', true);
ANALYZE;

--------------------------------------------------
-- hnsw search: with filter and score projection
BEGIN;
SET LOCAL enable_seqscan to off;
SET LOCAL documentdb.enableVectorPreFilter = on;
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_hnsw_filter", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3.0, 4.9, 1.0 ], "k": 4, "path": "v", "filter": {"meta.a.b": 3 }, "efSearch": 100 }  } } , { "$project": { "rank": {"$round":[{"$multiply": [{"$meta": "searchScore" }, 100000]}]} } } ], "cursor": {} }');
ROLLBACK;

BEGIN;
SET LOCAL enable_seqscan to off;
SET LOCAL documentdb.enableVectorPreFilter = on;
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_hnsw_filter", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3.0, 4.9, 1.0 ], "k": 4, "path": "v", "filter": {"meta.a.b": 3 }, "efSearch": 100 }  } } , { "$project": { "rank": {"$round":[{"$multiply": [{"$meta": "searchScore" }, 100000]}]} } } ], "cursor": {} }');
ROLLBACK;

-- hnsw search: with filter and score projection, efSearch = 4, match 1 document
BEGIN;
SET LOCAL enable_seqscan to off;
SET LOCAL documentdb.enableVectorPreFilter = on;
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_hnsw_filter", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3.0, 4.9, 1.0 ], "k": 2, "path": "v", "filter": {"meta": { "a" : [ { "b" : 3 } ] } }, "efSearch": 4 }  } } , { "$project": { "rank": {"$round":[{"$multiply": [{"$meta": "searchScore" }, 100000]}]} } } ], "cursor": {} }');
ROLLBACK;

BEGIN;
SET LOCAL enable_seqscan to off;
SET LOCAL documentdb.enableVectorPreFilter = on;
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_hnsw_filter", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3.0, 4.9, 1.0 ], "k": 2, "path": "v", "filter": {"meta": { "a" : [ { "b" : 3 } ] } }, "efSearch": 4 }  } } , { "$project": { "rank": {"$round":[{"$multiply": [{"$meta": "searchScore" }, 100000]}]} } } ], "cursor": {} }');
ROLLBACK;

-- hnsw search: with filter and score projection, efSearch = 3, no match document
BEGIN;
SET LOCAL enable_seqscan to off;
SET LOCAL documentdb.enableVectorPreFilter = on;
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_hnsw_filter", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3.0, 4.9, 1.0 ], "k": 2, "path": "v", "filter": {"meta.a": [ { "b" : 3 } ]}, "efSearch": 3 }  } } , { "$project": { "rank": {"$round":[{"$multiply": [{"$meta": "searchScore" }, 100000]}]} } } ], "cursor": {} }');
ROLLBACK;

BEGIN;
SET LOCAL enable_seqscan to off;
SET LOCAL documentdb.enableVectorPreFilter = on;
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_hnsw_filter", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3.0, 4.9, 1.0 ], "k": 2, "path": "v", "filter": {"meta.a": [ { "b" : 3 } ]}, "efSearch": 3 }  } } , { "$project": { "rank": {"$round":[{"$multiply": [{"$meta": "searchScore" }, 100000]}]} } } ], "cursor": {} }');
ROLLBACK;

-- hnsw search: with filter and score projection, $ne, efSearch = 1, no match document
BEGIN;
SET LOCAL enable_seqscan to off;
SET LOCAL documentdb.enableVectorPreFilter = on;
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_hnsw_filter", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3.0, 4.9, 1.0 ], "k": 4, "path": "v", "filter": {"meta.a":  { "$ne": "some sentence"}}, "efSearch": 1 }  } } , { "$project": { "rank": {"$round":[{"$multiply": [{"$meta": "searchScore" }, 100000]}]} } } ], "cursor": {} }');
ROLLBACK;

BEGIN;
SET LOCAL enable_seqscan to off;
SET LOCAL documentdb.enableVectorPreFilter = on;
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_hnsw_filter", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3.0, 4.9, 1.0 ], "k": 4, "path": "v", "filter": {"meta.a":  { "$ne": "some sentence"}}, "efSearch": 1 }  } } , { "$project": { "rank": {"$round":[{"$multiply": [{"$meta": "searchScore" }, 100000]}]} } } ], "cursor": {} }');
ROLLBACK;

BEGIN;
SET LOCAL enable_seqscan to off;
SET LOCAL documentdb.enableVectorPreFilter = on;
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_hnsw_filter", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3.0, 4.9, 1.0 ], "k": 2, "path": "v", "filter": {"meta.b": { "$gte": 2 } }, "efSearch": 100 }  } } , { "$project": { "rank": {"$round":[{"$multiply": [{"$meta": "searchScore" }, 100000]}]} } } ], "cursor": {} }');
ROLLBACK;

BEGIN;
SET LOCAL enable_seqscan to off;
SET LOCAL documentdb.enableVectorPreFilter = on;
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_hnsw_filter", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3.0, 4.9, 1.0 ], "k": 2, "path": "v", "filter": {"meta.b": { "$gte": 2 } }, "efSearch": 100 }  } } , { "$project": { "rank": {"$round":[{"$multiply": [{"$meta": "searchScore" }, 100000]}]} } } ], "cursor": {} }');
ROLLBACK;

BEGIN;
SET LOCAL enable_seqscan to off;
SET LOCAL documentdb.enableVectorPreFilter = on;
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_hnsw_filter", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3.0, 4.9, 1.0 ], "k": 2, "path": "v", "filter": {"c":  { "$eq": false}}, "efSearch": 100 }  } } , { "$project": { "rank": {"$round":[{"$multiply": [{"$meta": "searchScore" }, 100000]}]} } } ], "cursor": {} }');
ROLLBACK;

BEGIN;
SET LOCAL enable_seqscan to off;
SET LOCAL documentdb.enableVectorPreFilter = on;
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_hnsw_filter", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3.0, 4.9, 1.0 ], "k": 2, "path": "v", "filter": {"c":  { "$eq": false}}, "efSearch": 100 }  } } , { "$project": { "rank": {"$round":[{"$multiply": [{"$meta": "searchScore" }, 100000]}]} } } ], "cursor": {} }');
ROLLBACK;

-- hnsw search: with filter and score projection, $or, multiple filters
BEGIN;
SET LOCAL enable_seqscan to off;
SET LOCAL documentdb.enableVectorPreFilter = on;
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_hnsw_filter", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3.0, 4.9, 1.0 ], "k": 10, "path": "v", "filter": {"$or": [ { "meta.a": { "$eq": "some sentence" } }, { "meta.b": { "$gt": 2 } }, {"c":  { "$eq": false } } ] }, "efSearch": 1 }  } } , { "$project": { "rank": {"$round":[{"$multiply": [{"$meta": "searchScore" }, 100000]}]} } } ], "cursor": {} }');
ROLLBACK;

--------------------------------------------------
-- hnsw search: with filter and shard
-- TODO, current implementation does not support sharded collection, need to fix in part 3
BEGIN;
SET LOCAL client_min_messages TO WARNING;
SELECT documentdb_api.shard_collection('db','aggregation_pipeline_hnsw_filter', '{"_id":"hashed"}', false);
END;
ANALYZE;

-- hnsw search: with filter and shard, default efSearch, match 1 document
BEGIN;
SET LOCAL enable_seqscan to off;
SET LOCAL documentdb.enableVectorPreFilter = on;
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_hnsw_filter", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3.0, 4.9, 1.0 ], "k": 4, "path": "v", "filter": {"meta.a.b": 3 }, "efSearch": 1 }  } } , { "$project": { "rank": {"$round":[{"$multiply": [{"$meta": "searchScore" }, 100000]}]} } } ], "cursor": {} }');
ROLLBACK;

BEGIN;
SET LOCAL enable_seqscan to off;
SET LOCAL documentdb.enableVectorPreFilter = on;
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_hnsw_filter", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3.0, 4.9, 1.0 ], "k": 4, "path": "v", "filter": {"meta.a.b": 3 }, "efSearch": 1 }  } } , { "$project": { "rank": {"$round":[{"$multiply": [{"$meta": "searchScore" }, 100000]}]} } } ], "cursor": {} }');
ROLLBACK;

BEGIN;
SET LOCAL enable_seqscan to off;
SET LOCAL documentdb.enableVectorPreFilter = on;
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_hnsw_filter", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3.0, 4.9, 1.0 ], "k": 2, "path": "v", "filter": {"meta": { "a" : [ { "b" : 3 } ] } }, "efSearch": 1 }  } } , { "$project": { "rank": {"$round":[{"$multiply": [{"$meta": "searchScore" }, 100000]}]} } } ], "cursor": {} }');
ROLLBACK;

BEGIN;
SET LOCAL enable_seqscan to off;
SET LOCAL documentdb.enableVectorPreFilter = on;
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_hnsw_filter", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3.0, 4.9, 1.0 ], "k": 2, "path": "v", "filter": {"meta": { "a" : [ { "b" : 3 } ] } }, "efSearch": 1 }  } } , { "$project": { "rank": {"$round":[{"$multiply": [{"$meta": "searchScore" }, 100000]}]} } } ], "cursor": {} }');
ROLLBACK;

BEGIN;
SET LOCAL enable_seqscan to off;
SET LOCAL documentdb.enableVectorPreFilter = on;
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_hnsw_filter", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3.0, 4.9, 1.0 ], "k": 2, "path": "v", "filter": {"meta.a": [ { "b" : 3 } ]}, "efSearch": 1 }  } } , { "$project": { "rank": {"$round":[{"$multiply": [{"$meta": "searchScore" }, 100000]}]} } } ], "cursor": {} }');
ROLLBACK;

BEGIN;
SET LOCAL enable_seqscan to off;
SET LOCAL documentdb.enableVectorPreFilter = on;
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_hnsw_filter", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3.0, 4.9, 1.0 ], "k": 2, "path": "v", "filter": {"meta.a": [ { "b" : 3 } ]}, "efSearch": 1 }  } } , { "$project": { "rank": {"$round":[{"$multiply": [{"$meta": "searchScore" }, 100000]}]} } } ], "cursor": {} }');
ROLLBACK;

-- hnsw search: with filter and shard, $ne, efSearch = 1, match 3 documents
BEGIN;
SET LOCAL enable_seqscan to off;
SET LOCAL documentdb.enableVectorPreFilter = on;
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_hnsw_filter", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3.0, 4.9, 1.0 ], "k": 4, "path": "v", "filter": {"meta.a":  { "$ne": "some sentence"}}, "efSearch": 1 }  } } , { "$project": { "rank": {"$round":[{"$multiply": [{"$meta": "searchScore" }, 100000]}]} } } ], "cursor": {} }');
ROLLBACK;

BEGIN;
SET LOCAL enable_seqscan to off;
SET LOCAL documentdb.enableVectorPreFilter = on;
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_hnsw_filter", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3.0, 4.9, 1.0 ], "k": 4, "path": "v", "filter": {"meta.a":  { "$ne": "some sentence"}}, "efSearch": 1 }  } } , { "$project": { "rank": {"$round":[{"$multiply": [{"$meta": "searchScore" }, 100000]}]} } } ], "cursor": {} }');
ROLLBACK;

-- hnsw search: with filter and shard, number, match 2 documents
BEGIN;
SET LOCAL enable_seqscan to off;
SET LOCAL documentdb.enableVectorPreFilter = on;
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_hnsw_filter", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3.0, 4.9, 1.0 ], "k": 2, "path": "v", "filter": {"meta.b": { "$gte": 2 } }, "efSearch": 1 }  } } , { "$project": { "rank": {"$round":[{"$multiply": [{"$meta": "searchScore" }, 100000]}]} } } ], "cursor": {} }');
ROLLBACK;

BEGIN;
SET LOCAL enable_seqscan to off;
SET LOCAL documentdb.enableVectorPreFilter = on;
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_hnsw_filter", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3.0, 4.9, 1.0 ], "k": 2, "path": "v", "filter": {"meta.b": { "$gte": 2 } }, "efSearch": 1 }  } } , { "$project": { "rank": {"$round":[{"$multiply": [{"$meta": "searchScore" }, 100000]}]} } } ], "cursor": {} }');
ROLLBACK;

-- hnsw search: with filter and shard, boolean, match 2 documents
BEGIN;
SET LOCAL enable_seqscan to off;
SET LOCAL documentdb.enableVectorPreFilter = on;
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_hnsw_filter", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3.0, 4.9, 1.0 ], "k": 2, "path": "v", "filter": {"c":  { "$eq": false}}, "efSearch": 1 }  } } , { "$project": { "rank": {"$round":[{"$multiply": [{"$meta": "searchScore" }, 100000]}]} } } ], "cursor": {} }');
ROLLBACK;

BEGIN;
SET LOCAL enable_seqscan to off;
SET LOCAL documentdb.enableVectorPreFilter = on;
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_hnsw_filter", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3.0, 4.9, 1.0 ], "k": 2, "path": "v", "filter": {"c":  { "$eq": false}}, "efSearch": 1 }  } } , { "$project": { "rank": {"$round":[{"$multiply": [{"$meta": "searchScore" }, 100000]}]} } } ], "cursor": {} }');
ROLLBACK;

-- hnsw search: with filter and shard, $or, multiple filters
BEGIN;
SET LOCAL enable_seqscan to off;
SET LOCAL documentdb.enableVectorPreFilter = on;
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_hnsw_filter", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3.0, 4.9, 1.0 ], "k": 10, "path": "v", "filter": {"$or": [ { "meta.a": { "$eq": "some sentence" } }, { "meta.b": { "$gt": 2 } }, {"c":  { "$eq": false } } ] }, "efSearch": 1 }  } } , { "$project": { "rank": {"$round":[{"$multiply": [{"$meta": "searchScore" }, 100000]}]} } } ], "cursor": {} }');
ROLLBACK;

SET documentdb.enableVectorPreFilter = off;

--------------------------------------------------
-- create hnsw index and search with nProbes
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "aggregation_pipeline_vector", "indexes": [ { "key": { "v": "cosmosSearch" }, "name": "foo_1", "cosmosSearchOptions": { "kind": "vector-hnsw", "similarity": "COS", "dimensions": 3 } } ] }', true);
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_vector", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3.0, 4.9, 1.0 ], "k": 2, "path": "v", "nProbes": 10}  } } ], "cursor": {} }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_vector", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3.0, 4.9, 1.0 ], "k": 2, "path": "v", "efSearch": 5, "efSearch": 10 }  } } ], "cursor": {} }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_vector", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3.0, 4.9, 1.0 ], "k": 2, "path": "v", "nProbes": 5, "efSearch": 10 }  } } ], "cursor": {} }');

CALL documentdb_api.drop_indexes('db', '{ "dropIndexes": "aggregation_pipeline_vector", "index": "foo_1"}');

-- create ivf index and search with efSearch
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "aggregation_pipeline_vector", "indexes": [ { "key": { "v": "cosmosSearch" }, "name": "foo_1", "cosmosSearchOptions": { "kind": "vector-ivf", "numLists": 2, "similarity": "COS", "dimensions": 3 } } ] }', true);
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_vector", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3.0, 4.9, 1.0 ], "k": 2, "path": "v", "efSearch": 5 }  } } ], "cursor": {} }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_vector", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3.0, 4.9, 1.0 ], "k": 2, "path": "v", "efSearch": 10, "nProbes": 5 }  } } ], "cursor": {} }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_vector", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3.0, 4.9, 1.0 ], "k": 2, "path": "v", "nProbes": 10, "nProbes": 5 }  } } ], "cursor": {} }');

CALL documentdb_api.drop_indexes('db', '{ "dropIndexes": "aggregation_pipeline_vector", "index": "foo_1"}');

-- create ivf index and search with efSearch, hnsw index is disabled
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "aggregation_pipeline_vector", "indexes": [ { "key": { "v": "cosmosSearch" }, "name": "foo_1", "cosmosSearchOptions": { "kind": "vector-ivf", "numLists": 2, "similarity": "COS", "dimensions": 3 } } ] }', true);
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_vector", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3.0, 4.9, 1.0 ], "k": 2, "path": "v", "efSearch": 5 }  } } ], "cursor": {} }');
CALL documentdb_api.drop_indexes('db', '{ "dropIndexes": "aggregation_pipeline_vector", "index": "foo_1"}');

select documentdb_api.drop_collection('db', 'aggregation_pipeline_vector');

----------------------------------------------------------------------------------------------------
-- Vector search with empty vector field
-- hnsw
SET search_path TO documentdb_core,documentdb_api,documentdb_api_catalog,documentdb_api_internal;
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "aggregation_pipeline_empty_vector", "indexes": [ { "key": { "v": "cosmosSearch" }, "name": "vectorIndex", "cosmosSearchOptions": { "kind": "vector-hnsw", "m": 4, "efConstruction": 16, "similarity": "COS", "dimensions": 3 } } ] }', true);
SELECT documentdb_api.insert_one('db', 'aggregation_pipeline_empty_vector', '{ "_id": 1, "a": "some sentence", "v": [1, 2, 3 ] }');
SELECT documentdb_api.insert_one('db', 'aggregation_pipeline_empty_vector', '{ "_id": 2, "a": "some other sentence", "v": [1, 2.0, 4 ] }');
SELECT documentdb_api.insert_one('db', 'aggregation_pipeline_empty_vector', '{ "_id": 3, "a": "some sentence" }');
SELECT documentdb_api.insert_one('db', 'aggregation_pipeline_empty_vector', '{ "_id": 4, "a": "some other sentence", "v": [3, 2, 1 ] }');
SELECT documentdb_distributed_test_helpers.drop_primary_key('db','aggregation_pipeline_empty_vector');
ANALYZE;

BEGIN;
SET LOCAL enable_seqscan = off;
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_empty_vector", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3.0, 4.9, 1.0 ], "k": 4, "path": "v", "efSearch": 16 }  } }, { "$project": { "rank": {"$round":[{"$multiply": [{"$meta": "searchScore" }, 100000]}]} } } ], "cursor": {} }');
EXPLAIN (COSTS OFF)SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_empty_vector", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3.0, 4.9, 1.0 ], "k": 4, "path": "v", "efSearch": 5 }  } }, { "$project": { "rank": {"$round":[{"$multiply": [{"$meta": "searchScore" }, 100000]}]} } } ], "cursor": {} }');
COMMIT;

ANALYZE;
BEGIN;
SET LOCAL enable_seqscan = off;
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_empty_vector", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3.0, 4.9, 1.0 ], "k": 4, "path": "v", "efSearch": 16 }  } }, { "$project": { "rank": {"$round":[{"$multiply": [{"$meta": "searchScore" }, 100000]}]} } } ], "cursor": {} }');
EXPLAIN (COSTS OFF)SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_empty_vector", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3.0, 4.9, 1.0 ], "k": 4, "path": "v", "efSearch": 5 }  } }, { "$project": { "rank": {"$round":[{"$multiply": [{"$meta": "searchScore" }, 100000]}]} } } ], "cursor": {} }');
COMMIT;

SELECT drop_collection('db','aggregation_pipeline_empty_vector');

DROP ROLE IF EXISTS test_filter_user;


----------------------------------------------------------------------------------------------------
-- exact search
SELECT documentdb_api.insert_one('db', 'aggregation_pipeline_vector_hnsw_exact', '{ "_id": 6, "meta":{ "a": "some sentence", "b": 1 }, "c": true , "v": [3.0, 5.0, 1.1 ] }');
SELECT documentdb_api.insert_one('db', 'aggregation_pipeline_vector_hnsw_exact', '{ "_id": 7, "meta":{ "a": "some other sentence", "b": 2}, "c": true , "v": [8.0, 5.0, 0.1 ] }');
SELECT documentdb_api.insert_one('db', 'aggregation_pipeline_vector_hnsw_exact', '{ "_id": 8, "meta":{ "a": "other sentence", "b": 5 }, "c": false, "v": [13.0, 5.0, 0.1 ] }');
SELECT documentdb_api.insert_one('db', 'aggregation_pipeline_vector_hnsw_exact', '{ "_id": 9, "meta":{ "a" : [ { "b" : 3 } ] }, "c": false, "v": [15.0, 5.0, 0.1 ] }');
SELECT documentdb_api.insert_one('db', 'aggregation_pipeline_vector_hnsw_exact', '{ "_id": 10, "meta":{ "a" : [ { "b" : 5 } ] }, "c": false }');
SELECT documentdb_distributed_test_helpers.drop_primary_key('db','aggregation_pipeline_vector_hnsw_exact');

SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "aggregation_pipeline_vector_hnsw_exact", "indexes": [ { "key": { "v": "cosmosSearch" }, "name": "foo_1", "cosmosSearchOptions": { "kind": "vector-hnsw", "m": 4, "efConstruction": 16, "similarity": "COS", "dimensions": 3 } } ] }', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "aggregation_pipeline_vector_hnsw_exact", "indexes": [ { "key": { "meta.a": 1 }, "name": "idx_meta.a" } ] }', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "aggregation_pipeline_vector_hnsw_exact", "indexes": [ { "key": { "meta.b": 1 }, "name": "numberIndex_meta.b" } ] }', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "aggregation_pipeline_vector_hnsw_exact", "indexes": [ { "key": { "meta.a.b": 1 }, "name": "idx_meta.a.b" } ] }', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "aggregation_pipeline_vector_hnsw_exact", "indexes": [ { "key": { "meta": 1 }, "name": "documentIndex_meta" } ] }', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "aggregation_pipeline_vector_hnsw_exact", "indexes": [ { "key": { "c": 1 }, "name": "boolIndex_c" } ] }', true);
ANALYZE;

-- error cases
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_vector_hnsw_exact", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3.0, 4.9, 1.0 ], "k": 2, "path": "v", "efSearch": 1, "exact": {}  }  } } ], "cursor": {} }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_vector_hnsw_exact", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3.0, 4.9, 1.0 ], "k": 2, "path": "v", "efSearch": 1, "exact": 123  }  } } ], "cursor": {} }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_vector_hnsw_exact", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3.0, 4.9, 1.0 ], "k": 2, "path": "v", "efSearch": 1, "exact": "abc"  }  } } ], "cursor": {} }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_vector_hnsw_exact", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3.0, 4.9, 1.0 ], "k": 2, "path": "v", "efSearch": 1, "exact": [1,2,3]  }  } } ], "cursor": {} }');

-- COS
BEGIN;
SET LOCAL enable_seqscan = off;
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_vector_hnsw_exact", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3.0, 4.9, 1.0 ], "k": 2, "path": "v", "efSearch": 1, "exact": true }  } } ], "cursor": {} }');
EXPLAIN (VERBOSE on, COSTS off) SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_vector_hnsw_exact", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3.0, 4.9, 1.0 ], "k": 2, "path": "v", "efSearch": 1, "exact": true }  } } ], "cursor": {} }');
COMMIT;

-- IP
CALL documentdb_api.drop_indexes('db', '{ "dropIndexes": "aggregation_pipeline_vector_hnsw_exact", "index": "foo_1"}');
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "aggregation_pipeline_vector_hnsw_exact", "indexes": [ { "key": { "v": "cosmosSearch" }, "name": "foo_1", "cosmosSearchOptions": { "kind": "vector-hnsw", "m": 4, "efConstruction": 16, "similarity": "IP", "dimensions": 3 } } ] }', true);
ANALYZE;
BEGIN;
SET LOCAL enable_seqscan = off;
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_vector_hnsw_exact", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3.0, 4.9, 1.0 ], "k": 2, "path": "v", "efSearch": 1, "exact": true }  } } ], "cursor": {} }');
EXPLAIN (VERBOSE on, COSTS off) SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_vector_hnsw_exact", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3.0, 4.9, 1.0 ], "k": 2, "path": "v", "efSearch": 1, "exact": true }  } } ], "cursor": {} }');
COMMIT;

-- L2
CALL documentdb_api.drop_indexes('db', '{ "dropIndexes": "aggregation_pipeline_vector_hnsw_exact", "index": "foo_1"}');
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "aggregation_pipeline_vector_hnsw_exact", "indexes": [ { "key": { "v": "cosmosSearch" }, "name": "foo_1", "cosmosSearchOptions": { "kind": "vector-hnsw", "m": 4, "efConstruction": 16, "similarity": "L2", "dimensions": 3 } } ] }', true);
ANALYZE;
BEGIN;
SET LOCAL enable_seqscan = off;
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_vector_hnsw_exact", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3.0, 4.9, 1.0 ], "k": 2, "path": "v", "efSearch": 1, "exact": true }  } } ], "cursor": {} }');
EXPLAIN (VERBOSE on, COSTS off) SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_vector_hnsw_exact", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3.0, 4.9, 1.0 ], "k": 2, "path": "v", "efSearch": 1, "exact": true }  } } ], "cursor": {} }');
COMMIT;

-- efSearch
BEGIN;
SET LOCAL enable_seqscan = off;
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_vector_hnsw_exact", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3.0, 4.9, 1.0 ], "k": 2, "path": "v", "exact": true }  } } ], "cursor": {} }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_vector_hnsw_exact", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3.0, 4.9, 1.0 ], "k": 2, "path": "v", "efSearch": 1, "exact": true }  } } ], "cursor": {} }');
ROLLBACK;

-- exact = false
BEGIN;
SET LOCAL enable_seqscan = off;
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_vector_hnsw_exact", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3.0, 4.9, 1.0 ], "k": 2, "path": "v", "exact": false }  } } ], "cursor": {} }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_vector_hnsw_exact", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3.0, 4.9, 1.0 ], "k": 2, "path": "v", "efSearch": 1, "exact": false }  } } ], "cursor": {} }');
ROLLBACK;

-- filter:, with $eq, $gt, $lt, $gte, $lte, $ne, $in, $and, $or, $regex
BEGIN;
SET LOCAL enable_seqscan to off;
SET LOCAL documentdb.enableVectorPreFilter = on;
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_vector_hnsw_exact", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3.0, 4.9, 1.0 ], "k": 4, "path": "v", "filter": {"meta.a":  { "$eq": "some sentence"}}, "exact": true, "efSearch": 1 }  } }, { "$project": {"searchScore": {"$round": [ {"$multiply": ["$__cosmos_meta__.score", 100000]}]} } } ], "cursor": {} }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_vector_hnsw_exact", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3.0, 4.9, 1.0 ], "k": 4, "path": "v", "filter": {"meta.a":  { "$gt": "some sentence"}}, "exact": true, "efSearch": 1 }  } }, { "$project": {"searchScore": {"$round": [ {"$multiply": ["$__cosmos_meta__.score", 100000]}]} } } ], "cursor": {} }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_vector_hnsw_exact", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3.0, 4.9, 1.0 ], "k": 4, "path": "v", "filter": {"meta.a":  { "$lt": "some sentence"}}, "exact": true, "efSearch": 1 }  } }, { "$project": {"searchScore": {"$round": [ {"$multiply": ["$__cosmos_meta__.score", 100000]}]} } } ], "cursor": {} }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_vector_hnsw_exact", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 15.0, 5.0, 0.1 ], "k": 4, "path": "v", "filter": {"meta.a":  { "$ne": "some sentence"}}, "exact": true, "efSearch": 1 }  } }, { "$project": {"searchScore": {"$round": [ {"$multiply": ["$__cosmos_meta__.score", 100000]}]} } } ], "cursor": {} }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_vector_hnsw_exact", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3.0, 4.9, 1.0 ], "k": 4, "path": "v", "filter": {"meta.a":  { "$in": ["some sentence", "other sentence"]}}, "exact": true, "efSearch": 1 }  } }, { "$project": {"searchScore": {"$round": [ {"$multiply": ["$__cosmos_meta__.score", 100000]}]} } } ], "cursor": {} }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_vector_hnsw_exact", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3.0, 4.9, 1.0 ], "k": 4, "path": "v", "filter": {"meta.a":  { "$regex": "^some", "$options" : "i"}}, "exact": true, "efSearch": 1 }  } }, { "$project": {"searchScore": {"$round": [ {"$multiply": ["$__cosmos_meta__.score", 100000]}]} } } ], "cursor": {} }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_vector_hnsw_exact", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3.0, 4.9, 1.0 ], "k": 4, "path": "v", "filter": {"meta.a":  [ { "b" : 3 } ] }, "exact": true, "efSearch": 1 }  } }, { "$project": {"searchScore": {"$round": [ {"$multiply": ["$__cosmos_meta__.score", 100000]}]} } } ], "cursor": {} }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_vector_hnsw_exact", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3.0, 4.9, 1.0 ], "k": 4, "path": "v", "filter": {"$and": [ { "meta.a": { "$eq": "other sentence" } }, { "meta.b": { "$lt": 10 } } ] }, "exact": true, "efSearch": 1 }  } }, { "$project": {"searchScore": {"$round": [ {"$multiply": ["$__cosmos_meta__.score", 100000]}]} } } ], "cursor": {} }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_vector_hnsw_exact", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3.0, 4.9, 1.0 ], "k": 4, "path": "v", "filter": {"$or": [ { "meta.a": { "$gt": "other sentence" } }, { "meta.b": { "$gt": 1 } } ] }, "exact": true, "efSearch": 1 }  } }, { "$project": {"searchScore": {"$round": [ {"$multiply": ["$__cosmos_meta__.score", 100000]}]} } } ], "cursor": {} }');
ROLLBACK;

BEGIN;
SET LOCAL enable_seqscan to off;
SET LOCAL documentdb.enableVectorPreFilter = on;
EXPLAIN (VERBOSE on, COSTS off) SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_vector_hnsw_exact", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3.0, 4.9, 1.0 ], "k": 4, "path": "v", "filter": {"meta.a":  { "$in": ["some sentence", "other sentence"]}}, "exact": true, "efSearch": 1 }  } }, { "$project": {"searchScore": {"$round": [ {"$multiply": ["$__cosmos_meta__.score", 100000]}]} } } ], "cursor": {} }');
EXPLAIN (VERBOSE on, COSTS off) SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_vector_hnsw_exact", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3.0, 4.9, 1.0 ], "k": 4, "path": "v", "filter": {"$and": [ { "meta.a": { "eq": "other sentence" } }, { "meta.b": { "$lt": 10 } } ] }, "exact": true, "efSearch": 1 }  } }, { "$project": {"searchScore": {"$round": [ {"$multiply": ["$__cosmos_meta__.score", 100000]}]} } } ], "cursor": {} }');
EXPLAIN (VERBOSE on, COSTS off) SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_vector_hnsw_exact", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3.0, 4.9, 1.0 ], "k": 4, "path": "v", "filter": {"$or": [ { "meta.a": { "$gt": "other sentence" } }, { "meta.b": { "$gt": 1 } } ] }, "exact": true, "efSearch": 1 }  } }, { "$project": {"searchScore": {"$round": [ {"$multiply": ["$__cosmos_meta__.score", 100000]}]} } } ], "cursor": {} }');
ROLLBACK;

-- shard
SELECT documentdb_api.shard_collection('db','aggregation_pipeline_vector_hnsw_exact', '{"_id":"hashed"}', false);
ANALYZE;

BEGIN;
SET LOCAL enable_seqscan = off;
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_vector_hnsw_exact", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3.0, 4.9, 1.0 ], "k": 2, "path": "v", "efSearch": 1, "exact": true }  } } ], "cursor": {} }');
EXPLAIN (VERBOSE on, COSTS off) SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_vector_hnsw_exact", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3.0, 4.9, 1.0 ], "k": 2, "path": "v", "efSearch": 1, "exact": true }  } } ], "cursor": {} }');
COMMIT;

-- shard is not supported with filter yet
BEGIN;
SET LOCAL enable_seqscan to off;
SET LOCAL documentdb.enableVectorPreFilter = on;
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_vector_hnsw_exact", "pipeline": [ { "$search": { "cosmosSearch": { "vector": [ 3.0, 4.9, 1.0 ], "k": 4, "path": "v", "filter": {"meta.a":  { "$eq": "some sentence"}}, "exact": true, "efSearch": 1 }  } }, { "$project": {"searchScore": {"$round": [ {"$multiply": ["$__cosmos_meta__.score", 100000]}]} } } ], "cursor": {} }');
ROLLBACK;

SELECT drop_collection('db','aggregation_pipeline_vector_hnsw_exact');