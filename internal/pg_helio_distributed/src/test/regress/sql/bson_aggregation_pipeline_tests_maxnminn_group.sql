SET search_path to helio_core,helio_api,helio_api_catalog,pg_catalog;

SET citus.next_shard_id TO 1116000;
SET helio_api.next_collection_id TO 11160;
SET helio_api.next_collection_index_id TO 11160;

SELECT helio_api.insert_one('db','maxminn_test1',' { "gameId": "G1", "name" : "playerA", "score": 23 }');
SELECT helio_api.insert_one('db','maxminn_test1',' { "gameId": "G1", "name" : "playerB", "score": 128 }');
SELECT helio_api.insert_one('db','maxminn_test1',' { "gameId": "G1", "name" : "playerC", "score": 1 }');
SELECT helio_api.insert_one('db','maxminn_test1',' { "gameId": "G1", "name" : "playerD", "score": 32 }');
SELECT helio_api.insert_one('db','maxminn_test1',' { "gameId": "G2", "name" : "playerA", "score": 76 }');
SELECT helio_api.insert_one('db','maxminn_test1',' { "gameId": "G2", "name" : "playerB", "score": 34 }');
SELECT helio_api.insert_one('db','maxminn_test1',' { "gameId": "G2", "name" : "playerC", "score": 6 }');
SELECT helio_api.insert_one('db','maxminn_test1',' { "gameId": "G2", "name" : "playerD", "score": 87 }');

SELECT helio_api.insert_one('db','maxminn_test2',' { "gameId": "G1", "name" : "playerA", "score": null }');
SELECT helio_api.insert_one('db','maxminn_test2',' { "gameId": "G1", "name" : "playerB", "score": 128 }');
SELECT helio_api.insert_one('db','maxminn_test2',' { "gameId": "G1", "name" : "playerC", "score": {"$undefined":true} }');
SELECT helio_api.insert_one('db','maxminn_test2',' { "gameId": "G1", "name" : "playerD", "score": 32 }');
SELECT helio_api.insert_one('db','maxminn_test2',' { "gameId": "G1", "name" : "playerA", "score": null }');
SELECT helio_api.insert_one('db','maxminn_test2',' { "gameId": "G1", "name" : "playerB", "score": 179 }');
SELECT helio_api.insert_one('db','maxminn_test2',' { "gameId": "G2", "name" : "playerA", "score": null }');
SELECT helio_api.insert_one('db','maxminn_test2',' { "gameId": "G2", "name" : "playerB", "score": 34 }');
SELECT helio_api.insert_one('db','maxminn_test2',' { "gameId": "G2", "name" : "playerC", "score": 6 }');
SELECT helio_api.insert_one('db','maxminn_test2',' { "gameId": "G2", "name" : "playerD", "score": {"$undefined":true} }');
SELECT helio_api.insert_one('db','maxminn_test2',' { "gameId": "G2", "name" : "playerA", "score": 76 }');
SELECT helio_api.insert_one('db','maxminn_test2',' { "gameId": "G2", "name" : "playerB", "score": {"$undefined":true} }');

SELECT helio_api.insert_one('db','maxminn_test3',' { "gameId": "G1", "name" : "playerA", "score": 23 }');
SELECT helio_api.insert_one('db','maxminn_test3',' { "gameId": "G1", "name" : "playerB", "score": 128 }');
SELECT helio_api.insert_one('db','maxminn_test3',' { "gameId": "G1", "name" : "playerC", "score": [[1,3],5,2,4] }');
SELECT helio_api.insert_one('db','maxminn_test3',' { "gameId": "G1", "name" : "playerD", "score": 32 }');
SELECT helio_api.insert_one('db','maxminn_test3',' { "gameId": "G2", "name" : "playerA", "score": 76 }');
SELECT helio_api.insert_one('db','maxminn_test3',' { "gameId": "G2", "name" : "playerB", "score": [[7,8],3,4] }');
SELECT helio_api.insert_one('db','maxminn_test3',' { "gameId": "G2", "name" : "playerC", "score": 6 }');
SELECT helio_api.insert_one('db','maxminn_test3',' { "gameId": "G2", "name" : "playerD", "score": 87 }');

SELECT helio_api.insert_one('db','maxminn_test4',' { "_id": 1, "gameId": "G1", "name" : "playerA", "score": 23 }');
SELECT helio_api.insert_one('db','maxminn_test4',' { "_id": 2, "gameId": "G1", "name" : "playerB", "score": 128 }');
SELECT helio_api.insert_one('db','maxminn_test4',' { "_id": 3, "gameId": "G1", "name" : "playerC", "score": 1 }');
SELECT helio_api.insert_one('db','maxminn_test4',' { "_id": 4, "gameId": "G1", "name" : "playerD", "score": 32 }');
SELECT helio_api.insert_one('db','maxminn_test4',' { "_id": 5, "gameId": "G2", "name" : "playerA", "score": 76 }');
SELECT helio_api.insert_one('db','maxminn_test4',' { "_id": 6, "gameId": "G2", "name" : "playerB", "score": 34 }');
SELECT helio_api.insert_one('db','maxminn_test4',' { "_id": 7, "gameId": "G2", "name" : "playerC", "score": 6 }');
SELECT helio_api.insert_one('db','maxminn_test4',' { "_id": 8, "gameId": "G2", "name" : "playerD", "score": 87 }');

