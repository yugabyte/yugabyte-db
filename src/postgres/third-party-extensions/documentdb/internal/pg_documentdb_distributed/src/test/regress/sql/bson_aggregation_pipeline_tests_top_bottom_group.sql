SET search_path to documentdb_core,documentdb_api,documentdb_api_catalog,pg_catalog;

SET citus.next_shard_id TO 10120000;
SET documentdb.next_collection_id TO 101200;
SET documentdb.next_collection_index_id TO 101200;

SELECT documentdb_api.insert_one('db','pipeline_group_tests','{ "user": "Alpha", "team": "T1", "points": 10 }', NULL);
SELECT documentdb_api.insert_one('db','pipeline_group_tests','{ "user": "Beta", "team": "T1", "points": 5 }', NULL);
SELECT documentdb_api.insert_one('db','pipeline_group_tests','{ "user": "Gamma", "team": "T1", "points": 7 }', NULL);
SELECT documentdb_api.insert_one('db','pipeline_group_tests','{ "user": "Delta", "team": "T1", "points": 20 }', NULL);
SELECT documentdb_api.insert_one('db','pipeline_group_tests','{ "user": "Epsilon", "team": "T1"}', NULL);
SELECT documentdb_api.insert_one('db','pipeline_group_tests','{ "user": "Zeta", "team": "T1", "points": [3, 2] }', NULL);
SELECT documentdb_api.insert_one('db','pipeline_group_tests','{ "user": "Alpha", "team": "T2", "points": {"$undefined":true} }', NULL);
SELECT documentdb_api.insert_one('db','pipeline_group_tests','{ "user": "Beta", "team": "T2", "points": 15 }', NULL);
SELECT documentdb_api.insert_one('db','pipeline_group_tests','{ "user": "Gamma", "team": "T2", "points": 18 }', NULL);
SELECT documentdb_api.insert_one('db','pipeline_group_tests','{ "user": "Delta", "team": "T2", "points": 8 }', NULL);


