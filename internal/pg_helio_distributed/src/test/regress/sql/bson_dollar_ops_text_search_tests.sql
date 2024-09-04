set search_path to helio_core,helio_api,helio_api_catalog,pg_catalog;
SET citus.next_shard_id TO 7980000;
SET helio_api.next_collection_id TO 7980;
SET helio_api.next_collection_index_id TO 7980;


-- insert some docs
SELECT helio_api.insert_one('db', 'text_search', '{ "_id": 1, "a": "this is a cat" }');
SELECT helio_api.insert_one('db', 'text_search', '{ "_id": 2, "a": "this is a dog" }');
SELECT helio_api.insert_one('db', 'text_search', '{ "_id": 3, "a": "these are dogs" }');
SELECT helio_api.insert_one('db', 'text_search', '{ "_id": 4, "a": "these are cats" }');
SELECT helio_api.insert_one('db', 'text_search', '{ "_id": 5, "a": "these are catatonic" }');


-- do a $text query. Should fail (there's no index)
SELECT document FROM helio_api.collection('db', 'text_search') WHERE document @@ '{ "$text": { "$search": "cat" } }';

-- create a text index.
SELECT helio_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "text_search", "indexes": [ { "key": { "a": "text" }, "name": "a_text" } ] }', TRUE);

-- now do a $text query. Should succeed.
SELECT document FROM helio_api.collection('db', 'text_search') WHERE document @@ '{ "$text": { "$search": "cat" } }';
SELECT document FROM helio_api.collection('db', 'text_search') WHERE document @@ '{ "$text": { "$search": "dog" } }';
SELECT document FROM helio_api.collection('db', 'text_search') WHERE document @@ '{ "$text": { "$search": "cat | dog" } }';

EXPLAIN (COSTS OFF) SELECT document FROM helio_api.collection('db', 'text_search') WHERE document @@ '{ "$text": { "$search": "cat" } }';

-- invalid queries
-- $text on subsequent stages should fail.
WITH r1 AS (SELECT bson_dollar_project(document, '{ "a": 1 }') AS document FROM helio_api.collection('db', 'text_search'))
SELECT document FROM r1 WHERE document @@ '{ "$text": { "$search": "cat" } }';

-- no more than 1 $text:
SELECT document FROM helio_api.collection('db', 'text_search') WHERE document @@ '{ "$and": [ { "$text": { "$search": "cat" } }, { "$text": { "$search": "dogs" } }] }';

-- now let's try Mongo's text search example
CALL helio_api.drop_indexes('db', '{ "dropIndexes": "text_search", "index": "a_text" }');

SELECT helio_api.insert_one('db', 'text_search', '{ "_id": 11, "subject": "coffee", "author": "xyz", "views": 50 }');
SELECT helio_api.insert_one('db', 'text_search', '{ "_id": 12, "subject": "Coffee Shopping", "author": "efg", "views": 5 }');
SELECT helio_api.insert_one('db', 'text_search', '{ "_id": 13, "subject": "Baking a cake", "author": "abc", "views": 90  }');
SELECT helio_api.insert_one('db', 'text_search', '{ "_id": 14, "subject": "baking", "author": "xyz", "views": 100 }');
SELECT helio_api.insert_one('db', 'text_search', '{ "_id": 15, "subject": "Café Con Leche", "author": "abc", "views": 200 }');
SELECT helio_api.insert_one('db', 'text_search', '{ "_id": 16, "subject": "Сырники", "author": "jkl", "views": 80 }');
SELECT helio_api.insert_one('db', 'text_search', '{ "_id": 17, "subject": "coffee and cream", "author": "efg", "views": 10 }');
SELECT helio_api.insert_one('db', 'text_search', '{ "_id": 18, "subject": "Cafe con Leche", "author": "xyz", "views": 10 }');

SELECT helio_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "text_search", "indexes": [ { "key": { "subject": "text" }, "name": "subject_text" } ] }', TRUE);

SELECT document FROM helio_api.collection('db', 'text_search') WHERE document @@ '{ "$text": { "$search": "coffee" } }' ORDER BY object_id;

SELECT document FROM helio_api.collection('db', 'text_search') WHERE document @@ '{ "$text": { "$search": "bake coffee cake" } }' ORDER BY object_id;

