SET search_path to helio_core,helio_api,helio_api_catalog,pg_catalog;
SET citus.next_shard_id TO 7990000;
SET helio_api.next_collection_id TO 7990;
SET helio_api.next_collection_index_id TO 7990;

SET helio_api.enableCollation TO on;

-- (1) insert some docs
SELECT helio_api.insert_one('db', 'ci_search', '{ "_id": 1, "a": "Cat" }');
SELECT helio_api.insert_one('db', 'ci_search', '{ "_id": 2, "a": "dog" }');
SELECT helio_api.insert_one('db', 'ci_search', '{ "_id": 3, "a": "cat" }');
SELECT helio_api.insert_one('db', 'ci_search', '{ "_id": 4, "a": "Dog" }');
SELECT helio_api.insert_one('db', 'ci_search', '{ "_id": 5, "a": "caT" }');
SELECT helio_api.insert_one('db', 'ci_search', '{ "_id": 6, "a": "doG" }');
SELECT helio_api.insert_one('db', 'ci_search', '{ "_id": 7, "a": "goat" }');
SELECT helio_api.insert_one('db', 'ci_search', '{ "_id": 8, "a": "Goat" }');
SELECT helio_api.insert_one('db', 'ci_search', '{ "_id": 9, "b": "Cat" }');
SELECT helio_api.insert_one('db', 'ci_search', '{ "_id": 10, "b": "dog" }');
SELECT helio_api.insert_one('db', 'ci_search', '{ "_id": 11, "b": "cat" }');
SELECT helio_api.insert_one('db', 'ci_search', '{ "_id": 12, "b": "Dog" }');
SELECT helio_api.insert_one('db', 'ci_search', '{ "_id": 13, "b": "caT", "a" : "raBbIt" }');
SELECT helio_api.insert_one('db', 'ci_search', '{ "_id": 14, "b": "doG" }');
SELECT helio_api.insert_one('db', 'ci_search', '{ "_id": 15, "b": "goat" }');
SELECT helio_api.insert_one('db', 'ci_search', '{ "_id": 16, "b": "Goat" }');
SELECT helio_api.insert_one('db', 'ci_search', '{ "_id": 17, "a": ["Cat", "CAT", "dog"] }');
SELECT helio_api.insert_one('db', 'ci_search', '{ "_id": 18, "a": ["dog", "cat", "CAT"] }');
SELECT helio_api.insert_one('db', 'ci_search', '{ "_id": 19, "a": ["cat", "rabbit", "bAt"] }');
SELECT helio_api.insert_one('db', 'ci_search', '{ "_id": 20, "a": ["Cat"] }');
SELECT helio_api.insert_one('db', 'ci_search', '{ "_id": 21, "a": ["dog"] }');
SELECT helio_api.insert_one('db', 'ci_search', '{ "_id": 22, "a": ["cat"] }');
SELECT helio_api.insert_one('db', 'ci_search', '{ "_id": 23, "a": ["CAT"] }');
SELECT helio_api.insert_one('db', 'ci_search', '{ "_id": 24, "a": ["cAt"] }');


-- (2) Find query unsharded collection
SELECT document FROM bson_aggregation_find('db', '{ "find": "ci_search", "filter": { "a": { "$eq": "cat" } }, "sort": { "_id": 1 }, "skip": 0, "limit": 5, "collation": { "locale": "en", "strength" : 1} }');
EXPLAIN(VERBOSE ON, COSTS OFF) SELECT document FROM bson_aggregation_find('db', '{ "find": "ci_search", "filter": { "a": { "$eq": "cat" } }, "sort": { "_id": 1 }, "skip": 0, "limit": 5, "collation": { "locale": "en", "strength" : 1} }');

