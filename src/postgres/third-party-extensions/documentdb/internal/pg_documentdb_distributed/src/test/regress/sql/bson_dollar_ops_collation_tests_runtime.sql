SET search_path to documentdb_core,documentdb_api,documentdb_api_catalog,pg_catalog;
SET citus.next_shard_id TO 7990000;
SET documentdb.next_collection_id TO 7990;
SET documentdb.next_collection_index_id TO 7990;

-- (1) insert some docs
SELECT documentdb_api.insert_one('db', 'ci_search', '{ "_id": 1, "a": "Cat" }');
SELECT documentdb_api.insert_one('db', 'ci_search', '{ "_id": 2, "a": "dog" }');
SELECT documentdb_api.insert_one('db', 'ci_search', '{ "_id": 3, "a": "cat" }');
SELECT documentdb_api.insert_one('db', 'ci_search', '{ "_id": 4, "a": "Dog" }');
SELECT documentdb_api.insert_one('db', 'ci_search', '{ "_id": 5, "a": "caT" }');
SELECT documentdb_api.insert_one('db', 'ci_search', '{ "_id": 6, "a": "doG" }');
SELECT documentdb_api.insert_one('db', 'ci_search', '{ "_id": 7, "a": "goat" }');
SELECT documentdb_api.insert_one('db', 'ci_search', '{ "_id": 8, "a": "Goat" }');
SELECT documentdb_api.insert_one('db', 'ci_search', '{ "_id": 9, "b": "Cat" }');
SELECT documentdb_api.insert_one('db', 'ci_search', '{ "_id": 10, "b": "dog" }');
SELECT documentdb_api.insert_one('db', 'ci_search', '{ "_id": 11, "b": "cat" }');
SELECT documentdb_api.insert_one('db', 'ci_search', '{ "_id": 12, "b": "Dog" }');
SELECT documentdb_api.insert_one('db', 'ci_search', '{ "_id": 13, "b": "caT", "a" : "raBbIt" }');
SELECT documentdb_api.insert_one('db', 'ci_search', '{ "_id": 14, "b": "doG" }');
SELECT documentdb_api.insert_one('db', 'ci_search', '{ "_id": 15, "b": "goat" }');
SELECT documentdb_api.insert_one('db', 'ci_search', '{ "_id": 16, "b": "Goat" }');
SELECT documentdb_api.insert_one('db', 'ci_search', '{ "_id": 17, "a": ["Cat", "CAT", "dog"] }');
SELECT documentdb_api.insert_one('db', 'ci_search', '{ "_id": 18, "a": ["dog", "cat", "CAT"] }');
SELECT documentdb_api.insert_one('db', 'ci_search', '{ "_id": 19, "a": ["cat", "rabbit", "bAt"] }');
SELECT documentdb_api.insert_one('db', 'ci_search', '{ "_id": 20, "a": ["Cat"] }');
SELECT documentdb_api.insert_one('db', 'ci_search', '{ "_id": 21, "a": ["dog"] }');
SELECT documentdb_api.insert_one('db', 'ci_search', '{ "_id": 22, "a": ["cat"] }');
SELECT documentdb_api.insert_one('db', 'ci_search', '{ "_id": 23, "a": ["CAT"] }');
SELECT documentdb_api.insert_one('db', 'ci_search', '{ "_id": 24, "a": ["cAt"] }');
SELECT documentdb_api.insert_one('db', 'ci_search', '{ "_id": 25, "a": { "b" : "cAt"} }');
SELECT documentdb_api.insert_one('db', 'ci_search', '{ "_id": 26, "a": [{ "b": "CAT"}] }');

SET documentdb_core.enableCollation TO off;

-- With collation off and colation string in find and aggregate commands should not throw an error
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "ci_search", "pipeline": [ { "$sort": { "_id": 1 } }, { "$match": { "a": { "$eq": "cat" } } } ], "cursor": {}, "collation": { "locale": "en", "strength" : 1}  }');
SELECT document FROM bson_aggregation_find('db', '{ "find": "ci_search", "filter": { "b": { "$eq": "cat" } }, "sort": { "_id": 1 }, "skip": 0, "limit": 5, "collation": { "locale": "en", "strength" : 1} }');

SET documentdb_core.enableCollation TO on;

-- (2) Find query unsharded collection
SELECT document FROM bson_aggregation_find('db', '{ "find": "ci_search", "filter": { "a": { "$eq": "cat" } }, "sort": { "_id": 1 }, "skip": 0, "limit": 5, "collation": { "locale": "en", "strength" : 1} }');
EXPLAIN(VERBOSE ON, COSTS OFF) SELECT document FROM bson_aggregation_find('db', '{ "find": "ci_search", "filter": { "a": { "$eq": "cat" } }, "sort": { "_id": 1 }, "skip": 0, "limit": 5, "collation": { "locale": "en", "strength" : 1} }');

SELECT document FROM bson_aggregation_find('db', '{ "find": "ci_search", "filter": { "b": { "$eq": "cat" } }, "sort": { "_id": 1 }, "skip": 0, "limit": 5, "collation": { "locale": "en", "strength" : 1} }');
EXPLAIN(VERBOSE ON, COSTS OFF) SELECT document FROM bson_aggregation_find('db', '{ "find": "ci_search", "filter": { "b": { "$eq": "cat" } }, "sort": { "_id": 1 }, "skip": 0, "limit": 5, "collation": { "locale": "en", "strength" : 1} }');

SELECT document FROM bson_aggregation_find('db', '{ "find": "ci_search", "filter": { "$or" : [{ "a": { "$eq": "cat" } }, { "a": { "$eq": "DOG" } }] }, "sort": { "_id": 1 }, "skip": 0, "limit": 5, "collation": { "locale": "en", "strength" : 1} }');
SELECT document FROM bson_aggregation_find('db', '{ "find": "ci_search", "filter": { "$or" : [{ "a": { "$eq": "cat" } }, { "b": { "$eq": "DOG" } }] }, "sort": { "_id": 1 }, "skip": 0, "limit": 10, "collation": { "locale": "en", "strength" : 1 } }');
SELECT document FROM bson_aggregation_find('db', '{ "find": "ci_search", "filter": { "a": { "$all": ["cAt", "DOG"] } }, "skip": 0, "limit":
 5, "collation": { "locale": "en", "strength": 1} }');
SELECT document FROM bson_aggregation_find('db', '{ "find": "ci_search", "filter": { "a" : {"$in" : ["cat", "DOG" ] }}, "sort": { "_id": 1 }, "skip": 0, "limit": 100, "collation": { "locale": "en", "strength" : 5} }');
SELECT document FROM bson_aggregation_find('db', '{ "find": "ci_search", "filter": { "a" : {"$in" : ["cat", "DOG" ] }}, "sort": { "_id": 1 }, "skip": 0, "limit": 100, "collation": { "locale": "en", "strength" : 1} }');

-- range query without index on path "a"
SELECT document FROM bson_aggregation_find('db', '{ "find": "ci_search", "filter": { "a": { "$gt": "CAT" }, "a" : {"$lt" : "RABBIT"} }, "collation": { "locale": "en", "strength" : 1.93 } }');
EXPLAIN(VERBOSE ON, COSTS OFF) SELECT document FROM bson_aggregation_find('db', '{ "find": "ci_search", "filter": { "a": { "$gt": "CAT" }, "a" : {"$lt" : "RABBIT"} }, "collation": { "locale": "en", "strength" : 1.93 } }');


-- (3) Shard collection
SELECT documentdb_api.shard_collection('db', 'ci_search', '{ "_id": "hashed" }', false);


-- (4) Find query sharded collection
BEGIN;
SET LOCAL documentdb_core.enablecollation TO on;
SELECT document FROM bson_aggregation_find('db', '{ "find": "ci_search", "filter": { "a": { "$eq": "cat" } }, "sort": { "_id": 1 }, "skip": 0, "limit": 5, "collation": { "locale": "en", "strength" : 1} }');
END; 
BEGIN;
SET LOCAL documentdb_core.enablecollation TO on;
EXPLAIN(VERBOSE ON, COSTS OFF) SELECT document FROM bson_aggregation_find('db', '{ "find": "ci_search", "filter": { "a": { "$eq": "cat" } }, "sort": { "_id": 1 }, "skip": 0, "limit": 5, "collation": { "locale": "en", "strength" : 1} }');
END;

BEGIN;
SET LOCAL documentdb_core.enablecollation TO on;
SELECT document FROM bson_aggregation_find('db', '{ "find": "ci_search", "filter": { "b": { "$eq": "cat" } }, "sort": { "_id": 1 }, "skip": 0, "limit": 5, "collation": { "locale": "en", "strength" : 1} }');
END;
BEGIN;
SET LOCAL documentdb_core.enablecollation TO on;
EXPLAIN(VERBOSE ON, COSTS OFF) SELECT document FROM bson_aggregation_find('db', '{ "find": "ci_search", "filter": { "b": { "$eq": "cat" } }, "sort": { "_id": 1 }, "skip": 0, "limit": 5, "collation": { "locale": "en", "strength" : 1} }');
END;

-- elemMatch with collation
BEGIN;
SET LOCAL documentdb_core.enablecollation TO on;
SELECT document FROM bson_aggregation_find('db', '{ "find": "ci_search", "filter": { "a": { "$elemMatch": {"$eq": "cAt"} } }, "skip": 0, "limit": 7, "collation": { "locale": "en", "strength" : 1} }');
SELECT document FROM bson_aggregation_find('db', '{ "find": "ci_search", "filter": { "a": { "$elemMatch": {"$gt": "cAt"} } }, "skip": 0, "limit": 7, "collation": { "locale": "en", "strength" : 1} }');
SELECT document FROM bson_aggregation_find('db', '{ "find": "ci_search", "filter": { "a": { "$elemMatch": {"$lt": "cAt"} } }, "skip": 0, "limit": 7, "collation": { "locale": "en", "strength" : 1} }');
SELECT document FROM bson_aggregation_find('db', '{ "find": "ci_search", "filter": { "a": { "$elemMatch": {"$eq": "cAt", "gte" : "BAT"} } }, "skip": 0, "limit": 7, "collation": { "locale": "en", "strength" : 1} }');
END;

-- (5) Aggregation queries sharded collection
BEGIN;
SET LOCAL documentdb_core.enablecollation TO on;
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "ci_search", "pipeline": [ { "$sort": { "_id": 1 } }, { "$match": { "a": { "$eq": "cat" } } } ], "cursor": {}, "collation": { "locale": "en", "strength" : 1}  }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "ci_search", "pipeline": [ { "$match": { "a": { "$gt": "DOG" } } }], "cursor": {}, "collation": { "locale": "en", "strength" : 1} }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "ci_search", "pipeline": [ { "$match": { "a": { "$eq": "RABBIT" } } }, { "$project": { "b": 1 } }], "cursor": {}, "collation": { "locale": "en", "strength" : 1} }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "ci_search", "pipeline": [ { "$match": { "_id": { "$gt": 1 } } }, { "$project": { "b": 1, "c": "$a", "_id": 0 } }, { "$match": { "c": { "$eq": "rAbBiT" } } }], "cursor": {}, "collation": { "locale": "en", "strength" : 1} }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "ci_search", "pipeline": [ { "$unwind": "$a" },  {"$match": { "a": { "$gt": "POP", "$lt": "TOP" } } } ], "cursor": {}, "collation": { "locale": "en", "strength" : 1} }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "ci_search", "pipeline": [ { "$match": { "a": { "$gte": "hobbit" } } }, { "$unwind": "$a" } ], "cursor": {}, "collation": { "locale": "en", "strength" : 1} }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "ci_search", "pipeline": [ { "$addFields": { "x": "mANgO" } }, { "$addFields": { "Y": "TANGO" } }, { "$match": { "$and" : [{ "a": { "$gte": "POMELO" }}, { "x": { "$eq": "MANGO" }}]}} ], "cursor": {}, "collation": { "locale": "en", "strength" : 1} }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "ci_search", "pipeline": [ { "$addFields": { "e": {  "f": "$a" } } }, { "$replaceRoot": { "newRoot": "$e" } }, { "$match" : { "f": { "$elemMatch": {"$eq": "cAt"} } } } ],
 "cursor": {}, "collation": { "locale": "en", "strength" : 1} }');
SELECT document FROM bson_aggregation_find('db', '{ "find": "ci_search", "filter": { "a" : {"$in" : ["cat", "DOG" ] }}, "sort": { "_id": 1 }, "skip": 0, "limit": 100, "collation": { "locale": "en", "strength" : 5} }');
SELECT document FROM bson_aggregation_find('db', '{ "find": "ci_search", "filter": { "a" : {"$in" : ["cat", "DOG" ] }}, "sort": { "_id": 1 }, "skip": 0, "limit": 100, "collation": { "locale": "en", "strength" : 1} }');
END;

BEGIN;
SET LOCAL documentdb_core.enablecollation TO on;
EXPLAIN(VERBOSE ON, COSTS OFF)SELECT document FROM bson_aggregation_find('db', '{ "find": "ci_search", "filter": { "a" : {"$in" : [[{ "b" : "caT"}], [{ "c" : "caT"}]] }}, "sort": { "_id": 1 }, "skip": 0, "limit": 100, "collation": { "locale": "en", "strength" : 1} }');
END;

-- (6) currently unsupported scenarions:
-- unsupported: $bucket
SELECT document FROM bson_aggregation_pipeline('db', 
'{
    "aggregate": "ci_search",
    "pipeline": [
        {
            "$bucket": {
                "groupBy": "$price",
                "boundaries": [0, 10, 20, 30],
                "default": "Other",
                "output": {
                    "categoryMatch": {
                        "$sum": {
                            "$cond": [
                                { "$eq": ["$a", "PETS"] },
                                1,
                                0
                            ]
                        }
                    }
                }
            }
        }
    ],
    "collation": { "locale": "en", "strength": 1 }
}');

-- unsupported: $geoNear
SELECT document FROM bson_aggregation_pipeline('db',
'{ "aggregate": "ci_search",
   "pipeline": [
     {
       "$geoNear": {
         "near": { "type": "Point", "coordinates": [ 0 , 10 ] },
         "distanceField": "dist.calculated",
         "maxDistance": 2,
         "query": { "a": "cAT" }
       }
     }
   ],
   "collation": { "locale": "en", "strength": 1 }
}');

-- unsupported: $fill
SELECT document FROM bson_aggregation_pipeline('db', 
'{
    "aggregate": "ci_search",
    "pipeline": [
        {
            "$fill": {
                "sortBy": { "timestamp": 1 },
                "partitionBy": "$status",
                "output": {
                     "$cond": {
                        "if": { "$eq": ["$a", "cAt"] },
                        "then": { "type": "feline" },
                        "else": { "type": "other" }
                      }
                }
            }
        }
    ],
    "collation": { "locale": "en", "strength": 1 }
}');

-- unsupported: $group
SELECT document FROM bson_aggregation_pipeline('db',
'{ "aggregate": "ci_search",
   "pipeline": [
     { "$group": {
         "_id": "$a",
         "set": { "$addToSet": "$a" }
     }}
   ],
   "collation": { "locale": "en", "strength": 1 }
}');

-- unsupported: $setWindowFields
SELECT document FROM bson_aggregation_pipeline('db',
'{ "aggregate": "ci_search",
   "pipeline": [
     { "$setWindowFields": {
         "sortBy": { "_id": 1 },
         "output": {
             "total": { "$eq": ["$a", "cAt"] }
         }
     }}
   ],
   "collation": { "locale": "en", "strength": 1 }
}');

-- unsupported: $sortByCount
SELECT document FROM bson_aggregation_pipeline('db',
'{ "aggregate": "ci_search",
   "pipeline": [
     { "$sortByCount": {
         "input": "$a",
         "as": "a",
         "by": { "$cond": { 
           "if": { "$eq": [ "$a", "caT" ] }, 
           "then": [{"x": 30}] , 
           "else": [{"x": 30}] }}
     }}
   ],
   "collation": { "locale": "en", "strength": 1 }
}');

-- (6.B)
BEGIN;
SET LOCAL documentdb_core.enablecollation TO on;
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "ci_search", "pipeline": [ { "$sort": { "_id": 1 } }, { "$addFields": { "e": {  "f": "$a" } } }, { "$replaceRoot": { "newRoot": "$e" } }, { "$match" : { "f": { "$elemMatch": {"$eq": "cAt"} } } }, {"$project": { "items" : { "$filter" : { "input" : "$f", "as" : "animal", "cond" : { "$eq" : ["$$animal", "CAT"] } }} }} ],
 "cursor": {}, "collation": { "locale": "en", "strength" : 1} }');
END;

-- (7) Insert More docs
SELECT documentdb_api.insert_one('db', 'ci_search2', '{ "_id": 1, "a": "Cat" }');
SELECT documentdb_api.insert_one('db', 'ci_search2', '{ "_id": 2, "a": "dog" }');
SELECT documentdb_api.insert_one('db', 'ci_search2', '{ "_id": 3, "a": "cat" }');
SELECT documentdb_api.insert_one('db', 'ci_search2', '{ "_id": 4, "a": "CaT" }');
SELECT documentdb_api.insert_one('db', 'ci_search2', '{ "_id": 5, "b": "Dog" }');
SELECT documentdb_api.insert_one('db', 'ci_search2', '{ "_id": 6, "b": "DoG" }');

-- (8) Query results with different collations
SELECT document FROM bson_aggregation_find('db', '{ "find": "ci_search2", "filter": { "a": { "$eq": "cat" } }, "sort": { "_id": 1 }, "skip": 0, "limit": 5, "collation": { "locale": "en", "strength" : 1} }');
SELECT document FROM bson_aggregation_find('db', '{ "find": "ci_search2", "filter": { "$or" : [{ "a": { "$eq": "cat" } }, { "b": { "$eq": "DOG" } }] }, "sort": { "_id": 1 }, "skip": 0, "limit": 5, "collation": { "locale": "en", "strength" : 1} }');
SELECT document FROM bson_aggregation_find('db', '{ "find": "ci_search2", "filter": { "$or" : [{ "a": { "$eq": "cat" } }, { "b": { "$eq": "DOG" } }] }, "sort": { "_id": 1 }, "skip": 0, "limit": 5, "collation": { "locale": "en", "strength" : 3, "caseLevel": true, "caseFirst": "off", "numericOrdering": true } }');
SELECT document FROM bson_aggregation_find('db', '{ "find": "ci_search2", "filter": { "$or" : [{ "a": { "$eq": "cat" } }, { "b": { "$eq": "DOG" } }] }, "sort": { "_id": 1 }, "skip": 0, "limit": 5, "collation": { "locale": "en", "strength" : 1.93 } }');

-- (8) a. collation has no effect on $regex
SELECT document FROM bson_aggregation_find('db', '{ "find": "ci_search2", "filter": { "a": { "$regex": "^c", "$options": "" } }, "sort": { "_id": 1 }, "skip": 0, "limit": 5, "collation": { "locale": "en", "strength" : 1 } }');

