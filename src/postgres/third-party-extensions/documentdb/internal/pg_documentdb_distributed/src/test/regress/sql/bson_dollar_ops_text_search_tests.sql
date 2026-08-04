set search_path to documentdb_core,documentdb_api,documentdb_api_catalog,pg_catalog;
SET citus.next_shard_id TO 7980000;
SET documentdb.next_collection_id TO 7980;
SET documentdb.next_collection_index_id TO 7980;


-- insert some docs
SELECT documentdb_api.insert_one('db', 'bson_dollar_ops_text_search', '{ "_id": 1, "a": "this is a cat" }');
SELECT documentdb_api.insert_one('db', 'bson_dollar_ops_text_search', '{ "_id": 2, "a": "this is a dog" }');
SELECT documentdb_api.insert_one('db', 'bson_dollar_ops_text_search', '{ "_id": 3, "a": "these are dogs" }');
SELECT documentdb_api.insert_one('db', 'bson_dollar_ops_text_search', '{ "_id": 4, "a": "these are cats" }');
SELECT documentdb_api.insert_one('db', 'bson_dollar_ops_text_search', '{ "_id": 5, "a": "these are catatonic" }');


-- do a $text query. Should fail (there's no index)
SELECT document FROM documentdb_api.collection('db', 'bson_dollar_ops_text_search') WHERE document @@ '{ "$text": { "$search": "cat" } }';

-- create a text index.
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "bson_dollar_ops_text_search", "indexes": [ { "key": { "a": "text" }, "name": "a_text" } ] }', TRUE);

-- now do a $text query. Should succeed.
SELECT document FROM documentdb_api.collection('db', 'bson_dollar_ops_text_search') WHERE document @@ '{ "$text": { "$search": "cat" } }';
SELECT document FROM documentdb_api.collection('db', 'bson_dollar_ops_text_search') WHERE document @@ '{ "$text": { "$search": "dog" } }';
SELECT document FROM documentdb_api.collection('db', 'bson_dollar_ops_text_search') WHERE document @@ '{ "$text": { "$search": "cat | dog" } }';

EXPLAIN (COSTS OFF) SELECT document FROM documentdb_api.collection('db', 'bson_dollar_ops_text_search') WHERE document @@ '{ "$text": { "$search": "cat" } }';

-- invalid queries
-- $text on subsequent stages should fail.
WITH r1 AS (SELECT bson_dollar_project(document, '{ "a": 1 }') AS document FROM documentdb_api.collection('db', 'bson_dollar_ops_text_search'))
SELECT document FROM r1 WHERE document @@ '{ "$text": { "$search": "cat" } }';

-- no more than 1 $text:
SELECT document FROM documentdb_api.collection('db', 'bson_dollar_ops_text_search') WHERE document @@ '{ "$and": [ { "$text": { "$search": "cat" } }, { "$text": { "$search": "dogs" } }] }';

-- Test tsquery stack depth, max depth is 32
-- 32 should work
SELECT document FROM documentdb_api.collection('db', 'bson_dollar_ops_text_search') WHERE document @@ '{ "$text": { "$search": "--------------------------------cat" } }';
-- one more should fail
SELECT document FROM documentdb_api.collection('db', 'bson_dollar_ops_text_search') WHERE document @@ '{ "$text": { "$search": "---------------------------------cat" } }';

CALL documentdb_api.drop_indexes('db', '{ "dropIndexes": "bson_dollar_ops_text_search", "index": "a_text" }');

SELECT documentdb_api.insert_one('db', 'bson_dollar_ops_text_search', '{ "_id": 11, "topic": "apple", "writer": "Carol", "score": 50, "location": "New York", "published": true }');
SELECT documentdb_api.insert_one('db', 'bson_dollar_ops_text_search', '{ "_id": 12, "topic": "Apple Store", "writer": "Bob", "score": 5, "location": "San Francisco", "published": false }');
SELECT documentdb_api.insert_one('db', 'bson_dollar_ops_text_search', '{ "_id": 13, "topic": "Picking a berry", "writer": "Alice", "score": 90, "location": "London", "published": true }');
SELECT documentdb_api.insert_one('db', 'bson_dollar_ops_text_search', '{ "_id": 14, "topic": "picking", "writer": "Carol", "score": 100, "location": "Berlin", "published": false }');
SELECT documentdb_api.insert_one('db', 'bson_dollar_ops_text_search', '{ "_id": 15, "topic": "apple and berry", "writer": "Alice", "score": 200, "location": "Paris", "published": true }');
SELECT documentdb_api.insert_one('db', 'bson_dollar_ops_text_search', '{ "_id": 16, "topic": "Яблоко", "writer": "David", "score": 80, "location": "Moscow", "published": false }');
SELECT documentdb_api.insert_one('db', 'bson_dollar_ops_text_search', '{ "_id": 17, "topic": "pear and berry", "writer": "Bob", "score": 10, "location": "Madrid", "published": true }');
SELECT documentdb_api.insert_one('db', 'bson_dollar_ops_text_search', '{ "_id": 18, "topic": "Pear with Berry", "writer": "Carol", "score": 10, "location": "Rome", "published": false }');
SELECT documentdb_api.insert_one('db', 'bson_dollar_ops_text_search', '{ "_id": 30, "topic": "Me encanta la miel natural", "writer": "Alice", "score": 10, "location": "Barcelona", "published": true }');


SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "bson_dollar_ops_text_search", "indexes": [ { "key": { "topic": "text" }, "name": "topic_text" } ] }', TRUE);

SELECT document FROM documentdb_api.collection('db', 'bson_dollar_ops_text_search') WHERE document @@ '{ "$text": { "$search": "apple" } }' ORDER BY object_id;
SELECT document FROM documentdb_api.collection('db', 'bson_dollar_ops_text_search') WHERE document @@ '{ "$text": { "$search": "pick apple" } }' ORDER BY object_id;
SELECT document FROM documentdb_api.collection('db', 'bson_dollar_ops_text_search') WHERE document @@ '{ "$text": { "$search": "\"apple store\"" } }' ORDER BY object_id;
SELECT document FROM documentdb_api.collection('db', 'bson_dollar_ops_text_search') WHERE document @@ '{ "$text": { "$search": "apple -store" } }' ORDER BY object_id;

-- TODO: this is incorrect, we aren’t diacritic insensitive by default, it should return more documents.
-- See: https://www.postgresql.org/docs/current/unaccent.html
SELECT document FROM documentdb_api.collection('db', 'bson_dollar_ops_text_search') WHERE document @@ '{ "$text": { "$search": "яблоко" } }';

-- this partially works:
SELECT document FROM documentdb_api.collection('db', 'bson_dollar_ops_text_search') WHERE document @@ '{ "$text": { "$search": "miel", "$language": "es" } }';
-- invalid language
SELECT document FROM documentdb_api.collection('db', 'bson_dollar_ops_text_search') WHERE document @@ '{ "$text": { "$search": "miel", "$language": "xx" } }';


-- try these with the function.
SELECT document FROM documentdb_api.collection('db', 'bson_dollar_ops_text_search') WHERE documentdb_api_internal.bson_dollar_text(document, '{ "": { "$search": "apple" } }') ORDER BY object_id;
SELECT document FROM documentdb_api.collection('db', 'bson_dollar_ops_text_search') WHERE documentdb_api_internal.bson_dollar_text(document, '{ "": { "$search": "pick apple" } }') ORDER BY object_id;
SELECT document FROM documentdb_api.collection('db', 'bson_dollar_ops_text_search') WHERE documentdb_api_internal.bson_dollar_text(document, '{ "": { "$search": "\"apple store\"" } }') ORDER BY object_id;

-- shard collection & try the query again
SELECT documentdb_api.shard_collection('db', 'bson_dollar_ops_text_search', '{ "_id": "hashed" }', false);

SELECT document FROM documentdb_api.collection('db', 'bson_dollar_ops_text_search') WHERE document @@ '{ "$text": { "$search": "apple" } }' ORDER BY object_id;
SELECT document FROM documentdb_api.collection('db', 'bson_dollar_ops_text_search') WHERE document @@ '{ "$text": { "$search": "pick apple" } }' ORDER BY object_id;
SELECT document FROM documentdb_api.collection('db', 'bson_dollar_ops_text_search') WHERE document @@ '{ "$text": { "$search": "\"apple store\"" } }' ORDER BY object_id;
SELECT document FROM documentdb_api.collection('db', 'bson_dollar_ops_text_search') WHERE document @@ '{ "$text": { "$search": "apple -store" } }' ORDER BY object_id;

-- now repeat the above with default_languages.
CALL documentdb_api.drop_indexes('db', '{ "dropIndexes": "bson_dollar_ops_text_search", "index": "topic_text" }');