SELECT document FROM bson_aggregation_find('db', '{ "find": "ci_search", "filter": { "b": { "$eq": "cat" } }, "sort": { "_id": 1 }, "skip": 0, "limit": 5, "collation": { "locale": "en", "strength" : 1} }');
EXPLAIN(VERBOSE ON, COSTS OFF) SELECT document FROM bson_aggregation_find('db', '{ "find": "ci_search", "filter": { "b": { "$eq": "cat" } }, "sort": { "_id": 1 }, "skip": 0, "limit": 5, "collation": { "locale": "en", "strength" : 1} }');

SELECT document FROM bson_aggregation_find('db', '{ "find": "ci_search", "filter": { "$or" : [{ "a": { "$eq": "cat" } }, { "a": { "$eq": "DOG" } }] }, "sort": { "_id": 1 }, "skip": 0, "limit": 5, "collation": { "locale": "en", "strength" : 1} }');
SELECT document FROM bson_aggregation_find('db', '{ "find": "ci_search", "filter": { "$or" : [{ "a": { "$eq": "cat" } }, { "b": { "$eq": "DOG" } }] }, "sort": { "_id": 1 }, "skip": 0, "limit": 10, "collation": { "locale": "en", "strength" : 1 } }');
SELECT document FROM bson_aggregation_find('db', '{ "find": "ci_search", "filter": { "a": { "$all": ["cAt", "DOG"] } }, "skip": 0, "limit":
 5, "collation": { "locale": "en", "strength": 1} }');

-- range query without index on path "a"
SELECT document FROM bson_aggregation_find('db', '{ "find": "ci_search", "filter": { "a": { "$gt": "CAT" }, "a" : {"$lt" : "RABBIT"} }, "collation": { "locale": "en", "strength" : 1.93 } }');
EXPLAIN(VERBOSE ON, COSTS OFF) SELECT document FROM bson_aggregation_find('db', '{ "find": "ci_search", "filter": { "a": { "$gt": "CAT" }, "a" : {"$lt" : "RABBIT"} }, "collation": { "locale": "en", "strength" : 1.93 } }');


-- (3) Shard collection
SELECT helio_api.shard_collection('db', 'ci_search', '{ "_id": "hashed" }', false);


-- (4) Find query sharded collection
BEGIN;
SET LOCAL helio_api.enableCollation TO on;
SELECT document FROM bson_aggregation_find('db', '{ "find": "ci_search", "filter": { "a": { "$eq": "cat" } }, "sort": { "_id": 1 }, "skip": 0, "limit": 5, "collation": { "locale": "en", "strength" : 1} }');
END; 
BEGIN;
SET LOCAL helio_api.enableCollation TO on;
EXPLAIN(VERBOSE ON, COSTS OFF) SELECT document FROM bson_aggregation_find('db', '{ "find": "ci_search", "filter": { "a": { "$eq": "cat" } }, "sort": { "_id": 1 }, "skip": 0, "limit": 5, "collation": { "locale": "en", "strength" : 1} }');
END;

BEGIN;
SET LOCAL helio_api.enableCollation TO on;
SELECT document FROM bson_aggregation_find('db', '{ "find": "ci_search", "filter": { "b": { "$eq": "cat" } }, "sort": { "_id": 1 }, "skip": 0, "limit": 5, "collation": { "locale": "en", "strength" : 1} }');
END;
BEGIN;
SET LOCAL helio_api.enableCollation TO on;
EXPLAIN(VERBOSE ON, COSTS OFF) SELECT document FROM bson_aggregation_find('db', '{ "find": "ci_search", "filter": { "b": { "$eq": "cat" } }, "sort": { "_id": 1 }, "skip": 0, "limit": 5, "collation": { "locale": "en", "strength" : 1} }');
END;

