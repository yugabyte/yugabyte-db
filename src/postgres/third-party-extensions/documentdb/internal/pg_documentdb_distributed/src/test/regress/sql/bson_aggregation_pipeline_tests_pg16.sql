SET search_path TO documentdb_core,documentdb_api,documentdb_api_catalog,documentdb_api_internal;

SET citus.next_shard_id TO 411000;
SET documentdb.next_collection_id TO 4110;
SET documentdb.next_collection_index_id TO 4110;


SELECT documentdb_api.insert_one('db','aggregation_pipeline','{"_id":"1", "int": 10, "a" : { "b" : [ "x", 1, 2.0, true ] } }', NULL);
SELECT documentdb_api.insert_one('db','aggregation_pipeline','{"_id":"2", "double": 2.0, "a" : { "b" : {"c": 3} } }', NULL);
SELECT documentdb_api.insert_one('db','aggregation_pipeline','{"_id":"3", "boolean": false, "a" : "no", "b": "yes", "c": true }', NULL);

-- fetch all rows
SELECT shard_key_value, object_id, document FROM documentdb_api.collection('db', 'aggregation_pipeline') ORDER BY object_id;

-- add newField
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline", "pipeline": [ { "$addFields": { "newField" : "1", "a.y": ["p", "q"] } } ], "cursor": {} }');

-- do 2 addFields
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline", "pipeline": [ { "$addFields": { "newField" : "1", "a.y": ["p", "q"] } }, { "$addFields": { "newField2": "someOtherField" } } ], "cursor": {} }');

SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline", "pipeline": [ { "$addFields": { "newField" : "1", "a.y": ["p", "q"] } }, { "$set": { "newField2": "someOtherField" } } ], "cursor": {} }');

EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline", "pipeline": [ { "$addFields": { "newField" : "1", "a.y": ["p", "q"] } }, { "$addFields": { "newField2": "someOtherField" } } ], "cursor": {} }');

EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline", "pipeline": [ { "$addFields": { "newField" : "1", "a.y": ["p", "q"] } }, { "$set": { "newField2": "someOtherField" } } ], "cursor": {} }');

-- add $project
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline", "pipeline": [ { "$project": { "_id" : 1, "a.b": 1 } } ], "cursor": {} }');

EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline", "pipeline": [ { "$project": { "_id" : 1, "a.b": 1 } }, { "$set": { "newField2": "someOtherField" } }], "cursor": {} }');

SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline", "pipeline": [ { "$project": { "_id" : 1, "a.b": 1 } }, { "$set": { "newField2": "someOtherField" } }], "cursor": {} }');

-- add $unset
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline", "pipeline": [ { "$unset": "_id" }, { "$set": { "newField2": "someOtherField" } }], "cursor": {} }');

-- add skip
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline", "pipeline": [ { "$project": { "_id" : 1, "a.b": 1 } }, { "$skip": 1 }], "cursor": {} }');

EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline", "pipeline": [ { "$project": { "_id" : 1, "a.b": 1 } }, { "$skip": 1 }], "cursor": {} }');

-- add limit
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline", "pipeline": [ { "$project": { "_id" : 1, "a.b": 1 } }, { "$limit": 2 }], "cursor": {} }');
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline", "pipeline": [ { "$project": { "_id" : 1, "a.b": 1 } }, { "$limit": 2 }], "cursor": {} }');

-- add skip + limit
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline", "pipeline": [ { "$project": { "_id" : 1, "a.b": 1 } }, { "$limit": 1 }, { "$skip": 1 }], "cursor": {} }');
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline", "pipeline": [ { "$project": { "_id" : 1, "a.b": 1 } }, { "$limit": 1 }, { "$skip": 1 }], "cursor": {} }');

SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline", "pipeline": [ { "$project": { "_id" : 1, "a.b": 1 } }, { "$skip": 1 }, { "$limit": 1 }], "cursor": {} }');
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline", "pipeline": [ { "$project": { "_id" : 1, "a.b": 1 } }, { "$skip": 1 }, { "$limit": 1 }], "cursor": {} }');

-- try match
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline", "pipeline": [ { "$match": { "_id": { "$gt": "1" } } }], "cursor": {} }');
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline", "pipeline": [ { "$match": { "_id": { "$gt": "1" } } }], "cursor": {} }');

-- match + project
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline", "pipeline": [ { "$match": { "_id": { "$gt": "1" } } }, { "$project": { "a.b": 1 } }], "cursor": {} }');
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline", "pipeline": [ { "$match": { "_id": { "$gt": "1" } } }, { "$project": { "a.b": 1 } }], "cursor": {} }');

-- match + project + match
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline", "pipeline": [ { "$match": { "_id": { "$gt": "1" } } }, { "$project": { "a.b": 1, "c": "$_id", "_id": 0 } }, { "$match": { "c": { "$gt": "2" } } }], "cursor": {} }');
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline", "pipeline": [ { "$match": { "_id": { "$gt": "1" } } }, { "$project": { "a.b": 1, "c": "$_id", "_id": 0 } }, { "$match": { "c": { "$gt": "2" } } }], "cursor": {} }');

-- unwind
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline", "pipeline": [ { "$unwind": "$a.b" } ], "cursor": {} }');

-- match and then unwind
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline", "pipeline": [ { "$match": { "_id": { "$gt": "1" } } }, { "$unwind": "$a.b" } ], "cursor": {} }');

-- unwind and then match
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline", "pipeline": [ { "$unwind": "$a.b" }, { "$match": { "$expr": { "$not": { "$isArray": "$a.b" } } } } ], "cursor": {} }');

-- unwind and addfields
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline", "pipeline": [ { "$unwind": "$a.b" }, { "$addFields": { "xyz": "$_id" } } ], "cursor": {} }');

-- $addFields then addFields is inlined.
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline", "pipeline": [ { "$addFields": { "x": 1, "y": 2, "xyz": 3 } }, { "$addFields": { "xyz": "$_id" } } ], "cursor": {} }');
EXPLAIN (COSTS OFF, VERBOSE ON ) SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline", "pipeline": [ { "$addFields": { "x": 1, "y": 2, "xyz": 3 } }, { "$addFields": { "xyz": "$_id" } } ], "cursor": {} }');

-- $project then addFields can be inlined only on exclusion today
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline", "pipeline": [ { "$project": { "a": 0, "boolean": 0 } }, { "$addFields": { "a": 1, "xyz": "$_id" } } ], "cursor": {} }');
EXPLAIN (COSTS OFF, VERBOSE ON ) SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline", "pipeline": [ { "$project": { "a": 0, "boolean": 0 } }, { "$addFields": { "a": 1, "xyz": "$_id" } } ], "cursor": {} }');

