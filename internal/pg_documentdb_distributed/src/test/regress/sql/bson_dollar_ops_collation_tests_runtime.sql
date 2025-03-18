SET search_path to documentdb_core,documentdb_api,documentdb_api_catalog,pg_catalog;
SET citus.next_shard_id TO 7990000;
SET documentdb.next_collection_id TO 7990;
SET documentdb.next_collection_index_id TO 7990;

SET documentdb_core.enablecollation TO on;

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
-- (6.A) $in with nested objects
BEGIN;
SET LOCAL documentdb_core.enablecollation TO on;
SELECT document FROM bson_aggregation_find('db', '{ "find": "ci_search", "filter": { "a" : {"$in" : [{ "B" : "caT"}, { "c" : "caT"}] }}, "sort": { "_id": 1 }, "skip": 0, "limit": 100, "collation": { "locale": "en", "strength" : 1} }');
END;

BEGIN;
SET LOCAL documentdb_core.enablecollation TO on;
SELECT document FROM bson_aggregation_find('db', '{ "find": "ci_search", "filter": { "a" : {"$in" : [[{ "B" : "caT"}], [{ "c" : "caT"}]] }}, "sort": { "_id": 1 }, "skip": 0, "limit": 100, "collation": { "locale": "en", "strength" : 1} }');
END;
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

-- (9) Native Mongo error message compatibility, 
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
SELECT document FROM bson_aggregation_find('db', '{ "find": "coll_agg_proj", "filter": { "$expr": {"$ne": ["$a", "CAT"]} }, "sort": { "_id": 1 }, "skip": 0, "collation": { "locale": "fr", "strength" : 1 } }');
SELECT document FROM bson_aggregation_find('db', '{ "find": "coll_agg_proj", "filter": { "$expr": {"$lte": ["$a", "CAT"]} }, "sort": { "_id": 1 }, "skip": 0, "collation": { "locale": "en", "strength" : 1 } }');
SELECT document FROM bson_aggregation_find('db', '{ "find": "coll_agg_proj", "filter": { "$expr": {"$gte": ["$a", "CAT"]} }, "sort": { "_id": 1 }, "skip": 0, "collation": { "locale": "fr", "strength" : 3 } }');
SELECT document FROM bson_aggregation_find('db', '{ "find": "coll_agg_proj", "filter": { "$expr": {"$gt": ["$a", "CAT"]} }, "sort": { "_id": 1 }, "skip": 0, "collation": { "locale": "fr", "strength" : 1 } }');
SELECT document FROM bson_aggregation_find('db', '{ "find": "coll_agg_proj", "filter": { "$expr": { "$or": [{"$gte": [ "$a", "DOG" ]}, {"$gte": [ "$a", "CAT" ]}] } }, "sort": { "_id": 1 }, "skip": 0, "collation": { "locale": "fr", "strength" : 1 } }');
SELECT document FROM bson_aggregation_find('db', '{ "find": "coll_agg_proj", "filter": { "$expr": { "$and": [{"$lte": [ "$a", "DOG" ]}, {"$lte": [ "$a", "CAT" ]}] } }, "sort": { "_id": 1 }, "skip": 0, "collation": { "locale": "fr", "strength" : 2 } }');

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

-- $addFields
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "coll_agg_proj", "pipeline": [ { "$addFields": { "newField": { "$ne": ["$a", "CAT"] } } } ], "cursor": {}, "collation": { "locale": "fr", "strength" : 1} }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "coll_agg_proj", "pipeline": [ { "$addFields": { "newField": { "$lte": ["$a", "CAT"] } } } ], "cursor": {}, "collation": { "locale": "fr", "strength" : 1} }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "coll_agg_proj", "pipeline": [ { "$addFields": { "newField": { "$gte": ["$a", "CAT"] } } } ], "cursor": {}, "collation": { "locale": "fr", "strength" : 3} }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "coll_agg_proj", "pipeline": [ { "$addFields": { "newField": { "$gte": ["$a", "CAT"] } } } ], "cursor": {}, "collation": { "locale": "fr", "strength" : 1} }');