-- elemMatch with collation
BEGIN;
SET LOCAL helio_api.enableCollation TO on;
SELECT document FROM bson_aggregation_find('db', '{ "find": "ci_search", "filter": { "a": { "$elemMatch": {"$eq": "cAt"} } }, "skip": 0, "limit": 7, "collation": { "locale": "en", "strength" : 1} }');
SELECT document FROM bson_aggregation_find('db', '{ "find": "ci_search", "filter": { "a": { "$elemMatch": {"$gt": "cAt"} } }, "skip": 0, "limit": 7, "collation": { "locale": "en", "strength" : 1} }');
SELECT document FROM bson_aggregation_find('db', '{ "find": "ci_search", "filter": { "a": { "$elemMatch": {"$lt": "cAt"} } }, "skip": 0, "limit": 7, "collation": { "locale": "en", "strength" : 1} }');
SELECT document FROM bson_aggregation_find('db', '{ "find": "ci_search", "filter": { "a": { "$elemMatch": {"$eq": "cAt", "gte" : "BAT"} } }, "skip": 0, "limit": 7, "collation": { "locale": "en", "strength" : 1} }');
END;

-- (5) Aggregation queries sharded collection
BEGIN;
SET LOCAL helio_api.enableCollation TO on;
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "ci_search", "pipeline": [ { "$sort": { "_id": 1 } }, { "$match": { "a": { "$eq": "cat" } } } ], "cursor": {}, "collation": { "locale": "en", "strength" : 1}  }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "ci_search", "pipeline": [ { "$match": { "a": { "$gt": "DOG" } } }], "cursor": {}, "collation": { "locale": "en", "strength" : 1} }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "ci_search", "pipeline": [ { "$match": { "a": { "$eq": "RABBIT" } } }, { "$project": { "b": 1 } }], "cursor": {}, "collation": { "locale": "en", "strength" : 1} }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "ci_search", "pipeline": [ { "$match": { "_id": { "$gt": 1 } } }, { "$project": { "b": 1, "c": "$a", "_id": 0 } }, { "$match": { "c": { "$eq": "rAbBiT" } } }], "cursor": {}, "collation": { "locale": "en", "strength" : 1} }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "ci_search", "pipeline": [ { "$unwind": "$a" },  {"$match": { "a": { "$gt": "POP", "$lt": "TOP" } } } ], "cursor": {}, "collation": { "locale": "en", "strength" : 1} }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "ci_search", "pipeline": [ { "$match": { "a": { "$gte": "hobbit" } } }, { "$unwind": "$a" } ], "cursor": {}, "collation": { "locale": "en", "strength" : 1} }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "ci_search", "pipeline": [ { "$addFields": { "x": "mANgO" } }, { "$addFields": { "Y": "TANGO" } }, { "$match": { "$and" : [{ "a": { "$gte": "POMELO" }}, { "x": { "$eq": "MANGO" }}]}} ], "cursor": {}, "collation": { "locale": "en", "strength" : 1} }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "ci_search", "pipeline": [ { "$addFields": { "e": {  "f": "$a" } } }, { "$replaceRoot": { "newRoot": "$e" } }, { "$match" : { "f": { "$elemMatch": {"$eq": "cAt"} } } } ],
 "cursor": {}, "collation": { "locale": "en", "strength" : 1} }');
END;

-- (6) currently unsupported scenarions: 

-- (6.A) $in,$nin are not yet suported with collation
BEGIN;
SET LOCAL helio_api.enableCollation TO on;
SELECT document FROM bson_aggregation_find('db', '{ "find": "ci_search", "filter": { "a" : {"$in" : ["cat", "DOG" ] }}, "sort": { "_id": 1 }, "skip": 0, "limit": 5, "collation": { "locale": "en", "strength" : 1} }');
END;
-- (6.B) $cond needs collationString to be plumbed
BEGIN;
SET LOCAL helio_api.enableCollation TO on;
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "ci_search", "pipeline": [ { "$sort": { "_id": 1 } }, { "$addFields": { "e": {  "f": "$a" } } }, { "$replaceRoot": { "newRoot": "$e" } }, { "$match" : { "f": { "$elemMatch": {"$eq": "cAt"} } } }, {"$project": { "items" : { "$filter" : { "input" : "$f", "as" : "animal", "cond" : { "$eq" : ["$$animal", "CAT"] } }} }} ],
 "cursor": {}, "collation": { "locale": "en", "strength" : 1} }');
