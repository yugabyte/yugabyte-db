SET search_path TO documentdb_api,documentdb_core,documentdb_api_catalog;

SET documentdb.next_collection_id TO 300;
SET documentdb.next_collection_index_id TO 300;

SELECT COUNT(documentdb_api.insert_one('iosdb', 'iosc', bson_build_document('_id'::text, i, 'a'::text, i))) FROM generate_series(1, 20) i;

VACUUM (ANALYZE ON, FREEZE ON) documentdb_data.documents_301;

set documentdb.enableIndexOnlyScan to on;
set seq_page_cost to 1000;

SELECT documentdb_test_helpers.run_explain_and_trim($$ EXPLAIN (ANALYZE ON, COSTS OFF, BUFFERS OFF, VERBOSE ON, TIMING OFF, SUMMARY OFF) SELECT document FROM bson_aggregation_pipeline('iosdb', '{ "aggregate" : "iosc", "pipeline" : [{ "$match" : {"_id": {"$gt": 5 }} }, { "$count": "count" }]}') $$);
SELECT documentdb_test_helpers.run_explain_and_trim($$ EXPLAIN (ANALYZE ON, COSTS OFF, BUFFERS OFF, VERBOSE ON, TIMING OFF, SUMMARY OFF) SELECT document FROM bson_aggregation_pipeline('iosdb', '{ "aggregate" : "iosc", "pipeline" : [{ "$match" : {"_id": {"$gt": 5, "$lt": 8 }} }, { "$count": "count" }]}') $$);
SELECT documentdb_test_helpers.run_explain_and_trim($$ EXPLAIN (ANALYZE ON, COSTS OFF, BUFFERS OFF, VERBOSE ON, TIMING OFF, SUMMARY OFF) SELECT document FROM bson_aggregation_pipeline('iosdb', '{ "aggregate" : "iosc", "pipeline" : [{ "$match" : {"_id": 5 } }, { "$count": "count" }]}') $$);

-- does not work with other filters.
set documentdb.forceIndexOnlyScanIfAvailable to on;
SELECT documentdb_test_helpers.run_explain_and_trim($$ EXPLAIN (ANALYZE ON, COSTS OFF, BUFFERS OFF, VERBOSE ON, TIMING OFF, SUMMARY OFF) SELECT document FROM bson_aggregation_pipeline('iosdb', '{ "aggregate" : "iosc", "pipeline" : [{ "$match" : {"_id": {"$gt": 5, "$lt": 8 }, "a": { "$lt": { "$maxKey": 1 }}} }, { "$count": "count" }]}') $$);
SELECT documentdb_test_helpers.run_explain_and_trim($$ EXPLAIN (ANALYZE ON, COSTS OFF, BUFFERS OFF, VERBOSE ON, TIMING OFF, SUMMARY OFF) SELECT document FROM bson_aggregation_pipeline('iosdb', '{ "aggregate" : "iosc", "pipeline" : [{ "$match" : {"_id": {"$gt": 5, "$lt": 8 }, "_id": { "$size": 5 }} }, { "$count": "count" }]}') $$);

-- parallel index only scan is possible now.
SELECT CASE WHEN SUBSTRING(VERSION(), 11, 3)::int >= 16 THEN set_config('debug_parallel_query', 'on', false) ELSE set_config('force_parallel_mode', 'on', false) END;
SELECT documentdb_test_helpers.run_explain_and_trim($$ EXPLAIN (ANALYZE ON, COSTS OFF, BUFFERS OFF, VERBOSE ON, TIMING OFF, SUMMARY OFF) SELECT document FROM bson_aggregation_pipeline('iosdb', '{ "aggregate" : "iosc", "pipeline" : [{ "$match" : {"_id": {"$gt": 5 }} }, { "$count": "count" }]}') $$);
SELECT documentdb_test_helpers.run_explain_and_trim($$ EXPLAIN (ANALYZE ON, COSTS OFF, BUFFERS OFF, VERBOSE ON, TIMING OFF, SUMMARY OFF) SELECT document FROM bson_aggregation_pipeline('iosdb', '{ "aggregate" : "iosc", "pipeline" : [{ "$match" : {"_id": {"$gt": 5, "$lt": 8 }} }, { "$count": "count" }]}') $$);
SELECT documentdb_test_helpers.run_explain_and_trim($$ EXPLAIN (ANALYZE ON, COSTS OFF, BUFFERS OFF, VERBOSE ON, TIMING OFF, SUMMARY OFF) SELECT document FROM bson_aggregation_pipeline('iosdb', '{ "aggregate" : "iosc", "pipeline" : [{ "$match" : {"_id": 5 } }, { "$count": "count" }]}') $$);