-- error scenarios for $project
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline", "pipeline": [ { "$project": { "a": 0, "boolean": 1 } }, { "$addFields": { "a": 1, "xyz": "$_id" } } ], "cursor": {} }');

-- replaceRoot
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline", "pipeline": [ { "$addFields": { "e": {  "f": "$a.b" } } }, { "$replaceRoot": { "newRoot": "$e" } } ], "cursor": {} }');

-- count
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline", "pipeline": [ { "$count": "d" }], "cursor": {} }');

SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline", "pipeline": [ { "$unwind": "$a.b" }, { "$count": "d" }, { "$addFields": { "e": "$d" } }], "cursor": {} }');

-- replaceWith
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline", "pipeline": [ { "$addFields": { "e": {  "f": "$a.b" } } }, { "$replaceWith": "$e" } ], "cursor": {} }');

-- sort
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline", "pipeline": [ { "$sort": { "_id": 1 } }], "cursor": {} }');

-- sort + match
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline", "pipeline": [ { "$sort": { "_id": 1 } }, { "$match": { "_id": { "$gt": "1" } } } ], "cursor": {} }');

-- match + sort
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline", "pipeline": [ { "$match": { "_id": { "$gt": "1" } } }, { "$sort": { "_id": 1 } } ], "cursor": {} }');

-- sortByCount
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline", "pipeline": [ { "$sortByCount": { "$eq": [ { "$mod": [ { "$toInt": "$_id" }, 2 ] }, 0  ] } }, { "$sort": { "_id": 1 } }], "cursor": {} }');

-- $group
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline", "pipeline": [ { "$group": { "_id": { "$mod": [ { "$toInt": "$_id" }, 2 ] }, "d": { "$max": "$_id" }, "e": { "$count": 1 } } }], "cursor": {} }');

-- $group with keys having dotted path
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline", "pipeline": [ { "$group": { "_id": { "$mod": [ { "$toInt": "$_id" }, 2 ] }, "d.e": { "$max": "$_id" }, "e": { "$count": 1 } } }], "cursor": {} }');

-- $group with first/last
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline", "pipeline": [ { "$group": { "_id": { "$mod": [ { "$toInt": "$_id" }, 2 ] }, "d": { "$first": "$_id" }, "e": { "$last":  "$_id" } } }], "cursor": {} }');
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline", "pipeline": [ { "$group": { "_id": { "$mod": [ { "$toInt": "$_id" }, 2 ] }, "d": { "$first": "$_id" }, "e": { "$last":  "$_id" } } }], "cursor": {} }');

-- $group with first/last sorted (TODO)


-- add $sample
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline", "pipeline": [ { "$sample": { "size": 2 } }, { "$project": { "_id": "1" } } ], "cursor": {} }');
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline", "pipeline": [ { "$sample": { "size": 2 } }, { "$project": { "_id": "1" } } ], "cursor": {} }');

-- Sample after pass-through stages
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline", "pipeline": [ { "$project": { "_id": "1" } }, { "$sample": { "size": 2 } }], "cursor": {} }');

-- Sample after sample
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline", "pipeline": [ { "$sample": { "size": 3 } }, { "$sample": { "size": 2 } }], "cursor": {} }');

-- Sample after other stage
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline", "pipeline": [ { "$unwind": "$a.b" }, { "$sample": { "size": 2 } }], "cursor": {} }');

-- internalInhibitOptimization
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline", "pipeline": [ { "$addFields": { "newField" : "1", "a.y": ["p", "q"] } }, { "$_internalInhibitOptimization": 1 }, { "$addFields": { "newField2": "someOtherField" } } ], "cursor": {} }');
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline", "pipeline": [ { "$addFields": { "newField" : "1", "a.y": ["p", "q"] } }, { "$_internalInhibitOptimization": 1 }, { "$addFields": { "newField2": "someOtherField" } } ], "cursor": {} }');


-- facet
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline", "pipeline": [ { "$facet": { "a" : [ { "$addFields": { "newField" : "1", "a.y": ["p", "q"] } } ] } } ], "cursor": {} }');

SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline", "pipeline": [ { "$facet": { "a" : [ { "$addFields": { "newField" : "1", "a.y": ["p", "q"] } } ],  "b" : [ { "$count": "c" } ] } } ], "cursor": {} }');

SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline", "pipeline": [ { "$facet": { "a" : [ { "$addFields": { "newField" : "1", "a.y": ["p", "q"] } } ],  "b" : [ { "$count": "d" } ], "c": [ { "$unwind": "$a.b" } ] } } ], "cursor": {} }');

SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline", "pipeline" : [{ "$facet" : { "results" : [ { "$match" : { "result" : true } } ] } }, { "$facet" : { "results" : [ { "$match" : { "result" : true } } ] } }]}');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline", "pipeline" : [{ "$facet" : { "results" : [ { "$match" : { "result" : true } } ] } }, { "$facet" : { "results" : [ { "$match" : { "result" : true } } ] } }, { "$facet": { "a" : [ { "$addFields": { "newField" : "1", "a.y": ["p", "q"] } } ],  "b" : [ { "$count": "d" } ], "c": [ { "$unwind": "$a.b" } ] } }]}');


EXPlAIN (COSTS OFF, VERBOSE ON) SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline", "pipeline": [ { "$facet": { "a" : [ { "$addFields": { "newField" : "1", "a.y": ["p", "q"] } } ],  "b" : [ { "$count": "d" } ], "c": [ { "$unwind": "$a.b" } ] } } ], "cursor": {} }');

EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline", "pipeline" : [{ "$facet" : { "results" : [ { "$match" : { "result" : true } } ] } }, { "$facet" : { "results" : [ { "$match" : { "result" : true } } ] } }]}');
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline", "pipeline" : [{ "$facet" : { "results" : [ { "$match" : { "result" : true } } ] } }, { "$facet" : { "results" : [ { "$match" : { "result" : true } } ] } }, { "$facet": { "a" : [ { "$addFields": { "newField" : "1", "a.y": ["p", "q"] } } ],  "b" : [ { "$count": "d" } ], "c": [ { "$unwind": "$a.b" } ] } }]}');

-- facet with parent transform:
SELECT document FROM bson_aggregation_pipeline('db', 
'{ "aggregate": "aggregation_pipeline", "pipeline": [ { "$unwind": "$a.b" }, { "$facet": { "a" : [ { "$unset": "_id" } ],  "b" : [ { "$count": "d" } ], "c": [ { "$replaceWith": { "f": "$_id" } } ] } } ], "cursor": {} }');