-- (9) Error message Tests 
SELECT document FROM bson_aggregation_find('db', '{ "find": "ci_search2", "filter": { "$or" : [{ "a": { "$eq": "cat" } }, { "b": { "$eq": "DOG" } }] }, "sort": { "_id": 1 }, "skip": 0, "limit": 5, "collation": { "locale": "en", "strength" : 1, "caseLevel": true, "caseFirst": "upper", "numericOrdering": true, "alternate": "none"} }');
SELECT document FROM bson_aggregation_find('db', '{ "find": "ci_search2", "filter": { "$or" : [{ "a": { "$eq": "cat" } }, { "b": { "$eq": "DOG" } }] }, "sort": { "_id": 1 }, "skip": 0, "limit": 5, "collation": { "locale": "en_DB", "strength" : 1, "caseLevel": true, "caseFirst": "upper", "numericOrdering": true, "alternate": "shifted"} }');
SELECT document FROM bson_aggregation_find('db', '{ "find": "ci_search2", "filter": { "$or" : [{ "a": { "$eq": "cat" } }, { "b": { "$eq": "DOG" } }] }, "sort": { "_id": 1 }, "skip": 0, "limit": 5, "collation": { "locale": "en", "strength" : 1, "caseLevel": true, "caseFirst": "bad", "numericOrdering": true} }');
SELECT document FROM bson_aggregation_find('db', '{ "find": "ci_search2", "filter": { "$or" : [{ "a": { "$eq": "cat" } }, { "b": { "$eq": "DOG" } }] }, "sort": { "_id": 1 }, "skip": 0, "limit": 5, "collation": { "locale": "en", "strength" : 0, "caseLevel": true, "caseFirst": "bad", "numericOrdering": true} }');
SELECT document FROM bson_aggregation_find('db', '{ "find": "ci_search2", "filter": { "$or" : [{ "a": { "$eq": "cat" } }, { "b": { "$eq": "DOG" } }] }, "sort": { "_id": 1 }, "skip": 0, "limit": 5, "collation": { "locale": "en", "strength" : -1, "caseLevel": true, "caseFirst": "bad", "numericOrdering": true} }');
SELECT document FROM bson_aggregation_find('db', '{ "find": "ci_search2", "filter": { "$or" : [{ "a": { "$eq": "cat" } }, { "b": { "$eq": "DOG" } }] }, "sort": { "_id": 1 }, "skip": 0, "limit": 5, "collation": { "locale": "en", "strength" : 6, "caseLevel": true, "caseFirst": "bad", "numericOrdering": true} }');
SELECT document FROM bson_aggregation_find('db', '{ "find": "ci_search2", "filter": { "$or" : [{ "a": { "$eq": "cat" } }, { "b": { "$eq": "DOG" } }] }, "sort": { "_id": 1 }, "skip": 0, "limit": 5, "collation": { "locale": "abcd", "strength" : 1, "caseLevel": true, "caseFirst": "upper", "numericOrdering": true} }');
SELECT document FROM bson_aggregation_find('db', '{ "find": "ci_search2", "filter": { "$or" : [{ "a": { "$eq": "cat" } }, { "b": { "$eq": "DOG" } }] }, "sort": { "_id": 1 }, "skip": 0, "limit": 5, "collation": { "locale": "fr_FR", "strength" : 1, "caseLevel": true, "caseFirst": "lower", "numericOrdering": true} }');
SELECT document FROM bson_aggregation_find('db', '{ "find": "ci_search2", "filter": { "$or" : [{ "a": { "$eq": "cat" } }, { "b": { "$eq": "DOG" } }] }, "sort": { "_id": 1 }, "skip": 0, "limit": 5, "collation": { "locale": "en", "strength" : 1, "caseLevel": true, "caseFirst": "upper", "numericOrdering": true, "alternate": "shifted", "backwards" : "0"} }');
SELECT document FROM bson_aggregation_find('db', '{ "find": "ci_search2", "filter": { "$or" : [{ "a": { "$eq": "cat" } }, { "b": { "$eq": "DOG" } }] }, "sort": { "_id": 1 }, "skip": 0, "limit": 5, "collation": { "locale": "en", "strength" : 1, "caseLevel": true, "caseFirst": "lower", "numericOrdering": true, "alternate": "non-ignorable", "backwards" : true, "normalization" : 1} }');
SELECT document FROM bson_aggregation_find('db', '{ "find": "ci_search2", "filter": { "$or" : [{ "a": { "$eq": "cat" } }, { "b": { "$eq": "DOG" } }] }, "sort": { "_id": 1 }, "skip": 0, "limit": 5, "collation": { "locale": "en", "strength" : 1, "caseLevel": true, "caseFirst": "lower", "numericOrdering": true, "alternate": "non-ignorable", "backwards" : true, "normalization" : true} }');
SELECT document FROM bson_aggregation_find('db', '{ "find": "ci_search2", "filter": { "$or" : [{ "a": { "$eq": "cat" } }, { "b": { "$eq": "DOG" } }] }, "sort": { "_id": 1 }, "skip": 0, "limit": 5, "collation": { "locale": "en", "strength" : 0.9 } }');



-- (10) collation variations
SELECT document FROM bson_aggregation_find('db', '{ "find": "ci_search2", "filter": { "$or" : [{ "a": { "$eq": "cat" } }, { "b": { "$eq": "DOG" } }] }, "sort": { "_id": 1 }, "skip": 0, "limit": 5, "collation": { "locale": "en", "strength" : 2, "caseLevel": false, "caseFirst": "lower", "numericOrdering": true } }');
SELECT document FROM bson_aggregation_find('db', '{ "find": "ci_search2", "filter": { "$or" : [{ "a": { "$eq": "cat" } }, { "b": { "$eq": "DOG" } }] }, "sort": { "_id": 1 }, "skip": 0, "limit": 5, "collation": { "locale": "en", "strength" : 1, "caseLevel": true, "caseFirst": "lower", "numericOrdering": true} }');
SELECT document FROM bson_aggregation_find('db', '{ "find": "ci_search2", "filter": { "$or" : [{ "a": { "$eq": "cat" } }, { "b": { "$eq": "DOG" } }] }, "sort": { "_id": 1 }, "skip": 0, "limit": 5, "collation": { "locale": "fr", "strength" : 1, "caseLevel": false, "caseFirst": "lower", "numericOrdering": true} }');
SELECT document FROM bson_aggregation_find('db', '{ "find": "ci_search2", "filter": { "$or" : [{ "a": { "$eq": "cat" } }, { "b": { "$eq": "DOG" } }] }, "sort": { "_id": 1 }, "skip": 0, "limit": 5, "collation": { "locale": "de", "strength" : 1, "caseLevel": false, "caseFirst": "lower", "numericOrdering": true} }');
SELECT document FROM bson_aggregation_find('db', '{ "find": "ci_search2", "filter": { "$or" : [{ "a": { "$eq": "cat" } }, { "b": { "$eq": "DOG" } }] }, "sort": { "_id": 1 }, "skip": 0, "limit": 5, "collation": { "locale": "bn", "strength" : 1, "caseLevel": false, "caseFirst": "lower", "numericOrdering": true} }');

-- collation with sort/order by
SELECT documentdb_api.insert_one('db', 'coll_order_tests0', '{"_id": "CaT", "a": "cat", "b": "CaT"}');
SELECT documentdb_api.insert_one('db', 'coll_order_tests0', '{"_id": "CAt", "a": "cat", "b": "CAt"}');
SELECT documentdb_api.insert_one('db', 'coll_order_tests0', '{"_id": "CAT", "a": "cat", "b": "CAT"}');
SELECT documentdb_api.insert_one('db', 'coll_order_tests0', '{"_id": "cAT", "a": "cat", "b": "cAT"}');

SELECT document FROM bson_aggregation_find('db', '{ "find": "coll_order_tests0", "filter": { "a": {"$lte": "cat"} }, "sort": { "b": 1 }, "skip": 0, "limit": 5, "collation": { "locale": "en", "strength" : 3 } }');
SELECT document FROM bson_aggregation_find('db', '{ "find": "coll_order_tests0", "filter": { "a": {"$lte": "cat"} }, "sort": { "b": -1 }, "skip": 0, "limit": 5, "collation": { "locale": "en", "strength" : 3 } }');
SELECT document FROM bson_aggregation_find('db', '{ "find": "coll_order_tests0", "filter": { "a": {"$lte": "cat"} }, "sort": { "b": 1 }, "skip": 0, "limit": 5, "collation": { "locale": "en", "strength" : 1, "caseLevel": true, "caseFirst": "upper" } }');
SELECT document FROM bson_aggregation_find('db', '{ "find": "coll_order_tests0", "filter": { "a": {"$lte": "cat"} }, "sort": { "b": -1 }, "skip": 0, "limit": 5, "collation": { "locale": "en", "strength" : 1, "caseLevel": true, "caseFirst": "upper" } }');
SELECT document FROM bson_aggregation_find('db', '{ "find": "coll_order_tests0", "filter": { "a": {"$gte": "cat"} }, "sort": { "b": 1 }, "skip": 0, "limit": 5, "collation": { "locale": "en", "strength" : 1, "caseLevel": true, "caseFirst": "lower" } }');
SELECT document FROM bson_aggregation_find('db', '{ "find": "coll_order_tests0", "filter": { "a": {"$gte": "cat"} }, "sort": { "b": -1 }, "skip": 0, "limit": 5, "collation": { "locale": "en", "strength" : 1, "caseLevel": true, "caseFirst": "lower" } }');

-- collation with sort/order by with collation-aware _id
SELECT document FROM bson_aggregation_find('db', '{ "find": "coll_order_tests0", "filter": { "a": {"$lte": "cat"} }, "sort": { "_id": 1 }, "skip": 0, "limit": 5, "collation": { "locale": "en", "strength" : 3 } }');
SELECT document FROM bson_aggregation_find('db', '{ "find": "coll_order_tests0", "filter": { "a": {"$lte": "cat"} }, "sort": { "_id": -1 }, "skip": 0, "limit": 5, "collation": { "locale": "en", "strength" : 3 } }');
SELECT document FROM bson_aggregation_find('db', '{ "find": "coll_order_tests0", "filter": { "a": {"$lte": "cat"} }, "sort": { "_id": 1 }, "skip": 0, "limit": 5, "collation": { "locale": "en", "strength" : 1, "caseLevel": true, "caseFirst": "upper" } }');
SELECT document FROM bson_aggregation_find('db', '{ "find": "coll_order_tests0", "filter": { "a": {"$lte": "cat"} }, "sort": { "_id": -1 }, "skip": 0, "limit": 5, "collation": { "locale": "en", "strength" : 1, "caseLevel": true, "caseFirst": "upper" } }');
SELECT document FROM bson_aggregation_find('db', '{ "find": "coll_order_tests0", "filter": { "a": {"$gte": "cat"} }, "sort": { "_id": 1 }, "skip": 0, "limit": 5, "collation": { "locale": "en", "strength" : 1, "caseLevel": true, "caseFirst": "lower" } }');
SELECT document FROM bson_aggregation_find('db', '{ "find": "coll_order_tests0", "filter": { "a": {"$gte": "cat"} }, "sort": { "_id": -1 }, "skip": 0, "limit": 5, "collation": { "locale": "en", "strength" : 1, "caseLevel": true, "caseFirst": "lower" } }');

-- collation with sort/order by: numericOrdering is respected
SELECT documentdb_api.insert_one('db', 'coll_order_tests1', '{"_id": 1, "a": "cat", "b": "10"}');
SELECT documentdb_api.insert_one('db', 'coll_order_tests1', '{"_id": 2, "a": "cat", "b": "2"}');
SELECT documentdb_api.insert_one('db', 'coll_order_tests1', '{"_id": 3, "a": "cat", "b": "3"}');

SELECT document FROM bson_aggregation_find('db', '{ "find": "coll_order_tests1", "filter": { "a": "cat" }, "sort": { "b": 1 }, "skip": 0, "limit": 5, "collation": { "locale": "en", "numericOrdering" : false } }');
SELECT document FROM bson_aggregation_find('db', '{ "find": "coll_order_tests1", "filter": { "a": "cat" }, "sort": { "b": 1 }, "skip": 0, "limit": 5, "collation": { "locale": "en", "numericOrdering" : true } }');

SELECT document FROM bson_aggregation_find('db', '{ "find": "coll_order_tests1", "filter": { "a": "cat" }, "sort": { "b": -1 }, "skip": 0, "limit": 5, "collation": { "locale": "en", "numericOrdering" : false } }');
SELECT document FROM bson_aggregation_find('db', '{ "find": "coll_order_tests1", "filter": { "a": "cat" }, "sort": { "b": -1 }, "skip": 0, "limit": 5, "collation": { "locale": "en", "numericOrdering" : true } }');

-- collation with sort/order by: setWindowFields
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "coll_order_tests1", "pipeline":  [{"$setWindowFields": { "sortBy": {"b": -1}, "output": {"res": { "$push": "$b", "window": {"documents": ["unbounded", "unbounded"]}}}}}], "collation": { "locale": "en", "numericOrdering" : false } }');

SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "coll_order_tests1", "pipeline":  [{"$setWindowFields": { "sortBy": {"b": -1}, "output": {"res": { "$push": "$b", "window": {"documents": ["unbounded", "unbounded"]}}}}}], "collation": { "locale": "en", "numericOrdering" : true } }');


-- (11) Unsupported scenarios

SELECT documentdb_api.find_and_modify('fam', '{"findAndModify": "ci_search2", "query": {"a": 1}, "update": {"_id": 1, "b": 1}, "collation" : {"locale" : "en", "strength": 1} }');
SELECT documentdb_api.update('update', '{"update":"ci_search2", "updates":[{"q":{"_id": 134111, "b": [ 5, 2, 4 ] },"u":{"$set" : {"b.$[a]":3} },"upsert":true, "collation" : {"locale" : "en", "strength": 1}, "arrayFilters": [ { "a": 2 } ]}]}');
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "ci_search2", "indexes": [{"key": {"asparse": 1}, "name": "my_sparse_idx1", "sparse": true, "collation" : {"locale" : "en", "strength": 1}}]}', TRUE);


-- (12) Test Id filters respect collation

SELECT documentdb_api.insert_one('db', 'ci_search3', '{ "_id": "Cat" }');
SELECT documentdb_api.insert_one('db', 'ci_search3', '{ "_id": "dog" }');
SELECT documentdb_api.insert_one('db', 'ci_search3', '{ "_id": "cat" }');
SELECT documentdb_api.insert_one('db', 'ci_search3', '{ "_id": "CaT" }');
SELECT documentdb_api.insert_one('db', 'ci_search3', '{ "_id": "Dog" }');
SELECT documentdb_api.insert_one('db', 'ci_search3', '{ "_id": "DoG" }');
SELECT documentdb_api.insert_one('db', 'ci_search3', '{ "_id": { "a" : "cat" } }');
SELECT documentdb_api.insert_one('db', 'ci_search3',' { "_id": { "a": "CAT"} }');
SELECT documentdb_api.insert_one('db', 'ci_search3', '{ "a": { "a": "Dog" } } ');
SELECT documentdb_api.insert_one('db', 'ci_search3', '{ "_id": [ "cat", "CAT "] }');

SELECT document FROM bson_aggregation_find('db', '{ "find": "ci_search3", "filter": { "$or" : [{ "_id": { "$eq": "cat" } }, { "_id": { "$eq": "DOG" } }] }, "sort": { "_id": 1 }, "skip": 0, "limit": 5, "collation": { "locale": "en", "strength" : 1} }');
SELECT document FROM bson_aggregation_find('db', '{ "find": "ci_search3", "filter": { "$or" : [{ "_id.a": { "$eq": "cat" } }, { "_id.a": { "$eq": "DOG" } }] }, "sort": { "_id": 1 }, "skip": 0, "limit": 5, "collation": { "locale": "en", "strength" : 1} }');
EXPLAIN(VERBOSE ON, COSTS OFF)SELECT document FROM bson_aggregation_find('db', '{ "find": "ci_search3", "filter": { "$or" : [{ "_id.a": { "$eq": "cat" } }, { "_id.a": { "$eq": "DOG" } }] }, "sort": { "_id": 1 }, "skip": 0, "limit": 5, "collation": { "locale": "en", "strength" : 1} }');


SELECT documentdb_api.shard_collection('db', 'ci_search', '{ "_id": "hashed" }', false);
BEGIN;
SET LOCAL documentdb_core.enablecollation TO on;
SELECT document FROM bson_aggregation_find('db', '{ "find": "ci_search3", "filter": { "$or" : [{ "_id": { "$eq": "cat" } }, { "_id": { "$eq": "DOG" } }] }, "sort": { "_id": 1 }, "skip": 0, "limit": 5, "collation": { "locale": "en", "strength" : 1} }');
EXPLAIN (VERBOSE ON, COSTS OFF)SELECT document FROM bson_aggregation_find('db', '{ "find": "ci_search3", "filter": { "$or" : [{ "_id": { "$eq": "cat" } }, { "_id": { "$eq": "DOG" } }] }, "sort": { "_id": 1 }, "skip": 0, "limit": 5, "collation": { "locale": "en", "strength" : 1} }');
END;

BEGIN;
SET LOCAL documentdb_core.enablecollation TO on;
SELECT document FROM bson_aggregation_find('db', '{ "find": "ci_search3", "filter": { "$or" : [{ "_id.a": { "$eq": "cat" } }, { "_id.a": { "$eq": "DOG" } }] }, "sort": { "_id": 1 }, "skip": 0, "limit": 5, "collation": { "locale": "en", "strength" : 1} }');
END;

EXPLAIN(VERBOSE ON, COSTS OFF)SELECT document FROM bson_aggregation_find('db', '{ "find": "ci_search3", "filter": { "$or" : [{ "_id.a": { "$eq": "cat" } }, { "_id.a": { "$eq": "DOG" } }] }, "sort": { "_id": 1 }, "skip": 0, "limit": 5, "collation": { "locale": "en", "strength" : 1} }');


-- (12) Check index with partial filter expression with collation

SELECT documentdb_api.insert_one('db','ci_search4', '{"_id": 1, "a" : { "b" : "DOG" }}', NULL);
SELECT documentdb_api.insert_one('db','ci_search4', '{"_id": 2, "a" : { "b" : "dog" }}', NULL);
SELECT documentdb_api.insert_one('db','ci_search4', '{"_id": 3, "a" : { "b" : "Cat" }}', NULL);
SELECT documentdb_api.insert_one('db','ci_search4', '{"_id": 4, "a" : { "b" : "Dog" }}', NULL);
SELECT documentdb_api.insert_one('db','ci_search4', '{"_id": 5, "a" : { "b" : "cAT" }}', NULL);
SELECT documentdb_api.insert_one('db','ci_search4', '{"_id": 6, "a" : { "b" : "DoG" }}', NULL);
SELECT documentdb_api.insert_one('db','ci_search4', '{"_id": 7, "a" : { "b" : "DOG" }}', NULL);

SELECT documentdb_api_internal.create_indexes_non_concurrently(
  'db',
  '{
     "createIndexes": "ci_search4",
     "indexes": [
       {
         "key": {"a.b": 1}, "name": "my_idx_1",
         "partialFilterExpression":
         {
           "a.b": {"$eq": "dog" }
         },
         "collation" : {"locale" : "en", "strength" : 2}
       }
     ]
   }',
   TRUE
);

SELECT documentdb_api_internal.create_indexes_non_concurrently(
  'db',
  '{
     "createIndexes": "ci_search4",
     "indexes": [
       {
         "key": {"a.b": 1}, "name": "my_idx_1",
         "partialFilterExpression":
         {
           "a.b": {"$eq": "dog" }
         }
       }
     ]
   }',
   TRUE
);

BEGIN;
SET LOCAL seq_page_cost TO 100;
SET LOCAL documentdb.forceUseIndexIfAvailable to true;
-- query pushed to the index when no collattion
SELECT document FROM bson_aggregation_find('db', '{ "find": "ci_search4", "filter": { "a.b": { "$eq": "dog" }, "a": { "$ne" :  null } } }');
EXPLAIN (VERBOSE ON, COSTS OFF) SELECT document FROM bson_aggregation_find('db', '{ "find": "ci_search4", "filter": { "a.b": { "$eq": "dog" }, "a": { "$ne" :  null } } }');
ROLLBACK;