/* Negative tests */
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "pipeline_group_tests", "pipeline": [ {"$group": {"_id": "$team", "user": {"$top": {"output": [ "$user", "$points" ]}}}} ] }'); -- missing sortBy
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "pipeline_group_tests", "pipeline": [ {"$group": {"_id": "$team", "user": {"$top": {"sortBy": { "points": 1 }}}}} ] }'); -- missing output
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "pipeline_group_tests", "pipeline": [ {"$group": {"_id": "$team", "user": {"$top": {"sortBy": { "points": 1 }, "n": 1}}}} ] }'); -- n isn't supported with $top
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "pipeline_group_tests", "pipeline": [ {"$group": {"_id": "$team", "user": {"$bottom": {"output": [ "$user", "$points" ]}}}} ] }'); -- missing sortBy
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "pipeline_group_tests", "pipeline": [ {"$group": {"_id": "$team", "user": {"$bottom": {"sortBy": { "points": 1 }}}}} ] }'); -- missing output
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "pipeline_group_tests", "pipeline": [ {"$group": {"_id": "$team", "user": {"$bottom": {"sortBy": { "points": 1 }, "n": 1}}}} ] }'); -- n isn't supported with $top
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "pipeline_group_tests", "pipeline": [ {"$group": {"_id": "$team", "user": {"$topN": {"output": [ "$user", "$points" ], "n": 1}}}} ] }'); -- missing sortBy
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "pipeline_group_tests", "pipeline": [ {"$group": {"_id": "$team", "user": {"$topN": {"sortBy": {"points": 1}, "n": 1}}}} ] }'); -- missing output
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "pipeline_group_tests", "pipeline": [ {"$group": {"_id": "$team", "user": {"$topN": {"output": [ "$user", "$points" ], "sortBy": { "points": 1 }}}}} ] }'); -- missing n
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "pipeline_group_tests", "pipeline": [ {"$group": {"_id": "$team", "user": {"$topN": {"output": [ "$user", "$points" ], "sortBy": { "points": 1 }, "n": "a"}}}} ] }'); -- n is not a number
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "pipeline_group_tests", "pipeline": [ {"$group": {"_id": "$team", "user": {"$topN": {"output": [ "$user", "$points" ], "sortBy": { "points": 1 }, "n": {"$undefined": true}}}}} ] }'); -- n is not a number
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "pipeline_group_tests", "pipeline": [ {"$group": {"_id": "$team", "user": {"$topN": {"output": [ "$user", "$points" ], "sortBy": { "points": 1 }, "n": {"$numberDecimal": "Infinity"}}}}} ] }'); -- n is not a number
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "pipeline_group_tests", "pipeline": [ {"$group": {"_id": "$team", "user": {"$topN": {"output": [ "$user", "$points" ], "sortBy": { "points": 1 }, "n": -1}}}} ] }'); -- n is negative
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "pipeline_group_tests", "pipeline": [ {"$group": {"_id": "$team", "user": {"$topN": {"output": [ "$user", "$points" ], "sortBy": { "points": 1 }, "n": 0.5}}}} ] }'); -- n is not an integer
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "pipeline_group_tests", "pipeline": [ {"$group": {"_id": "$team", "user": {"$bottomN": {"output": [ "$user", "$points" ], "n": 1}}}} ] }'); -- missing sortBy
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "pipeline_group_tests", "pipeline": [ {"$group": {"_id": "$team", "user": {"$bottomN": {"sortBy": {"points": 1}, "n": 1}}}} ] }'); -- missing output
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "pipeline_group_tests", "pipeline": [ {"$group": {"_id": "$team", "user": {"$bottomN": {"output": [ "$user", "$points" ], "sortBy": { "points": 1 }}}}} ] }'); -- missing n
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "pipeline_group_tests", "pipeline": [ {"$group": {"_id": "$team", "user": {"$bottomN": {"output": [ "$user", "$points" ], "sortBy": { "points": 1 }, "n": "a"}}}} ] }'); -- n is not a number
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "pipeline_group_tests", "pipeline": [ {"$group": {"_id": "$team", "user": {"$bottomN": {"output": [ "$user", "$points" ], "sortBy": { "points": 1 }, "n": {"$undefined": true}}}}} ] }'); -- n is not a number
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "pipeline_group_tests", "pipeline": [ {"$group": {"_id": "$team", "user": {"$bottomN": {"output": [ "$user", "$points" ], "sortBy": { "points": 1 }, "n": {"$numberDecimal": "Infinity"}}}}} ] }'); -- n is not a number
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "pipeline_group_tests", "pipeline": [ {"$group": {"_id": "$team", "user": {"$bottomN": {"output": [ "$user", "$points" ], "sortBy": { "points": 1 }, "n": -1}}}} ] }'); -- n is negative
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "pipeline_group_tests", "pipeline": [ {"$group": {"_id": "$team", "user": {"$bottomN": {"output": [ "$user", "$points" ], "sortBy": { "points": 1 }, "n": 0.5}}}} ] }'); -- n is not an integer
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "pipeline_group_tests", "pipeline": [ {"$group": {"_id": "$team", "user": {"$bottomN": {"output": [ "$user", "$points" ], "sortBy": 1, "n": 1}}}} ] }'); -- sortBy is not an object


/* $top operator with $group */
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "pipeline_group_tests", "pipeline": [ {"$group": {"_id": "$team", "user": {"$top": {"output": [ "$user", "$points" ], "sortBy": { "points": 1 }}}}} ] }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "pipeline_group_tests", "pipeline": [{"$match" : { "team" : "T1" }}, {"$group": {"_id": "$team", "user": {"$top": {"output": [ "$user", "$points" ], "sortBy": { "points": -1 }}}}}] }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "pipeline_group_tests", "pipeline": [{"$group": { "_id": "$team",  "user": {"$top": {"output": [ "$user", "$points" ], "sortBy": { "points": -1 }}}}}] }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "pipeline_group_tests", "pipeline": [{"$sort": {"user": 1}}, {"$group": { "_id": "$team",  "user": {"$top": {"output": [ "$user", "$points" ], "sortBy": { "points": -1 }}}}}] }'); -- different sort in sortBy stage but documents in output field are sorted by $top spec

/* $topN operator with $group */
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "pipeline_group_tests", "pipeline": [ {"$group": {"_id": "$team", "user": {"$topN": {"output": [ "$user", "$points" ], "sortBy": { "points": 1 }, "n": 3}}}} ] }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "pipeline_group_tests", "pipeline": [{"$match" : { "team" : "T1" }}, {"$group": {"_id": "$team", "user": {"$topN": {"output": [ "$user", "$points" ], "sortBy": { "points": -1 }, "n": 3}}}}] }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "pipeline_group_tests", "pipeline": [{"$group": { "_id": "$team",  "user": {"$topN": {"output": [ "$user", "$points" ], "sortBy": { "points": -1 }, "n": 3}}}}] }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "pipeline_group_tests", "pipeline": [{"$sort": {"user": 1}}, {"$group": { "_id": "$team",  "user": {"$topN": {"output": [ "$user", "$points" ], "sortBy": { "points": -1 }, "n": 3}}}}] }'); -- different sort in sortBy stage but documents in output field are sorted by $top spec