SELECT document FROM helio_api.collection('db', 'text_search') WHERE document @@ '{ "$text": { "$search": "\"coffee shop\"" } }' ORDER BY object_id;

SELECT document FROM helio_api.collection('db', 'text_search') WHERE document @@ '{ "$text": { "$search": "coffee -shop" } }' ORDER BY object_id;

-- TODO: this is incorrect, we aren’t diacritic insensitive by default like Native Mongo, it should return more documents.
-- See: https://www.postgresql.org/docs/current/unaccent.html
SELECT document FROM helio_api.collection('db', 'text_search') WHERE document @@ '{ "$text": { "$search": "сы́рники CAFÉS" } }';

-- this partially works:
SELECT document FROM helio_api.collection('db', 'text_search') WHERE document @@ '{ "$text": { "$search": "leche", "$language": "es" } }';

-- invalid language
SELECT document FROM helio_api.collection('db', 'text_search') WHERE document @@ '{ "$text": { "$search": "leche", "$language": "ok" } }';


-- try these with the function.
SELECT document FROM helio_api.collection('db', 'text_search') WHERE bson_dollar_text(document, '{ "": { "$search": "coffee" } }') ORDER BY object_id;

SELECT document FROM helio_api.collection('db', 'text_search') WHERE bson_dollar_text(document, '{ "": { "$search": "bake coffee cake" } }') ORDER BY object_id;

SELECT document FROM helio_api.collection('db', 'text_search') WHERE bson_dollar_text(document, '{ "": { "$search": "\"coffee shop\"" } }') ORDER BY object_id;

-- shard collection & try the query again
SELECT helio_api.shard_collection('db', 'text_search', '{ "_id": "hashed" }', false);

SELECT document FROM helio_api.collection('db', 'text_search') WHERE document @@ '{ "$text": { "$search": "coffee" } }' ORDER BY object_id;

SELECT document FROM helio_api.collection('db', 'text_search') WHERE document @@ '{ "$text": { "$search": "bake coffee cake" } }' ORDER BY object_id;

SELECT document FROM helio_api.collection('db', 'text_search') WHERE document @@ '{ "$text": { "$search": "\"coffee shop\"" } }' ORDER BY object_id;

SELECT document FROM helio_api.collection('db', 'text_search') WHERE document @@ '{ "$text": { "$search": "coffee -shop" } }' ORDER BY object_id;


-- now repeat the above with default_languages.
CALL helio_api.drop_indexes('db', '{ "dropIndexes": "text_search", "index": "subject_text" }');

SELECT helio_api.insert_one('db', 'text_search', '{ "_id": 21, "sujeito": "Café", "author": "xyz", "views": 50 }');
SELECT helio_api.insert_one('db', 'text_search', '{ "_id": 22, "sujeito": "Comprar Café", "author": "efg", "views": 5 }');
SELECT helio_api.insert_one('db', 'text_search', '{ "_id": 23, "sujeito": "Cozinhando um bolo", "author": "abc", "views": 90  }');
SELECT helio_api.insert_one('db', 'text_search', '{ "_id": 24, "sujeito": "Cozinhar", "author": "xyz", "views": 100 }');
SELECT helio_api.insert_one('db', 'text_search', '{ "_id": 25, "sujeito": "Café com leite", "author": "abc", "views": 200 }');
SELECT helio_api.insert_one('db', 'text_search', '{ "_id": 27, "sujeito": "Café com azeite", "author": "efg", "views": 10 }');
SELECT helio_api.insert_one('db', 'text_search', '{ "_id": 28, "sujeito": "Cafe com azeite e leite", "author": "xyz", "views": 10 }');

SELECT helio_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "text_search", "indexes": [ { "key": { "sujeito": "text" }, "name": "sujeito_text", "default_language": "pt" } ] }', TRUE);

SELECT document FROM helio_api.collection('db', 'text_search') WHERE document @@ '{ "$text": { "$search": "Café" } }' ORDER BY object_id;
SELECT document FROM helio_api.collection('db', 'text_search') WHERE document @@ '{ "$text": { "$search": "Cozinhar" } }' ORDER BY object_id;
SELECT document FROM helio_api.collection('db', 'text_search') WHERE document @@ '{ "$text": { "$search": "Comprando Café" } }' ORDER BY object_id;