-- facet negative tests
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline", "pipeline": [ { "$facet": { "a" : [ "hello" ] } } ], "cursor": {} }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline", "pipeline": [ { "$facet": { "a" : [ { } ] } } ], "cursor": {} }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline", "pipeline": [ { "$facet": { "a" : [ { "$count": "d", "$unset": "b" } ] } } ], "cursor": {} }');

-- FIND
SELECT document FROM bson_aggregation_find('db', '{ "find": "aggregation_pipeline", "filter": { "_id": { "$gt": "1" } }, "projection": { "a.b": 1 }, "sort": { "_id": 1 }, "skip": 1, "limit": 2 }');

BEGIN;
set local documentdb.enableIndexOrderbyPushdown to on;
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM bson_aggregation_find('db', '{ "find": "aggregation_pipeline", "filter": { "_id": { "$gt": "1" } }, "projection": { "a.b": 1 }, "sort": { "_id": 1 }, "skip": 1, "limit": 2 }');
ROLLBACK;


SELECT document FROM bson_aggregation_find('db', '{ "find": "aggregation_pipeline", "filter": { "_id": { "$gt": "1" } }, "projection": { "a.b": 1 }, "sort": { "_id": 1 }, "skip": 1, "limit": -2 }');

-- count
SELECT document FROM bson_aggregation_count('db', '{ "count": "aggregation_pipeline" }');

SELECT document FROM bson_aggregation_count('db', '{ "count": "aggregation_pipeline", "query": { "_id": { "$gt": "1" } } }');

-- count with skip
SELECT document FROM bson_aggregation_count('db', '{ "count": "aggregation_pipeline", "query": {}, "skip": 0 }');

SELECT document FROM bson_aggregation_count('db', '{ "count": "aggregation_pipeline", "query": {}, "skip": 1 }');

SELECT document FROM bson_aggregation_count('db', '{ "count": "aggregation_pipeline", "query": {}, "skip": null }');

SELECT document FROM bson_aggregation_count('db', '{ "count": "aggregation_pipeline", "query": {}, "skip": -3.14159 }');

SELECT document FROM bson_aggregation_count('db', '{ "count": "aggregation_pipeline", "query": {}, "skip": -9223372036854775808 }');

SELECT document FROM bson_aggregation_count('db', '{ "count": "non_existent_coll" }');

SELECT document FROM documentdb_api.count_query('db', '{ "count": "aggregation_pipeline", "query": { "_id": { "$gt": "1" } } }');

SELECT document FROM documentdb_api.count_query('db', '{ "count": "aggregation_pipeline" }');

SELECT document FROM documentdb_api.count_query('db', '{ "count": "aggregation_pipeline", "query": {}, "skip": null }');

SELECT document FROM documentdb_api.count_query('db', '{ "count": "aggregation_pipeline", "query": {}, "skip": -3.14159 }');

SELECT document FROM documentdb_api.count_query('db', '{ "count": "aggregation_pipeline", "query": {}, "skip": -9223372036854775808 }');

-- handling of skip as an aggregation stage; this is different from skip in a count query
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline", "pipeline": [{ "$match": {}}, { "$skip": 0 }], "cursor": {}}');

SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline", "pipeline": [{ "$match": {}}, { "$skip": 1 }], "cursor": {}}');

SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline", "pipeline": [{ "$match": {}}, { "$skip": null }], "cursor": {}}');

SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline", "pipeline": [{ "$match": {}}, { "$skip": -3.14159 }], "cursor": {}}');

SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline", "pipeline": [{ "$match": {}}, { "$skip": -9223372036854775808 }], "cursor": {}}');

EXPLAIN (COSTS OFF) SELECT document FROM bson_aggregation_count('db', '{ "count": "aggregation_pipeline" }');

EXPLAIN (COSTS OFF) SELECT document FROM bson_aggregation_count('db', '{ "count": "aggregation_pipeline", "query": { "_id": { "$gt": "1" } } }');

EXPLAIN (COSTS OFF) SELECT document FROM bson_aggregation_count('db', '{ "count": "non_existent_coll" }');

-- distinct
SELECT document FROM bson_aggregation_distinct('db', '{ "distinct": "aggregation_pipeline", "key": "_id" }');

SELECT document FROM bson_aggregation_distinct('db', '{ "distinct": "non_existent_coll", "key": "foo" }');

SELECT document FROM documentdb_api.distinct_query('db', '{ "distinct": "aggregation_pipeline", "key": "_id" }');

SELECT document FROM documentdb_api.distinct_query('db', '{ "distinct": "non_existent_coll", "key": "foo" }');


-- Explain for LIMIT 1 + Point query
BEGIN;
set local documentdb.enableCursorsOnAggregationQueryRewrite to on;

-- with singleBatch we should not see customScan
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM bson_aggregation_find('db', '{ "find": "aggregation_pipeline", "filter": { "_id": "1" }, "singleBatch": true }');

-- with _id query and limit 1 we should see custom scan with primary key lookup.
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM bson_aggregation_find('db', '{ "find": "aggregation_pipeline", "filter": { "_id": "2" }, "limit": 1 }');

ROLLBACK;

-- $lookup
SELECT documentdb_api.insert_one('db','agg_pipeline_movie_screenings',' { "_id" : 1, "movie_title" : "Interstellar", "ticket_price" : 15, "tickets_sold" : 120 }', NULL);
SELECT documentdb_api.insert_one('db','agg_pipeline_movie_screenings','{ "_id" : 2, "movie_title" : "Inception", "ticket_price" : 13, "tickets_sold" : 100 }', NULL);
SELECT documentdb_api.insert_one('db','agg_pipeline_movie_screenings',' { "_id" : 3, "movie_title" : "Dune", "ticket_price" : 18, "tickets_sold" : 95 }', NULL);
SELECT documentdb_api.insert_one('db','agg_pipeline_movie_screenings',' { "_id" : 4, "movie_title" : ["Interstellar", "Dune", "Inception"], "ticket_price" : 14, "tickets_sold" : 250 }', NULL);
SELECT documentdb_api.insert_one('db','agg_pipeline_movie_screenings',' { "_id" : 5 }', NULL);
SELECT documentdb_api.insert_one('db','agg_pipeline_movie_screenings',' { "_id" : 6, "movie_title" : {"a": "v", "b" : 2, "c" : [5, 6, 7]} }', NULL);
SELECT documentdb_api.insert_one('db','agg_pipeline_movie_screenings',' { "_id" : 7, "movie_title" : [{"a": { "b" : 2}}, [5, 6, 7], 9, "z"] }', NULL);