SELECT documentdb_api.insert_one('db', 'bson_dollar_ops_text_search', '{ "_id": 21, "titular": "Manzana", "writer": "xyz", "score": 50 }');
SELECT documentdb_api.insert_one('db', 'bson_dollar_ops_text_search', '{ "_id": 22, "titular": "Comprar Manzana", "writer": "efg", "score": 5 }');
SELECT documentdb_api.insert_one('db', 'bson_dollar_ops_text_search', '{ "_id": 23, "titular": "Cosechando una baya", "writer": "abc", "score": 90  }');
SELECT documentdb_api.insert_one('db', 'bson_dollar_ops_text_search', '{ "_id": 24, "titular": "Cosechar", "writer": "xyz", "score": 100 }');
SELECT documentdb_api.insert_one('db', 'bson_dollar_ops_text_search', '{ "_id": 25, "titular": "Manzana con baya", "writer": "abc", "score": 200 }');
SELECT documentdb_api.insert_one('db', 'bson_dollar_ops_text_search', '{ "_id": 27, "titular": "Manzana con pera", "writer": "efg", "score": 10 }');
SELECT documentdb_api.insert_one('db', 'bson_dollar_ops_text_search', '{ "_id": 28, "titular": "Manzana con pera y baya", "writer": "xyz", "score": 10 }');

SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "bson_dollar_ops_text_search", "indexes": [ { "key": { "titular": "text" }, "name": "titular_text", "default_language": "es" } ] }', TRUE);

SELECT document FROM documentdb_api.collection('db', 'bson_dollar_ops_text_search') WHERE document @@ '{ "$text": { "$search": "Manzana" } }' ORDER BY object_id;
SELECT document FROM documentdb_api.collection('db', 'bson_dollar_ops_text_search') WHERE document @@ '{ "$text": { "$search": "Cosechar" } }' ORDER BY object_id;
SELECT document FROM documentdb_api.collection('db', 'bson_dollar_ops_text_search') WHERE document @@ '{ "$text": { "$search": "Comprando Manzana" } }' ORDER BY object_id;

-- now add projection.
SELECT bson_dollar_project(document, '{ "_id": 1, "titular": 1, "rank": { "$meta": "textScore" }}') FROM documentdb_api.collection('db', 'bson_dollar_ops_text_search') WHERE document @@ '{ "$text": { "$search": "Manzana baya cosechar" } }';
SELECT bson_dollar_add_fields(document, '{ "rank": { "$meta": "textScore" }}') FROM documentdb_api.collection('db', 'bson_dollar_ops_text_search') WHERE document @@ '{ "$text": { "$search": "Manzana baya cosechar" } }';
SELECT bson_dollar_project_find(document, '{ "_id": 1, "titular": 1, "rank": { "$meta": "textScore" }}') FROM documentdb_api.collection('db', 'bson_dollar_ops_text_search') WHERE document @@ '{ "$text": { "$search": "Manzana baya cosechar" } }';

SELECT cursorPage, continuation, persistConnection FROM documentdb_api.find_cursor_first_page('db', '{ "find": "bson_dollar_ops_text_search", "filter": { "$text": { "$search": "Manzana baya cosechar" } }, "projection": { "_id": 1, "titular": 1, "rank": { "$meta": "textScore" }} }');

-- pipeline cases
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "bson_dollar_ops_text_search", "cursor": {}, "pipeline": [ { "$project": { "_id": 1 } }, { "$match": { "$text": { "$search": "Manzana baya cosechar" } } } ] }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "bson_dollar_ops_text_search", "cursor": {}, "pipeline": [ { "$match": { "$text": { "$search": "Manzana baya cosechar" } } }, { "$sort": { "_id": 1 } } ] }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "bson_dollar_ops_text_search", "cursor": {}, "pipeline": [ { "$match": { "$text": { "$search": "Manzana baya cosechar" } } }, { "$project": { "_id": 1, "titular": 1, "rank": { "$meta": "textScore" } } } ] }');

-- now add sort
SELECT document FROM documentdb_api.collection('db', 'bson_dollar_ops_text_search') WHERE document @@ '{ "$text": { "$search": "Manzana Cosechando" } }' ORDER BY bson_orderby(document, '{ "score": {"$meta": "textScore"} }') DESC;

-- now add project & sort 
SELECT bson_dollar_project(document, '{ "_id": 1, "titular": 1, "rank": { "$meta": "textScore" }}') FROM documentdb_api.collection('db', 'bson_dollar_ops_text_search') WHERE document @@ '{ "$text": { "$search": "Manzana Comprando baya" } }' ORDER BY bson_orderby(document, '{ "score": {"$meta": "textScore"} }') DESC;