-- now add projection.
SELECT bson_dollar_project(document, '{ "_id": 1, "sujeito": 1, "rank": { "$meta": "textScore" }}') FROM helio_api.collection('db', 'text_search') WHERE document @@ '{ "$text": { "$search": "Café bolo cozinhar" } }';
SELECT bson_dollar_add_fields(document, '{ "rank": { "$meta": "textScore" }}') FROM helio_api.collection('db', 'text_search') WHERE document @@ '{ "$text": { "$search": "Café bolo cozinhar" } }';
SELECT bson_dollar_project_find(document, '{ "_id": 1, "sujeito": 1, "rank": { "$meta": "textScore" }}') FROM helio_api.collection('db', 'text_search') WHERE document @@ '{ "$text": { "$search": "Café bolo cozinhar" } }';

SELECT cursorPage, continuation, persistConnection FROM helio_api.find_cursor_first_page('db', '{ "find": "text_search", "filter": { "$text": { "$search": "Café bolo cozinhar" } }, "projection": { "_id": 1, "sujeito": 1, "rank": { "$meta": "textScore" }} }');

-- pipeline cases
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "text_search", "cursor": {}, "pipeline": [ { "$project": { "_id": 1 } }, { "$match": { "$text": { "$search": "Café bolo cozinhar" } } } ] }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "text_search", "cursor": {}, "pipeline": [ { "$match": { "$text": { "$search": "Café bolo cozinhar" } } }, { "$sort": { "_id": 1 } } ] }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "text_search", "cursor": {}, "pipeline": [ { "$match": { "$text": { "$search": "Café bolo cozinhar" } } }, { "$project": { "_id": 1, "sujeito": 1, "rank": { "$meta": "textScore" } } } ] }');

-- now add sort
SELECT document FROM helio_api.collection('db', 'text_search') WHERE document @@ '{ "$text": { "$search": "Café Cozinhando" } }' ORDER BY bson_orderby(document, '{ "score": {"$meta": "textScore"} }') DESC;

-- now add project & sort 
SELECT bson_dollar_project(document, '{ "_id": 1, "sujeito": 1, "rank": { "$meta": "textScore" }}') FROM helio_api.collection('db', 'text_search') WHERE document @@ '{ "$text": { "$search": "Café Comprando leite" } }' ORDER BY bson_orderby(document, '{ "score": {"$meta": "textScore"} }') DESC;

-- now do group
WITH r1 AS (SELECT document FROM helio_api.collection('db', 'text_search') WHERE document @@ '{ "$text": { "$search": "Café" } }' )
SELECT BSONMAX(bson_expression_get(document, '{ "": "$views" }')), bson_expression_get(document, '{ "": { "$meta": "textScore" } }') FROM r1 GROUP BY bson_expression_get(document, '{ "": { "$meta": "textScore" } }');


-- scenarios without $text should return 'query requires text score metadata, but it is not available'
SELECT bson_dollar_project(document, '{ "_id": 1, "sujeito": 1, "rank": { "$meta": "textScore" }}') FROM helio_api.collection('db', 'text_search') WHERE document @@ '{ "views": { "$exists": true } }';
SELECT bson_dollar_add_fields(document, '{ "_id": 1, "sujeito": 1, "rank": { "$meta": "textScore" }}') FROM helio_api.collection('db', 'text_search') WHERE document @@ '{ "views": { "$exists": true } }';
SELECT bson_dollar_project_find(document, '{ "_id": 1, "sujeito": 1, "rank": { "$meta": "textScore" }}') FROM helio_api.collection('db', 'text_search') WHERE document @@ '{ "views": { "$exists": true } }';
SELECT document FROM helio_api.collection('db', 'text_search') WHERE document @@ '{ "views": { "$exists": true } }' ORDER BY bson_orderby(document, '{ "score": {"$meta": "textScore"} }') DESC;
WITH r1 AS (SELECT document FROM helio_api.collection('db', 'text_search') WHERE document @@ '{ "views": { "$exists": true } }' )
SELECT BSONMAX(bson_expression_get(document, '{ "": "$views" }')) FROM r1 GROUP BY bson_expression_get(document, '{ "": { "$meta": "textScore" } }');


-- test with custom weights.
CALL helio_api.drop_indexes('db', '{ "dropIndexes": "text_search", "index": "sujeito_text" }');

SELECT helio_api.insert_one('db', 'text_search', '{ "_id": 31, "x": "az b x", "y": "c d m" }');
SELECT helio_api.insert_one('db', 'text_search', '{ "_id": 32, "x": "c d y", "y": "az b n" }');

