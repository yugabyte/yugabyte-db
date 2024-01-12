SET search_path TO helio_api,helio_core;

SET helio_api.next_collection_id TO 4000;
SET helio_api.next_collection_index_id TO 4000;

SELECT helio_api.insert_one('db','agg_facet_group','{ "_id": 1, "a": { "b": 1, "c": 1} }', NULL);
SELECT helio_api.insert_one('db','agg_facet_group','{ "_id": 2, "a": { "b": 1, "c": 2} }', NULL);
SELECT helio_api.insert_one('db','agg_facet_group','{ "_id": 3, "a": { "b": 1, "c": 3} }', NULL);
SELECT helio_api.insert_one('db','agg_facet_group','{ "_id": 4, "a": { "b": 2, "c": 1} }', NULL);
SELECT helio_api.insert_one('db','agg_facet_group','{ "_id": 5, "a": { "b": 2, "c": 2} }', NULL);
SELECT helio_api.insert_one('db','agg_facet_group','{ "_id": 6, "a": { "b": 2, "c": 3} }', NULL);
SELECT helio_api.insert_one('db','agg_facet_group','{ "_id": 7, "a": { "b": 3, "c": 1} }', NULL);
SELECT helio_api.insert_one('db','agg_facet_group','{ "_id": 8, "a": { "b": 3, "c": 2} }', NULL);
SELECT helio_api.insert_one('db','agg_facet_group','{ "_id": 9, "a": { "b": 3, "c": 3} }', NULL);

SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "agg_facet_group", "pipeline": [ { "$addFields": {"name": "$a.c"} }, { "$sort": { "a.b": 1, "name" : 1 } }, { "$facet": { "facet1" : [ { "$group": { "_id": "$a.b", "first": { "$first" : "$name" } } } ], "facet2" : [ { "$group": { "_id": "$a.b", "last": { "$last" : "$name" }}}]}} ] }');

SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "agg_facet_group", "pipeline": [ { "$addFields": {"name": "$a.c"} }, { "$sort": { "a.b": 1, "name" : 1 } },  { "$group": { "_id": "$a.b", "first": { "$first" : "$name" }, "last": { "$last": "$name" } } } ] }');

SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "agg_facet_group", "pipeline": [ { "$addFields": {"name": "$a.c"} }, { "$sort": { "a.b": 1, "name" : -1 } },  { "$group": { "_id": "$a.b", "first": { "$first" : "$name" }, "last": { "$last": "$name" } } } ] }');

SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "agg_facet_group", "pipeline": [ { "$addFields": {"name": "$a.c"} }, { "$sort": { "a.b": -1, "name" : 1 } },  { "$group": { "_id": "$a.b", "first": { "$first" : "$name" }, "last": { "$last": "$name" } } } ] }');

SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "agg_facet_group", "pipeline": [ { "$addFields": {"name": "$a.c"} }, { "$sort": { "a.b": -1, "name" : -1 } },  { "$group": { "_id": "$a.b", "first": { "$first" : "$name" }, "last": { "$last": "$name" } } } ] }');

SELECT helio_api.shard_collection('db', 'agg_facet_group', '{ "_id": "hashed" }', false);

SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "agg_facet_group", "pipeline": [ { "$addFields": {"name": "$a.c"} }, { "$sort": { "a.b": 1, "name" : 1 } }, { "$facet": { "facet1" : [ { "$group": { "_id": "$a.b", "first": { "$first" : "$name" } } } ], "facet2" : [ { "$group": { "_id": "$a.b", "last": { "$last" : "$name" }}}]}} ] }');

SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "agg_facet_group", "pipeline": [ { "$addFields": {"name": "$a.c"} }, { "$sort": { "a.b": 1, "name" : 1 } },  { "$group": { "_id": "$a.b", "first": { "$first" : "$name" }, "last": { "$last": "$name" } } } ] }');

SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "agg_facet_group", "pipeline": [ { "$addFields": {"name": "$a.c"} }, { "$sort": { "a.b": 1, "name" : -1 } },  { "$group": { "_id": "$a.b", "first": { "$first" : "$name" }, "last": { "$last": "$name" } } } ] }');

SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "agg_facet_group", "pipeline": [ { "$addFields": {"name": "$a.c"} }, { "$sort": { "a.b": -1, "name" : 1 } },  { "$group": { "_id": "$a.b", "first": { "$first" : "$name" }, "last": { "$last": "$name" } } } ] }');

SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "agg_facet_group", "pipeline": [ { "$addFields": {"name": "$a.c"} }, { "$sort": { "a.b": -1, "name" : -1 } },  { "$group": { "_id": "$a.b", "first": { "$first" : "$name" }, "last": { "$last": "$name" } } } ] }');