END;

-- (7) Insert More docs
SELECT helio_api.insert_one('db', 'ci_search2', '{ "_id": 1, "a": "Cat" }');
SELECT helio_api.insert_one('db', 'ci_search2', '{ "_id": 2, "a": "dog" }');
SELECT helio_api.insert_one('db', 'ci_search2', '{ "_id": 3, "a": "cat" }');
SELECT helio_api.insert_one('db', 'ci_search2', '{ "_id": 4, "a": "CaT" }');
SELECT helio_api.insert_one('db', 'ci_search2', '{ "_id": 5, "b": "Dog" }');
SELECT helio_api.insert_one('db', 'ci_search2', '{ "_id": 6, "b": "DoG" }');


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

SELECT helio_api.find_and_modify('fam', '{"findAndModify": "ci_search2", "query": {"a": 1}, "update": {"_id": 1, "b": 1}, "collation" : {"locale" : "en", "strength": 1} }');
SELECT helio_api.update('update', '{"update":"ci_search2", "updates":[{"q":{"_id": 134111, "b": [ 5, 2, 4 ] },"u":{"$set" : {"b.$[a]":3} },"upsert":true, "collation" : {"locale" : "en", "strength": 1}, "arrayFilters": [ { "a": 2 } ]}]}');
SELECT helio_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "ci_search2", "indexes": [{"key": {"asparse": 1}, "name": "my_sparse_idx1", "sparse": true, "collation" : {"locale" : "en", "strength": 1}}]}', TRUE);


-- (12) Test Id filters respect collation

SELECT helio_api.insert_one('db', 'ci_search3', '{ "_id": "Cat" }');
SELECT helio_api.insert_one('db', 'ci_search3', '{ "_id": "dog" }');
SELECT helio_api.insert_one('db', 'ci_search3', '{ "_id": "cat" }');
SELECT helio_api.insert_one('db', 'ci_search3', '{ "_id": "CaT" }');
SELECT helio_api.insert_one('db', 'ci_search3', '{ "_id": "Dog" }');
SELECT helio_api.insert_one('db', 'ci_search3', '{ "_id": "DoG" }');
SELECT helio_api.insert_one('db', 'ci_search3', '{ "_id": { "a" : "cat" } }');
SELECT helio_api.insert_one('db', 'ci_search3',' { "_id": { "a": "CAT"} }');
SELECT helio_api.insert_one('db', 'ci_search3', '{ "a": { "a": "Dog" } } ');
SELECT helio_api.insert_one('db', 'ci_search3', '{ "_id": [ "cat", "CAT "] }');

SELECT document FROM bson_aggregation_find('db', '{ "find": "ci_search3", "filter": { "$or" : [{ "_id": { "$eq": "cat" } }, { "_id": { "$eq": "DOG" } }] }, "sort": { "_id": 1 }, "skip": 0, "limit": 5, "collation": { "locale": "en", "strength" : 1} }');
SELECT document FROM bson_aggregation_find('db', '{ "find": "ci_search3", "filter": { "$or" : [{ "_id.a": { "$eq": "cat" } }, { "_id.a": { "$eq": "DOG" } }] }, "sort": { "_id": 1 }, "skip": 0, "limit": 5, "collation": { "locale": "en", "strength" : 1} }');
EXPLAIN(VERBOSE ON, COSTS OFF)SELECT document FROM bson_aggregation_find('db', '{ "find": "ci_search3", "filter": { "$or" : [{ "_id.a": { "$eq": "cat" } }, { "_id.a": { "$eq": "DOG" } }] }, "sort": { "_id": 1 }, "skip": 0, "limit": 5, "collation": { "locale": "en", "strength" : 1} }');


