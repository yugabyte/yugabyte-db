SET search_path TO documentdb_core,documentdb_api,documentdb_api_catalog,documentdb_api_internal;
SET citus.next_shard_id TO 50100;
SET documentdb.next_collection_id TO 5010;
SET documentdb.next_collection_index_id TO 5010;

SELECT documentdb_api.insert_one('db','rum_index_scan',' { "_id" : 1, "glbl_id" : "ABC", "price" : 12, "quantity" : 2 }', NULL);
SELECT documentdb_api.insert_one('db','rum_index_scan','{ "_id" : 2, "glbl_id" : "ABC", "item" : "pecans", "price" : 20, "quantity" : 1 }', NULL);
SELECT documentdb_api.insert_one('db','rum_index_scan',' { "_id" : 3, "glbl_id" : "ABC", "item" : "bread", "price" : 10, "quantity" : 5 }', NULL);
SELECT documentdb_api.insert_one('db','rum_index_scan',' { "_id" : 4, "glbl_id" : "ABC", "item" : ["almonds", "bread", "pecans"], "price" : 10, "quantity" : 5 }', NULL);

do $$
begin
for r in 1..500 loop
PERFORM documentdb_api.insert_one('db','rum_index_scan',' { "glbl_id" : "ABC", "item" : ["almonds", "bread", "pecans"], "price" : 10, "quantity" : 5 }', NULL);
end loop;
end;
$$;

SELECT documentdb_distributed_test_helpers.drop_primary_key('db','rum_index_scan');

EXPLAIN(costs off) SELECT count(*) FROM documentdb_api.collection('db', 'rum_index_scan') WHERE document OPERATOR(documentdb_api_catalog.@@) '{"glbl_id": "ABC" }'::bson LIMIT 10;

SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "rum_index_scan", "indexes": [{"key": {"glbl_id": 1}, "name": "glbl_1"}]}', true);

-- Even if there is an index glbl_1, performs BitmapHeapScan instead of Index Scan 
BEGIN;
set local enable_seqscan TO off;
SET LOCAL documentdb.ForceUseIndexIfAvailable to OFF;
EXPLAIN (COSTS OFF) SELECT count(*) FROM documentdb_api.collection('db', 'rum_index_scan') WHERE document OPERATOR(documentdb_api_catalog.@@) '{"glbl_id": "ABC" }'::bson LIMIT 10;
END;

-- Tets for paralell BitmapHeapScan. Needs all the 5 config and at 500 docs in the collection to enable parallel bitmap scan.
BEGIN;
set local parallel_tuple_cost TO 0.00001;
set local parallel_setup_cost TO 0;
set local min_parallel_table_scan_size TO 0;
set local min_parallel_index_scan_size TO 0;
SET local enable_seqscan to OFF;
SET LOCAL documentdb.ForceUseIndexIfAvailable to OFF;
EXPLAIN (COSTS OFF) WITH t1 as (SELECT document FROM documentdb_api.collection('db', 'rum_index_scan') WHERE document OPERATOR(documentdb_api_catalog.@@) '{"glbl_id": "GLBL12345" , "$and": [{"src_rcv_ts" : { "$lte":2000000}}]}'::bson  ) SELECT bson_repath_and_build('rxCount'::text, BSONAVERAGE(document -> 'month')) from t1 group by bson_expression_get(document, '{ "": "$drug_name" }');
ROLLBACK;

-- IndexScan is overritten by BitmapHeapScan when documentdb_api.forceRumIndexScantoBitmapHeapScan is toggled to off and then to on
BEGIN;
set local enable_seqscan TO off;
set local documentdb.forceRumIndexScantoBitmapHeapScan TO OFF;
set local documentdb.forceRumIndexScantoBitmapHeapScan TO true;
SET LOCAL documentdb.ForceUseIndexIfAvailable to OFF;
EXPLAIN (COSTS OFF) SELECT count(*) FROM documentdb_api.collection('db', 'rum_index_scan') WHERE document OPERATOR(documentdb_api_catalog.@@) '{"glbl_id": "ABC" }'::bson LIMIT 10;
END;

-- IndexScan is preferred when is turned off via setting documentdb_api.forceRumIndexScantoBitmapHeapScan TO off
BEGIN;
set local enable_seqscan TO off;
set local documentdb.forceRumIndexScantoBitmapHeapScan TO off;
set local enable_bitmapscan TO OFF;
SET LOCAL documentdb.ForceUseIndexIfAvailable to OFF;
EXPLAIN (COSTS OFF) SELECT count(*) FROM documentdb_api.collection('db', 'rum_index_scan') WHERE document OPERATOR(documentdb_api_catalog.@@) '{"glbl_id": "ABC" }'::bson LIMIT 10;
END;

-- IndexScan is preferred when is turned off via setting documentdb_api.forceRumIndexScantoBitmapHeapScan TO "off"
BEGIN;
set local enable_seqscan TO off;
set local documentdb.forceRumIndexScantoBitmapHeapScan TO off;
set local enable_bitmapscan TO OFF;
SET LOCAL documentdb.ForceUseIndexIfAvailable to OFF;
EXPLAIN (COSTS OFF) SELECT count(*) FROM documentdb_api.collection('db', 'rum_index_scan') WHERE document OPERATOR(documentdb_api_catalog.@@) '{"glbl_id": "ABC" }'::bson LIMIT 10;
END;

-- IndexScan is preferred when is turned off via setting documentdb_api.forceRumIndexScantoBitmapHeapScan TO false
BEGIN;
set local enable_seqscan TO off;
set local documentdb.forceRumIndexScantoBitmapHeapScan TO false;
set local enable_bitmapscan TO OFF;
SET LOCAL documentdb.ForceUseIndexIfAvailable to OFF;
EXPLAIN (COSTS OFF) SELECT count(*) FROM documentdb_api.collection('db', 'rum_index_scan') WHERE document OPERATOR(documentdb_api_catalog.@@) '{"glbl_id": "ABC" }'::bson LIMIT 10;
END;

-- IndexScan is preferred when is turned off via setting documentdb_api.forceRumIndexScantoBitmapHeapScan TO "false"
BEGIN;
set local enable_seqscan TO off;
set local documentdb.forceRumIndexScantoBitmapHeapScan TO "false";
set local enable_bitmapscan TO OFF;
SET LOCAL documentdb.ForceUseIndexIfAvailable to OFF;
EXPLAIN (COSTS OFF) SELECT count(*) FROM documentdb_api.collection('db', 'rum_index_scan') WHERE document OPERATOR(documentdb_api_catalog.@@) '{"glbl_id": "ABC" }'::bson LIMIT 10;
END;