-- $set
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "coll_agg_proj", "pipeline": [ { "$set": { "newField": { "$ne": ["$a", "CAT"] } } } ], "cursor": {}, "collation": { "locale": "fr", "strength" : 1} }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "coll_agg_proj", "pipeline": [ { "$set": { "newField": { "$lte": ["$a", "CAT"] } } } ], "cursor": {}, "collation": { "locale": "fr", "strength" : 1} }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "coll_agg_proj", "pipeline": [ { "$set": { "newField": { "$gte": ["$a", "CAT"] } } } ], "cursor": {}, "collation": { "locale": "fr", "strength" : 3} }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "coll_agg_proj", "pipeline": [ { "$set": { "newField": { "$gte": ["$a", "CAT"] } } } ], "cursor": {}, "collation": { "locale": "fr", "strength" : 1} }');

-- project
SELECT documentdb_api_internal.bson_dollar_project(document, '{ "newField": { "$eq": ["$a", "CAT"] } }', NULL, 'en-u-ks-level1') FROM documentdb_api.collection('db', 'coll_agg_proj');
SELECT documentdb_api_internal.bson_dollar_project(document, '{ "newField": { "$eq": ["$a", "DOG"] } }', NULL, 'en-u-ks-level1') FROM documentdb_api.collection('db', 'coll_agg_proj');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "coll_agg_proj", "pipeline": [ { "$project": { "a": 1, "newField": { "$ne": ["$a", "CAT"] } } } ], "cursor": {}, "collation": { "locale": "fr", "strength" : 1} }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "coll_agg_proj", "pipeline": [ { "$project": { "a": 1, "newField": { "$lte": ["$a", "CAT"] } } } ], "cursor": {}, "collation": { "locale": "fr", "strength" : 1} }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "coll_agg_proj", "pipeline": [ { "$project": { "a": 1, "newField": { "$gte": ["$a", "CAT"] } } } ], "cursor": {}, "collation": { "locale": "fr", "strength" : 3} }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "coll_agg_proj", "pipeline": [ { "$project": { "a": 1, "newField": { "$gte": ["$a", "CAT"] } } } ], "cursor": {}, "collation": { "locale": "fr", "strength" : 1} }');

-- replaceRoot
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "coll_agg_proj", "pipeline": [ { "$replaceRoot": { "newRoot": { "a": "$a", "newField": { "$eq": ["$a", "CAT"] } } } } ], "cursor": {}, "collation": { "locale": "en", "strength" : 1} }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "coll_agg_proj", "pipeline": [ { "$replaceRoot": { "newRoot": { "a": "$a", "newField": { "$ne": ["$a", "CAT"] } } } } ], "cursor": {}, "collation": { "locale": "en", "strength" : 3} }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "coll_agg_proj", "pipeline": [ { "$replaceRoot": { "newRoot": { "a": "$a", "newField": { "$lte": ["$a", "DoG"] } } } } ], "cursor": {}, "collation": { "locale": "fr", "strength" : 1} }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "coll_agg_proj", "pipeline": [ { "$replaceRoot": { "newRoot": { "a": "$a", "newField": { "$gte": ["$a", "doG"] } } } } ], "cursor": {}, "collation": { "locale": "en", "strength" : 3} }');

-- find
SELECT document FROM bson_aggregation_find('db', '{ "find": "coll_agg_proj", "projection": { "a": 1, "newField": { "$eq": ["$a", "CAT"] } }, "sort": { "_id": 1 }, "skip": 0, "limit": 5, "collation": { "locale": "en", "strength" : 1} }');
SELECT document FROM bson_aggregation_find('db', '{ "find": "coll_agg_proj", "projection": { "a": 1, "newField": { "$ne": ["$a", "CAT"] } }, "sort": { "_id": 1 }, "skip": 0, "limit": 5, "collation": { "locale": "en", "strength" : 2} }');
SELECT document FROM bson_aggregation_find('db', '{ "find": "coll_agg_proj", "projection": { "a": 1, "newField": { "$ne": ["$a", "CAT"] } }, "sort": { "_id": 1 }, "skip": 0, "limit": 5, "collation": { "locale": "en", "strength" : 3} }');
SELECT document FROM bson_aggregation_find('db', '{ "find": "coll_agg_proj", "projection": { "a": 1, "newField": { "$gte": ["$a", "CAT"] } }, "sort": { "_id": 1 }, "skip": 0, "limit": 5, "collation": { "locale": "en", "strength" : 1} }');
SELECT document FROM bson_aggregation_find('db', '{ "find": "coll_agg_proj", "projection": { "a": 1, "newField": { "$gte": ["$a", "CAT"] } }, "sort": { "_id": 1 }, "skip": 0, "limit": 5, "collation": { "locale": "en", "strength" : 3} }');

