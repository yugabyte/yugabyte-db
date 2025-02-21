SET search_path TO documentdb_api,documentdb_api_catalog,documentdb_core;

SET documentdb.next_collection_id TO 4500;
SET documentdb.next_collection_index_id TO 4500;
set application_name to 'coll_agnostic_tests';
set documentdb_api.current_op_application_name to 'coll_agnostic_tests';

-- this is further tested in isolation tests
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": 1, "pipeline": [ { "$currentOp": 1 }] }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": 1, "pipeline": [ { "$match": { } }, { "$currentOp": {} }] }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": 1, "pipeline": [ { "$currentOp": {} }] }');

SELECT document FROM bson_aggregation_pipeline('admin', '{ "aggregate": "coll", "pipeline": [ { "$currentOp": {} }] }');
SELECT document FROM bson_aggregation_pipeline('admin', '{ "aggregate": 1, "pipeline": [ { "$currentOp": {} }, { "$project": { "opid": 0, "op_prefix": 0, "currentOpTime": 0, "secs_running": 0 }}] }');

EXPLAIN (VERBOSE ON, COSTS OFF) SELECT document FROM bson_aggregation_pipeline('admin', '{ "aggregate": 1, "pipeline": [ { "$currentOp": {} }] }');

-- does the same as aggregation.
SELECT current_op_command('{ "op_prefix": { "$lt": 2 }}');

-- collection agnostic with no pipeline should work and return 0 rows.
SELECT document from bson_aggregation_pipeline('db', '{ "aggregate" : 1.0, "pipeline" : [  ], "cursor" : {  }, "txnNumber" : 0, "lsid" : { "id" : { "$binary" : { "base64": "H+W3J//vSn6obaefeJ6j/g==", "subType" : "04" } } }, "$db" : "admin" }');