BEGIN;
SET LOCAL documentdb_core.enablecollation TO on;
SET LOCAL seq_page_cost TO 100;
SET LOCAL documentdb.forceUseIndexIfAvailable to true;
-- query not pushed to the index when collattion is specified
SELECT document FROM bson_aggregation_find('db', '{ "find": "ci_search4", "filter": { "a.b": { "$eq": "dog" }, "a": { "$ne" :  null } }, "collation": { "locale": "en", "strength" : 1}  }');
EXPLAIN (VERBOSE ON, COSTS OFF) SELECT document FROM bson_aggregation_find('db', '{ "find": "ci_search4", "filter": { "a.b": { "$eq": "dog" }, "a": { "$ne" :  null } }, "collation": { "locale": "en", "strength" : 1}  }');
END;

SELECT documentdb_api.shard_collection('db', 'ci_search4', '{ "_id": "hashed" }', false);

BEGIN;
SET LOCAL documentdb_core.enablecollation TO on;
SELECT document FROM bson_aggregation_find('db', '{ "find": "ci_search4", "filter": { "$or" : [{ "a.b": { "$eq": "cat" } }, { "a.b": { "$eq": "DOG" } }] }, "sort": { "_id": 1 }, "skip": 0, "limit": 5, "collation": { "locale": "en", "strength" : 1} }');
EXPLAIN (VERBOSE ON, COSTS OFF) SELECT document FROM bson_aggregation_find('db', '{ "find": "ci_search4", "filter": { "$or" : [{ "a.b": { "$eq": "cat" } }, { "a.b": { "$eq": "DOG" } }] }, "sort": { "_id": 1 }, "skip": 0, "limit": 5, "collation": { "locale": "en", "strength" : 1} }');
END;

-- (13) Check index behavior with collation (TODO: update when index pushdown of collation is supported)

SELECT documentdb_api.insert_one('db','ci_search5', '{"_id": 1, "a" : { "b" : "DOG" }}', NULL);
SELECT documentdb_api.insert_one('db','ci_search5', '{"_id": 2, "a" : { "b" : "dog" }}', NULL);
SELECT documentdb_api.insert_one('db','ci_search5', '{"_id": 3, "a" : { "b" : "Cat" }}', NULL);
SELECT documentdb_api.insert_one('db','ci_search5', '{"_id": 4, "a" : { "b" : "Dog" }}', NULL);
SELECT documentdb_api.insert_one('db','ci_search5', '{"_id": 5, "a" : { "b" : "cAT" }}', NULL);
SELECT documentdb_api.insert_one('db','ci_search5', '{"_id": 6, "a" : { "b" : "DoG" }}', NULL);
SELECT documentdb_api.insert_one('db','ci_search5', '{"_id": 7, "a" : { "b" : "DOG" }}', NULL);

SELECT documentdb_api_internal.create_indexes_non_concurrently(
  'db',
  '{
     "createIndexes": "ci_search5",
     "indexes": [
       {
         "key": {"a.b": 1}, "name": "my_idx_1",
         "collation" : {"locale" : "en", "strength" : 2}
       }
     ]
   }',
   TRUE
);

SELECT documentdb_api_internal.create_indexes_non_concurrently(
  'db',
  '{
     "createIndexes": "ci_search5",
     "indexes": [
       {
         "key": {"a.b": 1}, "name": "my_idx_1"
       }
     ]
   }',
   TRUE
);

BEGIN;
SET LOCAL seq_page_cost TO 100;
SET LOCAL documentdb.forceUseIndexIfAvailable to true;
-- query pushed to the index when no collattion
SELECT document FROM bson_aggregation_find('db', '{ "find": "ci_search5", "filter": { "$or" : [{ "a.b": { "$eq": "cat" } }, { "a.b": { "$eq": "DOG" } }] }, "sort": { "_id": 1 }, "skip": 0, "limit": 5 }');
EXPLAIN (VERBOSE ON, COSTS OFF) SELECT document FROM bson_aggregation_find('db', '{ "find": "ci_search5", "filter": { "$or" : [{ "a.b": { "$eq": "cat" } }, { "a.b": { "$eq": "DOG" } }] }, "sort": { "_id": 1 }, "skip": 0, "limit": 5 }');

ROLLBACK;

BEGIN;
SET LOCAL documentdb_core.enablecollation TO on;
SET LOCAL seq_page_cost TO 100;
SET LOCAL documentdb.forceUseIndexIfAvailable to true;
-- query not pushed to the index when collation is specified
SELECT document FROM bson_aggregation_find('db', '{ "find": "ci_search5", "filter": { "$or" : [{ "a.b": { "$eq": "cat" } }, { "a.b": { "$eq": "DOG" } }] }, "sort": { "_id": 1 }, "skip": 0, "limit": 5, "collation": { "locale": "en", "strength" : 1} }');
EXPLAIN (VERBOSE ON, COSTS OFF) SELECT document FROM bson_aggregation_find('db', '{ "find": "ci_search5", "filter": { "$or" : [{ "a.b": { "$eq": "cat" } }, { "a.b": { "$eq": "DOG" } }] }, "sort": { "_id": 1 }, "skip": 0, "limit": 5, "collation": { "locale": "en", "strength" : 1} }');
END;

-- range query with index on path "a.b"
SELECT document FROM bson_aggregation_find('db', '{ "find": "ci_search5", "filter": { "a.b": { "$gt": "CAT" }, "a.b" : {"$lte" : "DOG"} }, "collation": { "locale": "en", "strength" : 1.93 } }');
BEGIN;
SET LOCAL documentdb_core.enablecollation TO on;
EXPLAIN(VERBOSE ON, COSTS OFF)SELECT document FROM bson_aggregation_find('db', '{ "find": "ci_search5", "filter": { "a.b": { "$gt": "CAT" }, "a.b" : {"$lte" : "DOG"} }, "collation": { "locale": "en", "strength" : 1.93 } }');
END;

SELECT documentdb_api.shard_collection('db', 'ci_search5', '{ "_id": "hashed" }', false);

BEGIN;
SET LOCAL documentdb_core.enablecollation TO on;
SET LOCAL seq_page_cost TO 100;
SET LOCAL documentdb.forceUseIndexIfAvailable to true;
SELECT document FROM bson_aggregation_find('db', '{ "find": "ci_search5", "filter": { "$or" : [{ "a.b": { "$eq": "cat" } }, { "a.b": { "$eq": "DOG" } }] }, "sort": { "_id": 1 }, "skip": 0, "limit": 5, "collation": { "locale": "en", "strength" : 1} }');
EXPLAIN (VERBOSE ON, COSTS OFF) SELECT document FROM bson_aggregation_find('db', '{ "find": "ci_search5", "filter": { "$or" : [{ "a.b": { "$eq": "cat" } }, { "a.b": { "$eq": "DOG" } }] }, "sort": { "_id": 1 }, "skip": 0, "limit": 5, "collation": { "locale": "en", "strength" : 1} }');
END;

-- nested pipleline tests

SELECT documentdb_api.insert_one('db','ci_search6', '{"_id": "DOG", "a" : { "b" : "DOG" }}', NULL);
SELECT documentdb_api.insert_one('db','ci_search6', '{"_id": "dog", "a" : { "b" : "dog" }}', NULL);
SELECT documentdb_api.insert_one('db','ci_search6', '{"_id": "Cat", "a" : { "b" : "Cat" }}', NULL);
SELECT documentdb_api.insert_one('db','ci_search6', '{"_id": "Dog", "a" : { "b" : "Dog" }}', NULL);
SELECT documentdb_api.insert_one('db','ci_search6', '{"_id": "cAT", "a" : { "b" : "cAT" }}', NULL);
SELECT documentdb_api.insert_one('db','ci_search6', '{"_id": "DoG", "a" : { "b" : "DoG" }}', NULL);
SELECT documentdb_api.insert_one('db','ci_search6', '{"_id": "dOg", "a" : { "b" : "dOg" }}', NULL);

-- lookup with id join (collation aware)
SELECT document FROM bson_aggregation_pipeline('db', 
    '{ "aggregate": "ci_search6", "pipeline": [ { "$lookup": { "from": "ci_search6", "as": "matched_docs", "localField": "_id", "foreignField": "_id", "pipeline": [ { "$match": { "$or" : [ { "a.b": "cat" }, { "a.b": "dog" } ] } } ] } } ], "cursor": {}, "collation": { "locale": "en", "strength" : 1}  }');

BEGIN;
SET LOCAL documentdb_core.enablecollation TO on;
EXPLAIN (VERBOSE ON, COSTS OFF) SELECT document FROM bson_aggregation_pipeline('db', 
    '{ "aggregate": "ci_search6", "pipeline": [ { "$lookup": { "from": "ci_search6", "as": "matched_docs", "localField": "_id", "foreignField": "_id", "pipeline": [ { "$match": { "$or" : [ { "a.b": "cat" }, { "a.b": "dog" } ] } } ] } } ], "cursor": {}, "collation": { "locale": "en", "strength" : 1}  }');
END;

-- lookup with id join optimized (explicitly asked to make _id join collation agnostic)
BEGIN;
SET LOCAL documentdb.enableLookupIdJoinOptimizationOnCollation to true;
SET LOCAL documentdb_core.enablecollation TO on;
SELECT document FROM bson_aggregation_pipeline('db', 
    '{ "aggregate": "ci_search6", "pipeline": [ { "$lookup": { "from": "ci_search6", "as": "matched_docs", "localField": "_id", "foreignField": "_id", "pipeline": [ { "$match": { "$or" : [ { "a.b": "cat" }, { "a.b": "dog" } ] } } ] } } ], "cursor": {}, "collation": { "locale": "en", "strength" : 1}  }');
EXPLAIN (VERBOSE ON, COSTS OFF) SELECT document FROM bson_aggregation_pipeline('db', 
    '{ "aggregate": "ci_search6", "pipeline": [ { "$lookup": { "from": "ci_search6", "as": "matched_docs", "localField": "_id", "foreignField": "_id", "pipeline": [ { "$match": { "$or" : [ { "a.b": "cat" }, { "a.b": "dog" } ] } } ] } } ], "cursor": {}, "collation": { "locale": "en", "strength" : 1}  }');
END;

-- lookup with non-id join (collation aware)
SELECT document FROM bson_aggregation_pipeline('db', 
    '{ "aggregate": "ci_search6", "pipeline": [ { "$lookup": { "from": "ci_search6", "as": "matched_docs", "localField": "a.b", "foreignField": "a.b", "pipeline": [ { "$match": { "$or" : [ { "a.b": "cat" }, { "a.b": "dog" } ] } } ] } } ], "cursor": {}, "collation": { "locale": "en", "strength" : 1}  }');

-- lookup with non-id join (collation aware - explain)
BEGIN;
SET LOCAL documentdb_core.enablecollation TO on;
EXPLAIN (VERBOSE ON, COSTS OFF) SELECT document FROM bson_aggregation_pipeline('db', 
    '{ "aggregate": "ci_search6", "pipeline": [ { "$lookup": { "from": "ci_search6", "as": "matched_docs", "localField": "a.b", "foreignField": "a.b", "pipeline": [ { "$match": { "$or" : [ { "a.b": "cat" }, { "a.b": "dog" } ] } } ] } } ], "cursor": {}, "collation": { "locale": "en", "strength" : 1}  }');
END;

-- $facet and $unionwith
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "ci_search6", "pipeline": [ { "$facet": { "a" : [ { "$match": { "a.b": "cat" } }, { "$count": "catCount" } ], "b" : [ { "$match": { "a.b": "dog" } }, { "$count": "dogCount" } ]  } } ], "cursor": {}, "collation": { "locale": "en", "strength" : 1}}');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "ci_search6", "pipeline": [ { "$unionWith": { "coll": "ci_search6", "pipeline" : [ { "$match": { "a.b": "cat" }}] } }], "cursor": {}, "collation": { "locale": "en", "strength" : 1} }');

-- $graphLookup 
SELECT documentdb_api.insert_one('db','ci_search7', '{"_id": "alice", "pet" : "dog" }', NULL);
SELECT documentdb_api.insert_one('db','ci_search7', '{"_id": "bob", "pet" : "cat" }', NULL);

SELECT documentdb_api.insert_one('db','ci_search8', '{"_id": "DOG", "name" : "DOG" }', NULL);
SELECT documentdb_api.insert_one('db','ci_search8', '{"_id": "dog", "name" : "dog" }', NULL);
SELECT documentdb_api.insert_one('db','ci_search8', '{"_id": "CAT", "name" : "CAT" }', NULL);
SELECT documentdb_api.insert_one('db','ci_search8', '{"_id": "cAT", "name" : "cAT" }', NULL);

BEGIN;
SET LOCAL documentdb_core.enablecollation TO on;
EXPLAIN (VERBOSE ON, COSTS OFF) SELECT document FROM bson_aggregation_pipeline('db',
    '{ "aggregate": "ci_search7", "pipeline": [ { "$graphLookup": { "from": "ci_search8", "startWith": "$pet", "connectFromField": "name", "connectToField": "_id", "as": "destinations", "depthField": "depth" } } ],  "collation": { "locale": "en", "strength" : 3} }');
SELECT document FROM bson_aggregation_pipeline('db',
    '{ "aggregate": "ci_search7", "pipeline": [ { "$graphLookup": { "from": "ci_search8", "startWith": "$pet", "connectFromField": "name", "connectToField": "_id", "as": "destinations", "depthField": "depth" } } ],  "collation": { "locale": "en", "strength" : 3} }');
END;

SELECT document FROM bson_aggregation_pipeline('db',
    '{ "aggregate": "ci_search6", "pipeline": [ { "$graphLookup": { "from": "ci_search6", "startWith": "$a.b", "connectFromField": "a.b", "connectToField": "a.b", "as": "destinations", "depthField": "depth" } } ],  "collation": { "locale": "en", "strength" : 1} }');
SELECT document FROM bson_aggregation_pipeline('db',
    '{ "aggregate": "ci_search7", "pipeline": [ { "$graphLookup": { "from": "ci_search8", "startWith": "$pet", "connectFromField": "name", "connectToField": "_id", "as": "destinations", "depthField": "depth" } } ],  "collation": { "locale": "en", "strength" : 1} }');
SELECT document FROM bson_aggregation_pipeline('db',
    '{ "aggregate": "ci_search7", "pipeline": [ { "$graphLookup": { "from": "ci_search8", "startWith": "$pet", "connectFromField": "name", "connectToField": "_id", "as": "destinations", "depthField": "depth" } } ],  "collation": { "locale": "en", "strength" : 2} }');
SELECT document FROM bson_aggregation_pipeline('db',
    '{ "aggregate": "ci_search7", "pipeline": [ { "$graphLookup": { "from": "ci_search8", "startWith": "$pet", "connectFromField": "name", "connectToField": "_id", "as": "destinations", "depthField": "depth" } } ],  "collation": { "locale": "fr", "strength" : 1, "alternate": "shifted" } }');
SELECT document FROM bson_aggregation_pipeline('db',
    '{ "aggregate": "ci_search7", "pipeline": [ { "$graphLookup": { "from": "ci_search8", "startWith": "$pet", "connectFromField": "name", "connectToField": "_id", "as": "destinations", "depthField": "depth" } } ],  "collation": { "locale": "hi", "strength" : 2, "caseFirst": "lower" } }');

-- test $graphlookup without auto variables ($$NOW) generation 
BEGIN;
SET LOCAL documentdb.enableNowSystemVariable='off';

EXPLAIN (VERBOSE ON, COSTS OFF) SELECT document FROM bson_aggregation_pipeline('db',
    '{ "aggregate": "ci_search7", "pipeline": [ { "$graphLookup": { "from": "ci_search8", "startWith": "$pet", "connectFromField": "name", "connectToField": "_id", "as": "destinations", "depthField": "depth" } } ],  "collation": { "locale": "en", "strength" : 3} }');
SELECT document FROM bson_aggregation_pipeline('db',
    '{ "aggregate": "ci_search7", "pipeline": [ { "$graphLookup": { "from": "ci_search8", "startWith": "$pet", "connectFromField": "name", "connectToField": "_id", "as": "destinations", "depthField": "depth" } } ],  "collation": { "locale": "en", "strength" : 3} }');
END;

--- $graphLookup on sharded collection: unsupported
SELECT documentdb_api.shard_collection('db', 'ci_search7', '{ "_id": "hashed" }', false);
SELECT documentdb_api.shard_collection('db', 'ci_search8', '{ "_id": "hashed" }', false);

SELECT document FROM bson_aggregation_pipeline('db',
    '{ "aggregate": "ci_search7", "pipeline": [ { "$graphLookup": { "from": "ci_search8", "startWith": "$pet", "connectFromField": "name", "connectToField": "_id", "as": "destinations", "depthField": "depth" } } ],  "collation": { "locale": "fr", "strength" : 1, "alternate": "shifted" } }');

-- unsupported $merge 
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "ci_search6", "pipeline": [{"$merge" : { "into": "ci_search9", "whenMatched" : "replace" }} ], "collation": { "locale": "en", "strength" : 1} }');

SELECT documentdb_api.shard_collection('db', 'ci_search6', '{ "_id": "hashed" }', false);

-- lookup with id join (collation aware)
SELECT document FROM bson_aggregation_pipeline('db', 
    '{ "aggregate": "ci_search6", "pipeline": [ { "$lookup": { "from": "ci_search6", "as": "matched_docs", "localField": "_id", "foreignField": "_id", "pipeline": [ { "$match": { "$or" : [ { "a.b": "cat" }, { "a.b": "dog" } ] } } ] } } ], "cursor": {}, "collation": { "locale": "en", "strength" : 1}  }');

BEGIN;
SET LOCAL documentdb_core.enablecollation TO on;
EXPLAIN (VERBOSE ON, COSTS OFF) SELECT document FROM bson_aggregation_pipeline('db', 
    '{ "aggregate": "ci_search6", "pipeline": [ { "$lookup": { "from": "ci_search6", "as": "matched_docs", "localField": "_id", "foreignField": "_id", "pipeline": [ { "$match": { "$or" : [ { "a.b": "cat" }, { "a.b": "dog" } ] } } ] } } ], "cursor": {}, "collation": { "locale": "en", "strength" : 1}  }');
END;

-- lookup with non-id join (collation aware)
SELECT document FROM bson_aggregation_pipeline('db', 
    '{ "aggregate": "ci_search6", "pipeline": [ { "$lookup": { "from": "ci_search6", "as": "matched_docs", "localField": "a.b", "foreignField": "a.b", "pipeline": [ { "$match": { "$or" : [ { "a.b": "cat" }, { "a.b": "dog" } ] } } ] } } ], "cursor": {}, "collation": { "locale": "en", "strength" : 1}  }');

BEGIN;
SET LOCAL documentdb_core.enablecollation TO on;
EXPLAIN (VERBOSE ON, COSTS OFF) SELECT document FROM bson_aggregation_pipeline('db', 
    '{ "aggregate": "ci_search6", "pipeline": [ { "$lookup": { "from": "ci_search6", "as": "matched_docs", "localField": "a.b", "foreignField": "a.b", "pipeline": [ { "$match": { "$or" : [ { "a.b": "cat" }, { "a.b": "dog" } ] } } ] } } ], "cursor": {}, "collation": { "locale": "en", "strength" : 1}  }');
END;

-- $facet and $unionwith
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "ci_search6", "pipeline": [ { "$facet": { "a" : [ { "$match": { "a.b": "cat" } }, { "$count": "catCount" } ], "b" : [ { "$match": { "a.b": "dog" } }, { "$count": "dogCount" } ]  } } ], "cursor": {}, "collation": { "locale": "en", "strength" : 1}}');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "ci_search6", "pipeline": [ { "$unionWith": { "coll": "ci_search6", "pipeline" : [ { "$match": { "a.b": "cat" }}] } }], "cursor": {}, "collation": { "locale": "en", "strength" : 1} }');

-- unsupported $merge
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "ci_search6", "pipeline": [{"$merge" : { "into": "ci_search7", "whenMatched" : "replace" }} ], "collation": { "locale": "en", "strength" : 1} }');

