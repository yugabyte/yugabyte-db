SET search_path to documentdb_core,documentdb_api,documentdb_api_catalog,pg_catalog;

SET citus.next_shard_id TO 10120000;
SET documentdb.next_collection_id TO 101200;
SET documentdb.next_collection_index_id TO 101200;

SELECT documentdb_api.insert_one('db','games','{ "playerId": "PlayerA", "gameId": "G1", "score": 85 }', NULL);
SELECT documentdb_api.insert_one('db','games','{ "playerId": "PlayerB", "gameId": "G1", "score": 2 }', NULL);
SELECT documentdb_api.insert_one('db','games','{ "playerId": "PlayerC", "gameId": "G1", "score": 3 }', NULL);
SELECT documentdb_api.insert_one('db','games','{ "playerId": "PlayerD", "gameId": "G1", "score": 99 }', NULL);
SELECT documentdb_api.insert_one('db','games','{ "playerId": "PlayerE", "gameId": "G1"}', NULL);
SELECT documentdb_api.insert_one('db','games','{ "playerId": "PlayerF", "gameId": "G1", "score": [24, 23] }', NULL);
SELECT documentdb_api.insert_one('db','games','{ "playerId": "PlayerA", "gameId": "G2", "score": {"$undefined":true} }', NULL);
SELECT documentdb_api.insert_one('db','games','{ "playerId": "PlayerB", "gameId": "G2", "score": 33 }', NULL);
SELECT documentdb_api.insert_one('db','games','{ "playerId": "PlayerC", "gameId": "G2", "score": 40 }', NULL);
SELECT documentdb_api.insert_one('db','games','{ "playerId": "PlayerD", "gameId": "G2", "score": 15 }', NULL);


/* Negative tests */
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "games", "pipeline": [ {"$group": {"_id": "$gameId", "playerId": {"$top": {"output": [ "$playerId", "$score" ]}}}} ] }'); -- missing sortBy
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "games", "pipeline": [ {"$group": {"_id": "$gameId", "playerId": {"$top": {"sortBy": { "score": 1 }}}}} ] }'); -- missing output
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "games", "pipeline": [ {"$group": {"_id": "$gameId", "playerId": {"$top": {"sortBy": { "score": 1 }, "n": 1}}}} ] }'); -- n isn't supported with $top
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "games", "pipeline": [ {"$group": {"_id": "$gameId", "playerId": {"$bottom": {"output": [ "$playerId", "$score" ]}}}} ] }'); -- missing sortBy
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "games", "pipeline": [ {"$group": {"_id": "$gameId", "playerId": {"$bottom": {"sortBy": { "score": 1 }}}}} ] }'); -- missing output
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "games", "pipeline": [ {"$group": {"_id": "$gameId", "playerId": {"$bottom": {"sortBy": { "score": 1 }, "n": 1}}}} ] }'); -- n isn't supported with $top
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "games", "pipeline": [ {"$group": {"_id": "$gameId", "playerId": {"$topN": {"output": [ "$playerId", "$score" ], "n": 1}}}} ] }'); -- missing sortBy
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "games", "pipeline": [ {"$group": {"_id": "$gameId", "playerId": {"$topN": {"sortBy": {"score": 1}, "n": 1}}}} ] }'); -- missing output
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "games", "pipeline": [ {"$group": {"_id": "$gameId", "playerId": {"$topN": {"output": [ "$playerId", "$score" ], "sortBy": { "score": 1 }}}}} ] }'); -- missing n
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "games", "pipeline": [ {"$group": {"_id": "$gameId", "playerId": {"$topN": {"output": [ "$playerId", "$score" ], "sortBy": { "score": 1 }, "n": "a"}}}} ] }'); -- n is not a number
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "games", "pipeline": [ {"$group": {"_id": "$gameId", "playerId": {"$topN": {"output": [ "$playerId", "$score" ], "sortBy": { "score": 1 }, "n": {"$undefined": true}}}}} ] }'); -- n is not a number
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "games", "pipeline": [ {"$group": {"_id": "$gameId", "playerId": {"$topN": {"output": [ "$playerId", "$score" ], "sortBy": { "score": 1 }, "n": {"$numberDecimal": "Infinity"}}}}} ] }'); -- n is not a number
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "games", "pipeline": [ {"$group": {"_id": "$gameId", "playerId": {"$topN": {"output": [ "$playerId", "$score" ], "sortBy": { "score": 1 }, "n": -1}}}} ] }'); -- n is negative
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "games", "pipeline": [ {"$group": {"_id": "$gameId", "playerId": {"$topN": {"output": [ "$playerId", "$score" ], "sortBy": { "score": 1 }, "n": 0.5}}}} ] }'); -- n is not an integer
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "games", "pipeline": [ {"$group": {"_id": "$gameId", "playerId": {"$bottomN": {"output": [ "$playerId", "$score" ], "n": 1}}}} ] }'); -- missing sortBy
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "games", "pipeline": [ {"$group": {"_id": "$gameId", "playerId": {"$bottomN": {"sortBy": {"score": 1}, "n": 1}}}} ] }'); -- missing output
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "games", "pipeline": [ {"$group": {"_id": "$gameId", "playerId": {"$bottomN": {"output": [ "$playerId", "$score" ], "sortBy": { "score": 1 }}}}} ] }'); -- missing n
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "games", "pipeline": [ {"$group": {"_id": "$gameId", "playerId": {"$bottomN": {"output": [ "$playerId", "$score" ], "sortBy": { "score": 1 }, "n": "a"}}}} ] }'); -- n is not a number
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "games", "pipeline": [ {"$group": {"_id": "$gameId", "playerId": {"$bottomN": {"output": [ "$playerId", "$score" ], "sortBy": { "score": 1 }, "n": {"$undefined": true}}}}} ] }'); -- n is not a number
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "games", "pipeline": [ {"$group": {"_id": "$gameId", "playerId": {"$bottomN": {"output": [ "$playerId", "$score" ], "sortBy": { "score": 1 }, "n": {"$numberDecimal": "Infinity"}}}}} ] }'); -- n is not a number
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "games", "pipeline": [ {"$group": {"_id": "$gameId", "playerId": {"$bottomN": {"output": [ "$playerId", "$score" ], "sortBy": { "score": 1 }, "n": -1}}}} ] }'); -- n is negative
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "games", "pipeline": [ {"$group": {"_id": "$gameId", "playerId": {"$bottomN": {"output": [ "$playerId", "$score" ], "sortBy": { "score": 1 }, "n": 0.5}}}} ] }'); -- n is not an integer
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "games", "pipeline": [ {"$group": {"_id": "$gameId", "playerId": {"$bottomN": {"output": [ "$playerId", "$score" ], "sortBy": 1, "n": 1}}}} ] }'); -- sortBy is not an object