SELECT helio_api.insert_one('db','maxminn_test5',' { "gameId": "G1", "name" : "playerA", "score": 23 }');
SELECT helio_api.insert_one('db','maxminn_test5',' { "gameId": "G1", "name" : "playerB", "score": [[1,2],[3,4]] }');
SELECT helio_api.insert_one('db','maxminn_test5',' { "gameId": "G1", "name" : "playerC", "score": "apple" }');
SELECT helio_api.insert_one('db','maxminn_test5',' { "gameId": "G1", "name" : "playerD", "score": 32 }');
SELECT helio_api.insert_one('db','maxminn_test5',' { "gameId": "G2", "name" : "playerA", "score": 76 }');
SELECT helio_api.insert_one('db','maxminn_test5',' { "gameId": "G2", "name" : "playerC", "score": [[7,8],2,1] }');
SELECT helio_api.insert_one('db','maxminn_test5',' { "gameId": "G2", "name" : "playerB", "score": "port" }');
SELECT helio_api.insert_one('db','maxminn_test5',' { "gameId": "G2", "name" : "playerD", "score": 87 }');

SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "maxminn_test1", "pipeline": [ { "$group": { "_id": "$gameId", "NScore":{"$maxN": {"input": "$score", "n": 3 } } } } ] }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "maxminn_test1", "pipeline": [ { "$group": { "_id": "$gameId", "NScore":{"$minN": {"input": "$score", "n": 3 } } } } ] }');

/*n == 1*/
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "maxminn_test1", "pipeline": [ { "$group": { "_id": "$gameId", "NScore":{"$maxN": {"input": "$score", "n": 1 } } } } ] }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "maxminn_test1", "pipeline": [ { "$group": { "_id": "$gameId", "NScore":{"$minN": {"input": "$score", "n": 1 } } } } ] }');

/*n == 0*/
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "maxminn_test1", "pipeline": [ { "$group": { "_id": "$gameId", "NScore":{"$maxN": {"input": "$score", "n": 0 } } } } ] }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "maxminn_test1", "pipeline": [ { "$group": { "_id": "$gameId", "NScore":{"$minN": {"input": "$score", "n": 0 } } } } ] }');

/*n == -1*/
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "maxminn_test1", "pipeline": [ { "$group": { "_id": "$gameId", "NScore":{"$maxN": {"input": "$score", "n": -1 } } } } ] }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "maxminn_test1", "pipeline": [ { "$group": { "_id": "$gameId", "NScore":{"$minN": {"input": "$score", "n": -1 } } } } ] }');

/*n == 0.5*/
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "maxminn_test1", "pipeline": [ { "$group": { "_id": "$gameId", "NScore":{"$maxN": {"input": "$score", "n": 0.5 } } } } ] }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "maxminn_test1", "pipeline": [ { "$group": { "_id": "$gameId", "NScore":{"$minN": {"input": "$score", "n": 0.5 } } } } ] }');

/*n is a variable*/
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "maxminn_test1", "pipeline": [ { "$group": { "_id": "$gameId", "NScore":{"$maxN": {"input": "$score", "n": "$$nValue" } } } } ], "let": { "nValue": 3 } }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "maxminn_test1", "pipeline": [ { "$group": { "_id": "$gameId", "NScore":{"$minN": {"input": "$score", "n": "$$nValue" } } } } ], "let": { "nValue": 3 } }');

/*input is a document*/
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "maxminn_test1", "pipeline": [ { "$group": { "_id": "$gameId", "NScore":{"$maxN": {"input": {"$bitAnd": ["$score", 7]}, "n": 3 } } } } ] }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "maxminn_test1", "pipeline": [ { "$group": { "_id": "$gameId", "NScore":{"$minN": {"input": {"$bitAnd": ["$score", 7]}, "n": 3 } } } } ] }');

/*n is a object*/
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "maxminn_test1", "pipeline": [ { "$group": { "_id": "$gameId", "NScore":{"$maxN": {"input": {"key" :{"value": {"score": "$score" }}}, "n": 3 } } } } ] }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "maxminn_test1", "pipeline": [ { "$group": { "_id": "$gameId", "NScore":{"$minN": {"input": {"key" :{"value": {"score": "$score" }}}, "n": 3 } } } } ] }');

/*input is a nested array*/
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "maxminn_test1", "pipeline": [ { "$group": { "_id": "$gameId", "NScore":{"$maxN": {"input": [[2,3], "$score"], "n": 3 } } } } ] }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "maxminn_test1", "pipeline": [ { "$group": { "_id": "$gameId", "NScore":{"$minN": {"input": [[2,3], "$score"], "n": 3 } } } } ] }');