-- $expr
SELECT documentdb_api.insert_one('db', 'coll_agg_proj', '{ "_id": 1, "a": "cat" }');
SELECT documentdb_api.insert_one('db', 'coll_agg_proj', '{ "_id": 2, "a": "dog" }');
SELECT documentdb_api.insert_one('db', 'coll_agg_proj', '{ "_id": 3, "a": "cAt" }');
SELECT documentdb_api.insert_one('db', 'coll_agg_proj', '{ "_id": 4, "a": "dOg" }');
SELECT documentdb_api.insert_one('db', 'coll_agg_proj', '{ "_id": "hen", "a": "hen" }');
SELECT documentdb_api.insert_one('db', 'coll_agg_proj', '{ "_id": "bat", "a": "bat" }');

SELECT document FROM bson_aggregation_find('db', '{ "find": "coll_agg_proj", "filter": { "$expr": {"$eq": ["$a", "CAT"]} }, "sort": { "_id": 1 }, "skip": 0, "collation": { "locale": "en", "strength" : 1 } }');
SELECT document FROM bson_aggregation_find('db', '{ "find": "coll_agg_proj", "filter": { "$expr": {"$eq": ["$a", "CAT"]} }, "sort": { "_id": 1 }, "skip": 0, "collation": { "locale": "hi", "strength" : 2 } }');
SELECT document FROM bson_aggregation_find('db', '{ "find": "coll_agg_proj", "filter": { "$expr": {"$ne": ["$a", "CAT"]} }, "sort": { "_id": 1 }, "skip": 0, "collation": { "locale": "fi", "strength" : 1 } }');
SELECT document FROM bson_aggregation_find('db', '{ "find": "coll_agg_proj", "filter": { "$expr": {"$lte": ["$a", "CAT"]} }, "sort": { "_id": 1 }, "skip": 0, "collation": { "locale": "en", "strength" : 1 } }');
SELECT document FROM bson_aggregation_find('db', '{ "find": "coll_agg_proj", "filter": { "$expr": {"$gte": ["$a", "CAT"]} }, "sort": { "_id": 1 }, "skip": 0, "collation": { "locale": "fr", "strength" : 3 } }');
SELECT document FROM bson_aggregation_find('db', '{ "find": "coll_agg_proj", "filter": { "$expr": {"$gte": ["$a", "CAT"]} }, "sort": { "_id": 1 }, "skip": 0, "collation": { "locale": "fr_CA", "strength" : 3 } }');
SELECT document FROM bson_aggregation_find('db', '{ "find": "coll_agg_proj", "filter": { "$expr": {"$gte": ["$a", "CAT"]} }, "sort": { "_id": 1 }, "skip": 0, "collation": { "locale": "es@collation=search", "strength" : 3 } }');
SELECT document FROM bson_aggregation_find('db', '{ "find": "coll_agg_proj", "filter": { "$expr": {"$gt": ["$a", "CAT"]} }, "sort": { "_id": 1 }, "skip": 0, "collation": { "locale": "fr", "strength" : 1 } }');
SELECT document FROM bson_aggregation_find('db', '{ "find": "coll_agg_proj", "filter": { "$expr": { "$or": [{"$gte": [ "$a", "DOG" ]}, {"$gte": [ "$a", "CAT" ]}] } }, "sort": { "_id": 1 }, "skip": 0, "collation": { "locale": "fr", "strength" : 1 } }');
SELECT document FROM bson_aggregation_find('db', '{ "find": "coll_agg_proj", "filter": { "$expr": { "$and": [{"$lte": [ "$a", "DOG" ]}, {"$lte": [ "$a", "CAT" ]}] } }, "sort": { "_id": 1 }, "skip": 0, "collation": { "locale": "fr", "strength" : 2 } }');

-- test $expr without auto variables ($$NOW) generation
BEGIN;
SET LOCAL documentdb.enableNowSystemVariable='off';
EXPLAIN (VERBOSE ON, COSTS OFF) SELECT document FROM bson_aggregation_find('db', '{ "find": "coll_agg_proj", "filter": { "$expr": {"$eq": ["$a", "CAT"]} }, "sort": { "_id": 1 }, "skip": 0, "collation": { "locale": "en", "strength" : 1 } }');
SELECT document FROM bson_aggregation_find('db', '{ "find": "coll_agg_proj", "filter": { "$expr": {"$eq": ["$a", "CAT"]} }, "sort": { "_id": 1 }, "skip": 0, "collation": { "locale": "en", "strength" : 1 } }');
ROLLBACK;

-- en_US_POSIX uses a c-style comparison. POSIX locale ignores case insensitivity. This is the ICU semantics.
SELECT document FROM bson_aggregation_find('db', '{ "find": "coll_agg_proj", "filter": { "$expr": {"$eq": ["$a", "cat"]} }, "sort": { "_id": 1 }, "skip": 0, "collation": { "locale": "en_US_POSIX", "strength" : 1 } }');
SELECT document FROM bson_aggregation_find('db', '{ "find": "coll_agg_proj", "filter": { "$expr": {"$eq": ["$a", "CAT"]} }, "sort": { "_id": 1 }, "skip": 0, "collation": { "locale": "en_US_POSIX", "strength" : 1 } }');

-- simple collation
SELECT document FROM bson_aggregation_find('db', '{ "find": "coll_agg_proj",  "filter": { "$expr": {"$eq": ["$a", "CAT"]} }, "sort": { "_id": 1 }, "skip": 0, "limit": 5, "collation": { "locale": "simple"} }');
SELECT document FROM bson_aggregation_find('db', '{ "find": "coll_agg_proj",  "filter": { "$expr": {"$eq": ["$a", "cat"]} }, "sort": { "_id": 1 }, "skip": 0, "limit": 5, "collation": { "locale": "simple"} }');

-- simple locale ignores other options
SELECT document FROM bson_aggregation_find('db', '{ "find": "coll_agg_proj",  "filter": { "$expr": {"$eq": ["$a", "cat"]} }, "sort": { "_id": 1 }, "skip": 0, "limit": 5, "collation": { "locale": "simple", "strength": 1} }');
SELECT document FROM bson_aggregation_find('db', '{ "find": "coll_agg_proj",  "filter": { "$expr": {"$eq": ["$a", "cat"]} }, "sort": { "_id": 1 }, "skip": 0, "limit": 5, "collation": { "locale": "simple", "strength": 3} }');
SELECT document FROM bson_aggregation_find('db', '{ "find": "coll_agg_proj",  "filter": { "$expr": {"$eq": ["$a", "cat"]} }, "sort": { "_id": 1 }, "skip": 0, "limit": 5, "collation": { "locale": "simple", "caseFirst": "upper"} }');
SELECT document FROM bson_aggregation_find('db', '{ "find": "coll_agg_proj",  "filter": { "$expr": {"$eq": ["$a", "cat"]} }, "sort": { "_id": 1 }, "skip": 0, "limit": 5, "collation": { "locale": "simple", "caseFirst": "lower"} }');

-- support for $filter
SELECT document FROM bson_aggregation_find('db', '{ "find": "coll_agg_proj", "filter": { "$expr": { "$eq": [["$a"], { "$filter": { "input": ["$a"], "as": "item", "cond": { "$eq": [ "$$item", "CAT" ] } } } ] } }, "sort": { "_id": 1 }, "collation": { "locale": "en", "strength": 1 } }');
SELECT document FROM bson_aggregation_find('db', '{ "find": "coll_agg_proj", "filter": { "$expr": { "$eq": [["$a"], { "$filter": { "input": ["$a"], "as": "item", "cond": { "$eq": [ "$$item", "CAT" ] } } } ] } }, "sort": { "_id": 1 }, "collation": { "locale": "en", "strength": 3 } }');
SELECT document FROM bson_aggregation_find('db', '{ "find": "coll_agg_proj", "filter": { "$expr": { "$eq": [["$a"], { "$filter": { "input": ["$a"], "as": "item", "cond": { "$ne": [ "$$item", "CAT" ] } } } ] } }, "sort": { "_id": 1 }, "collation": { "locale": "en", "strength": 2 } }');
SELECT document FROM bson_aggregation_find('db', '{ "find": "coll_agg_proj", "filter": { "$expr": { "$eq": [["$a"], { "$filter": { "input": ["$a"], "as": "item", "cond": { "$or": [{"$gte": [ "$$item", "DOG" ]}, {"$gte": [ "$$item", "CAT" ]}] } } } ] } }, "sort": { "_id": 1 }, "collation": { "locale": "en", "strength": 1 } }');
SELECT document FROM bson_aggregation_find('db', '{ "find": "coll_agg_proj", "filter": { "$expr": { "$eq": [["$a"], { "$filter": { "input": ["$a"], "as": "item", "cond": { "$and": [{"$gte": [ "$$item", "DOG" ]}, {"$gte": [ "$$item", "CAT" ]}] } } } ] } }, "sort": { "_id": 1 }, "collation": { "locale": "en", "strength": 1 } }');
SELECT document FROM bson_aggregation_find('db', '{ "find": "coll_agg_proj", "filter": { "$expr": { "$eq": [["$_id"], { "$filter": { "input": ["$_id"], "as": "item", "cond": { "$and": [{"$gte": [ "$$item", "HEN" ]}, {"$gte": [ "$$item", "BAT" ]}] } } } ] } }, "sort": { "_id": 1 }, "collation": { "locale": "en", "strength": 1 } }');

-- support for $in
SELECT document FROM bson_aggregation_find('db', '{ "find": "coll_agg_proj", "filter": { "$expr": {"$in": ["$a", ["CAT", "DOG"]]} }, "sort": { "_id": 1 }, "skip": 0, "collation": { "locale": "fr", "strength" : 1 } }');
SELECT document FROM bson_aggregation_find('db', '{ "find": "coll_agg_proj", "filter": { "$expr": {"$in": ["$a", ["CAT", "DOG"]]} }, "sort": { "_id": 1 }, "skip": 0, "collation": { "locale": "en", "strength" : 2 } }');
SELECT document FROM bson_aggregation_find('db', '{ "find": "coll_agg_proj", "filter": { "$expr": {"$in": ["$a", ["CAT", "DOG"]]} }, "sort": { "_id": 1 }, "skip": 0, "collation": { "locale": "en", "strength" : 3 } }');
SELECT document FROM bson_aggregation_find('db', '{ "find": "coll_agg_proj", "filter": { "$expr": {"$in": ["$_id", ["HEN", "BAT"]]} }, "sort": { "_id": 1 }, "skip": 0, "collation": { "locale": "en", "strength" : 1 } }');
SELECT document FROM bson_aggregation_find('db', '{ "find": "coll_agg_proj", "filter": { "$expr": {"$in": ["$_id", ["HEN", "BAT"]]} }, "sort": { "_id": 1 }, "skip": 0, "collation": { "locale": "en", "strength" : 3 } }');

-- support for $indexOfArray
SELECT document FROM bson_aggregation_find('db', '{ "find": "coll_agg_proj", "filter": { "$expr": {"$eq": [{ "$indexOfArray": [ ["CAT", "DOG"], "$a" ] }, 0]} }, "sort": { "_id": 1 }, "skip": 0, "collation": { "locale": "en", "strength" : 1 } }');
SELECT document FROM bson_aggregation_find('db', '{ "find": "coll_agg_proj", "filter": { "$expr": {"$eq": [{ "$indexOfArray": [ ["CAT", "DOG"], "$a" ] }, 0]} }, "sort": { "_id": 1 }, "skip": 0, "collation": { "locale": "en", "strength" : 3 } }');

-- support for $indexOfBytes (doesn't respect collation)
SELECT document FROM bson_aggregation_find('db', '{ "find": "coll_agg_proj", "filter": { "$expr": {"$eq": [{ "$indexOfBytes": [ "cAtALoNa", "$a" ] }, 0]} }, "sort": { "_id": 1 }, "skip": 0, "collation": { "locale": "en", "strength" : 1 } }');
SELECT document FROM bson_aggregation_find('db', '{ "find": "coll_agg_proj", "filter": { "$expr": {"$eq": [{ "$indexOfBytes": [ "cAtALoNa", "$a" ] }, 0]} }, "sort": { "_id": 1 }, "skip": 0, "collation": { "locale": "en", "strength" : 3 } }');
SELECT document FROM bson_aggregation_find('db', '{ "find": "coll_agg_proj", "filter": { "$expr": {"$eq": [{ "$indexOfBytes": [ "$a", "aT" ] }, 1]} }, "sort": { "_id": 1 }, "skip": 0, "collation": { "locale": "en", "strength" : 2 } }');
SELECT document FROM bson_aggregation_find('db', '{ "find": "coll_agg_proj", "filter": { "$expr": {"$eq": [{ "$indexOfBytes": [ "$a", "AT" ] }, -1]} }, "sort": { "_id": 1 }, "skip": 0, "collation": { "locale": "en", "strength" : 2 } }');

-- support for $indexOfCP (doesn't respect collation)
SELECT document FROM bson_aggregation_find('db', '{ "find": "coll_agg_proj", "filter": { "$expr": {"$eq": [{ "$indexOfCP": [ "cAtALoNa", "$a" ] }, 0]} }, "sort": { "_id": 1 }, "skip": 0, "collation": { "locale": "en", "strength" : 1 } }');
SELECT document FROM bson_aggregation_find('db', '{ "find": "coll_agg_proj", "filter": { "$expr": {"$eq": [{ "$indexOfCP": [ "cAtALoNa", "$a" ] }, 0]} }, "sort": { "_id": 1 }, "skip": 0, "collation": { "locale": "en", "strength" : 3 } }');
SELECT document FROM bson_aggregation_find('db', '{ "find": "coll_agg_proj", "filter": { "$expr": {"$eq": [{ "$indexOfCP": [ "$a", "at" ] }, 1]} }, "sort": { "_id": 1 }, "skip": 0, "collation": { "locale": "en", "strength" : 2 } }');
SELECT document FROM bson_aggregation_find('db', '{ "find": "coll_agg_proj", "filter": { "$expr": {"$eq": [{ "$indexOfCP": [ "$a", "AT" ] }, 1]} }, "sort": { "_id": 1 }, "skip": 0, "collation": { "locale": "en", "strength" : 2 } }');

-- support for $strcasecmp (doesn't respect collation)
SELECT document FROM bson_aggregation_find('db', '{ "find": "coll_agg_proj", "filter": { "$expr": {"$eq": [{ "$strcasecmp": ["$a", "CAT"] }, 0]} }, "sort": { "_id": 1 }, "skip": 0, "collation": { "locale": "en", "strength" : 1 } }');
SELECT document FROM bson_aggregation_find('db', '{ "find": "coll_agg_proj", "filter": { "$expr": {"$eq": [{ "$strcasecmp": ["$a", "CAT"] }, 0]} }, "sort": { "_id": 1 }, "skip": 0, "collation": { "locale": "en", "strength" : 3 } }');
SELECT document FROM bson_aggregation_find('db', '{ "find": "coll_agg_proj", "filter": { "$expr": {"$eq": [{ "$strcasecmp": ["$a", "CAT"] }, 0]} }, "sort": { "_id": 1 }, "skip": 0, "collation": { "locale": "en", "strength" : 2 } }');
SELECT document FROM bson_aggregation_find('db', '{ "find": "coll_agg_proj", "filter": { "$expr": {"$eq": [{ "$strcasecmp": ["$a", "CAT"] }, 0]} }, "sort": { "_id": 1 }, "skip": 0, "collation": { "locale": "en", "strength" : 3 } }');

-- $addFields
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "coll_agg_proj", "pipeline": [ { "$addFields": { "newField": { "$ne": ["$a", "CAT"] } } } ], "cursor": {}, "collation": { "locale": "fr", "strength" : 1} }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "coll_agg_proj", "pipeline": [ { "$addFields": { "newField": { "$lte": ["$a", "CAT"] } } } ], "cursor": {}, "collation": { "locale": "fr", "strength" : 1} }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "coll_agg_proj", "pipeline": [ { "$addFields": { "newField": { "$gte": ["$a", "CAT"] } } } ], "cursor": {}, "collation": { "locale": "fr", "strength" : 3} }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "coll_agg_proj", "pipeline": [ { "$addFields": { "newField": { "$gte": ["$a", "CAT"] } } } ], "cursor": {}, "collation": { "locale": "fr", "strength" : 1} }');

-- test $addFields auto $$NOW generation
BEGIN;
SET LOCAL documentdb.enableNowSystemVariable='off';
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "coll_agg_proj", "pipeline": [ { "$addFields": { "newField": { "$gte": ["$a", "CAT"] } } } ], "cursor": {}, "collation": { "locale": "fr", "strength" : 1} }');
EXPLAIN (VERBOSE ON, COSTS OFF) SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "coll_agg_proj", "pipeline": [ { "$addFields": { "newField": { "$gte": ["$a", "CAT"] } } } ], "cursor": {}, "collation": { "locale": "fr", "strength" : 1} }');
ROLLBACK;

-- $set
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "coll_agg_proj", "pipeline": [ { "$set": { "newField": { "$ne": ["$a", "CAT"] } } } ], "cursor": {}, "collation": { "locale": "fr", "strength" : 1} }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "coll_agg_proj", "pipeline": [ { "$set": { "newField": { "$lte": ["$a", "CAT"] } } } ], "cursor": {}, "collation": { "locale": "fr", "strength" : 1} }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "coll_agg_proj", "pipeline": [ { "$set": { "newField": { "$gte": ["$a", "CAT"] } } } ], "cursor": {}, "collation": { "locale": "fr", "strength" : 3} }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "coll_agg_proj", "pipeline": [ { "$set": { "newField": { "$gte": ["$a", "CAT"] } } } ], "cursor": {}, "collation": { "locale": "fr", "strength" : 1} }');

-- test $set auto $$NOW generation
BEGIN;
SET LOCAL documentdb.enableNowSystemVariable='off';
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "coll_agg_proj", "pipeline": [ { "$set": { "newField": { "$gte": ["$a", "CAT"] } } } ], "cursor": {}, "collation": { "locale": "fr", "strength" : 1} }');
EXPLAIN (VERBOSE ON, COSTS OFF) SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "coll_agg_proj", "pipeline": [ { "$set": { "newField": { "$gte": ["$a", "CAT"] } } } ], "cursor": {}, "collation": { "locale": "fr", "strength" : 1} }');
ROLLBACK;

-- project
SELECT documentdb_api_internal.bson_dollar_project(document, '{ "newField": { "$eq": ["$a", "CAT"] } }', '{}', 'en-u-ks-level1') FROM documentdb_api.collection('db', 'coll_agg_proj');
SELECT documentdb_api_internal.bson_dollar_project(document, '{ "newField": { "$eq": ["$a", "DOG"] } }', '{}', 'en-u-ks-level1') FROM documentdb_api.collection('db', 'coll_agg_proj');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "coll_agg_proj", "pipeline": [ { "$project": { "a": 1, "newField": { "$ne": ["$a", "CAT"] } } } ], "cursor": {}, "collation": { "locale": "fr", "strength" : 1} }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "coll_agg_proj", "pipeline": [ { "$project": { "a": 1, "newField": { "$lte": ["$a", "CAT"] } } } ], "cursor": {}, "collation": { "locale": "fr", "strength" : 1} }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "coll_agg_proj", "pipeline": [ { "$project": { "a": 1, "newField": { "$gte": ["$a", "CAT"] } } } ], "cursor": {}, "collation": { "locale": "fr", "strength" : 3} }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "coll_agg_proj", "pipeline": [ { "$project": { "a": 1, "newField": { "$gte": ["$a", "CAT"] } } } ], "cursor": {}, "collation": { "locale": "fr", "strength" : 1} }');

