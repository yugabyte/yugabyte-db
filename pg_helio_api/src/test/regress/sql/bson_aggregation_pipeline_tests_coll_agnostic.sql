SET search_path TO helio_api,helio_core;

SET helio_api.next_collection_id TO 4500;
SET helio_api.next_collection_index_id TO 4500;

-- this is further tested in isolation tests
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": 1, "pipeline": [ { "$currentOp": 1 }] }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": 1, "pipeline": [ { "$match": { } }, { "$currentOp": {} }] }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": 1, "pipeline": [ { "$currentOp": {} }] }');

SELECT document FROM bson_aggregation_pipeline('admin', '{ "aggregate": "coll", "pipeline": [ { "$currentOp": {} }] }');
SELECT document FROM bson_aggregation_pipeline('admin', '{ "aggregate": 1, "pipeline": [ { "$currentOp": {} }] }');

EXPLAIN (VERBOSE ON, COSTS OFF) SELECT document FROM bson_aggregation_pipeline('admin', '{ "aggregate": 1, "pipeline": [ { "$currentOp": {} }] }');


-- collection agnostic with no pipeline should work and return 0 rows.
SELECT document from bson_aggregation_pipeline('db', '{ "aggregate" : 1.0, "pipeline" : [  ], "cursor" : {  }, "txnNumber" : 0, "lsid" : { "id" : { "$binary" : { "base64": "H+W3J//vSn6obaefeJ6j/g==", "subType" : "04" } } }, "$db" : "admin" }');