SET search_path to documentdb_core,documentdb_api,documentdb_api_catalog,pg_catalog;

SET citus.next_shard_id TO 1116000;
SET documentdb.next_collection_id TO 11160;
SET documentdb.next_collection_index_id TO 11160;

SELECT documentdb_api.insert_one('db','bson_maxminn_group_test1',' { "teamId": "T1", "person" : "userA", "points": 10 }');
SELECT documentdb_api.insert_one('db','bson_maxminn_group_test1',' { "teamId": "T1", "person" : "userB", "points": 55 }');
SELECT documentdb_api.insert_one('db','bson_maxminn_group_test1',' { "teamId": "T1", "person" : "userC", "points": 3 }');
SELECT documentdb_api.insert_one('db','bson_maxminn_group_test1',' { "teamId": "T1", "person" : "userD", "points": 20 }');
SELECT documentdb_api.insert_one('db','bson_maxminn_group_test1',' { "teamId": "T2", "person" : "userA", "points": 40 }');
SELECT documentdb_api.insert_one('db','bson_maxminn_group_test1',' { "teamId": "T2", "person" : "userB", "points": 18 }');
SELECT documentdb_api.insert_one('db','bson_maxminn_group_test1',' { "teamId": "T2", "person" : "userC", "points": 7 }');
SELECT documentdb_api.insert_one('db','bson_maxminn_group_test1',' { "teamId": "T2", "person" : "userD", "points": 60 }');

SELECT documentdb_api.insert_one('db','bson_maxminn_group_test2',' { "teamId": "T1", "person" : "userA", "points": null }');
SELECT documentdb_api.insert_one('db','bson_maxminn_group_test2',' { "teamId": "T1", "person" : "userB", "points": 55 }');
SELECT documentdb_api.insert_one('db','bson_maxminn_group_test2',' { "teamId": "T1", "person" : "userC", "points": {"$undefined":true} }');
SELECT documentdb_api.insert_one('db','bson_maxminn_group_test2',' { "teamId": "T1", "person" : "userD", "points": 20 }');
SELECT documentdb_api.insert_one('db','bson_maxminn_group_test2',' { "teamId": "T1", "person" : "userA", "points": null }');
SELECT documentdb_api.insert_one('db','bson_maxminn_group_test2',' { "teamId": "T1", "person" : "userB", "points": 99 }');
SELECT documentdb_api.insert_one('db','bson_maxminn_group_test2',' { "teamId": "T2", "person" : "userA", "points": null }');
SELECT documentdb_api.insert_one('db','bson_maxminn_group_test2',' { "teamId": "T2", "person" : "userB", "points": 18 }');
SELECT documentdb_api.insert_one('db','bson_maxminn_group_test2',' { "teamId": "T2", "person" : "userC", "points": 7 }');
SELECT documentdb_api.insert_one('db','bson_maxminn_group_test2',' { "teamId": "T2", "person" : "userD", "points": {"$undefined":true} }');
SELECT documentdb_api.insert_one('db','bson_maxminn_group_test2',' { "teamId": "T2", "person" : "userA", "points": 40 }');
SELECT documentdb_api.insert_one('db','bson_maxminn_group_test2',' { "teamId": "T2", "person" : "userB", "points": {"$undefined":true} }');

SELECT documentdb_api.insert_one('db','bson_maxminn_group_test3',' { "teamId": "T1", "person" : "userA", "points": 10 }');
SELECT documentdb_api.insert_one('db','bson_maxminn_group_test3',' { "teamId": "T1", "person" : "userB", "points": 55 }');
SELECT documentdb_api.insert_one('db','bson_maxminn_group_test3',' { "teamId": "T1", "person" : "userC", "points": [[2,4],8,1,5] }');
SELECT documentdb_api.insert_one('db','bson_maxminn_group_test3',' { "teamId": "T1", "person" : "userD", "points": 20 }');
SELECT documentdb_api.insert_one('db','bson_maxminn_group_test3',' { "teamId": "T2", "person" : "userA", "points": 40 }');
SELECT documentdb_api.insert_one('db','bson_maxminn_group_test3',' { "teamId": "T2", "person" : "userB", "points": [[3,6],2,7] }');
SELECT documentdb_api.insert_one('db','bson_maxminn_group_test3',' { "teamId": "T2", "person" : "userC", "points": 7 }');
SELECT documentdb_api.insert_one('db','bson_maxminn_group_test3',' { "teamId": "T2", "person" : "userD", "points": 60 }');