-- test $project auto $$NOW generation
BEGIN;
SET LOCAL documentdb.enableNowSystemVariable='off';
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "coll_agg_proj", "pipeline": [ { "$project": { "a": 1, "newField": { "$gte": ["$a", "CAT"] } } } ], "cursor": {}, "collation": { "locale": "fr", "strength" : 1} }');
EXPLAIN (VERBOSE ON, COSTS OFF) SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "coll_agg_proj", "pipeline": [ { "$project": { "a": 1, "newField": { "$gte": ["$a", "CAT"] } } } ], "cursor": {}, "collation": { "locale": "fr", "strength" : 1} }');
ROLLBACK;

-- $replaceRoot
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "coll_agg_proj", "pipeline": [ { "$replaceRoot": { "newRoot": { "a": "$a", "newField": { "$eq": ["$a", "CAT"] } } } } ], "cursor": {}, "collation": { "locale": "en", "strength" : 1} }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "coll_agg_proj", "pipeline": [ { "$replaceRoot": { "newRoot": { "a": "$a", "newField": { "$ne": ["$a", "CAT"] } } } } ], "cursor": {}, "collation": { "locale": "en", "strength" : 3} }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "coll_agg_proj", "pipeline": [ { "$replaceRoot": { "newRoot": { "a": "$a", "newField": { "$lte": ["$a", "DoG"] } } } } ], "cursor": {}, "collation": { "locale": "fr", "strength" : 1} }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "coll_agg_proj", "pipeline": [ { "$replaceRoot": { "newRoot": { "a": "$a", "newField": { "$gte": ["$a", "doG"] } } } } ], "cursor": {}, "collation": { "locale": "en", "strength" : 3} }');

-- test $replaceRoot auto $$NOW generation
BEGIN;
SET LOCAL documentdb.enableNowSystemVariable='off';
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "coll_agg_proj", "pipeline": [ { "$replaceRoot": { "newRoot": { "a": "$a", "newField": { "$gte": ["$a", "doG"] } } } } ], "cursor": {}, "collation": { "locale": "en", "strength" : 3} }');
EXPLAIN (VERBOSE ON, COSTS OFF) SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "coll_agg_proj", "pipeline": [ { "$replaceRoot": { "newRoot": { "a": "$a", "newField": { "$gte": ["$a", "doG"] } } } } ], "cursor": {}, "collation": { "locale": "en", "strength" : 3} }');
ROLLBACK;

-- $replaceWith
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "coll_agg_proj", "pipeline": [ { "$replaceWith": { "a": "$a", "newField": { "$eq": ["$a", "CAT"] } } } ], "cursor": {}, "collation": { "locale": "en", "strength" : 1} }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "coll_agg_proj", "pipeline": [ { "$replaceWith": { "a": "$a", "newField": { "$ne": ["$a", "CAT"] } } } ], "cursor": {}, "collation": { "locale": "en", "strength" : 3} }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "coll_agg_proj", "pipeline": [ { "$replaceWith": { "a": "$a", "newField": { "$lte": ["$a", "DoG"] } } } ], "cursor": {}, "collation": { "locale": "fr", "strength" : 1} }');

-- test $replaceWith auto $$NOW generation
BEGIN;
SET LOCAL documentdb.enableNowSystemVariable='off';
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "coll_agg_proj", "pipeline": [ { "$replaceWith": { "a": "$a", "newField": { "$gte": ["$a", "doG"] } } } ], "cursor": {}, "collation": { "locale": "en", "strength" : 3} }');
EXPLAIN (VERBOSE ON, COSTS OFF) SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "coll_agg_proj", "pipeline": [ { "$replaceWith": { "a": "$a", "newField": { "$gte": ["$a", "doG"] } } } ], "cursor": {}, "collation": { "locale": "en", "strength" : 3} }');
ROLLBACK;

-- $documents
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": 1, "pipeline": [ { "$documents": { "$cond": { "if": { "$eq": [ "CaT", "cAt" ] }, "then": [{"result": "case insensitive"}] , "else": [{"res": "case sensitive"}] }} } ], "cursor": {}, "collation": { "locale": "en", "strength" : 1} }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": 1, "pipeline": [ { "$documents": { "$cond": { "if": { "$eq": [ "CaT", "cAt" ] }, "then": [{"result": "case insensitive"}] , "else": [{"res": "case sensitive"}] }} } ], "cursor": {}, "collation": { "locale": "en", "strength" : 3} }');

-- test $documents without auto variables ($$NOW) generation
BEGIN;
SET LOCAL documentdb.enableNowSystemVariable='off';
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": 1, "pipeline": [ { "$documents": { "$cond": { "if": { "$eq": [ "CaT", "cAt" ] }, "then": [{"result": "case insensitive"}] , "else": [{"res": "case sensitive"}] }} } ], "cursor": {}, "collation": { "locale": "en", "strength" : 3} }');
EXPLAIN (VERBOSE ON, COSTS OFF) SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": 1, "pipeline": [ { "$documents": { "$cond": { "if": { "$eq": [ "CaT", "cAt" ] }, "then": [{"result": "case insensitive"}] , "else": [{"res": "case sensitive"}] }} } ], "cursor": {}, "collation": { "locale": "en", "strength" : 3} }');
ROLLBACK;

-- $sortArray
SELECT documentdb_api.insert_one('db', 'coll_sortArray', '{"_id":1,"a":"one", "b":["10","1"]}', NULL);
SELECT documentdb_api.insert_one('db', 'coll_sortArray', '{"_id":2,"a":"two", "b":["2","020"]}', NULL);

SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "coll_sortArray", "pipeline": [ { "$project": { "sortedArray": { "$sortArray": { "input": ["cat", "dog", "DOG"], "sortBy": 1 } } } } ], "cursor": {}, "collation": { "locale": "en", "strength" : 1} }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "coll_sortArray", "pipeline": [ { "$project": { "sortedArray": { "$sortArray": { "input": ["cat", "dog", "DOG"], "sortBy": 1 } } } } ], "cursor": {}, "collation": { "locale": "en", "strength" : 3} }');

SELECT document FROM bson_aggregation_find('db', '{
  "find": "coll_sortArray",
  "filter": { "a": {"$lte": "two"} },
  "projection": {
    "sortedArray": { "$sortArray": { "input": "$b", "sortBy": 1 } }
  },
  "collation": { "locale": "en", "numericOrdering" : false },
  "limit": 5
}');

SELECT document FROM bson_aggregation_find('db', '{
  "find": "coll_sortArray",
  "filter": { "a": {"$lte": "two"} },
  "projection": {
    "sortedArray": { "$sortArray": { "input": "$b", "sortBy": 1 } }
  },
  "collation": { "locale": "en", "numericOrdering" : true },
  "limit": 5
}');

SELECT documentdb_api.drop_collection('db', 'coll_sortArray');

-- find
SELECT document FROM bson_aggregation_find('db', '{ "find": "coll_agg_proj", "projection": { "a": 1, "newField": { "$eq": ["$a", "CAT"] } }, "sort": { "_id": 1 }, "skip": 0, "limit": 5, "collation": { "locale": "en", "strength" : 1} }');
SELECT document FROM bson_aggregation_find('db', '{ "find": "coll_agg_proj", "projection": { "a": 1, "newField": { "$ne": ["$a", "CAT"] } }, "sort": { "_id": 1 }, "skip": 0, "limit": 5, "collation": { "locale": "en", "strength" : 2} }');
SELECT document FROM bson_aggregation_find('db', '{ "find": "coll_agg_proj", "projection": { "a": 1, "newField": { "$ne": ["$a", "CAT"] } }, "sort": { "_id": 1 }, "skip": 0, "limit": 5, "collation": { "locale": "en", "strength" : 3} }');
SELECT document FROM bson_aggregation_find('db', '{ "find": "coll_agg_proj", "projection": { "a": 1, "newField": { "$gte": ["$a", "CAT"] } }, "sort": { "_id": 1 }, "skip": 0, "limit": 5, "collation": { "locale": "en", "strength" : 1} }');
SELECT document FROM bson_aggregation_find('db', '{ "find": "coll_agg_proj", "projection": { "a": 1, "newField": { "$gte": ["$a", "CAT"] } }, "sort": { "_id": 1 }, "skip": 0, "limit": 5, "collation": { "locale": "en", "strength" : 3} }');

-- test $find without auto variables ($$NOW) generation
BEGIN;
SET LOCAL documentdb.enableNowSystemVariable='off';
SELECT document FROM bson_aggregation_find('db', '{ "find": "coll_agg_proj", "projection": { "a": 1, "newField": { "$gte": ["$a", "CAT"] } }, "sort": { "_id": 1 }, "skip": 0, "limit": 5, "collation": { "locale": "en", "strength" : 3} }');
EXPLAIN (VERBOSE ON, COSTS OFF) SELECT document FROM bson_aggregation_find('db', '{ "find": "coll_agg_proj", "projection": { "a": 1, "newField": { "$gte": ["$a", "CAT"] } }, "sort": { "_id": 1 }, "skip": 0, "limit": 5, "collation": { "locale": "en", "strength" : 3} }');
ROLLBACK;

-- $redact
SELECT documentdb_api.insert_one('db','coll_redact','{ "_id": 1, "level": "public", "content": "content 1", "details": { "level": "public", "value": "content 1.1", "moreDetails": { "level": "restricted", "info": "content 1.1.1" } } }', NULL);
SELECT documentdb_api.insert_one('db','coll_redact','{ "_id": 2, "level": "restricted", "content": "content 2", "details": { "level": "public", "value": "content 2.1", "moreDetails": { "level": "restricted", "info": "content 2.1.1" } } }', NULL);
SELECT documentdb_api.insert_one('db','coll_redact','{ "_id": 3, "level": "public", "content": "content 3", "details": { "level": "restricted", "value": "content 3.1", "moreDetails": { "level": "public", "info": "content 3.1.1" } } }', NULL);
SELECT documentdb_api.insert_one('db','coll_redact','{ "_id": 4, "content": "content 4", "details": { "level": "public", "value": "content 4.1" } }', NULL);
SELECT documentdb_api.insert_one('db','coll_redact','{ "_id": 5, "level": "public", "content": "content 5", "details": { "level": "public", "value": "content 5.1", "moreDetails": [{ "level": "restricted", "info": "content 5.1.1" }, { "level": "public", "info": "content 5.1.2" }] } }', NULL);

SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "coll_redact", "pipeline": [ { "$redact": { "$cond": { "if": { "$eq": ["$level", "PUBLIC"] }, "then": "$$KEEP", "else": "$$PRUNE" } } }  ], "cursor": {}, "collation": { "locale": "en", "strength" : 1} }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "coll_redact", "pipeline": [ { "$redact": { "$cond": { "if": { "$eq": ["$level", "puBliC"] }, "then": "$$DESCEND", "else": "$$PRUNE" } } }  ], "cursor": {}, "collation": { "locale": "en", "strength" : 1} }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "coll_redact", "pipeline": [ { "$redact": { "$switch": { "branches": [ { "case": { "$eq": ["$level", "PUBLIC"] }, "then": "$$PRUNE" }, { "case": { "$eq": ["$classification", "RESTRICTED"] }, "then": { "$cond": { "if": { "$eq": ["$content", null] }, "then": "$$KEEP", "else": "$$PRUNE" } } }], "default": "$$KEEP" } }  }], "cursor": {}, "collation": { "locale": "en", "strength" : 1} }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "coll_redact", "pipeline": [ { "$redact": { "$switch": { "branches": [ { "case": { "$eq": ["$level", "PUBLIC"] }, "then": "$$PRUNE" }, { "case": { "$eq": ["$classification", "RESTRICTED"] }, "then": { "$cond": { "if": { "$eq": ["$content", null] }, "then": "$$KEEP", "else": "$$PRUNE" } } }], "default": "$$KEEP" } }  }], "cursor": {}, "collation": { "locale": "en", "strength" : 1} }');

-- test $redact auto $$NOW generation
BEGIN;
SET LOCAL documentdb.enableNowSystemVariable='off';
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "coll_redact", "pipeline": [ { "$redact": { "$switch": { "branches": [ { "case": { "$eq": ["$level", "PUBLIC"] }, "then": "$$PRUNE" }, { "case": { "$eq": ["$classification", "RESTRICTED"] }, "then": { "$cond": { "if": { "$eq": ["$content", null] }, "then": "$$KEEP", "else": "$$PRUNE" } } }], "default": "$$KEEP" } }  }], "cursor": {}, "collation": { "locale": "en", "strength" : 1} }');
EXPLAIN (VERBOSE ON, COSTS OFF) SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "coll_redact", "pipeline": [ { "$redact": { "$switch": { "branches": [ { "case": { "$eq": ["$level", "PUBLIC"] }, "then": "$$PRUNE" }, { "case": { "$eq": ["$classification", "RESTRICTED"] }, "then": { "$cond": { "if": { "$eq": ["$content", null] }, "then": "$$KEEP", "else": "$$PRUNE" } } }], "default": "$$KEEP" } }  }], "cursor": {}, "collation": { "locale": "en", "strength" : 1} }');
ROLLBACK;

-- support for $setEquals
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "coll_agg_proj", "pipeline": [ { "$project": { "a": 1, "newField": {"$setEquals": [["$a"], ["CAT"]]} } }], "cursor": {}, "collation": { "locale": "en", "strength" : 1} }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "coll_agg_proj", "pipeline": [ { "$project": { "a": 1, "newField": {"$setEquals": [["$a"], ["DOG"]]} } }], "cursor": {}, "collation": { "locale": "en", "strength" : 2} }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "coll_agg_proj", "pipeline": [ { "$project": { "a": 1, "newField": {"$setEquals": [["$a"], ["DOG", "dOg"]]} } }], "cursor": {}, "collation": { "locale": "en", "strength" : 2} }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "coll_agg_proj", "pipeline": [ { "$project": { "a": 1, "newField": {"$setEquals": [["$a", "dog"], ["CAT", "DOG"]]} } }], "cursor": {}, "collation": { "locale": "en", "strength" : 1} }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "coll_agg_proj", "pipeline": [ { "$project": { "a": 1, "newField": {"$setEquals": [["$a", "cAT", "dog"], ["CAT", "DOG"]]} } }], "cursor": {}, "collation": { "locale": "en", "strength" : 1} }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "coll_agg_proj", "pipeline": [ { "$project": { "a": 1, "newField": {"$setEquals": [["$a", "cAT", "dog"], ["CAT", "DOG"]]} } }], "cursor": {}, "collation": { "locale": "en", "strength" : 3} }');

-- support for $setIntersection
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "coll_agg_proj", "pipeline": [ { "$project": { "a": 1, "newField": {"$setIntersection": [["$a"], ["CAT"]]} } }], "cursor": {}, "collation": { "locale": "en", "strength" : 1} }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "coll_agg_proj", "pipeline": [ { "$project": { "a": 1, "newField": {"$setIntersection": [["$a"], ["DOG"]]} } }], "cursor": {}, "collation": { "locale": "en", "strength" : 2} }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "coll_agg_proj", "pipeline": [ { "$project": { "a": 1, "newField": {"$setIntersection": [["$a"], ["DOG", "dOg"]]} } }], "cursor": {}, "collation": { "locale": "en", "strength" : 2} }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "coll_agg_proj", "pipeline": [ { "$project": { "a": 1, "newField": {"$setIntersection": [["$a", "dog"], ["CAT", "DOG"]]} } }], "cursor": {}, "collation": { "locale": "en", "strength" : 1} }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "coll_agg_proj", "pipeline": [ { "$project": { "a": 1, "newField": {"$setIntersection": [["$a", "cAT", "dog"], ["CAT", "DOG"]]} } }], "cursor": {}, "collation": { "locale": "en", "strength" : 1} }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "coll_agg_proj", "pipeline": [ { "$project": { "a": 1, "newField": {"$setIntersection": [["$a", "cAT", "dog"], ["CAT", "DOG"]]} } }], "cursor": {}, "collation": { "locale": "en", "strength" : 3} }');

-- support for $setUnion
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "coll_agg_proj", "pipeline": [ { "$project": { "a": 1, "newField": {"$setUnion": [["$a"], ["CAT"]]} } }], "cursor": {}, "collation": { "locale": "en", "strength" : 1} }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "coll_agg_proj", "pipeline": [ { "$project": { "a": 1, "newField": {"$setUnion": [["$a"], ["DOG"]]} } }], "cursor": {}, "collation": { "locale": "en", "strength" : 2} }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "coll_agg_proj", "pipeline": [ { "$project": { "a": 1, "newField": {"$setUnion": [["$a"], ["DOG", "dOg"]]} } }], "cursor": {}, "collation": { "locale": "en", "strength" : 2} }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "coll_agg_proj", "pipeline": [ { "$project": { "a": 1, "newField": {"$setUnion": [["$a", "dog"], ["CAT", "DOG"]]} } }], "cursor": {}, "collation": { "locale": "en", "strength" : 1} }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "coll_agg_proj", "pipeline": [ { "$project": { "a": 1, "newField": {"$setUnion": [["$a", "cAT", "dog"], ["CAT", "DOG"]]} } }], "cursor": {}, "collation": { "locale": "en", "strength" : 1} }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "coll_agg_proj", "pipeline": [ { "$project": { "a": 1, "newField": {"$setUnion": [["$a", "cAT", "dog"], ["CAT", "DOG"]]} } }], "cursor": {}, "collation": { "locale": "en", "strength" : 3} }');

-- support for $setDifference
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "coll_agg_proj", "pipeline": [ { "$project": { "a": 1, "newField": {"$setDifference": [["$a"], ["CAT"]]} } }], "cursor": {}, "collation": { "locale": "en", "strength" : 1} }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "coll_agg_proj", "pipeline": [ { "$project": { "a": 1, "newField": {"$setDifference": [["$a"], ["DOG"]]} } }], "cursor": {}, "collation": { "locale": "en", "strength" : 2} }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "coll_agg_proj", "pipeline": [ { "$project": { "a": 1, "newField": {"$setDifference": [["$a"], ["DOG", "dOg"]]} } }], "cursor": {}, "collation": { "locale": "en", "strength" : 2} }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "coll_agg_proj", "pipeline": [ { "$project": { "a": 1, "newField": {"$setDifference": [["$a", "dog"], ["CAT", "DOG"]]} } }], "cursor": {}, "collation": { "locale": "en", "strength" : 1} }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "coll_agg_proj", "pipeline": [ { "$project": { "a": 1, "newField": {"$setDifference": [["$a", "cAT", "dog"], ["CAT", "DOG"]]} } }], "cursor": {}, "collation": { "locale": "en", "strength" : 1} }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "coll_agg_proj", "pipeline": [ { "$project": { "a": 1, "newField": {"$setDifference": [["$a", "cAT", "dog"], ["CAT", "DOG"]]} } }], "cursor": {}, "collation": { "locale": "en", "strength" : 3} }');

-- support for $setIsSubset
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "coll_agg_proj", "pipeline": [ { "$project": { "a": 1, "newField": {"$setIsSubset": [["$a"], ["CAT"]]} } }], "cursor": {}, "collation": { "locale": "en", "strength" : 1} }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "coll_agg_proj", "pipeline": [ { "$project": { "a": 1, "newField": {"$setIsSubset": [["$a"], ["DOG"]]} } }], "cursor": {}, "collation": { "locale": "en", "strength" : 2} }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "coll_agg_proj", "pipeline": [ { "$project": { "a": 1, "newField": {"$setIsSubset": [["$a"], ["DOG", "dOg"]]} } }], "cursor": {}, "collation": { "locale": "en", "strength" : 2} }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "coll_agg_proj", "pipeline": [ { "$project": { "a": 1, "newField": {"$setIsSubset": [["$a", "dog"], ["CAT", "DOG"]]} } }], "cursor": {}, "collation": { "locale": "en", "strength" : 1} }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "coll_agg_proj", "pipeline": [ { "$project": { "a": 1, "newField": {"$setIsSubset": [["$a", "cAT", "dog"], ["CAT", "DOG"]]} } }], "cursor": {}, "collation": { "locale": "en", "strength" : 1} }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "coll_agg_proj", "pipeline": [ { "$project": { "a": 1, "newField": {"$setIsSubset": [["$a", "cAT", "dog"], ["CAT", "DOG"]]} } }], "cursor": {}, "collation": { "locale": "en", "strength" : 3} }');