-- redact
SELECT documentdb_api.insert_one('db','coll_redact','{ "_id": 1, "level": "public", "content": "content 1", "details": { "level": "public", "value": "content 1.1", "moreDetails": { "level": "restricted", "info": "content 1.1.1" } } }', NULL);
SELECT documentdb_api.insert_one('db','coll_redact','{ "_id": 2, "level": "restricted", "content": "content 2", "details": { "level": "public", "value": "content 2.1", "moreDetails": { "level": "restricted", "info": "content 2.1.1" } } }', NULL);
SELECT documentdb_api.insert_one('db','coll_redact','{ "_id": 3, "level": "public", "content": "content 3", "details": { "level": "restricted", "value": "content 3.1", "moreDetails": { "level": "public", "info": "content 3.1.1" } } }', NULL);
SELECT documentdb_api.insert_one('db','coll_redact','{ "_id": 4, "content": "content 4", "details": { "level": "public", "value": "content 4.1" } }', NULL);
SELECT documentdb_api.insert_one('db','coll_redact','{ "_id": 5, "level": "public", "content": "content 5", "details": { "level": "public", "value": "content 5.1", "moreDetails": [{ "level": "restricted", "info": "content 5.1.1" }, { "level": "public", "info": "content 5.1.2" }] } }', NULL);

SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "coll_redact", "pipeline": [ { "$redact": { "$cond": { "if": { "$eq": ["$level", "PUBLIC"] }, "then": "$$KEEP", "else": "$$PRUNE" } } }  ], "cursor": {}, "collation": { "locale": "en", "strength" : 1} }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "coll_redact", "pipeline": [ { "$redact": { "$cond": { "if": { "$eq": ["$level", "puBliC"] }, "then": "$$DESCEND", "else": "$$PRUNE" } } }  ], "cursor": {}, "collation": { "locale": "en", "strength" : 1} }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "coll_redact", "pipeline": [ { "$redact": { "$switch": { "branches": [ { "case": { "$eq": ["$level", "PUBLIC"] }, "then": "$$PRUNE" }, { "case": { "$eq": ["$classification", "RESTRICTED"] }, "then": { "$cond": { "if": { "$eq": ["$content", null] }, "then": "$$KEEP", "else": "$$PRUNE" } } }], "default": "$$KEEP" } }  }], "cursor": {}, "collation": { "locale": "en", "strength" : 1} }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "coll_redact", "pipeline": [ { "$redact": { "$switch": { "branches": [ { "case": { "$eq": ["$level", "PUBLIC"] }, "then": "$$PRUNE" }, { "case": { "$eq": ["$classification", "RESTRICTED"] }, "then": { "$cond": { "if": { "$eq": ["$content", null] }, "then": "$$KEEP", "else": "$$PRUNE" } } }], "default": "$$KEEP" } }  }], "cursor": {}, "collation": { "locale": "en", "strength" : 1} }');

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

-- query match
-- enableLetAndCollationForQueryMatch GUC off: ignore collation
SET documentdb.enableLetAndCollationForQueryMatch TO off;
SELECT documentdb_api_internal.bson_query_match('{"a": "cat"}', '{"a": "CAT"}', NULL, 'en-u-ks-level1');

-- enableLetAndCollationForQueryMatch GUC on: enforce collation
SET documentdb.enableLetAndCollationForQueryMatch TO on;