SELECT helio_api.shard_collection('db', 'ci_search', '{ "_id": "hashed" }', false);
BEGIN;
SET LOCAL helio_api.enableCollation TO on;
SELECT document FROM bson_aggregation_find('db', '{ "find": "ci_search3", "filter": { "$or" : [{ "_id": { "$eq": "cat" } }, { "_id": { "$eq": "DOG" } }] }, "sort": { "_id": 1 }, "skip": 0, "limit": 5, "collation": { "locale": "en", "strength" : 1} }');
END;

BEGIN;
SET LOCAL helio_api.enableCollation TO on;
SELECT document FROM bson_aggregation_find('db', '{ "find": "ci_search3", "filter": { "$or" : [{ "_id.a": { "$eq": "cat" } }, { "_id.a": { "$eq": "DOG" } }] }, "sort": { "_id": 1 }, "skip": 0, "limit": 5, "collation": { "locale": "en", "strength" : 1} }');
END;

EXPLAIN(VERBOSE ON, COSTS OFF)SELECT document FROM bson_aggregation_find('db', '{ "find": "ci_search3", "filter": { "$or" : [{ "_id.a": { "$eq": "cat" } }, { "_id.a": { "$eq": "DOG" } }] }, "sort": { "_id": 1 }, "skip": 0, "limit": 5, "collation": { "locale": "en", "strength" : 1} }');


-- (12) Check index with partial filter expression with collation

SELECT helio_api.insert_one('db','ci_search4', '{"_id": 1, "a" : { "b" : "DOG" }}', NULL);
SELECT helio_api.insert_one('db','ci_search4', '{"_id": 2, "a" : { "b" : "dog" }}', NULL);
SELECT helio_api.insert_one('db','ci_search4', '{"_id": 3, "a" : { "b" : "Cat" }}', NULL);
SELECT helio_api.insert_one('db','ci_search4', '{"_id": 4, "a" : { "b" : "Dog" }}', NULL);
SELECT helio_api.insert_one('db','ci_search4', '{"_id": 5, "a" : { "b" : "cAT" }}', NULL);
SELECT helio_api.insert_one('db','ci_search4', '{"_id": 6, "a" : { "b" : "DoG" }}', NULL);
SELECT helio_api.insert_one('db','ci_search4', '{"_id": 7, "a" : { "b" : "DOG" }}', NULL);