-- now do group
WITH r1 AS (SELECT document FROM documentdb_api.collection('db', 'bson_dollar_ops_text_search') WHERE document @@ '{ "$text": { "$search": "Manzana" } }' )
SELECT BSONMAX(bson_expression_get(document, '{ "": "$score" }')), bson_expression_get(document, '{ "": { "$meta": "textScore" } }') FROM r1 GROUP BY bson_expression_get(document, '{ "": { "$meta": "textScore" } }');


-- scenarios without $text should return 'query requires text score metadata, but it is not available'
SELECT bson_dollar_project(document, '{ "_id": 1, "headline": 1, "rank": { "$meta": "textScore" }}') FROM documentdb_api.collection('db', 'bson_dollar_ops_text_search') WHERE document @@ '{ "score": { "$exists": true } }';
SELECT bson_dollar_add_fields(document, '{ "_id": 1, "headline": 1, "rank": { "$meta": "textScore" }}') FROM documentdb_api.collection('db', 'bson_dollar_ops_text_search') WHERE document @@ '{ "score": { "$exists": true } }';
SELECT bson_dollar_project_find(document, '{ "_id": 1, "headline": 1, "rank": { "$meta": "textScore" }}') FROM documentdb_api.collection('db', 'bson_dollar_ops_text_search') WHERE document @@ '{ "score": { "$exists": true } }';
SELECT document FROM documentdb_api.collection('db', 'bson_dollar_ops_text_search') WHERE document @@ '{ "score": { "$exists": true } }' ORDER BY bson_orderby(document, '{ "score": {"$meta": "textScore"} }') DESC;
WITH r1 AS (SELECT document FROM documentdb_api.collection('db', 'bson_dollar_ops_text_search') WHERE document @@ '{ "score": { "$exists": true } }' )
SELECT BSONMAX(bson_expression_get(document, '{ "": "$score" }')) FROM r1 GROUP BY bson_expression_get(document, '{ "": { "$meta": "textScore" } }');


-- test with custom weights.
CALL documentdb_api.drop_indexes('db', '{ "dropIndexes": "bson_dollar_ops_text_search", "index": "titular_text" }');

SELECT documentdb_api.insert_one('db', 'bson_dollar_ops_text_search', '{ "_id": 31, "alpha": "red blue green", "beta": "yellow orange" }');
SELECT documentdb_api.insert_one('db', 'bson_dollar_ops_text_search', '{ "_id": 32, "alpha": "yellow orange blue", "beta": "red blue purple" }');

SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "bson_dollar_ops_text_search", "indexes": [ { "key": { "alpha": "text" }, "name": "alpha_1", "weights": { "alpha": 10, "beta": 1 } } ] }', TRUE);

-- returns 31, 32
SELECT bson_dollar_add_fields(document, '{ "rank": { "$meta": "textScore" }}') FROM documentdb_api.collection('db', 'bson_dollar_ops_text_search') WHERE document @@ '{ "$text": { "$search": "red" } }' ORDER BY bson_orderby(document, '{ "score": {"$meta": "textScore"} }') DESC;

-- Returns 32, 31
SELECT bson_dollar_add_fields(document, '{ "rank": { "$meta": "textScore" }}') FROM documentdb_api.collection('db', 'bson_dollar_ops_text_search') WHERE document @@ '{ "$text": { "$search": "orange" } }' ORDER BY bson_orderby(document, '{ "score": {"$meta": "textScore"} }') DESC;

-- test wildcard handling
-- this is a variant of the JS test: blog_textwild.js
CALL documentdb_api.drop_indexes('db', '{ "dropIndexes": "bson_dollar_ops_text_search", "index": "alpha_1" }');

SELECT documentdb_api.insert_one('db', 'bson_dollar_ops_text_search', '{ "_id": 33, "heading": "my travel story", "content": "this is a new travel blog I am writing. cheers sam" }');
SELECT documentdb_api.insert_one('db', 'bson_dollar_ops_text_search', '{ "_id": 34, "heading": "my 2nd story", "content": "this is a new travel blog I am writing. cheers" }');
SELECT documentdb_api.insert_one('db', 'bson_dollar_ops_text_search', '{ "_id": 35, "heading": "mountains are Beautiful for writing sam", "content": "this is a new travel blog I am writing. cheers" }');

SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "bson_dollar_ops_text_search", "indexes": [ { "key": { "dummy": "text", "$**": "text" }, "name": "alpha_1" } ] }', TRUE);