-- query match: _id tests
SELECT documentdb_api_internal.bson_query_match('{"_id": "cat"}', '{"_id": "CAT"}', NULL, 'en-u-ks-level1');
SELECT documentdb_api_internal.bson_query_match('{"_id": "cat"}', '{"_id": "CAT"}', NULL, 'en-u-ks-level2');

-- query match: $eq
SELECT documentdb_api_internal.bson_query_match('{"a": "cat"}', '{"a": "CAT"}', NULL, 'en-u-ks-level1');
SELECT documentdb_api_internal.bson_query_match('{"a": "cat"}', '{ "a": {"$eq" : "CAT"} }', NULL, 'de-u-ks-level1');
SELECT documentdb_api_internal.bson_query_match('{"a": "cat"}', '{ "a": {"$eq" : "càt"} }', NULL, 'fr-u-ks-level3');
SELECT documentdb_api_internal.bson_query_match('{"a": "cat", "b": "dog"}', '{"a": "CAT", "b": "DOG"}', NULL, 'en-u-ks-level1');
SELECT documentdb_api_internal.bson_query_match('{"a": "cat", "b": "dog"}', '{"a": "CAT", "b": "DOG"}', NULL, 'sv-u-ks-level1');

-- query match: $ne
SELECT documentdb_api_internal.bson_query_match('{"a": "cat"}', '{ "a": {"$ne" : "CAT"} }', NULL, 'de-u-ks-level1');
SELECT documentdb_api_internal.bson_query_match('{"a": "cat"}', '{ "a": {"$ne" : "càt"} }', NULL, 'fr-u-ks-level3');
SELECT documentdb_api_internal.bson_query_match('{"a": "cat", "b": "dog"}', '{"a": "CAT", "b": "DOG"}', NULL, 'en-u-ks-level1');
SELECT documentdb_api_internal.bson_query_match('{"a": "cat", "b": "dog"}', '{"a": "CAT", "b": "DOG"}', NULL, 'sv-u-ks-level1');

-- query match: $gt/$gte
SELECT documentdb_api_internal.bson_query_match('{"a": "cat"}', '{ "a": {"$gt" : "CAT"} }', NULL, 'de-u-ks-level1');
SELECT documentdb_api_internal.bson_query_match('{"a": "cat"}', '{ "a": {"$gte" : "CAT"} }', NULL, 'en-u-ks-level1');

-- query match: $lt/$lte
SELECT documentdb_api_internal.bson_query_match('{"a": "cat"}', '{ "a": {"$lte" : "CAT"} }', NULL, 'de-u-ks-level1');
SELECT documentdb_api_internal.bson_query_match('{"a": "cat"}', '{ "a": {"$lte" : "càt"} }', NULL, 'fr-u-ks-level3');

-- query match: $in
SELECT documentdb_api_internal.bson_query_match('{"a": "cat"}', '{ "a": {"$in" : ["CAT", "DOG"]} }', NULL, 'de-u-ks-level1');
SELECT documentdb_api_internal.bson_query_match('{"a": "cat"}', '{ "a": {"$in" : ["càt", "dòg"]} }', NULL, 'fr-u-ks-level3');

-- query match: $nin
SELECT documentdb_api_internal.bson_query_match('{"a": "cat"}', '{ "a": {"$nin" : ["CAT", "DOG"]} }', NULL, 'en-u-ks-level1');
SELECT documentdb_api_internal.bson_query_match('{"a": "cat"}', '{ "a": {"$nin" : ["càt", "dòg"]} }', NULL, 'fr-u-ks-level3');

-- not supported yet
-- query match: sharded collection with collation-aware shard key
SELECT documentdb_api.insert_one('db', 'coll_query_op', '{ "_id": "cat", "a": "cat" }');
SELECT documentdb_api.insert_one('db', 'coll_query_op', '{ "_id": "dog", "a": "dog" }');

SELECT documentdb_api.shard_collection('db', 'coll_query_op', '{ "_id": "hashed" }', false);

SELECT document from documentdb_api.collection('db', 'coll_query_op') WHERE documentdb_api_internal.bson_query_match(document, '{ "_id": "CAT" }', NULL, 'en-u-ks-level1');

RESET documentdb.enableLetAndCollationForQueryMatch;

RESET documentdb_core.enablecollation;