-- support in $let
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "coll_agg_proj", "pipeline": [ { "$project": { "a": 1, "newField": { "$let": { "vars": { "var1": "$a" }, "in": { "$cond": { "if": { "$eq": ["$$var1", "CAT"] }, "then": 1, "else": 0 } } } } } }], "cursor": {}, "collation": { "locale": "en", "strength" : 1} }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "coll_agg_proj", "pipeline": [ { "$project": { "a": 1, "newField": { "$let": { "vars": { "var1": "$a" }, "in": { "$cond": { "if": { "$eq": ["$$var1", "CAT"] }, "then": 1, "else": 0 } } } } } }], "cursor": {}, "collation": { "locale": "en", "strength" : 3} }');

-- support for $zip
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "coll_agg_proj", "pipeline": [ { "$project": { "a": 1, "newField": { "$zip": { "inputs": [ {"$cond": [{"$eq": ["CAT", "$a"]}, ["$a"], ["null"]]}, ["$a"]] } } } }], "cursor": {}, "collation": { "locale": "en", "strength" : 1} }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "coll_agg_proj", "pipeline": [ { "$project": { "a": 1, "newField": { "$zip": { "inputs": [ {"$cond": [{"$eq": ["CAT", "$a"]}, ["$a"], ["null"]]}, ["$a"]] } } } }], "cursor": {}, "collation": { "locale": "en", "strength" : 3} }');

-- test without auto variables ($$NOW) generation 
BEGIN;
SET LOCAL documentdb.enableNowSystemVariable='off';
SELECT document FROM bson_aggregation_find('db', '{ "find": "coll_agg_proj", "projection": { "a": 1, "newField": { "$eq": ["$a", "CAT"] } }, "sort": { "_id": 1 }, "skip": 0, "limit": 5, "collation": { "locale": "en", "strength" : 1} }');
EXPLAIN (VERBOSE ON, COSTS OFF)  SELECT document FROM bson_aggregation_find('db', '{ "find": "coll_agg_proj", "projection": { "a": 1, "newField": { "$eq": ["$a", "CAT"] } }, "sort": { "_id": 1 }, "skip": 0, "limit": 5, "collation": { "locale": "en", "strength" : 1} }');
ROLLBACK;

-- query match
-- ignore collation (make sure all 3 GUCs are off)
SET documentdb.enableLetAndCollationForQueryMatch TO off;
SET documentdb.enableVariablesSupportForWriteCommands TO off;
SET documentdb_core.enableCollation TO off;

SELECT documentdb_api_internal.bson_query_match('{"a": "cat"}', '{"a": "CAT"}', '{}', 'en-u-ks-level1');

-- enforce collation
SET documentdb_core.enableCollation TO on;
SET documentdb.enableLetAndCollationForQueryMatch TO on;

-- query match: _id tests
SELECT documentdb_api_internal.bson_query_match('{"_id": "cat"}', '{"_id": "CAT"}', '{}', 'en-u-ks-level1');
SELECT documentdb_api_internal.bson_query_match('{"_id": "cat"}', '{"_id": "CAT"}', '{}', 'en-u-ks-level2');
SELECT documentdb_api_internal.bson_query_match('{"_id": "cat"}', '{"_id": "CAT"}', '{}', 'en-US-u-ks-level2');

-- query match: $eq
SELECT documentdb_api_internal.bson_query_match('{"a": "cat"}', '{"a": "CAT"}', '{}', 'en-u-ks-level1');
SELECT documentdb_api_internal.bson_query_match('{"a": "cat"}', '{ "a": {"$eq" : "CAT"} }', '{}', 'de-u-ks-level1');
SELECT documentdb_api_internal.bson_query_match('{"a": "cat"}', '{ "a": {"$eq" : "càt"} }', '{}', 'fr-u-ks-level3');
SELECT documentdb_api_internal.bson_query_match('{"a": "cat", "b": "dog"}', '{"a": "CAT", "b": "DOG"}', '{}', 'en-u-ks-level1');
SELECT documentdb_api_internal.bson_query_match('{"a": "cat", "b": "dog"}', '{"a": "CAT", "b": "DOG"}', '{}', 'sv-u-ks-level1');

-- query match: $ne
SELECT documentdb_api_internal.bson_query_match('{"a": "cat"}', '{ "a": {"$ne" : "CAT"} }', '{}', 'de-u-ks-level1');
SELECT documentdb_api_internal.bson_query_match('{"a": "cat"}', '{ "a": {"$ne" : "càt"} }', '{}', 'fr-u-ks-level3');
SELECT documentdb_api_internal.bson_query_match('{"a": "cat", "b": "dog"}', '{"a": "CAT", "b": "DOG"}', '{}', 'en-u-ks-level1');
SELECT documentdb_api_internal.bson_query_match('{"a": "cat", "b": "dog"}', '{"a": "CAT", "b": "DOG"}', '{}', 'sv-u-ks-level1');

-- query match: $gt/$gte
SELECT documentdb_api_internal.bson_query_match('{"a": "cat"}', '{ "a": {"$gt" : "CAT"} }', '{}', 'de-u-ks-level1');
SELECT documentdb_api_internal.bson_query_match('{"a": "cat"}', '{ "a": {"$gte" : "CAT"} }', '{}', 'en-u-ks-level1');

-- query match: $lt/$lte
SELECT documentdb_api_internal.bson_query_match('{"a": "cat"}', '{ "a": {"$lte" : "CAT"} }', '{}', 'de-u-ks-level1');
SELECT documentdb_api_internal.bson_query_match('{"a": "cat"}', '{ "a": {"$lte" : "càt"} }', '{}', 'fr-u-ks-level3');
SELECT documentdb_api_internal.bson_query_match('{"a": "cat"}', '{ "a": {"$lte" : "càt"} }', '{}', 'fr-CA-u-ks-level3');

-- query match: $in
SELECT documentdb_api_internal.bson_query_match('{"a": "cat"}', '{ "a": {"$in" : ["CAT", "DOG"]} }', '{}', 'de-u-ks-level1');
SELECT documentdb_api_internal.bson_query_match('{"a": "cat"}', '{ "a": {"$in" : ["càt", "dòg"]} }', '{}', 'fr-u-ks-level3');

-- query match: $nin
SELECT documentdb_api_internal.bson_query_match('{"a": "cat"}', '{ "a": {"$nin" : ["CAT", "DOG"]} }', '{}', 'en-u-ks-level1');
SELECT documentdb_api_internal.bson_query_match('{"a": "cat"}', '{ "a": {"$nin" : ["càt", "dòg"]} }', '{}', 'fr-u-ks-level3');

-- query match: sharded collection
ALTER SYSTEM SET documentdb_core.enablecollation='on';
SELECT pg_reload_conf();

SELECT documentdb_api.insert_one('db', 'coll_qm_sharded', '{ "_id": "cat", "a": "cat" }');
SELECT documentdb_api.insert_one('db', 'coll_qm_sharded', '{ "_id": "dog", "a": "dog" }');
SELECT documentdb_api.insert_one('db', 'coll_qm_sharded', '{ "_id": 3, "a": "peacock" }');

-- query match: single shard key
SELECT documentdb_api.shard_collection('db', 'coll_qm_sharded', '{ "_id": "hashed" }', false);

-- we expect the query to be distributed: shard key value is collation-aware
BEGIN;
SELECT document from documentdb_api.collection('db', 'coll_qm_sharded') WHERE documentdb_api_internal.bson_query_match(document, '{ "_id": "CAT" }', '{}', 'en-u-ks-level1');
EXPLAIN (COSTS OFF) SELECT document from documentdb_api.collection('db', 'coll_qm_sharded') WHERE documentdb_api_internal.bson_query_match(document, '{ "_id": "CAT" }', '{}', 'en-u-ks-level1');
ROLLBACK;

-- we do not expect the query to be distributed: shard key value is not collation-aware
BEGIN;
SELECT document from documentdb_api.collection('db', 'coll_qm_sharded') WHERE documentdb_api_internal.bson_query_match(document, '{ "_id": 3 }', '{}', 'en-u-ks-level1');
EXPLAIN (COSTS OFF) SELECT document from documentdb_api.collection('db', 'coll_qm_sharded') WHERE documentdb_api_internal.bson_query_match(document, '{ "_id": 3 }', '{}', 'en-u-ks-level1');
ROLLBACK;

-- query match: compound shard key
SELECT documentdb_api.drop_collection('db', 'coll_qm_sharded');

SELECT documentdb_api.insert_one('db', 'coll_qm_sharded', '{ "_id": "cAt", "a": "cAt" }');
SELECT documentdb_api.insert_one('db', 'coll_qm_sharded', '{ "_id": "doG", "a": "DOg" }');
SELECT documentdb_api.insert_one('db', 'coll_qm_sharded', '{ "_id": 3, "a": "doG" }');

SELECT documentdb_api.shard_collection('db', 'coll_qm_sharded', '{ "_id": "hashed", "a": "hashed" }', false);

-- we expect the query to be distributed: shard key filter values is collation-aware
BEGIN;
SELECT document from documentdb_api.collection('db', 'coll_qm_sharded') WHERE documentdb_api_internal.bson_query_match(document, '{ "_id": "CAT", "a": "CAT" }', '{}', 'en-u-ks-level1');
EXPLAIN (COSTS OFF) SELECT document from documentdb_api.collection('db', 'coll_qm_sharded') WHERE documentdb_api_internal.bson_query_match(document, '{ "_id": "CAT", "a": "CAT" }', '{}', 'en-u-ks-level1');
ROLLBACK;

BEGIN;
SELECT document from documentdb_api.collection('db', 'coll_qm_sharded') WHERE documentdb_api_internal.bson_query_match(document, '{"$and": [{"_id": "cat", "a": "1"}, {"_id": 3, "a": 3}] }', '{}', 'en-u-ks-level1');
EXPLAIN (COSTS OFF) SELECT document from documentdb_api.collection('db', 'coll_qm_sharded') WHERE documentdb_api_internal.bson_query_match(document, '{"$and": [{"_id": "cat", "a": "1"}, {"_id": 3, "a": 3}] }', '{}', 'en-u-ks-level1');
ROLLBACK;

BEGIN;
SELECT document from documentdb_api.collection('db', 'coll_qm_sharded') WHERE documentdb_api_internal.bson_query_match(document, '{"$or": [{"_id": "CAT", "a": "CAT"}, {"_id": 3, "a": "dog"}] }', '{}', 'en-u-ks-level1');
EXPLAIN (COSTS OFF) SELECT document from documentdb_api.collection('db', 'coll_qm_sharded') WHERE documentdb_api_internal.bson_query_match(document, '{"$or": [{"_id": "cat", "a": "CAT"}, {"_id": 3, "a": "dog"}] }', '{}', 'en-u-ks-level1');
ROLLBACK;

-- we do not expect the query to be distributed
BEGIN;
SELECT document from documentdb_api.collection('db', 'coll_qm_sharded') WHERE documentdb_api_internal.bson_query_match(document, '{ "_id": 1, "a": "CAT" }', '{}', 'en-u-ks-level1');
EXPLAIN (COSTS OFF) SELECT document from documentdb_api.collection('db', 'coll_qm_sharded') WHERE documentdb_api_internal.bson_query_match(document, '{ "_id": 1, "a": "CAT" }', '{}', 'en-u-ks-level1');
ROLLBACK;

BEGIN;
SELECT document from documentdb_api.collection('db', 'coll_qm_sharded') WHERE documentdb_api_internal.bson_query_match(document, '{ "_id": 3, "a": 4 }', '{}', 'en-u-ks-level1');
EXPLAIN (COSTS OFF) SELECT document from documentdb_api.collection('db', 'coll_qm_sharded') WHERE documentdb_api_internal.bson_query_match(document, '{ "_id": 3, "a": 4 }', '{}', 'en-u-ks-level1');
ROLLBACK;

BEGIN;
SELECT document from documentdb_api.collection('db', 'coll_qm_sharded') WHERE documentdb_api_internal.bson_query_match(document, '{"$or": [{"_id": 3, "a": "dog"}, {"_id": 3, "a": 3}] }', '{}', 'en-u-ks-level1');
EXPLAIN (COSTS OFF) SELECT document from documentdb_api.collection('db', 'coll_qm_sharded') WHERE documentdb_api_internal.bson_query_match(document, '{"$or": [{"_id": 3, "a": "dog"}, {"_id": 3, "a": 3}] }', '{}', 'en-u-ks-level1');
ROLLBACK;

-- collation on sharded collections: aggregation
SELECT documentdb_api.insert_one('db', 'coll_sharded_agg', '{ "_id": "cat", "a": "cat" }');
SELECT documentdb_api.insert_one('db', 'coll_sharded_agg', '{ "_id": "cAt", "a": "cAt" }');
SELECT documentdb_api.insert_one('db', 'coll_sharded_agg', '{ "_id": "dog", "a": "dog" }');
SELECT documentdb_api.insert_one('db', 'coll_sharded_agg', '{ "_id": "dOg", "a": "dOg" }');

-- simple shard key
SELECT documentdb_api.shard_collection('db', 'coll_sharded_agg', '{ "_id": "hashed" }', false);

-- query is distributed to all shards
SELECT document FROM bson_aggregation_find('db', '{ "find": "coll_sharded_agg", "filter": { "_id": { "$eq": "cat" } }, "sort": { "_id": 1 }, "skip": 0, "limit": 5, "collation": { "locale": "en", "strength" : 1} }');
EXPLAIN (VERBOSE ON, COSTS OFF) SELECT document FROM bson_aggregation_find('db', '{ "find": "coll_sharded_agg", "filter": { "_id": { "$eq": "cat" } }, "sort": { "_id": 1 }, "skip": 0, "limit": 5, "collation": { "locale": "en", "strength" : 1} }');

SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "coll_sharded_agg", "pipeline": [ { "$match": { "_id": { "$eq": "CAT" } } }], "cursor": {}, "collation": { "locale": "en", "strength" : 1} }');
EXPLAIN (VERBOSE ON, COSTS OFF) SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "coll_sharded_agg", "pipeline": [ { "$match": { "_id": { "$eq": "CAT" } } }], "cursor": {}, "collation": { "locale": "en", "strength" : 2} }');

-- query is not distributed to all shards (shard key value is not collation-aware)
SELECT document FROM bson_aggregation_find('db', '{ "find": "coll_sharded_agg", "filter": { "_id": { "$eq": 2 } }, "sort": { "_id": 1 }, "skip": 0, "limit": 5, "collation": { "locale": "en", "strength" : 1} }');
EXPLAIN (VERBOSE ON, COSTS OFF) SELECT document FROM bson_aggregation_find('db', '{ "find": "coll_sharded_agg", "filter": { "_id": { "$eq": 2 } }, "sort": { "_id": 1 }, "skip": 0, "limit": 5, "collation": { "locale": "en", "strength" : 1} }');

SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "coll_sharded_agg", "pipeline": [ { "$match": { "_id": { "$eq": 1 } } }], "cursor": {}, "collation": { "locale": "en", "strength" : 1} }');
EXPLAIN (VERBOSE ON, COSTS OFF) SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "coll_sharded_agg", "pipeline": [ { "$match": { "_id": { "$eq": 1 } } }], "cursor": {}, "collation": { "locale": "en", "strength" : 2} }');

-- compound shard key
SELECT documentdb_api.drop_collection('db', 'coll_sharded_agg');
SELECT documentdb_api.insert_one('db', 'coll_sharded_agg', '{ "_id": "cAt", "a": "cAt" }');
SELECT documentdb_api.insert_one('db', 'coll_sharded_agg', '{ "_id": "doG", "a": "Dog" }');

SELECT documentdb_api.shard_collection('db', 'coll_sharded_agg', '{ "_id": "hashed", "a": "hashed" }', false);

-- query is distributed to all shards (filter by shard_key_value is omitted)
SELECT document FROM bson_aggregation_find('db', '{ "find": "coll_sharded_agg", "filter": { "a": { "$eq": "Cat" }, "_id": "caT" }, "sort": { "_id": 1 }, "skip": 0, "limit": 5, "collation": { "locale": "en", "strength" : 1} }');
EXPLAIN (VERBOSE ON, COSTS OFF) SELECT document FROM bson_aggregation_find('db', '{ "find": "coll_sharded_agg", "filter": { "a": { "$eq": "Cat" }, "_id": "caT" }, "sort": { "_id": 1 }, "skip": 0, "limit": 5, "collation": { "locale": "en", "strength" : 1} }');

BEGIN;
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "coll_sharded_agg", "pipeline": [ { "$match":{"_id": "CAT", "a": "CAT"} } ], "cursor": {}, "collation": { "locale": "en", "strength" : 1} }');
EXPLAIN (COSTS OFF) SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "coll_sharded_agg", "pipeline": [ { "$match":{"_id": "CAT", "a": "CAT"} } ], "cursor": {}, "collation": { "locale": "en", "strength" : 1} }');
ROLLBACK;

SELECT document FROM bson_aggregation_find('db', '{ "find": "coll_sharded_agg", "filter": { "$or": [{"_id": "CAT", "a": "CAT"}, {"_id": 3, "a": "dog"}] }, "sort": { "_id": 1 }, "skip": 0, "limit": 5, "collation": { "locale": "en", "strength" : 1} }');
EXPLAIN (VERBOSE ON, COSTS OFF) SELECT document FROM bson_aggregation_find('db', '{ "find": "coll_sharded_agg", "filter": { "$or": [{"_id": "CAT", "a": "CAT"}, {"_id": 3, "a": "dog"}] }, "sort": { "_id": 1 }, "skip": 0, "limit": 5, "collation": { "locale": "en", "strength" : 1} }');

BEGIN;
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "coll_sharded_agg", "pipeline": [ { "$match":{"$or": [{"_id": "1", "a": "CaT"}, {"_id": "DOG"}]} } ], "cursor": {}, "collation": { "locale": "en", "strength" : 1} }');
EXPLAIN (COSTS OFF) SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "coll_sharded_agg", "pipeline": [ { "$match":{"$or": [{"_id": "1", "a": "CaT"}, {"_id": "DOG"}]} } ], "cursor": {}, "collation": { "locale": "en", "strength" : 1} }');
ROLLBACK;

SELECT document FROM bson_aggregation_find('db', '{ "find": "coll_sharded_agg", "filter": { "$and": [{"_id": "CAT", "a": "CAT"}, {"_id": 3, "a": "dog"}] }, "sort": { "_id": 1 }, "skip": 0, "limit": 5, "collation": { "locale": "en", "strength" : 1} }');
EXPLAIN (VERBOSE ON, COSTS OFF) SELECT document FROM bson_aggregation_find('db', '{ "find": "coll_sharded_agg", "filter": { "$and": [{"_id": "CAT", "a": "CAT"}, {"_id": 3, "a": "dog"}] }, "sort": { "_id": 1 }, "skip": 0, "limit": 5, "collation": { "locale": "en", "strength" : 1} }');

BEGIN;
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "coll_sharded_agg", "pipeline": [ { "$match":{"$or": [{"_id": "cat", "a": 2 }, {"_id": 1, "a": 1 }]} } ], "cursor": {}, "collation": { "locale": "en", "strength" : 1} }');
EXPLAIN (COSTS OFF) SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "coll_sharded_agg", "pipeline": [ { "$match":{"$or": [{"_id": "cat", "a": 2 }, {"_id": 1, "a": 1 }]} } ], "cursor": {}, "collation": { "locale": "en", "strength" : 1} }');
ROLLBACK;