SELECT documentdb_api.insert_one('db','agg_pipeline_movie_catalog',' { "_id" : 11, "title" : "Interstellar", "genre": "Sci-Fi", "available_seats" : 50 }', NULL);
SELECT documentdb_api.insert_one('db','agg_pipeline_movie_catalog',' { "_id" : 12, "title" : "Interstellar", "genre": "Sci-Fi", "available_seats" : 70 }', NULL);
SELECT documentdb_api.insert_one('db','agg_pipeline_movie_catalog','{ "_id" : 13, "title" : "Dune", "genre": "Sci-Fi", "available_seats" : 30 }', NULL);
SELECT documentdb_api.insert_one('db','agg_pipeline_movie_catalog','{ "_id" : 14, "title" : "Blade Runner", "genre": "Cyberpunk", "available_seats" : 40 }', NULL);
SELECT documentdb_api.insert_one('db','agg_pipeline_movie_catalog','{ "_id" : 15, "title" : "Inception", "genre": "Thriller", "available_seats" : 60 }', NULL);
SELECT documentdb_api.insert_one('db','agg_pipeline_movie_catalog','{ "_id" : 16, "title" : null, "genre": "Unknown", "available_seats" : 0 }', NULL);
SELECT documentdb_api.insert_one('db','agg_pipeline_movie_catalog','{ "_id" : 17, "title" :  {"a": "x", "b" : 1, "c" : [1, 2, 3]}, "genre": "Experimental" }', NULL);
SELECT documentdb_api.insert_one('db','agg_pipeline_movie_catalog','{ "_id" : 18, "title" : [{"a": { "b" : 1}}, [1, 2, 3], 1, "x"], "genre": "Experimental Array" }', NULL);
SELECT documentdb_api.insert_one('db','agg_pipeline_movie_catalog','{ "_id" : 19, "title" : [{"a": { "b" : 1}}, [1, 2, 3], 1, "x"], "genre": "Experimental Array" }', NULL);


SELECT document FROM bson_aggregation_pipeline('db', 
    '{ "aggregate": "agg_pipeline_movie_screenings", "pipeline": [ { "$lookup": { "from": "agg_pipeline_movie_catalog", "as": "matched_docs", "localField": "title", "foreignField": "title" } } ], "cursor": {} }');

SELECT document FROM bson_aggregation_pipeline('db', 
    '{ "aggregate": "agg_pipeline_movie_screenings", "pipeline": [ { "$lookup": { "from": "agg_pipeline_movie_catalog", "as": "matched_docs", "pipeline": [ { "$count": "efe" } ] } } ], "cursor": {} }');

SELECT document FROM bson_aggregation_pipeline('db', 
    '{ "aggregate": "agg_pipeline_movie_screenings", "pipeline": [ { "$lookup": { "from": "agg_pipeline_movie_catalog", "as": "matched_docs", "localField": "title", "foreignField": "title", "pipeline": [ { "$count": "efe" } ] } } ], "cursor": {} }');

EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM bson_aggregation_pipeline('db', 
    '{ "aggregate": "agg_pipeline_movie_screenings", "pipeline": [ { "$lookup": { "from": "agg_pipeline_movie_catalog", "as": "matched_docs", "pipeline": [ { "$count": "efe" } ] } }, { "$sample": { "size": 3 } } ], "cursor": {} }');

EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM bson_aggregation_pipeline('db', 
    '{ "aggregate": "agg_pipeline_movie_screenings", "pipeline": [ { "$lookup": { "from": "agg_pipeline_movie_catalog", "as": "matched_docs", "localField": "title", "foreignField": "title", "pipeline": [ { "$count": "efe" } ] } } ], "cursor": {} }');

SELECT document FROM bson_aggregation_pipeline('db', 
    '{ "aggregate": "agg_pipeline_movie_screenings", "pipeline": [ { "$lookup": { "from": "coll_dne", "as": "matched_docs", "localField": "title", "foreignField": "title", "pipeline": [ { "$count": "efe" } ] } } ], "cursor": {} }');

SELECT document FROM bson_aggregation_pipeline('db', 
    '{ "aggregate": "agg_pipeline_movie_screenings", "pipeline": [ { "$lookup": { "from": "colldne", "pipeline": [], "as": "c" } } ], "cursor": {} }');

BEGIN;
set local citus.enable_local_execution to off;
SELECT document FROM bson_aggregation_pipeline('db', 
    '{ "aggregate": "agg_pipeline_movie_screenings", "pipeline": [ { "$lookup": { "from": "agg_pipeline_movie_catalog", "as": "matched_docs", "localField": "title", "foreignField": "title" } } ], "cursor": {} }');

SELECT document FROM bson_aggregation_pipeline('db', 
    '{ "aggregate": "agg_pipeline_movie_screenings", "pipeline": [ { "$lookup": { "from": "agg_pipeline_movie_catalog", "as": "matched_docs", "pipeline": [ { "$count": "efe" } ] } } ], "cursor": {} }');

SELECT document FROM bson_aggregation_pipeline('db', 
    '{ "aggregate": "agg_pipeline_movie_screenings", "pipeline": [ { "$lookup": { "from": "agg_pipeline_movie_catalog", "as": "matched_docs", "localField": "title", "foreignField": "title", "pipeline": [ { "$count": "efe" } ] } } ], "cursor": {} }');

ROLLBACK;

SELECT documentdb_api.shard_collection('db', 'agg_pipeline_movie_screenings', '{ "_id": "hashed" }', false);

SELECT document FROM bson_aggregation_pipeline('db', 
    '{ "aggregate": "agg_pipeline_movie_screenings", "pipeline": [ { "$lookup": { "from": "agg_pipeline_movie_catalog", "as": "matched_docs", "localField": "title", "foreignField": "title" } } ], "cursor": {} }');

SELECT document FROM bson_aggregation_pipeline('db', 
    '{ "aggregate": "agg_pipeline_movie_screenings", "pipeline": [ { "$lookup": { "from": "agg_pipeline_movie_catalog", "as": "matched_docs", "pipeline": [ { "$count": "efe" } ] } } ], "cursor": {} }');