SELECT document FROM documentdb_api.collection('db', 'bson_dollar_ops_text_search') WHERE document @@ '{ "$text": { "$search": "travel" } }' ORDER BY object_id;

CALL documentdb_api.drop_indexes('db', '{ "dropIndexes": "bson_dollar_ops_text_search", "index": "alpha_1" }');

-- recreate this so that test output for further tests does not change
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "bson_dollar_ops_text_search", "indexes": [ { "key": { "alpha": "text" }, "name": "alpha_1", "weights": { "alpha": 10, "beta": 1 } } ] }', TRUE);

-- Test TSQuery generation.
SELECT documentdb_api_internal.bson_query_to_tsquery('{ "$search": "\"mental health\"" }'::documentdb_core.bson);
SELECT documentdb_api_internal.bson_query_to_tsquery('{ "$search": "\"mental health\" wellbeing nutrition" }'::documentdb_core.bson);
SELECT documentdb_api_internal.bson_query_to_tsquery('{ "$search": "hydrate stretch recover" }'::documentdb_core.bson);
SELECT documentdb_api_internal.bson_query_to_tsquery('{ "$search": "\"cardio training\"" }'::documentdb_core.bson);
SELECT documentdb_api_internal.bson_query_to_tsquery('{ "$search": "cardio -training" }'::documentdb_core.bson);
SELECT documentdb_api_internal.bson_query_to_tsquery('{ "$search": "cardio -training -burnout" }'::documentdb_core.bson);
SELECT documentdb_api_internal.bson_query_to_tsquery('{ "$search": "\"cardio training\" -fatigue -burnout" }'::documentdb_core.bson);
SELECT documentdb_api_internal.bson_query_to_tsquery('{ "$search": "\"cardio training\" diet plan -fatigue -burnout" }'::documentdb_core.bson);
SELECT documentdb_api_internal.bson_query_to_tsquery('{ "$search": "\"cardio training\" diet plan -fatigue -burnout wellbeing" }'::documentdb_core.bson);

-- this matches
SELECT to_tsvector('consistent cardio training with a diet plan improves wellbeing') @@ documentdb_api_internal.bson_query_to_tsquery('{ "$search": "\"cardio training\" diet plan -fatigue -burnout wellbeing" }'::documentdb_core.bson);

-- this shouldn't match
SELECT to_tsvector('cardio training with a diet plan can lead to fatigue') @@ documentdb_api_internal.bson_query_to_tsquery('{ "$search": "\"cardio training\" diet plan -fatigue -burnout wellbeing" }'::documentdb_core.bson);
SELECT to_tsvector('stretching improves general wellbeing and focus') @@ documentdb_api_internal.bson_query_to_tsquery('{ "$search": "\"cardio training\" diet plan -fatigue -burnout wellbeing" }'::documentdb_core.bson);
SELECT to_tsvector('burnout is a risk when doing cardio training without breaks') @@ documentdb_api_internal.bson_query_to_tsquery('{ "$search": "\"cardio training\" diet plan -fatigue -burnout wellbeing" }'::documentdb_core.bson);

-- Phrase: First one doesn't match, second one does.
SELECT to_tsvector('consistent stretching is the cornerstone of wellbeing') @@ documentdb_api_internal.bson_query_to_tsquery('{ "$search": "\"cardio training\" diet plan -fatigue -burnout wellbeing" }'::documentdb_core.bson);
SELECT to_tsvector('a good diet plan improves long-term wellbeing') @@ documentdb_api_internal.bson_query_to_tsquery('{ "$search": "diet plan -fatigue -burnout wellbeing" }'::documentdb_core.bson);
-- Use language check
-- synonyms within a language work.
SELECT to_tsvector('portuguese', 'Em atualidade, sempre é possível manter-se saudável') @@ documentdb_api_internal.bson_query_to_tsquery('{ "$search": "atualmente", "$language": "pt" }'::documentdb_core.bson);

-- Only one text index allowed
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "bson_dollar_ops_text_search2", "indexes": [ { "key": { "gamma": "text" }, "name": "gamma_text" } ] }', TRUE);
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "bson_dollar_ops_text_search2", "indexes": [ { "key": { "gamma": "text" }, "name": "gamma_text" } ] }', TRUE);
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "bson_dollar_ops_text_search", "indexes": [ { "key": { "desc": "text", "extra": "text" }, "name": "desc_text" } ] }', TRUE);