-- query is not distributed to all shards
BEGIN;
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "coll_sharded_agg", "pipeline": [ { "$match":{"_id": 1, "a": 1 } } ], "cursor": {}, "collation": { "locale": "en", "strength" : 1} }');
EXPLAIN (COSTS OFF) SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "coll_sharded_agg", "pipeline": [ { "$match":{"_id": 1, "a": 1 } } ], "cursor": {}, "collation": { "locale": "en", "strength" : 1} }');
ROLLBACK;

-- nested arrays
SELECT documentdb_api_internal.bson_query_match('{"a": ["cat"]}', '{ "a": {"$in" : [["CAT"], "DOG"]} }', '{}', 'de-u-ks-level1');
SELECT documentdb_api_internal.bson_query_match('{"a": ["cat"]}', '{ "a": {"$in" : [["CAT"], ["DOG"]] } }', '{}', 'de-u-ks-level3');

SELECT documentdb_api.insert_one('db', 'nested_arrays', '{ "_id": 1, "a": ["dog"] }');
SELECT documentdb_api.insert_one('db', 'nested_arrays', '{ "_id": 2, "a": ["cat", "dog"] }');
SELECT documentdb_api.insert_one('db', 'nested_arrays', '{ "_id": 3, "a": [[["cat"]]] }');

SELECT document FROM bson_aggregation_find('db', '{ "find": "nested_arrays", "filter": { "a" : {"$in" : [ ["dOG"] ] }}, "sort": { "_id": 1 }, "skip": 0, "limit": 5, "collation": { "locale": "en", "strength" : 1} }');
SELECT document FROM bson_aggregation_find('db', '{ "find": "nested_arrays", "filter": { "a" : {"$in" : [ [["CAT"]] ] }}, "sort": { "_id": 1 }, "skip": 0, "limit": 5, "collation": { "locale": "en", "strength" : 1} }');
SELECT document FROM bson_aggregation_find('db', '{ "find": "nested_arrays", "filter": { "a" : {"$in" : [["CAT"], ["DOG"]] }}, "sort": { "_id": 1 }, "skip": 0, "limit": 5, "collation": { "locale": "en", "strength" : 1} }');

-- nested documents
SELECT documentdb_api_internal.bson_query_match('{"a": {"b": "cat"}}', '{ "a": {"b": "CAT"} }', '{}', 'en-u-ks-level1');
SELECT documentdb_api_internal.bson_query_match('{"a": {"b": {"c": "cat"}}}', '{ "a": {"b": {"c": "CAT"}} }', '{}', 'en-u-ks-level2');

SELECT documentdb_api.insert_one('db', 'nested_docs', '{ "_id": 1, "a": { "b": "cat" } }');
SELECT documentdb_api.insert_one('db', 'nested_docs', '{ "_id": 2, "a": { "b": "dog" } }');
SELECT documentdb_api.insert_one('db', 'nested_docs', '{ "_id": 3, "a": { "b": { "c": "cat" } } }');
SELECT documentdb_api.insert_one('db', 'nested_docs', '{ "_id": 4, "a": { "b": { "c": "dog" } } }');
SELECT documentdb_api.insert_one('db', 'nested_docs', '{ "_id": 5, "a": { "b": { "c": { "d": "cat" } } } }');

SELECT document FROM bson_aggregation_find('db', '{ "find": "nested_docs", "filter": { "a" : {"$in" : [ {"b": "dOG"} ] }}, "sort": { "_id": 1 }, "skip": 0, "limit": 5, "collation": { "locale": "en", "strength" : 1} }');
SELECT document FROM bson_aggregation_find('db', '{ "find": "nested_docs", "filter": { "a" : {"$in" : [ {"b": { "c": "dOg" }} ] }}, "sort": { "_id": 1 }, "skip": 0, "limit": 5, "collation": { "locale": "en", "strength" : 1} }');
SELECT document FROM bson_aggregation_find('db', '{ "find": "nested_docs", "filter": { "a" : {"$in" : [ {"b": { "c": { "d": "dOg" }}} ] }}, "sort": { "_id": 1 }, "skip": 0, "limit": 5, "collation": { "locale": "en", "strength" : 1} }');

-- nested documents: keys are collation-agnostic
SELECT document FROM bson_aggregation_find('db', '{ "find": "nested_docs", "filter": { "a" : {"$in" : [ {"B": "dOG"} ] }}, "sort": { "_id": 1 }, "skip": 0, "limit": 5, "collation": { "locale": "en", "strength" : 1} }');

-- nested arrays and documents
SELECT documentdb_api.insert_one('db', 'nested_arrays_docs', '{ "_id": 1, "a": { "b": ["cat"] } }');
SELECT documentdb_api.insert_one('db', 'nested_arrays_docs', '{ "_id": 2, "a": { "b": ["dog"] } }');
SELECT documentdb_api.insert_one('db', 'nested_arrays_docs', '{ "_id": 3, "a": { "b": ["cat", "dog"] } }');
SELECT documentdb_api.insert_one('db', 'nested_arrays_docs', '{ "_id": 4, "a": {"b": [["dog"]] } }');
SELECT documentdb_api.insert_one('db', 'nested_arrays_docs', '{ "_id": 5, "a": { "b": [[["cat"]]] } }');
SELECT documentdb_api.insert_one('db', 'nested_arrays_docs', '{ "_id": 6, "a": "cat" }');

SELECT document FROM bson_aggregation_find('db', '{ "find": "nested_arrays_docs", "filter": { "a" : {"$in" : [ {"b": ["dOG"]}, "CAT" ] }}, "sort": { "_id": 1 }, "skip": 0, "limit": 5, "collation": { "locale": "en", "strength" : 1} }');
SELECT document FROM bson_aggregation_find('db', '{ "find": "nested_arrays_docs", "filter": { "a" : {"$in" : [ {"b": [["dOg"]] } ] }}, "sort": { "_id": 1 }, "skip": 0, "limit": 5, "collation": { "locale": "en", "strength" : 1} }');

-- test without auto variables ($$NOW) generation
BEGIN;
SET LOCAL documentdb.enableNowSystemVariable='off';
SELECT document FROM bson_aggregation_find('db', '{ "find": "nested_arrays_docs", "filter": { "a" : {"$in" : [ {"b": ["dOG"]}, "CAT" ] }}, "sort": { "_id": 1 }, "skip": 0, "limit": 5, "collation": { "locale": "en", "strength" : 1} }');
EXPLAIN (VERBOSE ON, COSTS OFF) SELECT document FROM bson_aggregation_find('db', '{ "find": "nested_arrays_docs", "filter": { "a" : {"$in" : [ {"b": ["dOG"]}, "CAT" ] }}, "sort": { "_id": 1 }, "skip": 0, "limit": 5, "collation": { "locale": "en", "strength" : 1} }');
ROLLBACK;

SET documentdb.enableLetAndCollationForQueryMatch to off;

-- delete
SELECT documentdb_api.insert_one('db', 'coll_delete', '{"_id": "dog", "a":"dog"}');
SELECT documentdb_api.insert_one('db', 'coll_delete', '{"_id": "DOG", "a":"DOG"}');
SELECT documentdb_api.insert_one('db', 'coll_delete', '{"_id": "cat", "a":"cat"}');
SELECT documentdb_api.insert_one('db', 'coll_delete', '{"_id": "CAT", "a":"CAT"}');

-- GUC off: collation ignored
SET documentdb_core.enableCollation TO off;

BEGIN;
SELECT documentdb_api.delete('db', '{ "delete": "coll_delete", "deletes": [ {"q": {"_id": "DoG" }, "limit": 0, "collation": { "locale": "fr", "strength" : 2}} ]}');
ROLLBACK;

-- enable GUC
SET documentdb_core.enableCollation TO on;
BEGIN;
-- query on _id
SET citus.log_remote_commands TO ON;

-- _id is not collation-aware
SELECT documentdb_api.delete('db', '{ "delete": "coll_delete", "deletes": [ {"q": {"_id": 1 }, "limit": 0, "collation": { "locale": "fr", "strength" : 2}} ]}');

SELECT documentdb_api.delete('db', '{ "delete": "coll_delete", "deletes": [ {"q": {"_id": "DoG" }, "limit": 0, "collation": { "locale": "fr", "strength" : 2}} ]}');
SELECT documentdb_api.delete('db', '{ "delete": "coll_delete", "deletes": [ {"q": {"_id": "cAT" }, "limit": 0, "collation": { "locale": "fr", "strength" : 3}} ]}');
ROLLBACK;

BEGIN;
--- deleteMany
SELECT document from documentdb_api.collection('db', 'coll_delete');
SELECT documentdb_api.delete('db', '{ "delete": "coll_delete", "deletes": [ { "q": {"a": "CaT" }, "limit": 0, "collation": { "locale": "en", "strength" : 3}}]}');
SELECT documentdb_api.delete('db', '{ "delete": "coll_delete", "deletes": [ { "q": {"a": "CaT" }, "limit": 0, "collation": { "locale": "en", "strength" : 1}}]}');
SELECT documentdb_api.delete('db', '{ "delete": "coll_delete", "deletes": [ { "q": {"a": "DoG" }, "limit": 0, "collation": { "locale": "en", "strength" : 3}}]}');
SELECT documentdb_api.delete('db', '{ "delete": "coll_delete", "deletes": [ { "q": {"a": "DoG" }, "limit": 0, "collation": { "locale": "en", "strength" : 1}}]}');
SELECT document from documentdb_api.collection('db', 'coll_delete');
ROLLBACK;

BEGIN;
--- deleteOne
SELECT document from documentdb_api.collection('db', 'coll_delete');
SELECT documentdb_api.delete('db', '{ "delete": "coll_delete", "deletes": [ { "q": {"a": "CaT" }, "limit": 1, "collation": { "locale": "en", "strength" : 1}}]}');
SELECT documentdb_api.delete('db', '{ "delete": "coll_delete", "deletes": [ { "q": {"a": "CaT" }, "limit": 1, "collation": { "locale": "en", "strength" : 3}}]}');
SELECT documentdb_api.delete('db', '{ "delete": "coll_delete", "deletes": [ { "q": {"a": "DoG" }, "limit": 1, "collation": { "locale": "en", "strength" : 1}}]}');
SELECT documentdb_api.delete('db', '{ "delete": "coll_delete", "deletes": [ { "q": {"a": "DoG" }, "limit": 1, "collation": { "locale": "en", "strength" : 3}}]}');
SELECT document from documentdb_api.collection('db', 'coll_delete');
ROLLBACK;

BEGIN;
-- more operators in query
SELECT documentdb_api.delete('db', '{ "delete": "coll_delete", "deletes": [ {"q": {"a": {"$lt": "DeG"} },"limit": 1, "collation": { "locale": "fr", "strength" : 1} }] }');
SELECT documentdb_api.delete('db', '{ "delete": "coll_delete", "deletes": [ {"q": {"a": {"$lt": "DoG"}, "a": {"$lt": "Goat"} },"limit": 1, "collation": { "locale": "fr", "strength" : 2} }] }');
SELECT documentdb_api.delete('db', '{ "delete": "coll_delete", "deletes": [ {"q": {"a": { "$exists": true }, "a": {"$gt": "DoG"}, "a": {"$lt": "goat"} },"limit": 1, "collation": { "locale": "fr", "strength" : 1 } }] }');

SELECT document from documentdb_api.collection('db', 'coll_delete');
ROLLBACK;

-- test without auto variables ($$NOW) generation
BEGIN;
SET LOCAL documentdb.enableNowSystemVariable='off';
SELECT documentdb_api.delete('db', '{ "delete": "coll_delete", "deletes": [ { "q": {"a": "CaT" }, "limit": 0, "collation": { "locale": "en", "strength" : 3}}]}');
EXPLAIN (VERBOSE ON, COSTS OFF) SELECT documentdb_api.delete('db', '{ "delete": "coll_delete", "deletes": [ { "q": {"a": "CaT" }, "limit": 0, "collation": { "locale": "en", "strength" : 3}}]}');
ROLLBACK;

BEGIN;
SET LOCAL documentdb.enableNowSystemVariable='off';
SELECT documentdb_api.delete('db', '{ "delete": "coll_delete", "deletes": [ { "q": {"a": "CaT" }, "limit": 1, "collation": { "locale": "en", "strength" : 3}}]}');
EXPLAIN (VERBOSE ON, COSTS OFF) SELECT documentdb_api.delete('db', '{ "delete": "coll_delete", "deletes": [ { "q": {"a": "CaT" }, "limit": 1, "collation": { "locale": "en", "strength" : 3}}]}');
ROLLBACK;

-- delete with sort obeys collation
SELECT documentdb_api.insert_one('db', 'coll_delete_sort', '{"_id": "dog", "a": "dog"}');
SELECT documentdb_api.insert_one('db', 'coll_delete_sort', '{"_id": "DOG", "a": "dog"}');

BEGIN;
-- sort respects collation: ASC
SELECT document from documentdb_api.collection('db', 'coll_delete_sort');

SELECT collection_id AS coll_delete_sort FROM documentdb_api_catalog.collections WHERE database_name = 'db' AND collection_name = 'coll_delete_sort' \gset
SELECT documentdb_api_internal.delete_worker(
    p_collection_id=>:coll_delete_sort,
    p_shard_key_value=>:coll_delete_sort,
    p_shard_oid => 0,
    p_update_internal_spec => '{ "deleteOne": { "query": { "a": "dog" }, "collation": "en-u-ks-level3",  "sort": { "_id": 1 }, "returnDocument": 1, "returnFields": { "a": 0} } }'::bson,
    p_update_internal_docs=>null::bsonsequence,
    p_transaction_id=>null::text
) FROM documentdb_api.collection('db', 'coll_delete_sort');
    
SELECT document from documentdb_api.collection('db', 'coll_delete_sort');
ROLLBACK;

BEGIN;
-- sort respects collation: DESC
SELECT document from documentdb_api.collection('db', 'coll_delete_sort');

SELECT collection_id AS coll_delete_sort FROM documentdb_api_catalog.collections WHERE database_name = 'db' AND collection_name = 'coll_delete_sort' \gset
SELECT documentdb_api_internal.delete_worker(
    p_collection_id=>:coll_delete_sort,
    p_shard_key_value=>:coll_delete_sort,
    p_shard_oid => 0,
    p_update_internal_spec => '{ "deleteOne": { "query": { "a": "dog" }, "collation": "en-u-ks-level3",  "sort": { "_id": -1 }, "returnDocument": 1, "returnFields": { "a": 0} } }'::bson,
    p_update_internal_docs=>null::bsonsequence,
    p_transaction_id=>null::text
) FROM documentdb_api.collection('db', 'coll_delete_sort');
    
SELECT document from documentdb_api.collection('db', 'coll_delete_sort');
ROLLBACK;

-- $in: []
BEGIN;
SELECT document from documentdb_api.collection('db', 'coll_delete');
SELECT documentdb_api.delete('db', '{ "delete": "coll_delete", "deletes": [ { "q": {"_id": {"$in": []} }, "limit": 0, "collation": { "locale": "en", "strength" : 1}}]}');
SELECT documentdb_api.delete('db', '{ "delete": "coll_delete", "deletes": [ { "q": {"_id": {"$in": []} }, "limit": 1, "collation": { "locale": "en", "strength" : 1}}]}');
ROLLBACK;

-- delete on sharded collection
SELECT documentdb_api.shard_collection('db', 'coll_delete', '{ "a": "hashed" }', false);

BEGIN;
--- deleteMany
SELECT document from documentdb_api.collection('db', 'coll_delete');
SELECT documentdb_api.delete('db', '{ "delete": "coll_delete", "deletes": [ { "q": {"a": "CaT" }, "limit": 0, "collation": { "locale": "en", "strength" : 3}}]}');
SELECT documentdb_api.delete('db', '{ "delete": "coll_delete", "deletes": [ { "q": {"a": "CaT" }, "limit": 0, "collation": { "locale": "en", "strength" : 1}}]}');
SELECT documentdb_api.delete('db', '{ "delete": "coll_delete", "deletes": [ { "q": {"a": "DoG" }, "limit": 0, "collation": { "locale": "en", "strength" : 3}}]}');
SELECT documentdb_api.delete('db', '{ "delete": "coll_delete", "deletes": [ { "q": {"a": "DoG" }, "limit": 0, "collation": { "locale": "en", "strength" : 1}}]}');
SELECT document from documentdb_api.collection('db', 'coll_delete');
ROLLBACK;

-- deleteOne: error: no _id filter and no shard key value filter
SELECT documentdb_api.delete('db', '{ "delete": "coll_delete", "deletes": [ { "q": {"b": "CaT" }, "limit": 1, "collation": { "locale": "en", "strength" : 1}}]}');

-- deleteOne: error: no _id filter and collation-aware shard key value filter
SELECT documentdb_api.delete('db', '{ "delete": "coll_delete", "deletes": [ { "q": {"a": "CaT" }, "limit": 1, "collation": { "locale": "en", "strength" : 3}}]}');

BEGIN;
--- deleteOne
SELECT document from documentdb_api.collection('db', 'coll_delete');
SELECT documentdb_api.delete('db', '{ "delete": "coll_delete", "deletes": [ { "q": {"_id": "CaT", "a": "CaT" }, "limit": 1, "collation": { "locale": "en", "strength" : 1}}]}');
SELECT documentdb_api.delete('db', '{ "delete": "coll_delete", "deletes": [ { "q": {"_id": "dog", "a": "DoG" }, "limit": 1, "collation": { "locale": "en", "strength" : 1}}]}');
SELECT document from documentdb_api.collection('db', 'coll_delete');
ROLLBACK;

BEGIN;
--- deleteOne
SELECT document from documentdb_api.collection('db', 'coll_delete');
SELECT documentdb_api.delete('db', '{ "delete": "coll_delete", "deletes": [ { "q": {"_id": "CaT" }, "limit": 1, "collation": { "locale": "en", "strength" : 1}}]}');
SELECT documentdb_api.delete('db', '{ "delete": "coll_delete", "deletes": [ { "q": {"_id": "CaT" }, "limit": 1, "collation": { "locale": "en", "strength" : 3}}]}');
SELECT documentdb_api.delete('db', '{ "delete": "coll_delete", "deletes": [ { "q": {"_id": "DoG" }, "limit": 1, "collation": { "locale": "en", "strength" : 1}}]}');
SELECT documentdb_api.delete('db', '{ "delete": "coll_delete", "deletes": [ { "q": {"_id": "DoG" }, "limit": 1, "collation": { "locale": "en", "strength" : 3}}]}');
SELECT document from documentdb_api.collection('db', 'coll_delete');
ROLLBACK;

SELECT documentdb_api.shard_collection('db', 'coll_delete_sort', '{ "a": "hashed" }', false);

BEGIN;
-- sort respects collation: ASC
SELECT document from documentdb_api.collection('db', 'coll_delete_sort');

SELECT collection_id AS coll_delete_sort FROM documentdb_api_catalog.collections WHERE database_name = 'db' AND collection_name = 'coll_delete_sort' \gset
SELECT documentdb_api_internal.delete_worker(
    p_collection_id=>:coll_delete_sort,
    p_shard_key_value=>:coll_delete_sort,
    p_shard_oid => 0,
    p_update_internal_spec => '{ "deleteOne": { "query": { "_id": "dog" }, "collation": "en-u-ks-level3",  "sort": { "_id": 1 }, "returnDocument": 1, "returnFields": { "a": 0} } }'::bson,
    p_update_internal_docs=>null::bsonsequence,
    p_transaction_id=>null::text
) FROM documentdb_api.collection('db', 'coll_delete_sort');
    
SELECT document from documentdb_api.collection('db', 'coll_delete_sort');
ROLLBACK;

BEGIN;
-- sort respects collation: DESC
SELECT document from documentdb_api.collection('db', 'coll_delete_sort');