SELECT document FROM bson_aggregation_pipeline('db', 
    '{ "aggregate": "agg_pipeline_movie_screenings", "pipeline": [ { "$lookup": { "from": "agg_pipeline_movie_catalog", "as": "matched_docs", "localField": "title", "foreignField": "title", "pipeline": [ { "$count": "efe" } ] } } ], "cursor": {} }');

BEGIN;
set local citus.enable_local_execution to off;
SELECT document FROM bson_aggregation_pipeline('db', 
    '{ "aggregate": "agg_pipeline_movie_screenings", "pipeline": [ { "$lookup": { "from": "agg_pipeline_movie_catalog", "as": "matched_docs", "localField": "title", "foreignField": "title" } } ], "cursor": {} }');

SELECT document FROM bson_aggregation_pipeline('db', 
    '{ "aggregate": "agg_pipeline_movie_screenings", "pipeline": [ { "$lookup": { "from": "agg_pipeline_movie_catalog", "as": "matched_docs", "pipeline": [ { "$count": "efe" } ] } } ], "cursor": {} }');

SELECT document FROM bson_aggregation_pipeline('db', 
    '{ "aggregate": "agg_pipeline_movie_screenings", "pipeline": [ { "$lookup": { "from": "agg_pipeline_movie_catalog", "as": "matched_docs", "localField": "title", "foreignField": "title", "pipeline": [ { "$count": "efe" } ] } } ], "cursor": {} }');

ROLLBACK;

SELECT documentdb_api.shard_collection('db', 'agg_pipeline_movie_catalog', '{ "_id": "hashed" }', false);

SELECT document FROM bson_aggregation_pipeline('db', 
    '{ "aggregate": "agg_pipeline_movie_screenings", "pipeline": [ { "$lookup": { "from": "agg_pipeline_movie_catalog", "as": "matched_docs", "localField": "title", "foreignField": "title" } } ], "cursor": {} }');

SELECT document FROM bson_aggregation_pipeline('db', 
    '{ "aggregate": "agg_pipeline_movie_screenings", "pipeline": [ { "$lookup": { "from": "agg_pipeline_movie_catalog", "as": "matched_docs", "pipeline": [ { "$count": "efe" } ] } } ], "cursor": {} }');

SELECT document FROM bson_aggregation_pipeline('db', 
    '{ "aggregate": "agg_pipeline_movie_screenings", "pipeline": [ { "$lookup": { "from": "agg_pipeline_movie_catalog", "as": "matched_docs", "localField": "title", "foreignField": "title", "pipeline": [ { "$count": "efe" } ] } } ], "cursor": {} }');

SELECT document FROM bson_aggregation_pipeline('db', 
    '{ "aggregate": "agg_pipeline_movie_screenings", "pipeline": [ { "$lookup": { "from": "coll_dne", "as": "matched_docs", "localField": "title", "foreignField": "title", "pipeline": [ { "$count": "efe" } ] } } ], "cursor": {} }');

SELECT document FROM bson_aggregation_pipeline('db', 
    '{ "aggregate": "agg_pipeline_movie_screenings", "pipeline": [ { "$lookup": { "from": "colldne", "pipeline": [], "as": "c" } } ], "cursor": {} }');

SELECT document FROM bson_aggregation_pipeline('db', 
    '{ "aggregate": "agg_pipeline_movie_screenings", "pipeline": [ { "$lookup": { "as": "matched_docs", "pipeline": [ { "$documents": [ { "a": 1}, { "a": 2 } ] }  ] } } ], "cursor": {} }');

SELECT document FROM bson_aggregation_pipeline('db', 
    '{ "aggregate": "agg_pipeline_movie_screenings", "pipeline": [ { "$lookup": { "as": "matched_docs",  "foreignField": "foo", "localField": "_id", "pipeline": [ { "$documents": [ { "a": 1}, { "a": 2 } ] }  ] } } ], "cursor": {} }');

-- $lookup.pipeline Negative tests
SELECT document FROM bson_aggregation_pipeline('db', 
    '{ "aggregate": "agg_pipeline_movie_screenings", "pipeline": [ { "$lookup": { "from": "colldne", "pipeline": { "$count": "efe" }, "as": "c" } } ], "cursor": {} }');

SELECT document FROM bson_aggregation_pipeline('db', 
    '{ "aggregate": "agg_pipeline_movie_screenings", "pipeline": [ { "$lookup": { "from": "colldne", "pipeline": [ "$count" ], "as": "c" } } ], "cursor": {} }');

SELECT document FROM bson_aggregation_pipeline('db', 
    '{ "aggregate": "agg_pipeline_movie_screenings", "pipeline": [ { "$lookup": { "from": "colldne", "pipeline": [ { } ], "as": "c" } } ], "cursor": {} }');

SELECT document FROM bson_aggregation_pipeline('db', 
    '{ "aggregate": "agg_pipeline_movie_screenings", "pipeline": [ { "$lookup": { "from": "colldne", "pipeline": [ { "$count": "efe", "$match": { "year": 2018 } } ], "as": "c" } } ], "cursor": {} }');


SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": 1, "pipeline": [ { "$addFields": { "newField" : "1", "a.y": ["p", "q"] } } ], "cursor": {} }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": 1, "pipeline": [ { "$match": { "newField" : "1", "a.y": ["p", "q"] } } ], "cursor": {} }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": 1, "pipeline": [ { "$project": { "newField" : "1", "a.y": ["p", "q"] } } ], "cursor": {} }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": 1, "pipeline": [ { "$skip": 1 } ], "cursor": {} }');


-- test sort behavior on sharded/unsharded
SELECT documentdb_distributed_test_helpers.mask_plan_id_from_distributed_subplan($Q$
EXPLAIN (COSTS OFF) SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate" : "agg_pipeline_movie_catalog", "pipeline" : [ { "$match" : { "$or" : [ { "_id" : { "$lt" : 9999.0 }, "some_other_field" : { "$ne" : 3.0 } }, { "this_predicate_matches_nothing" : true } ] } }, { "$sort" : { "_id" : -1.0 } }, { "$limit" : 1.0 }, { "$project" : { "_id" : 1.0, "b" : { "$round" : "$a" } } } ], "cursor" : {  }, "lsid" : { "id" : { "$binary" : { "base64": "VJmzOaS5R46C4aFkQzrFaQ==", "subType" : "04" } } }, "$db" : "test" }');
$Q$);
SELECT documentdb_distributed_test_helpers.drop_primary_key('db','aggregation_pipeline');
SELECT documentdb_distributed_test_helpers.mask_plan_id_from_distributed_subplan($Q$
EXPLAIN (COSTS OFF) SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate" : "aggregation_pipeline", "pipeline" : [ { "$match" : { "$or" : [ { "_id" : { "$lt" : 9999.0 }, "some_other_field" : { "$ne" : 3.0 } }, { "this_predicate_matches_nothing" : true } ] } }, { "$sort" : { "_id" : -1.0 } }, { "$limit" : 1.0 }, { "$project" : { "_id" : 1.0, "b" : { "$round" : "$a" } } } ], "cursor" : {  }, "lsid" : { "id" : { "$binary" : { "base64": "VJmzOaS5R46C4aFkQzrFaQ==", "subType" : "04" } } }, "$db" : "test" }')
$Q$);