/*n too large */
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "maxminn_test1", "pipeline": [ { "$group": { "_id": "$gameId", "NScore":{"$maxN": {"input": "$score", "n": 100 } } } } ] }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "maxminn_test1", "pipeline": [ { "$group": { "_id": "$gameId", "NScore":{"$minN": {"input": "$score", "n": 100 } } } } ] }');

/*n missing */
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "maxminn_test1", "pipeline": [ { "$group": { "_id": "$gameId", "NScore":{"$maxN": {"input": "$score"} } } } ] }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "maxminn_test1", "pipeline": [ { "$group": { "_id": "$gameId", "NScore":{"$minN": {"input": "$score"} } } } ] }');

/*input missing */
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "maxminn_test1", "pipeline": [ { "$group": { "_id": "$gameId", "NScore":{"$maxN": {"n": 3 } } } } ] }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "maxminn_test1", "pipeline": [ { "$group": { "_id": "$gameId", "NScore":{"$minN": {"n": 3 } } } } ] }');

/*test null/$undefined */
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "maxminn_test2", "pipeline": [ { "$group": { "_id": "$gameId", "NScore":{"$maxN": {"input": "$score", "n": 3 } } } } ] }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "maxminn_test2", "pipeline": [ { "$group": { "_id": "$gameId", "NScore":{"$minN": {"input": "$score", "n": 3 } } } } ] }');

SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "maxminn_test2", "pipeline": [ { "$group": { "_id": "$gameId", "NScore":{"$maxN": {"input": "$score", "n": 100 } } } } ] }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "maxminn_test2", "pipeline": [ { "$group": { "_id": "$gameId", "NScore":{"$minN": {"input": "$score", "n": 100 } } } } ] }');

/* nested array */
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "maxminn_test3", "pipeline": [ { "$group": { "_id": "$gameId", "NScore":{"$maxN": {"input": "$score", "n": 3 } } } } ] }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "maxminn_test3", "pipeline": [ { "$group": { "_id": "$gameId", "NScore":{"$minN": {"input": "$score", "n": 3 } } } } ] }');

SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "maxminn_test3", "pipeline": [ { "$group": { "_id": "$gameId", "NScore":{"$maxN": {"input": "$score", "n": 100 } } } } ] }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "maxminn_test3", "pipeline": [ { "$group": { "_id": "$gameId", "NScore":{"$minN": {"input": "$score", "n": 100 } } } } ] }');

/* sharded tests */
EXPLAIN (COSTS OFF) SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "maxminn_test4", "pipeline": [ { "$group": { "_id": "$gameId", "NScore":{"$maxN": {"input": "$score", "n": 3 } } } } ] }');
EXPLAIN (COSTS OFF) SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "maxminn_test4", "pipeline": [ { "$group": { "_id": "$gameId", "NScore":{"$minN": {"input": "$score", "n": 3 } } } } ] }');

SELECT helio_api.shard_collection('db', 'maxminn_test4', '{ "_id": "hashed" }', false);

EXPLAIN (COSTS OFF) SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "maxminn_test4", "pipeline": [ { "$group": { "_id": "$gameId", "NScore":{"$maxN": {"input": "$score", "n": 3 } } } } ] }');
EXPLAIN (COSTS OFF) SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "maxminn_test4", "pipeline": [ { "$group": { "_id": "$gameId", "NScore":{"$minN": {"input": "$score", "n": 3 } } } } ] }');


SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "maxminn_test4", "pipeline": [ { "$group": { "_id": "$gameId", "NScore":{"$maxN": {"input": "$score", "n": 3 } } } } ] }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "maxminn_test4", "pipeline": [ { "$group": { "_id": "$gameId", "NScore":{"$minN": {"input": "$score", "n": 3 } } } } ] }');

SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "maxminn_test4", "pipeline": [ { "$group": { "_id": "$gameId", "NScore":{"$maxN": {"input": "$score", "n": 100 } } } } ] }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "maxminn_test4", "pipeline": [ { "$group": { "_id": "$gameId", "NScore":{"$minN": {"input": "$score", "n": 100 } } } } ] }');

/* test string and nested array */
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "maxminn_test5", "pipeline": [ { "$group": { "_id": "$gameId", "NScore":{"$maxN": {"input": "$score", "n": 3 } } } } ] }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "maxminn_test5", "pipeline": [ { "$group": { "_id": "$gameId", "NScore":{"$minN": {"input": "$score", "n": 3 } } } } ] }');

SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "maxminn_test5", "pipeline": [ { "$group": { "_id": "$gameId", "NScore":{"$maxN": {"input": "$score", "n": 100 } } } } ] }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "maxminn_test5", "pipeline": [ { "$group": { "_id": "$gameId", "NScore":{"$minN": {"input": "$score", "n": 100 } } } } ] }');