SELECT documentdb_api.insert_one('db','bson_maxminn_group_test4',' { "_id": 1, "teamId": "T1", "person" : "userA", "points": 10 }');
SELECT documentdb_api.insert_one('db','bson_maxminn_group_test4',' { "_id": 2, "teamId": "T1", "person" : "userB", "points": 55 }');
SELECT documentdb_api.insert_one('db','bson_maxminn_group_test4',' { "_id": 3, "teamId": "T1", "person" : "userC", "points": 3 }');
SELECT documentdb_api.insert_one('db','bson_maxminn_group_test4',' { "_id": 4, "teamId": "T1", "person" : "userD", "points": 20 }');
SELECT documentdb_api.insert_one('db','bson_maxminn_group_test4',' { "_id": 5, "teamId": "T2", "person" : "userA", "points": 40 }');
SELECT documentdb_api.insert_one('db','bson_maxminn_group_test4',' { "_id": 6, "teamId": "T2", "person" : "userB", "points": 18 }');
SELECT documentdb_api.insert_one('db','bson_maxminn_group_test4',' { "_id": 7, "teamId": "T2", "person" : "userC", "points": 7 }');
SELECT documentdb_api.insert_one('db','bson_maxminn_group_test4',' { "_id": 8, "teamId": "T2", "person" : "userD", "points": 60 }');

SELECT documentdb_api.insert_one('db','bson_maxminn_group_test5',' { "teamId": "T1", "person" : "userA", "points": 10 }');
SELECT documentdb_api.insert_one('db','bson_maxminn_group_test5',' { "teamId": "T1", "person" : "userB", "points": [[1,2],[3,4]] }');
SELECT documentdb_api.insert_one('db','bson_maxminn_group_test5',' { "teamId": "T1", "person" : "userC", "points": "apple" }');
SELECT documentdb_api.insert_one('db','bson_maxminn_group_test5',' { "teamId": "T1", "person" : "userD", "points": 20 }');
SELECT documentdb_api.insert_one('db','bson_maxminn_group_test5',' { "teamId": "T2", "person" : "userA", "points": 40 }');
SELECT documentdb_api.insert_one('db','bson_maxminn_group_test5',' { "teamId": "T2", "person" : "userC", "points": [[3,4],8,1] }');
SELECT documentdb_api.insert_one('db','bson_maxminn_group_test5',' { "teamId": "T2", "person" : "userB", "points": "port" }');
SELECT documentdb_api.insert_one('db','bson_maxminn_group_test5',' { "teamId": "T2", "person" : "userD", "points": 60 }');

SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "bson_maxminn_group_test1", "pipeline": [ { "$group": { "_id": "$teamId", "NScore":{"$maxN": {"input": "$points", "n": 3 } } } } ] }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "bson_maxminn_group_test1", "pipeline": [ { "$group": { "_id": "$teamId", "NScore":{"$minN": {"input": "$points", "n": 3 } } } } ] }');

/*n == 1*/
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "bson_maxminn_group_test1", "pipeline": [ { "$group": { "_id": "$teamId", "NScore":{"$maxN": {"input": "$points", "n": 1 } } } } ] }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "bson_maxminn_group_test1", "pipeline": [ { "$group": { "_id": "$teamId", "NScore":{"$minN": {"input": "$points", "n": 1 } } } } ] }');

/*n == 0*/
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "bson_maxminn_group_test1", "pipeline": [ { "$group": { "_id": "$teamId", "NScore":{"$maxN": {"input": "$points", "n": 0 } } } } ] }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "bson_maxminn_group_test1", "pipeline": [ { "$group": { "_id": "$teamId", "NScore":{"$minN": {"input": "$points", "n": 0 } } } } ] }');

/*n == -1*/
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "bson_maxminn_group_test1", "pipeline": [ { "$group": { "_id": "$teamId", "NScore":{"$maxN": {"input": "$points", "n": -1 } } } } ] }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "bson_maxminn_group_test1", "pipeline": [ { "$group": { "_id": "$teamId", "NScore":{"$minN": {"input": "$points", "n": -1 } } } } ] }');