/* $top operator with $group */
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "games", "pipeline": [ {"$group": {"_id": "$gameId", "playerId": {"$top": {"output": [ "$playerId", "$score" ], "sortBy": { "score": 1 }}}}} ] }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "games", "pipeline": [{"$match" : { "gameId" : "G1" }}, {"$group": {"_id": "$gameId", "playerId": {"$top": {"output": [ "$playerId", "$score" ], "sortBy": { "score": -1 }}}}}] }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "games", "pipeline": [{"$group": { "_id": "$gameId",  "playerId": {"$top": {"output": [ "$playerId", "$score" ], "sortBy": { "score": -1 }}}}}] }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "games", "pipeline": [{"$sort": {"playerId": 1}}, {"$group": { "_id": "$gameId",  "playerId": {"$top": {"output": [ "$playerId", "$score" ], "sortBy": { "score": -1 }}}}}] }'); -- different sort in sortBy stage but documents in output field are sorted by $top spec

/* $topN operator with $group */
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "games", "pipeline": [ {"$group": {"_id": "$gameId", "playerId": {"$topN": {"output": [ "$playerId", "$score" ], "sortBy": { "score": 1 }, "n": 3}}}} ] }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "games", "pipeline": [{"$match" : { "gameId" : "G1" }}, {"$group": {"_id": "$gameId", "playerId": {"$topN": {"output": [ "$playerId", "$score" ], "sortBy": { "score": -1 }, "n": 3}}}}] }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "games", "pipeline": [{"$group": { "_id": "$gameId",  "playerId": {"$topN": {"output": [ "$playerId", "$score" ], "sortBy": { "score": -1 }, "n": 3}}}}] }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "games", "pipeline": [{"$sort": {"playerId": 1}}, {"$group": { "_id": "$gameId",  "playerId": {"$topN": {"output": [ "$playerId", "$score" ], "sortBy": { "score": -1 }, "n": 3}}}}] }'); -- different sort in sortBy stage but documents in output field are sorted by $top spec