/* $bottom operator with $group */
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "pipeline_group_tests", "pipeline": [ {"$group": {"_id": "$team", "user": {"$bottom": {"output": [ "$user", "$points" ], "sortBy": { "points": 1 }}}}} ] }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "pipeline_group_tests", "pipeline": [{"$match" : { "team" : "T1" }}, {"$group": {"_id": "$team", "user": {"$bottom": {"output": [ "$user", "$points" ], "sortBy": { "points": -1 }}}}}] }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "pipeline_group_tests", "pipeline": [{"$group": { "_id": "$team",  "user": {"$bottom": {"output": [ "$user", "$points" ], "sortBy": { "points": -1 }}}}}] }');

/* $bottomN operator with $group */
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "pipeline_group_tests", "pipeline": [ {"$group": {"_id": "$team", "user": {"$bottomN": {"output": [ "$user", "$points" ], "sortBy": { "points": 1 }, "n": 3}}}} ] }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "pipeline_group_tests", "pipeline": [{"$match" : { "team" : "T1" }}, {"$group": {"_id": "$team", "user": {"$bottomN": {"output": [ "$user", "$points" ], "sortBy": { "points": -1 }, "n": 3}}}}] }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "pipeline_group_tests", "pipeline": [{"$group": { "_id": "$team",  "user": {"$bottomN": {"output": [ "$user", "$points" ], "sortBy": { "points": -1 }, "n": 3}}}}] }');

/* shard collection */
SELECT documentdb_api.shard_collection('db', 'pipeline_group_tests', '{ "_id": "hashed" }', false);

/* run same queries to ensure consistency */

/* $top operator with $group */
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "pipeline_group_tests", "pipeline": [ {"$group": {"_id": "$team", "user": {"$top": {"output": [ "$user", "$points" ], "sortBy": { "points": 1 }}}}} ] }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "pipeline_group_tests", "pipeline": [{"$match" : { "team" : "T1" }}, {"$group": {"_id": "$team", "user": {"$top": {"output": [ "$user", "$points" ], "sortBy": { "points": -1 }}}}}] }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "pipeline_group_tests", "pipeline": [{"$group": { "_id": "$team",  "user": {"$top": {"output": [ "$user", "$points" ], "sortBy": { "points": -1 }}}}}] }');

/* $topN operator with $group */
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "pipeline_group_tests", "pipeline": [ {"$group": {"_id": "$team", "user": {"$topN": {"output": [ "$user", "$points" ], "sortBy": { "points": 1 }, "n": 3}}}} ] }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "pipeline_group_tests", "pipeline": [{"$match" : { "team" : "T1" }}, {"$group": {"_id": "$team", "user": {"$topN": {"output": [ "$user", "$points" ], "sortBy": { "points": -1 }, "n": 3}}}}] }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "pipeline_group_tests", "pipeline": [{"$group": { "_id": "$team",  "user": {"$topN": {"output": [ "$user", "$points" ], "sortBy": { "points": -1 }, "n": 3}}}}] }');

/* $bottom operator with $group */
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "pipeline_group_tests", "pipeline": [ {"$group": {"_id": "$team", "user": {"$bottom": {"output": [ "$user", "$points" ], "sortBy": { "points": 1 }}}}} ] }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "pipeline_group_tests", "pipeline": [{"$match" : { "team" : "T1" }}, {"$group": {"_id": "$team", "user": {"$bottom": {"output": [ "$user", "$points" ], "sortBy": { "points": -1 }}}}}] }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "pipeline_group_tests", "pipeline": [{"$group": { "_id": "$team",  "user": {"$bottom": {"output": [ "$user", "$points" ], "sortBy": { "points": -1 }}}}}] }');

/* $bottomN operator with $group */
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "pipeline_group_tests", "pipeline": [ {"$group": {"_id": "$team", "user": {"$bottomN": {"output": [ "$user", "$points" ], "sortBy": { "points": 1 }, "n": 3}}}} ] }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "pipeline_group_tests", "pipeline": [{"$match" : { "team" : "T1" }}, {"$group": {"_id": "$team", "user": {"$bottomN": {"output": [ "$user", "$points" ], "sortBy": { "points": -1 }, "n": 3}}}}] }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "pipeline_group_tests", "pipeline": [{"$group": { "_id": "$team",  "user": {"$bottomN": {"output": [ "$user", "$points" ], "sortBy": { "points": -1 }, "n": 3}}}}] }');