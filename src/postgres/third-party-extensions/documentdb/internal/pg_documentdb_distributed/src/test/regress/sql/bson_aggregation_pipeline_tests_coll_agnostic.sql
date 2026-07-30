SET search_path TO documentdb_core,documentdb_api,documentdb_api_catalog,documentdb_api_internal;

SET citus.next_shard_id TO 314000;
SET documentdb.next_collection_id TO 3140;
SET documentdb.next_collection_index_id TO 3140;


-- collection agnostic with no pipeline should work and return 0 rows.
SELECT document from bson_aggregation_pipeline('agnosticTests', '{ "aggregate" : 1.0, "pipeline" : [  ], "cursor" : {  }, "txnNumber" : 100, "lsid" : { "id" : { "$binary" : { "base64": "AAAAAA==", "subType" : "04" } } }, "$db" : "agnosticTests" }');


-- $document tests
SELECT document FROM bson_aggregation_pipeline('agnosticTests', '{ "aggregate": 1, "pipeline": [ { "$documents": [] } ], "cursor": {}}');
SELECT document FROM bson_aggregation_pipeline('agnosticTests', '{ "aggregate": 1, "pipeline": [ { "$documents": [ { "field1": 10 }, { "field2": 20 } ] }], "cursor": {} }');

SELECT document FROM bson_aggregation_pipeline('agnosticTests', '{ "aggregate": 1, "pipeline": [ { "$documents": [ { "field1": { "$isArray": "field1" } }, { "field2": 20 } ] }], "cursor": {}}');
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM bson_aggregation_pipeline('agnosticTests', '{ "aggregate": 1, "pipeline": [ { "$documents": [ { "field1": 10 }, { "field2": 20 } ] }], "cursor": {} }');

EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM bson_aggregation_pipeline('agnosticTests', '{ "aggregate": 1, "pipeline": [ { "$documents": [ { "field1": 10 }, { "field2": 20 } ] }, { "$addFields": { "field2": 30 } } ], "cursor": {} }');

-- error cases
SELECT document FROM bson_aggregation_pipeline('agnosticTests', '{ "aggregate": 1, "pipeline": [ { "$documents": null }], "cursor": {} }');
SELECT document FROM bson_aggregation_pipeline('agnosticTests', '{ "aggregate": 1, "pipeline": [ { "$documents": "String Value" }], "cursor": {} }');
SELECT document FROM bson_aggregation_pipeline('agnosticTests', '{ "aggregate": 1, "pipeline": [ { "$documents": {} }], "cursor": {}}');


-- bugfix scenario:
SELECT document FROM bson_aggregation_pipeline('agnosticTests', '{ "aggregate": 1, "pipeline": [{ "$documents": [ { "userId": "User1", "sessionId": "S1", "points": 100 } ] }, { "$group": { "_id": "$sessionId", "firstFivePoints": { "$firstN": { "input": "$points", "n": 5 } } } } ] }');