-- unionWith
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline", "pipeline": [ { "$unionWith": "aggregation_pipeline" } ], "cursor": {} }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline", "pipeline": [ { "$unionWith": { "coll": "aggregation_pipeline" } } ], "cursor": {} }');

SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline", "pipeline": [ { "$unionWith": { "coll": "aggregation_pipeline" } }, { "$unionWith": { "coll": "aggregation_pipeline" } }, { "$unionWith": { "coll": "aggregation_pipeline" } } ], "cursor": {} }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline", "pipeline": [ { "$unionWith": { "coll": "aggregation_pipeline" } }, { "$unionWith": { "coll": "aggregation_pipeline" } }, { "$match": { "a": "no" } }], "cursor": {} }');

SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline", "pipeline": [ { "$unionWith": { "coll": "aggregation_pipeline", "pipeline": [ ] } } ], "cursor": {} }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline", "pipeline": [ { "$unionWith": { "coll": "aggregation_pipeline", "pipeline": [ { "$addFields": { "newField" : "1", "a.y": ["p", "q"] } } ] } } ], "cursor": {} }');
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline", "pipeline": [ { "$unionWith": { "coll": "aggregation_pipeline", "pipeline": [ { "$addFields": { "newField" : "1", "a.y": ["p", "q"] } } ] } } ], "cursor": {} }');

SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline", "pipeline": [ { "$unionWith": { "coll": "agg_pipeline_movie_catalog", "pipeline": [ { "$match" : { "$or" : [ { "_id" : { "$lt" : 9999.0 }, "some_other_field" : { "$ne" : 3.0 } } ] } } ] } } ], "cursor": {} }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline", "pipeline": [{ "$match": { "nonexist": { "$exists": true } } }, { "$unionWith": { "coll": "agg_pipeline_movie_catalog", "pipeline": [ { "$match" : { "$or" : [ { "_id" : { "$lt" : 9999.0 }, "some_other_field" : { "$ne" : 3.0 } } ] } } ] } } ], "cursor": {} }');

SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline", "pipeline": [ { "$unionWith": { } } ], "cursor": {} }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline", "pipeline": [ { "$unionWith": { "pipeline": [ ] } } ], "cursor": {} }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline", "pipeline": [ { "$unionWith": { "pipeline": [ { "$match" : { "$or" : [ { "_id" : { "$lt" : 9999.0 }, "some_other_field" : { "$ne" : 3.0 } } ] } } ] } } ], "cursor": {} }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline", "pipeline": [ { "$unionWith": { "pipeline": [ { "$documents": [ { "a": 1 }, { "a": 2 } ]}] } } ], "cursor": {} }');

-- $addFields nested usage
SELECT documentdb_api.insert_one('db','aggregation_pipeline','{ "_id": 100, "movie": "Nebula Drift", "critics": [7, 8, 9], "audience": [8, 7], "bonusPoints": 2 }', NULL);
SELECT documentdb_api.insert_one('db','aggregation_pipeline','{ "_id": 200, "movie": "Quantum Heist", "critics": [6, 6, 7], "audience": [7, 6], "bonusPoints": 3 }', NULL);

SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline", "pipeline": [ { "$match": { "bonusPoints": { "$gte": 0 } } }, { "$addFields": { "totalCritics": { "$sum": "$critics" }, "totalAudience": { "$sum": "$audience" } } }, { "$addFields": { "totalScore": { "$add": [ "$totalCritics", "$totalAudience", "$bonusPoints" ] } } } ], "cursor": {} }');

-- NumberDouble Range+1 and Range-1 Negative test $skip
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline", "pipeline": [ { "$skip": {"$numberDouble" : "-9223372036854776833"} } ], "cursor": {} }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline", "pipeline": [ { "$skip": {"$numberDouble" : "9223372036854775296"} } ], "cursor": {} }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline", "pipeline": [ { "$skip": {"$numberDouble" : "-9223372036854776832"} } ], "cursor": {} }');
-- samplerate
/* insert 100 documents */
/* test unshard case */
DO $$
DECLARE i int;
BEGIN
FOR i IN 1..100 LOOP
PERFORM documentdb_api.insert_one('db', 'agg_pipeline_samplerate', FORMAT('{ "_id": %s }',i)::documentdb_core.bson);
END LOOP;
END;
$$;
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "agg_pipeline_samplerate", "pipeline": [ { "$match": { "$sampleRate": 1 } }, {"$count": "count"} ], "cursor": {} }');
EXPLAIN (COSTS OFF) SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "agg_pipeline_samplerate", "pipeline": [ { "$match": { "$sampleRate": 1 } }, {"$count": "count"} ], "cursor": {} }');

SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "agg_pipeline_samplerate", "pipeline": [ { "$match": { "$sampleRate": 0 } }, {"$count": "count"} ], "cursor": {} }');
EXPLAIN (COSTS OFF) SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "agg_pipeline_samplerate", "pipeline": [ { "$match": { "$sampleRate": 0 } }, {"$count": "count"} ], "cursor": {} }');
/* sampleRate will random select document, use greater than 0 to make sure slice of documents is selected */
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "agg_pipeline_samplerate", "pipeline": [ { "$match": { "$sampleRate": 0.5 } }, { "$count": "numMatches" }, { "$addFields": { "gtZero": { "$gt": ["$numMatches", 0] } } }, {"$project": { "_id": 0, "gtZero": 1 } }], "cursor": {} }');
EXPLAIN (COSTS OFF) SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "agg_pipeline_samplerate", "pipeline": [ { "$match": { "$sampleRate": 0.5 } }, { "$count": "numMatches" }, { "$addFields": { "gtZero": { "$gt": ["$numMatches", 0] } } }, {"$project": { "_id": 0, "gtZero": 1 } }], "cursor": {} }');

/* test shard case */
SELECT documentdb_api.shard_collection('db', 'agg_pipeline_samplerate', '{ "_id": "hashed" }', false);

SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "agg_pipeline_samplerate", "pipeline": [ { "$match": { "$sampleRate": 1 } }, {"$count": "count"} ], "cursor": {} }');
EXPLAIN (COSTS OFF) SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "agg_pipeline_samplerate", "pipeline": [ { "$match": { "$sampleRate": 1 } }, {"$count": "count"} ], "cursor": {} }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "agg_pipeline_samplerate", "pipeline": [ { "$match": { "$sampleRate": 0 } }, {"$count": "count"} ], "cursor": {} }');
EXPLAIN (COSTS OFF) SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "agg_pipeline_samplerate", "pipeline": [ { "$match": { "$sampleRate": 0 } }, {"$count": "count"} ], "cursor": {} }');
/* sampleRate will random select document, use greater than 0 to make sure slice of documents is selected */
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "agg_pipeline_samplerate", "pipeline": [ { "$match": { "$sampleRate": 0.5 } }, { "$count": "numMatches" }, { "$addFields": { "gtZero": { "$gt": ["$numMatches", 0] } } }, {"$project": { "_id": 0, "gtZero": 1 } }], "cursor": {} }');
-- negative samplerate
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "agg_pipeline_samplerate", "pipeline": [ { "$match": { "$sampleRate": -1.23 } }, { "$limit": 1 }, {"$count": "count"} ], "cursor": {} }');

SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "agg_pipeline_samplerate", "pipeline": [ { "$match": { "$sampleRate": null } }, { "$limit": 1 }, {"$count": "count"} ], "cursor": {} }');

SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "agg_pipeline_samplerate", "pipeline": [ { "$match": { "$sampleRate": NaN } }, { "$limit": 1 }, {"$count": "count"} ], "cursor": {} }');

SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "agg_pipeline_samplerate", "pipeline": [ { "$match": { "$sampleRate": Infinity } }, { "$limit": 1 }, {"$count": "count"} ], "cursor": {} }');

SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "agg_pipeline_samplerate", "pipeline": [ { "$match": { "$sampleRate": -Infinity } }, { "$limit": 1 }, {"$count": "count"} ], "cursor": {} }');

SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "agg_pipeline_samplerate", "pipeline": [ { "$match": { "$sampleRate": "0.65" } }, { "$limit": 1 }, {"$count": "count"} ], "cursor": {} }');

SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "agg_pipeline_samplerate", "pipeline": [ { "$match": { "$sampleRate": 10 } }, { "$limit": 1 }, {"$count": "count"} ], "cursor": {} }');

SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "agg_pipeline_samplerate", "pipeline": [ { "$match": { "$sampleRate": false } }, { "$limit": 1 }, {"$count": "count"} ], "cursor": {} }');

-- turn off using new count aggregates
set documentdb.enableNewCountAggregates to off;

SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "agg_pipeline_samplerate", "pipeline": [ { "$match": { "$sampleRate": 1 } }, {"$count": "count"} ], "cursor": {} }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline", "pipeline": [ { "$unwind": "$a.b" }, { "$count": "d" }, { "$addFields": { "e": "$d" } }], "cursor": {} }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline", "pipeline": [ { "$count": "d" }], "cursor": {} }');

EXPLAIN (ANALYZE ON, VERBOSE ON, COSTS OFF, BUFFERS OFF, TIMING OFF, SUMMARY OFF) SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline", "pipeline": [ { "$unwind": "$a.b" }, { "$count": "d" }, { "$addFields": { "e": "$d" } }], "cursor": {} }');
EXPLAIN (ANALYZE ON, VERBOSE ON, COSTS OFF, BUFFERS OFF, TIMING OFF, SUMMARY OFF) SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline", "pipeline": [ { "$count": "d" }], "cursor": {} }');

SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline", "pipeline": [ { "$group": {"_id": null, "count": { "$sum": 1 } } }], "cursor": {} }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline", "pipeline": [ { "$group": {"_id": null, "count": { "$sum": {} } } }], "cursor": {} }');


EXPLAIN (ANALYZE ON, VERBOSE ON, COSTS OFF, BUFFERS OFF, TIMING OFF, SUMMARY OFF) SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline", "pipeline": [ { "$group": {"_id": null, "count": { "$sum": 1 } } }], "cursor": {} }');
EXPLAIN (ANALYZE ON, VERBOSE ON, COSTS OFF, BUFFERS OFF, TIMING OFF, SUMMARY OFF) SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline", "pipeline": [ { "$group": {"_id": null, "count": { "$sum": {} } } }], "cursor": {} }');

EXPLAIN (ANALYZE ON, VERBOSE ON, COSTS OFF, BUFFERS OFF, TIMING OFF, SUMMARY OFF) SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline", "pipeline": [ { "$group": {"_id": "$a", "count": { "$sum": 1 } } }], "cursor": {} }');
EXPLAIN (ANALYZE ON, VERBOSE ON, COSTS OFF, BUFFERS OFF, TIMING OFF, SUMMARY OFF) SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline", "pipeline": [ { "$group": {"_id": "$a", "count": { "$sum": 5 } } }], "cursor": {} }');

set documentdb.enableNewCountAggregates to on;


-- Pipeline directly push to shards if all stages refer to same collection and that is not sharded and is present on the same node as coordinator.

SELECT documentdb_api.insert_one('pipelineDB','agg_pipeline_optimizations','{ "_id": 1, "a": "RANDOM_A", "b": {"c": ["SAMPLE1", "SAMPLE2"], "d": [[1,2], [3, 4]]} }', NULL);
SELECT documentdb_api.insert_one('pipelineDB','agg_pipeline_optimizations','{ "_id": 2, "a": "RANDOM_B", "b": {"c": ["SAMPLE3", "SAMPLE4"], "d": [[5,6], [7, 8]]} }', NULL);

SELECT document FROM bson_aggregation_pipeline('pipelineDB', '{ "aggregate": "agg_pipeline_optimizations", "pipeline": [ { "$match": { "a": "RANDOM_A" } } ] }');
EXPLAIN (VERBOSE ON, COSTS OFF) SELECT document FROM bson_aggregation_pipeline('pipelineDB', '{ "aggregate": "agg_pipeline_optimizations", "pipeline": [ { "$match": { "a": "RANDOM_A" } } ] }');

SELECT document FROM bson_aggregation_pipeline('pipelineDB', '{ "aggregate": "agg_pipeline_optimizations", "pipeline": [ { "$project": { "a" : 1 } } ] }');
EXPLAIN (VERBOSE ON, COSTS OFF) SELECT document FROM bson_aggregation_pipeline('pipelineDB', '{ "aggregate": "agg_pipeline_optimizations", "pipeline": [ { "$project": { "a" : 1 } } ] }');