SELECT helio_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "text_search", "indexes": [ { "key": { "x": "text" }, "name": "x_1", "weights": { "x": 10, "y": 1 } } ] }', TRUE);

-- returns 31, 32
SELECT bson_dollar_add_fields(document, '{ "rank": { "$meta": "textScore" }}') FROM helio_api.collection('db', 'text_search') WHERE document @@ '{ "$text": { "$search": "az" } }' ORDER BY bson_orderby(document, '{ "score": {"$meta": "textScore"} }') DESC;

-- Returns 32, 31
SELECT bson_dollar_add_fields(document, '{ "rank": { "$meta": "textScore" }}') FROM helio_api.collection('db', 'text_search') WHERE document @@ '{ "$text": { "$search": "d" } }' ORDER BY bson_orderby(document, '{ "score": {"$meta": "textScore"} }') DESC;

-- Test TSQuery generation.
SELECT helio_api_internal.bson_query_to_tsquery('{ "$search": "\"ssl certificate\"" }'::helio_core.bson);

SELECT helio_api_internal.bson_query_to_tsquery('{ "$search": "\"ssl certificate\" authority key" }'::helio_core.bson);

SELECT helio_api_internal.bson_query_to_tsquery('{ "$search": "bake coffee cake" }'::helio_core.bson);

SELECT helio_api_internal.bson_query_to_tsquery('{ "$search": "\"coffee shop\"" }'::helio_core.bson);

SELECT helio_api_internal.bson_query_to_tsquery('{ "$search": "coffee -shop" }'::helio_core.bson);

SELECT helio_api_internal.bson_query_to_tsquery('{ "$search": "coffee -shop -nightmare" }'::helio_core.bson);

SELECT helio_api_internal.bson_query_to_tsquery('{ "$search": "\"coffee shop\" -track -nightmare" }'::helio_core.bson);

SELECT helio_api_internal.bson_query_to_tsquery('{ "$search": "\"coffee shop\" cow -track -nightmare" }'::helio_core.bson);

SELECT helio_api_internal.bson_query_to_tsquery('{ "$search": "\"coffee shop\" cow -track -nightmare search" }'::helio_core.bson);

-- this matches
SELECT to_tsvector('the coffee shop cows searches the barn') @@ helio_api_internal.bson_query_to_tsquery('{ "$search": "\"coffee shop\" cow -track -nightmare search" }'::helio_core.bson);

-- this shouldn't match
SELECT to_tsvector('the coffee shop cows track me') @@ helio_api_internal.bson_query_to_tsquery('{ "$search": "\"coffee shop\" cow -track -nightmare search" }'::helio_core.bson);
SELECT to_tsvector('the coffee shop cows searches the track') @@ helio_api_internal.bson_query_to_tsquery('{ "$search": "\"coffee shop\" cow -track -nightmare search" }'::helio_core.bson);
SELECT to_tsvector('coffee shop searches my nightmares') @@ helio_api_internal.bson_query_to_tsquery('{ "$search": "\"coffee shop\" cow -track -nightmare search" }'::helio_core.bson);

-- Phrase: First one doesn't match, second one does.
SELECT to_tsvector('google is the paragon of search') @@ helio_api_internal.bson_query_to_tsquery('{ "$search": "\"coffee shop\" cow -track -nightmare search" }'::helio_core.bson);
SELECT to_tsvector('google is the paragon of search') @@ helio_api_internal.bson_query_to_tsquery('{ "$search": "cow -track -nightmare search" }'::helio_core.bson);

-- Use language check
-- synonyms within a language work.
SELECT to_tsvector('portuguese', 'Em atualidade, Sempre e possivel') @@ helio_api_internal.bson_query_to_tsquery('{ "$search": "atualmente", "$language": "pt" }'::helio_core.bson);

-- Only one text index allowed
SELECT helio_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "text_search2", "indexes": [ { "key": { "c": "text" }, "name": "c_text" } ] }', TRUE);
SELECT helio_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "text_search2", "indexes": [ { "key": { "c": "text" }, "name": "c_text" } ] }', TRUE);
SELECT helio_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "text_search", "indexes": [ { "key": { "a": "text", "b": "text" }, "name": "a_text" } ] }', TRUE);