/* $bottom operator with $group */
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "games", "pipeline": [ {"$group": {"_id": "$gameId", "playerId": {"$bottom": {"output": [ "$playerId", "$score" ], "sortBy": { "score": 1 }}}}} ] }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "games", "pipeline": [{"$match" : { "gameId" : "G1" }}, {"$group": {"_id": "$gameId", "playerId": {"$bottom": {"output": [ "$playerId", "$score" ], "sortBy": { "score": -1 }}}}}] }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "games", "pipeline": [{"$group": { "_id": "$gameId",  "playerId": {"$bottom": {"output": [ "$playerId", "$score" ], "sortBy": { "score": -1 }}}}}] }');

/* $bottomN operator with $group */
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "games", "pipeline": [ {"$group": {"_id": "$gameId", "playerId": {"$bottomN": {"output": [ "$playerId", "$score" ], "sortBy": { "score": 1 }, "n": 3}}}} ] }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "games", "pipeline": [{"$match" : { "gameId" : "G1" }}, {"$group": {"_id": "$gameId", "playerId": {"$bottomN": {"output": [ "$playerId", "$score" ], "sortBy": { "score": -1 }, "n": 3}}}}] }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "games", "pipeline": [{"$group": { "_id": "$gameId",  "playerId": {"$bottomN": {"output": [ "$playerId", "$score" ], "sortBy": { "score": -1 }, "n": 3}}}}] }');

/* shard collection */
SELECT documentdb_api.shard_collection('db', 'games', '{ "_id": "hashed" }', false);

/* run same queries to ensure consistency */

/* $top operator with $group */
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "games", "pipeline": [ {"$group": {"_id": "$gameId", "playerId": {"$top": {"output": [ "$playerId", "$score" ], "sortBy": { "score": 1 }}}}} ] }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "games", "pipeline": [{"$match" : { "gameId" : "G1" }}, {"$group": {"_id": "$gameId", "playerId": {"$top": {"output": [ "$playerId", "$score" ], "sortBy": { "score": -1 }}}}}] }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "games", "pipeline": [{"$group": { "_id": "$gameId",  "playerId": {"$top": {"output": [ "$playerId", "$score" ], "sortBy": { "score": -1 }}}}}] }');

/* $topN operator with $group */
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "games", "pipeline": [ {"$group": {"_id": "$gameId", "playerId": {"$topN": {"output": [ "$playerId", "$score" ], "sortBy": { "score": 1 }, "n": 3}}}} ] }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "games", "pipeline": [{"$match" : { "gameId" : "G1" }}, {"$group": {"_id": "$gameId", "playerId": {"$topN": {"output": [ "$playerId", "$score" ], "sortBy": { "score": -1 }, "n": 3}}}}] }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "games", "pipeline": [{"$group": { "_id": "$gameId",  "playerId": {"$topN": {"output": [ "$playerId", "$score" ], "sortBy": { "score": -1 }, "n": 3}}}}] }');

/* $bottom operator with $group */
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "games", "pipeline": [ {"$group": {"_id": "$gameId", "playerId": {"$bottom": {"output": [ "$playerId", "$score" ], "sortBy": { "score": 1 }}}}} ] }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "games", "pipeline": [{"$match" : { "gameId" : "G1" }}, {"$group": {"_id": "$gameId", "playerId": {"$bottom": {"output": [ "$playerId", "$score" ], "sortBy": { "score": -1 }}}}}] }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "games", "pipeline": [{"$group": { "_id": "$gameId",  "playerId": {"$bottom": {"output": [ "$playerId", "$score" ], "sortBy": { "score": -1 }}}}}] }');

/* $bottomN operator with $group */
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "games", "pipeline": [ {"$group": {"_id": "$gameId", "playerId": {"$bottomN": {"output": [ "$playerId", "$score" ], "sortBy": { "score": 1 }, "n": 3}}}} ] }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "games", "pipeline": [{"$match" : { "gameId" : "G1" }}, {"$group": {"_id": "$gameId", "playerId": {"$bottomN": {"output": [ "$playerId", "$score" ], "sortBy": { "score": -1 }, "n": 3}}}}] }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "games", "pipeline": [{"$group": { "_id": "$gameId",  "playerId": {"$bottomN": {"output": [ "$playerId", "$score" ], "sortBy": { "score": -1 }, "n": 3}}}}] }'); 