EXPLAIN (VERBOSE OFF, COSTS OFF) SELECT document FROM bson_aggregation_pipeline('pipelineDB', '{ "aggregate": "agg_pipeline_optimizations", "pipeline": [ { "$merge": "agg_pipeline_optimizations" } ] }');

SELECT documentdb_api.create_collection('pipelineDB', 'agg_pipeline_optimizations_new');
EXPLAIN (VERBOSE OFF, COSTS OFF) SELECT document FROM bson_aggregation_pipeline('pipelineDB', '{ "aggregate": "agg_pipeline_optimizations", "pipeline": [ { "$merge": "agg_pipeline_optimizations_new" } ] }');

EXPLAIN (VERBOSE OFF, COSTS OFF) SELECT document FROM bson_aggregation_pipeline('pipelineDB', '{ "aggregate": "agg_pipeline_optimizations", "pipeline": [ { "$merge": "agg_pipeline_optimizations" } ] }');

-- Shard the collection now
SELECT documentdb_api.shard_collection('pipelineDB', 'agg_pipeline_optimizations', '{ "_id": "hashed" }', false);

SELECT document FROM bson_aggregation_pipeline('pipelineDB', '{ "aggregate": "agg_pipeline_optimizations", "pipeline": [ { "$match": { "a": "RANDOM_A" } } ] }');
EXPLAIN (VERBOSE ON, COSTS OFF) SELECT document FROM bson_aggregation_pipeline('pipelineDB', '{ "aggregate": "agg_pipeline_optimizations", "pipeline": [ { "$match": { "a": "RANDOM_A" } } ] }');

SELECT document FROM bson_aggregation_pipeline('pipelineDB', '{ "aggregate": "agg_pipeline_optimizations", "pipeline": [ { "$project": { "a" : 1 } } ] }');
EXPLAIN (VERBOSE ON, COSTS OFF) SELECT document FROM bson_aggregation_pipeline('pipelineDB', '{ "aggregate": "agg_pipeline_optimizations", "pipeline": [ { "$project": { "a" : 1 } } ] }');


-- Test limit on aggregation stages
CREATE OR REPLACE FUNCTION check_aggregation_stages_limit(num_stages int)
RETURNS boolean AS $fn$
DECLARE
    pipeline jsonb := '[]'::jsonb;
    aggregate_spec jsonb;
BEGIN
    -- Create a json array of form [{$match: {}}] with num_stages elements
    FOR i IN 1..num_stages LOOP
        pipeline := pipeline || '{"$match": {}}'::jsonb;
    END LOOP;
    aggregate_spec := jsonb_build_object('aggregate', 'dummy', 'pipeline', pipeline);

    PERFORM document FROM bson_aggregation_pipeline('db', aggregate_spec::text::documentdb_core.bson);

    RETURN true;
END;
$fn$ LANGUAGE plpgsql;

CREATE OR REPLACE FUNCTION check_lookup_stages_limit(num_inline_stages int, num_non_inline_stages int)
RETURNS boolean AS $fn$
DECLARE
    pipeline jsonb := '[]'::jsonb;
    aggregate_spec jsonb;
    lookup_spec jsonb;
BEGIN
    -- Create a json array of form [{$match: {}}] with num_stages elements
    FOR i IN 1..num_inline_stages LOOP
        pipeline := pipeline || '{"$match": {  }}'::jsonb;
    END LOOP;
    FOR i IN 1..num_non_inline_stages LOOP
        pipeline := pipeline || '{"$count": "dummy" }'::jsonb;
    END LOOP;
    
    lookup_spec := jsonb_build_object('$lookup', jsonb_build_object('from', 'dummy', 'as', 'dummy', 'localField', 'dummy', 'foreignField', 'dummy', 'pipeline', pipeline));
    aggregate_spec := jsonb_build_object('aggregate', 'dummy', 'pipeline', '[]'::jsonb || lookup_spec);

    PERFORM document FROM bson_aggregation_pipeline('db', aggregate_spec::text::documentdb_core.bson);

    RETURN true;
END;
$fn$ LANGUAGE plpgsql;

SELECT check_aggregation_stages_limit(100);
SELECT check_aggregation_stages_limit(1000);
SELECT check_aggregation_stages_limit(1001);

SELECT check_lookup_stages_limit(1, 1);
SELECT check_lookup_stages_limit(1000, 0);
SELECT check_lookup_stages_limit(0, 1000);
SELECT check_lookup_stages_limit(500, 500);
SELECT check_lookup_stages_limit(1000, 1);
SELECT check_lookup_stages_limit(1, 1000);

SET documentdb.aggregation_stages_limit = 1005;
SELECT check_aggregation_stages_limit(1001);
SELECT check_lookup_stages_limit(1000, 1);
SELECT check_lookup_stages_limit(1, 1000);
RESET documentdb.aggregation_stages_limit;

-- $lookup and $unwind stage combined when null results need to be preserved
SELECT documentdb_api.insert_one('db','lookup_directors','{ "_id": 1, "name": "Alex Veridian" }', NULL);
SELECT documentdb_api.insert_one('db','lookup_directors','{ "_id": 2, "name": "Morgan Slate" }', NULL);
SELECT documentdb_api.insert_one('db','lookup_movies','{ "_id": 1, "title": "Shadow Horizon", "director": "Alex Veridian" }', NULL);
SELECT documentdb_api.insert_one('db','lookup_movies','{ "_id": 2, "title": "Neon Abyss", "director": "Morgan Slate" }', NULL);
SELECT documentdb_api.insert_one('db','lookup_movies','{ "_id": 3, "title": "Celestial Rift", "director": "Alex Veridian" }', NULL);

SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "lookup_movies", "pipeline": [ { "$lookup": { "from": "lookup_directors", "localField": "director", "foreignField": "name", "as": "director_info" } }, { "$unwind": { "path": "$director_info", "preserveNullAndEmptyArrays": true } }, { "$match": { "title": "Celestial Rift" } } ], "cursor": {} }');
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "lookup_movies", "pipeline": [ { "$lookup": { "from": "lookup_directors", "localField": "director", "foreignField": "name", "as": "director_info" } }, { "$unwind": { "path": "$director_info", "preserveNullAndEmptyArrays": true } }, { "$match": { "title": "Celestial Rift" } } ], "cursor": {} }');