SELECT helio_api_internal.create_indexes_non_concurrently(
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

SELECT helio_api_internal.create_indexes_non_concurrently(
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
SET LOCAL helio_api.forceUseIndexIfAvailable to true;
-- query pushed to the index when no collattion
SELECT document FROM bson_aggregation_find('db', '{ "find": "ci_search4", "filter": { "a.b": { "$eq": "dog" }, "a": { "$ne" :  null } } }');
EXPLAIN (VERBOSE ON, COSTS OFF) SELECT document FROM bson_aggregation_find('db', '{ "find": "ci_search4", "filter": { "a.b": { "$eq": "dog" }, "a": { "$ne" :  null } } }');
ROLLBACK;


BEGIN;
SET LOCAL helio_api.enableCollation TO on;
SET LOCAL seq_page_cost TO 100;
SET LOCAL helio_api.forceUseIndexIfAvailable to true;
-- query not pushed to the index when collattion is specified
SELECT document FROM bson_aggregation_find('db', '{ "find": "ci_search4", "filter": { "a.b": { "$eq": "dog" }, "a": { "$ne" :  null } }, "collation": { "locale": "en", "strength" : 1}  }');
EXPLAIN (VERBOSE ON, COSTS OFF) SELECT document FROM bson_aggregation_find('db', '{ "find": "ci_search4", "filter": { "a.b": { "$eq": "dog" }, "a": { "$ne" :  null } }, "collation": { "locale": "en", "strength" : 1}  }');
END;

SELECT helio_api.shard_collection('db', 'ci_search4', '{ "_id": "hashed" }', false);

BEGIN;
SET LOCAL helio_api.enableCollation TO on;
SELECT document FROM bson_aggregation_find('db', '{ "find": "ci_search4", "filter": { "$or" : [{ "a.b": { "$eq": "cat" } }, { "a.b": { "$eq": "DOG" } }] }, "sort": { "_id": 1 }, "skip": 0, "limit": 5, "collation": { "locale": "en", "strength" : 1} }');
EXPLAIN (VERBOSE ON, COSTS OFF) SELECT document FROM bson_aggregation_find('db', '{ "find": "ci_search4", "filter": { "$or" : [{ "a.b": { "$eq": "cat" } }, { "a.b": { "$eq": "DOG" } }] }, "sort": { "_id": 1 }, "skip": 0, "limit": 5, "collation": { "locale": "en", "strength" : 1} }');
END;

-- (13) Check index behavior with collation (TODO: update when index pushdown of collation is supported)

SELECT helio_api.insert_one('db','ci_search5', '{"_id": 1, "a" : { "b" : "DOG" }}', NULL);
SELECT helio_api.insert_one('db','ci_search5', '{"_id": 2, "a" : { "b" : "dog" }}', NULL);
SELECT helio_api.insert_one('db','ci_search5', '{"_id": 3, "a" : { "b" : "Cat" }}', NULL);
SELECT helio_api.insert_one('db','ci_search5', '{"_id": 4, "a" : { "b" : "Dog" }}', NULL);
SELECT helio_api.insert_one('db','ci_search5', '{"_id": 5, "a" : { "b" : "cAT" }}', NULL);
SELECT helio_api.insert_one('db','ci_search5', '{"_id": 6, "a" : { "b" : "DoG" }}', NULL);
SELECT helio_api.insert_one('db','ci_search5', '{"_id": 7, "a" : { "b" : "DOG" }}', NULL);

SELECT helio_api_internal.create_indexes_non_concurrently(
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

SELECT helio_api_internal.create_indexes_non_concurrently(
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
SET LOCAL helio_api.forceUseIndexIfAvailable to true;
-- query pushed to the index when no collattion
SELECT document FROM bson_aggregation_find('db', '{ "find": "ci_search5", "filter": { "$or" : [{ "a.b": { "$eq": "cat" } }, { "a.b": { "$eq": "DOG" } }] }, "sort": { "_id": 1 }, "skip": 0, "limit": 5 }');
EXPLAIN (VERBOSE ON, COSTS OFF) SELECT document FROM bson_aggregation_find('db', '{ "find": "ci_search5", "filter": { "$or" : [{ "a.b": { "$eq": "cat" } }, { "a.b": { "$eq": "DOG" } }] }, "sort": { "_id": 1 }, "skip": 0, "limit": 5 }');

ROLLBACK;

BEGIN;
SET LOCAL helio_api.enableCollation TO on;
SET LOCAL seq_page_cost TO 100;
SET LOCAL helio_api.forceUseIndexIfAvailable to true;
-- query not pushed to the index when collation is specified
SELECT document FROM bson_aggregation_find('db', '{ "find": "ci_search5", "filter": { "$or" : [{ "a.b": { "$eq": "cat" } }, { "a.b": { "$eq": "DOG" } }] }, "sort": { "_id": 1 }, "skip": 0, "limit": 5, "collation": { "locale": "en", "strength" : 1} }');
EXPLAIN (VERBOSE ON, COSTS OFF) SELECT document FROM bson_aggregation_find('db', '{ "find": "ci_search5", "filter": { "$or" : [{ "a.b": { "$eq": "cat" } }, { "a.b": { "$eq": "DOG" } }] }, "sort": { "_id": 1 }, "skip": 0, "limit": 5, "collation": { "locale": "en", "strength" : 1} }');
END;

-- range query with index on path "a.b"
SELECT document FROM bson_aggregation_find('db', '{ "find": "ci_search5", "filter": { "a.b": { "$gt": "CAT" }, "a.b" : {"$lte" : "DOG"} }, "collation": { "locale": "en", "strength" : 1.93 } }');
EXPLAIN(VERBOSE ON, COSTS OFF)SELECT document FROM bson_aggregation_find('db', '{ "find": "ci_search5", "filter": { "a.b": { "$gt": "CAT" }, "a.b" : {"$lte" : "DOG"} }, "collation": { "locale": "en", "strength" : 1.93 } }');


SELECT helio_api.shard_collection('db', 'ci_search5', '{ "_id": "hashed" }', false);

BEGIN;
SET LOCAL helio_api.enableCollation TO on;
SET LOCAL seq_page_cost TO 100;
SET LOCAL helio_api.forceUseIndexIfAvailable to true;
SELECT document FROM bson_aggregation_find('db', '{ "find": "ci_search5", "filter": { "$or" : [{ "a.b": { "$eq": "cat" } }, { "a.b": { "$eq": "DOG" } }] }, "sort": { "_id": 1 }, "skip": 0, "limit": 5, "collation": { "locale": "en", "strength" : 1} }');
EXPLAIN (VERBOSE ON, COSTS OFF) SELECT document FROM bson_aggregation_find('db', '{ "find": "ci_search5", "filter": { "$or" : [{ "a.b": { "$eq": "cat" } }, { "a.b": { "$eq": "DOG" } }] }, "sort": { "_id": 1 }, "skip": 0, "limit": 5, "collation": { "locale": "en", "strength" : 1} }');
END;

-- nested pipleline tests

SELECT helio_api.insert_one('db','ci_search6', '{"_id": 1, "a" : { "b" : "DOG" }}', NULL);
SELECT helio_api.insert_one('db','ci_search6', '{"_id": 2, "a" : { "b" : "dog" }}', NULL);
SELECT helio_api.insert_one('db','ci_search6', '{"_id": 3, "a" : { "b" : "Cat" }}', NULL);
SELECT helio_api.insert_one('db','ci_search6', '{"_id": 4, "a" : { "b" : "Dog" }}', NULL);
SELECT helio_api.insert_one('db','ci_search6', '{"_id": 5, "a" : { "b" : "cAT" }}', NULL);
SELECT helio_api.insert_one('db','ci_search6', '{"_id": 6, "a" : { "b" : "DoG" }}', NULL);
SELECT helio_api.insert_one('db','ci_search6', '{"_id": 7, "a" : { "b" : "DOG" }}', NULL);

SELECT document FROM bson_aggregation_pipeline('db', 
    '{ "aggregate": "ci_search6", "pipeline": [ { "$lookup": { "from": "ci_search6", "as": "matched_docs", "localField": "_id", "foreignField": "_id", "pipeline": [ { "$match": { "a.b": "cat" } } ] } } ], "cursor": {}, "collation": { "locale": "en", "strength" : 1}  }');

SELECT document FROM bson_aggregation_pipeline('db',
    '{ "aggregate": "ci_search6", "pipeline": [ { "$graphLookup": { "from": "ci_search6", "startWith": "$a.b", "connectFromField": "a.b", "connectToField": "a.b", "as": "destinations", "depthField": "depth" } } ],  "collation": { "locale": "en", "strength" : 1} }');

SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "ci_search6", "pipeline": [{"$merge" : { "into": "ci_search7", "whenMatched" : "replace" }} ], "collation": { "locale": "en", "strength" : 1} }');


SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "ci_search6", "pipeline": [ { "$facet": { "a" : [ { "$match": { "a.b": "cat" } }, { "$count": "catCount" } ], "b" : [ { "$match": { "a.b": "dog" } }, { "$count": "dogCount" } ]  } } ], "cursor": {}, "collation": { "locale": "en", "strength" : 1}}');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "ci_search6", "pipeline": [ { "$unionWith": { "coll": "ci_search6", "pipeline" : [ { "$match": { "a.b": "cat" }}] } }], "cursor": {}, "collation": { "locale": "en", "strength" : 1} }');


RESET helio_api.enableCollation;