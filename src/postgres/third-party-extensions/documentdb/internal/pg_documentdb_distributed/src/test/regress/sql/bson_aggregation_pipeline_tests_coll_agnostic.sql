SET search_path TO documentdb_core,documentdb_api,documentdb_api_catalog,documentdb_api_internal;

SET citus.next_shard_id TO 314000;
SET documentdb.next_collection_id TO 3140;
SET documentdb.next_collection_index_id TO 3140;


-- collection agnostic with no pipeline should work and return 0 rows.
SELECT document from bson_aggregation_pipeline('db', '{ "aggregate" : 1.0, "pipeline" : [  ], "cursor" : {  }, "txnNumber" : 0, "lsid" : { "id" : { "$binary" : { "base64": "H+W3J//vSn6obaefeJ6j/g==", "subType" : "04" } } }, "$db" : "admin" }');


-- $document tests
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": 1, "pipeline": [ { "$documents": [] } ], "cursor": {}}');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": 1, "pipeline": [ { "$documents": [ { "a": 1 }, { "b": 2 } ] }], "cursor": {} }');

SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": 1, "pipeline": [ { "$documents": [ { "a": { "$isArray": "a" } }, { "b": 2 } ] }], "cursor": {}}');
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": 1, "pipeline": [ { "$documents": [ { "a": 1 }, { "b": 2 } ] }], "cursor": {} }');

EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": 1, "pipeline": [ { "$documents": [ { "a": 1 }, { "b": 2 } ] }, { "$addFields": { "b": 1 } } ], "cursor": {} }');

-- error cases
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": 1, "pipeline": [ { "$documents": null }], "cursor": {} }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": 1, "pipeline": [ { "$documents": "String Value" }], "cursor": {} }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": 1, "pipeline": [ { "$documents": {} }], "cursor": {}}');


-- bugfix scenario:
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": 1, "pipeline": [{ "$documents": [ { "playerId": "PlayerA", "gameId": "G1", "score": 1 } ] }, { "$group": { "_id": "$gameId", "firstFiveScores": { "$firstN": { "input": "$score", "n": 5 } } } } ] }');