SELECT collection_id AS coll_delete_sort FROM documentdb_api_catalog.collections WHERE database_name = 'db' AND collection_name = 'coll_delete_sort' \gset
SELECT documentdb_api_internal.delete_worker(
    p_collection_id=>:coll_delete_sort,
    p_shard_key_value=>:coll_delete_sort,
    p_shard_oid => 0,
    p_update_internal_spec => '{ "deleteOne": { "query": { "a": "dog" }, "collation": "en-u-ks-level3",  "sort": { "_id": -1 }, "returnDocument": 1, "returnFields": { "a": 0} } }'::bson,
    p_update_internal_docs=>null::bsonsequence,
    p_transaction_id=>null::text
) FROM documentdb_api.collection('db', 'coll_delete_sort');
    
SELECT document from documentdb_api.collection('db', 'coll_delete_sort');
ROLLBACK;

-- test without auto variables ($$NOW) generation
BEGIN;
SET LOCAL documentdb.enableNowSystemVariable='off';
SELECT documentdb_api.delete('db', '{ "delete": "coll_delete", "deletes": [ { "q": {"a": "CaT" }, "limit": 0, "collation": { "locale": "en", "strength" : 3}}]}');
EXPLAIN (VERBOSE ON, COSTS OFF) SELECT documentdb_api.delete('db', '{ "delete": "coll_delete", "deletes": [ { "q": {"a": "CaT" }, "limit": 0, "collation": { "locale": "en", "strength" : 3}}]}');
ROLLBACK;

BEGIN;
SET LOCAL documentdb.enableNowSystemVariable='off';
SELECT documentdb_api.delete('db', '{ "delete": "coll_delete", "deletes": [ { "q": {"_id": "DoG" }, "limit": 1, "collation": { "locale": "en", "strength" : 3}}]}');
EXPLAIN (VERBOSE ON, COSTS OFF) SELECT documentdb_api.delete('db', '{ "delete": "coll_delete", "deletes": [ { "q": {"_id": "DoG" }, "limit": 1, "collation": { "locale": "en", "strength" : 3}}]}');
ROLLBACK;

-- $in: []
BEGIN;
SELECT document from documentdb_api.collection('db', 'coll_delete');
SELECT documentdb_api.delete('db', '{ "delete": "coll_delete", "deletes": [ { "q": {"_id": {"$in": []} }, "limit": 0, "collation": { "locale": "en", "strength" : 1}}]}');
SELECT documentdb_api.delete('db', '{ "delete": "coll_delete", "deletes": [ { "q": {"_id": {"$in": []} }, "limit": 1, "collation": { "locale": "en", "strength" : 1}}]}');
ROLLBACK;

SELECT documentdb_api.drop_collection('db', 'coll_delete');
SELECT documentdb_api.drop_collection('db', 'coll_delete_sort');

-- find with positional queries
SELECT documentdb_api.insert_one('db', 'coll_find_positional', '{"_id":1, "a":"cat", "b":[{"a":"cat"},{"a":"caT"}], "c": ["cat"]}', NULL);
SELECT documentdb_api.insert_one('db', 'coll_find_positional', '{"_id":2, "a":"dog", "b":[{"a":"dog"},{"a":"doG"}], "c": ["dog"]}', NULL);
SELECT documentdb_api.insert_one('db', 'coll_find_positional', '{"_id":3, "a":"caT", "b":[{"a":"caT"},{"a":"cat"}], "c": ["caT"]}', NULL);

SELECT document FROM bson_aggregation_find('db', '{
  "find": "coll_find_positional",
  "filter": { "a": "CAT", "b": { "$elemMatch": { "a": "CAT" } } },
  "projection": { "_id": 1, "b.$": 1 },
  "sort": { "_id": 1 },
  "skip": 0,
  "limit": 5,
  "collation": { "locale": "en", "strength" : 3}
}');

SELECT document FROM bson_aggregation_find('db', '{
  "find": "coll_find_positional",
  "filter": { "a": "CAT", "b": { "$elemMatch": { "a": "CAT" } } },
  "projection": { "_id": 1, "b.$": 1 },
  "sort": { "_id": 1 },
  "skip": 0,
  "limit": 5,
  "collation": { "locale": "en", "strength" : 1 }
}');

EXPLAIN (VERBOSE ON, COSTS OFF) SELECT document FROM bson_aggregation_find('db', '{
  "find": "coll_find_positional",
  "filter": { "a": "CAT", "b": { "$elemMatch": { "a": "CAT" } } },
  "projection": { "_id": 1, "b.$": 1 },
  "sort": { "_id": 1 },
  "skip": 0,
  "limit": 5,
  "collation": { "locale": "en", "strength" : 1 }
}');

-- sharded: find with positional queries
SELECT documentdb_api.shard_collection('db', 'coll_find_positional', '{ "a": "hashed" }', false);

SELECT document FROM bson_aggregation_find('db', '{
  "find": "coll_find_positional",
  "filter": { "a": "CAT", "b": { "$elemMatch": { "a": "CAT" } } },
  "projection": { "_id": 1, "b.$": 1 },
  "sort": { "_id": 1 },
  "skip": 0,
  "limit": 5,
  "collation": { "locale": "en", "strength" : 3}
}');

SELECT document FROM bson_aggregation_find('db', '{
  "find": "coll_find_positional",
  "filter": { "a": "CAT", "b": { "$elemMatch": { "a": "CAT" } } },
  "projection": { "_id": 1, "b.$": 1 },
  "sort": { "_id": 1 },
  "skip": 0,
  "limit": 5,
  "collation": { "locale": "en", "strength" : 1 }
}');

EXPLAIN (VERBOSE ON, COSTS OFF) SELECT document FROM bson_aggregation_find('db', '{
  "find": "coll_find_positional",
  "filter": { "a": "CAT", "b": { "$elemMatch": { "a": "CAT" } } },
  "projection": { "_id": 1, "b.$": 1 },
  "sort": { "_id": 1 },
  "skip": 0,
  "limit": 5,
  "collation": { "locale": "en", "strength" : 1 }
}');

SELECT documentdb_api.drop_collection('db', 'coll_find_positional');


-- $in: []
SELECT documentdb_api.insert_one('db', 'collTest', '{"_id": 1, "name": "cat", "sound": "meow"}');
SELECT documentdb_api.insert_one('db', 'collTest', '{"_id": 2, "name": "dog", "sound": "woof"}');
SELECT documentdb_api.insert_one('db', 'collTest', '{"_id": 3, "sound": "moo"}');
SELECT documentdb_api.insert_one('db', 'collTest', '{"_id": 4, "name": "sheep", "sound": "baa"}');
SELECT documentdb_api.insert_one('db', 'collTest', '{"_id": 5, "name": "duck"}');

-- unsharded

-- $in: [] with bson_dollar_add_fields
SELECT document FROM bson_aggregation_pipeline('db', '{ 
  "aggregate": "collTest", 
  "pipeline": [ {"$match": {"_id": { "$in": [] }}}, { "$addFields": { "newField": "animal" } } ], 
  "cursor": {} }');

-- with variableSpec and collation
SELECT document FROM bson_aggregation_pipeline('db', '{ 
  "aggregate": "collTest", 
  "pipeline": [ {"$match": {"_id": { "$in": [] }}}, { "$addFields": { "newField": "animal" } } ], 
  "let": { "varRef": "lion"},
  "cursor": {} }');

SELECT document FROM bson_aggregation_pipeline('db', '{ 
  "aggregate": "collTest", 
  "pipeline": [ {"$match": {"_id": { "$in": [] }}}, { "$addFields": { "newField": "animal" } } ], 
  "collation": { "locale": "en", "strength" : 1},
  "cursor": {} }');

SELECT document FROM bson_aggregation_pipeline('db', '{ 
  "aggregate": "collTest", 
  "pipeline": [ {"$match": {"sound": "moo", "_id": { "$in": [] }}}, { "$addFields": { "newField": "$$varRef" } } ], 
  "let": { "varRef": "lion"},
  "collation": { "locale": "en", "strength" : 1},
  "cursor": {} }');

-- $in: [] with bson_dollar_redact
SELECT document FROM bson_aggregation_pipeline('db', '{ 
  "aggregate": "collTest", 
  "pipeline": [ {"$match": {"_id": { "$in": [] }}}, { "$redact": { "$cond": [ { "$eq": [ "$sound", "meow" ] }, "$$KEEP", "$$PRUNE" ] } } ], 
  "cursor": {} }');

-- $in: [] with bson_dollar_redact; with variableSpec and collation
SELECT document FROM bson_aggregation_pipeline('db', '{ 
  "aggregate": "collTest", 
  "pipeline": [ {"$match": {"owner": { "$in": [] }}}, { "$redact": { "$cond": [ { "$eq": [ "$sound", "meow" ] }, "$$KEEP", "$$PRUNE" ] } } ], 
  "let": { "varRef": "lion"},
  "cursor": {} }');

SELECT document FROM bson_aggregation_pipeline('db', '{ 
  "aggregate": "collTest", 
  "pipeline": [ {"$match": {"sound": { "$in": [] }}}, { "$redact": { "$cond": [ { "$eq": [ "$sound", "meow" ] }, "$$KEEP", "$$PRUNE" ] } } ], 
  "collation": { "locale": "en", "strength" : 1},
  "cursor": {} }');

SELECT document FROM bson_aggregation_pipeline('db', '{ 
  "aggregate": "collTest", 
  "pipeline": [ {"$match": {"_id": { "$in": [] }}}, { "$redact": { "$cond": [ { "$eq": [ "$sound", "meow" ] }, "$$KEEP", "$$PRUNE" ] } } ], 
  "let": { "varRef": "lion"},
  "collation": { "locale": "en", "strength" : 1},
  "cursor": {} }');

-- projection via bson_dollar_project (no let/collation)
SELECT document FROM bson_aggregation_pipeline('db', '{ 
  "aggregate": "collTest", 
  "pipeline": [ {"$match": {"name": { "$in": [] }}}, { "$project": { "_id": 0, "sound": 1 } } ], 
  "cursor": {} }');

-- projection via bson_dollar_project with let variables
SELECT document FROM bson_aggregation_pipeline('db', '{ 
  "aggregate": "collTest", 
  "pipeline": [ {"$match": {"_id": { "$in": [] } }}, { "$project": { "_id": 0, "name": 1, "varEcho": "$$varRef" } } ], 
  "let": { "varRef": "lion"},
  "cursor": {} }');

-- projection via bson_dollar_project with collation
SELECT document FROM bson_aggregation_pipeline('db', '{ 
  "aggregate": "collTest", 
  "pipeline": [ {"$match": {"_id": { "$in": [] } }}, { "$project": { "_id": 0, "name": 1 } } ], 
  "collation": { "locale": "en", "strength" : 1},
  "cursor": {} }');

-- projection via bson_dollar_project with let variables and collation
SELECT document FROM bson_aggregation_pipeline('db', '{ 
  "aggregate": "collTest", 
  "pipeline": [ {"$match": {"_id": { "$in": [] } }}, { "$project": { "_id": 0, "name": 1, "varEcho": "$$varRef" } } ], 
  "let": { "varRef": "lion"},
  "collation": { "locale": "en", "strength" : 1},
  "cursor": {} }');

-- replaceRoot via bson_dollar_replace_root (no let/collation)
SELECT document FROM bson_aggregation_pipeline('db', '{ 
  "aggregate": "collTest", 
  "pipeline": [ {"$match": {"name": {"$in": [] } }}, { "$replaceRoot": { "newRoot": { "animal": "$name", "call": "$sound" } } } ], 
  "cursor": {} }');

-- replaceRoot via bson_dollar_replace_root with let variables
SELECT document FROM bson_aggregation_pipeline('db', '{ 
  "aggregate": "collTest", 
  "pipeline": [ {"$match": {"name": {"$in": [] } }}, { "$replaceRoot": { "newRoot": { "animal": "$$varRef", "call": "$sound" } } } ], 
  "let": { "varRef": "lion"},
  "cursor": {} }');

-- replaceRoot via bson_dollar_replace_root with collation
SELECT document FROM bson_aggregation_pipeline('db', '{ 
  "aggregate": "collTest", 
  "pipeline": [ {"$match": {"name": {"$in": [] } }}, { "$replaceRoot": { "newRoot": { "animal": "$name", "call": "$sound" } } } ], 
  "collation": { "locale": "en", "strength" : 1},
  "cursor": {} }');

-- replaceRoot via bson_dollar_replace_root with let variables and collation
SELECT document FROM bson_aggregation_pipeline('db', '{ 
  "aggregate": "collTest", 
  "pipeline": [ {"$match": {"name": {"$in": [] } }}, { "$replaceRoot": { "newRoot": { "animal": "$$varRef", "call": "$sound" } } } ], 
  "let": { "varRef": "lion"},
  "collation": { "locale": "en", "strength" : 1},
  "cursor": {} }');

-- bson_dollar_project_find with let variables and collation
SELECT document FROM bson_aggregation_find('db', '{
  "find": "collTest",
  "filter": {
    "_id": { "$in": [] }
  },
  "projection": { "name": true },
  "let": { "varRef": "lion" },
  "collation": { "locale": "en", "strength" : 1 }
}');

-- sharded
SELECT documentdb_api.shard_collection('db', 'collTest', '{"name": "hashed"}', false);


-- $in: [] with bson_dollar_add_fields
SELECT document FROM bson_aggregation_pipeline('db', '{ 
  "aggregate": "collTest", 
  "pipeline": [ {"$match": {"_id": { "$in": [] }}}, { "$addFields": { "newField": "animal" } } ], 
  "cursor": {} }');

-- with variableSpec and collation
SELECT document FROM bson_aggregation_pipeline('db', '{ 
  "aggregate": "collTest", 
  "pipeline": [ {"$match": {"_id": { "$in": [] }}}, { "$addFields": { "newField": "animal" } } ], 
  "let": { "varRef": "lion"},
  "cursor": {} }');

SELECT document FROM bson_aggregation_pipeline('db', '{ 
  "aggregate": "collTest", 
  "pipeline": [ {"$match": {"_id": { "$in": [] }}}, { "$addFields": { "newField": "animal" } } ], 
  "collation": { "locale": "en", "strength" : 1},
  "cursor": {} }');

SELECT document FROM bson_aggregation_pipeline('db', '{ 
  "aggregate": "collTest", 
  "pipeline": [ {"$match": {"sound": "moo", "_id": { "$in": [] }}}, { "$addFields": { "newField": "$$varRef" } } ], 
  "let": { "varRef": "lion"},
  "collation": { "locale": "en", "strength" : 1},
  "cursor": {} }');

-- $in: [] with bson_dollar_redact
SELECT document FROM bson_aggregation_pipeline('db', '{ 
  "aggregate": "collTest", 
  "pipeline": [ {"$match": {"_id": { "$in": [] }}}, { "$redact": { "$cond": [ { "$eq": [ "$sound", "meow" ] }, "$$KEEP", "$$PRUNE" ] } } ], 
  "cursor": {} }');

-- $in: [] with bson_dollar_redact; with variableSpec and collation
SELECT document FROM bson_aggregation_pipeline('db', '{ 
  "aggregate": "collTest", 
  "pipeline": [ {"$match": {"owner": { "$in": [] }}}, { "$redact": { "$cond": [ { "$eq": [ "$sound", "meow" ] }, "$$KEEP", "$$PRUNE" ] } } ], 
  "let": { "varRef": "lion"},
  "cursor": {} }');

SELECT document FROM bson_aggregation_pipeline('db', '{ 
  "aggregate": "collTest", 
  "pipeline": [ {"$match": {"sound": { "$in": [] }}}, { "$redact": { "$cond": [ { "$eq": [ "$sound", "meow" ] }, "$$KEEP", "$$PRUNE" ] } } ], 
  "collation": { "locale": "en", "strength" : 1},
  "cursor": {} }');

SELECT document FROM bson_aggregation_pipeline('db', '{ 
  "aggregate": "collTest", 
  "pipeline": [ {"$match": {"_id": { "$in": [] }}}, { "$redact": { "$cond": [ { "$eq": [ "$sound", "meow" ] }, "$$KEEP", "$$PRUNE" ] } } ], 
  "let": { "varRef": "lion"},
  "collation": { "locale": "en", "strength" : 1},
  "cursor": {} }');

-- projection via bson_dollar_project (no let/collation)
SELECT document FROM bson_aggregation_pipeline('db', '{ 
  "aggregate": "collTest", 
  "pipeline": [ {"$match": {"name": { "$in": [] }}}, { "$project": { "_id": 0, "sound": 1 } } ], 
  "cursor": {} }');

-- projection via bson_dollar_project with let variables
SELECT document FROM bson_aggregation_pipeline('db', '{ 
  "aggregate": "collTest", 
  "pipeline": [ {"$match": {"_id": { "$in": [] } }}, { "$project": { "_id": 0, "name": 1, "varEcho": "$$varRef" } } ], 
  "let": { "varRef": "lion"},
  "cursor": {} }');

-- projection via bson_dollar_project with collation
SELECT document FROM bson_aggregation_pipeline('db', '{ 
  "aggregate": "collTest", 
  "pipeline": [ {"$match": {"_id": { "$in": [] } }}, { "$project": { "_id": 0, "name": 1 } } ], 
  "collation": { "locale": "en", "strength" : 1},
  "cursor": {} }');

-- projection via bson_dollar_project with let variables and collation
SELECT document FROM bson_aggregation_pipeline('db', '{ 
  "aggregate": "collTest", 
  "pipeline": [ {"$match": {"_id": { "$in": [] } }}, { "$project": { "_id": 0, "name": 1, "varEcho": "$$varRef" } } ], 
  "let": { "varRef": "lion"},
  "collation": { "locale": "en", "strength" : 1},
  "cursor": {} }');

-- replaceRoot via bson_dollar_replace_root (no let/collation)
SELECT document FROM bson_aggregation_pipeline('db', '{ 
  "aggregate": "collTest", 
  "pipeline": [ {"$match": {"name": {"$in": [] } }}, { "$replaceRoot": { "newRoot": { "animal": "$name", "call": "$sound" } } } ], 
  "cursor": {} }');

-- replaceRoot via bson_dollar_replace_root with let variables
SELECT document FROM bson_aggregation_pipeline('db', '{ 
  "aggregate": "collTest", 
  "pipeline": [ {"$match": {"name": {"$in": [] } }}, { "$replaceRoot": { "newRoot": { "animal": "$$varRef", "call": "$sound" } } } ], 
  "let": { "varRef": "lion"},
  "cursor": {} }');

-- replaceRoot via bson_dollar_replace_root with collation
SELECT document FROM bson_aggregation_pipeline('db', '{ 
  "aggregate": "collTest", 
  "pipeline": [ {"$match": {"name": {"$in": [] } }}, { "$replaceRoot": { "newRoot": { "animal": "$name", "call": "$sound" } } } ], 
  "collation": { "locale": "en", "strength" : 1},
  "cursor": {} }');

-- replaceRoot via bson_dollar_replace_root with let variables and collation
SELECT document FROM bson_aggregation_pipeline('db', '{ 
  "aggregate": "collTest", 
  "pipeline": [ {"$match": {"name": {"$in": [] } }}, { "$replaceRoot": { "newRoot": { "animal": "$$varRef", "call": "$sound" } } } ], 
  "let": { "varRef": "lion"},
  "collation": { "locale": "en", "strength" : 1},
  "cursor": {} }');

-- bson_dollar_project_find with let variables and collation
SELECT document FROM bson_aggregation_find('db', '{
  "find": "collTest",
  "filter": {
    "_id": { "$in": [] }
  },
  "projection": { "name": true },
  "let": { "varRef": "lion" },
  "collation": { "locale": "en", "strength" : 1 }
}');

SELECT documentdb_api.drop_collection('db', 'collTest');

ALTER SYSTEM SET documentdb_core.enablecollation='off';
SELECT pg_reload_conf();