/*n == 0.5*/
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "bson_maxminn_group_test1", "pipeline": [ { "$group": { "_id": "$teamId", "NScore":{"$maxN": {"input": "$points", "n": 0.5 } } } } ] }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "bson_maxminn_group_test1", "pipeline": [ { "$group": { "_id": "$teamId", "NScore":{"$minN": {"input": "$points", "n": 0.5 } } } } ] }');

/*n is a variable*/
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "bson_maxminn_group_test1", "pipeline": [ { "$group": { "_id": "$teamId", "NScore":{"$maxN": {"input": "$points", "n": "$$nValue" } } } } ], "let": { "nValue": 3 } }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "bson_maxminn_group_test1", "pipeline": [ { "$group": { "_id": "$teamId", "NScore":{"$minN": {"input": "$points", "n": "$$nValue" } } } } ], "let": { "nValue": 3 } }');

/*input is a document*/
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "bson_maxminn_group_test1", "pipeline": [ { "$group": { "_id": "$teamId", "NScore":{"$maxN": {"input": {"$bitAnd": ["$points", 7]}, "n": 3 } } } } ] }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "bson_maxminn_group_test1", "pipeline": [ { "$group": { "_id": "$teamId", "NScore":{"$minN": {"input": {"$bitAnd": ["$points", 7]}, "n": 3 } } } } ] }');

/*n is a object*/
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "bson_maxminn_group_test1", "pipeline": [ { "$group": { "_id": "$teamId", "NScore":{"$maxN": {"input": {"key" :{"value": {"points": "$points" }}}, "n": 3 } } } } ] }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "bson_maxminn_group_test1", "pipeline": [ { "$group": { "_id": "$teamId", "NScore":{"$minN": {"input": {"key" :{"value": {"points": "$points" }}}, "n": 3 } } } } ] }');

/*input is a nested array*/
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "bson_maxminn_group_test1", "pipeline": [ { "$group": { "_id": "$teamId", "NScore":{"$maxN": {"input": [[2,3], "$points"], "n": 3 } } } } ] }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "bson_maxminn_group_test1", "pipeline": [ { "$group": { "_id": "$teamId", "NScore":{"$minN": {"input": [[2,3], "$points"], "n": 3 } } } } ] }');

/*n too large */
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "bson_maxminn_group_test1", "pipeline": [ { "$group": { "_id": "$teamId", "NScore":{"$maxN": {"input": "$points", "n": 100 } } } } ] }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "bson_maxminn_group_test1", "pipeline": [ { "$group": { "_id": "$teamId", "NScore":{"$minN": {"input": "$points", "n": 100 } } } } ] }');

/*n missing */
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "bson_maxminn_group_test1", "pipeline": [ { "$group": { "_id": "$teamId", "NScore":{"$maxN": {"input": "$points"} } } } ] }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "bson_maxminn_group_test1", "pipeline": [ { "$group": { "_id": "$teamId", "NScore":{"$minN": {"input": "$points"} } } } ] }');

/*input missing */
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "bson_maxminn_group_test1", "pipeline": [ { "$group": { "_id": "$teamId", "NScore":{"$maxN": {"n": 3 } } } } ] }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "bson_maxminn_group_test1", "pipeline": [ { "$group": { "_id": "$teamId", "NScore":{"$minN": {"n": 3 } } } } ] }');

/*test null/$undefined */
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "bson_maxminn_group_test2", "pipeline": [ { "$group": { "_id": "$teamId", "NScore":{"$maxN": {"input": "$points", "n": 3 } } } } ] }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "bson_maxminn_group_test2", "pipeline": [ { "$group": { "_id": "$teamId", "NScore":{"$minN": {"input": "$points", "n": 3 } } } } ] }');

SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "bson_maxminn_group_test2", "pipeline": [ { "$group": { "_id": "$teamId", "NScore":{"$maxN": {"input": "$points", "n": 100 } } } } ] }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "bson_maxminn_group_test2", "pipeline": [ { "$group": { "_id": "$teamId", "NScore":{"$minN": {"input": "$points", "n": 100 } } } } ] }');

/* nested array */
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "bson_maxminn_group_test3", "pipeline": [ { "$group": { "_id": "$teamId", "NScore":{"$maxN": {"input": "$points", "n": 3 } } } } ] }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "bson_maxminn_group_test3", "pipeline": [ { "$group": { "_id": "$teamId", "NScore":{"$minN": {"input": "$points", "n": 3 } } } } ] }');

SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "bson_maxminn_group_test3", "pipeline": [ { "$group": { "_id": "$teamId", "NScore":{"$maxN": {"input": "$points", "n": 100 } } } } ] }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "bson_maxminn_group_test3", "pipeline": [ { "$group": { "_id": "$teamId", "NScore":{"$minN": {"input": "$points", "n": 100 } } } } ] }');

/* sharded tests */
EXPLAIN (COSTS OFF) SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "bson_maxminn_group_test4", "pipeline": [ { "$group": { "_id": "$teamId", "NScore":{"$maxN": {"input": "$points", "n": 3 } } } } ] }');
EXPLAIN (COSTS OFF) SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "bson_maxminn_group_test4", "pipeline": [ { "$group": { "_id": "$teamId", "NScore":{"$minN": {"input": "$points", "n": 3 } } } } ] }');

SELECT documentdb_api.shard_collection('db', 'bson_maxminn_group_test4', '{ "_id": "hashed" }', false);

EXPLAIN (COSTS OFF) SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "bson_maxminn_group_test4", "pipeline": [ { "$group": { "_id": "$teamId", "NScore":{"$maxN": {"input": "$points", "n": 3 } } } } ] }');
EXPLAIN (COSTS OFF) SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "bson_maxminn_group_test4", "pipeline": [ { "$group": { "_id": "$teamId", "NScore":{"$minN": {"input": "$points", "n": 3 } } } } ] }');


SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "bson_maxminn_group_test4", "pipeline": [ { "$group": { "_id": "$teamId", "NScore":{"$maxN": {"input": "$points", "n": 3 } } } } ] }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "bson_maxminn_group_test4", "pipeline": [ { "$group": { "_id": "$teamId", "NScore":{"$minN": {"input": "$points", "n": 3 } } } } ] }');

SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "bson_maxminn_group_test4", "pipeline": [ { "$group": { "_id": "$teamId", "NScore":{"$maxN": {"input": "$points", "n": 100 } } } } ] }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "bson_maxminn_group_test4", "pipeline": [ { "$group": { "_id": "$teamId", "NScore":{"$minN": {"input": "$points", "n": 100 } } } } ] }');

/* test string and nested array */
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "bson_maxminn_group_test5", "pipeline": [ { "$group": { "_id": "$teamId", "NScore":{"$maxN": {"input": "$points", "n": 3 } } } } ] }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "bson_maxminn_group_test5", "pipeline": [ { "$group": { "_id": "$teamId", "NScore":{"$minN": {"input": "$points", "n": 3 } } } } ] }');

SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "bson_maxminn_group_test5", "pipeline": [ { "$group": { "_id": "$teamId", "NScore":{"$maxN": {"input": "$points", "n": 100 } } } } ] }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "bson_maxminn_group_test5", "pipeline": [ { "$group": { "_id": "$teamId", "NScore":{"$minN": {"input": "$points", "n": 100 } } } } ] }');

/* $maxN are subject to the 100 MB limit */
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "bson_maxminn_group_test1", "pipeline": [ { "$group": { "_id": "$teamId", "NScore":{"$maxN": {"input": "$points", "n": 12345678 } } } } ] }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "bson_maxminn_group_test1", "pipeline": [ { "$group": { "_id": "$teamId", "NScore":{"$maxN": {"input": "$points", "n": 9223372036854775807 } } } } ] }');

/* $minN are subject to the 100 MB limit */
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "bson_maxminn_group_test1", "pipeline": [ { "$group": { "_id": "$teamId", "NScore":{"$minN": {"input": "$points", "n": 12345678 } } } } ] }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "bson_maxminn_group_test1", "pipeline": [ { "$group": { "_id": "$teamId", "NScore":{"$minN": {"input": "$points", "n": 9223372036854775807 } } } } ] }');