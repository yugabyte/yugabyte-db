SET search_path TO helio_core,helio_api,helio_api_catalog,helio_api_internal;
SET citus.next_shard_id TO 4910000;
SET helio_api.next_collection_id TO 4910;
SET helio_api.next_collection_index_id TO 4910;


-- $$ROOT as a variable.

SELECT helio_api_catalog.bson_expression_get('{ "a": 1 }', '{ "c": "$$ROOT" }');
SELECT helio_api_catalog.bson_expression_get('{ "a": { "b": [ 1, 2, 3 ] } }', '{ "c": "$$ROOT.a" }');
SELECT helio_api_catalog.bson_expression_get('{ "a": { "b": [ 1, 2, 3 ] } }', '{ "c": "$$ROOT.a.b" }');
SELECT helio_api_catalog.bson_expression_get('{ "a": { "b": [ 1, 2, 3 ] } }', '{ "c": "$$ROOT.c" }');
SELECT helio_api_catalog.bson_expression_get('{ "a": { "b": [ 1, 2, 3 ] } }', '{ "c": { "$isArray": "$$ROOT.a.b" } }');
SELECT helio_api_catalog.bson_expression_get('{ "a": { "b": [ 1, 2, 3 ] } }', '{ "c": { "$isArray": "$$ROOT.a" } }');
SELECT helio_api_catalog.bson_expression_get('{ "a": { "b": [ 1, 2, 3 ] } }', '{ "c": { "$size": "$$ROOT.a.b" } }');

SELECT helio_api_catalog.bson_expression_get('{ "a": { "b": 2 } }', '{ "c": { "$eq": [ "$$ROOT.a.b", 2 ] } }');
SELECT helio_api_catalog.bson_expression_get('{ "a": { "b": 2 } }', '{ "c": { "$eq": [ "$$ROOT.a.b", 3 ] } }');

-- override $$CURRENT should change the meaning of path expressions since $a -> $$CURRENT.a
SELECT * FROM bson_dollar_project('{"a": 1}', '{"b": {"$filter": {"input": [{"a": 10, "b": 8}, {"a": 7, "b": 8}], "as": "CURRENT", "cond": {"$and": [{"$gt": ["$a", 8]}, {"$gt": ["$b", 7]}]}}}}');

-- $reduce with multiple documents in a collection to ensure variable context is reset between every row evaluation
SELECT helio_api.insert_one('db', 'variable_tests', '{"_id": 1, "a": ["a", "b", "c"]}');
SELECT helio_api.insert_one('db', 'variable_tests', '{"_id": 2, "a": ["d", "e", "f"]}');
SELECT helio_api.insert_one('db', 'variable_tests', '{"_id": 3, "a": ["g", "h", "i"]}');

select bson_dollar_project(document, '{"result": { "$reduce": { "input": "$a", "initialValue": "", "in": { "$concat": ["$$value", "$$this"] } } } }') from helio_api.collection('db', 'variable_tests');

-- $$REMOVE should not be written
select bson_dollar_project('{}', '{"result": "$$REMOVE" }');
select bson_dollar_project(document, '{"result": [ "$a", "$$REMOVE", "final" ]  }') from helio_api.collection('db', 'variable_tests');
select bson_dollar_project(document, '{"result": { "$reduce": { "input": "$a", "initialValue": "", "in": { "$concat": ["$$REMOVE", "$$this"] } } } }') from helio_api.collection('db', 'variable_tests');

-- $let aggregation operator
-- verify that defined variables work
SELECT * FROM bson_dollar_project('{}', '{"result": {"$let": {"vars": {"foo": "this is my foo variable"}, "in": "$$foo"}}}');
SELECT * FROM bson_dollar_project('{ "a": 10}', '{"result": {"$let": {"vars": {"sum": {"$add": ["$a", 10]}}, "in": "$$sum"}}}');
SELECT * FROM bson_dollar_project('{ "a": 10}', '{"result": {"$let": {"vars": {"sumValue": {"$add": ["$a", 10]}}, "in": "$$sumValue"}}}');
SELECT * FROM bson_dollar_project('{ "a": 10}', '{"result": {"$let": {"vars": {"value": "$a"}, "in": "$$value"}}}');
SELECT * FROM bson_dollar_project('{ "a": 10}', '{"result": {"$let": {"vars": {"value": "$$CURRENT"}, "in": "$$value"}}}');
SELECT * FROM bson_dollar_project('{ "a": 10}', '{"result": {"$let": {"vars": {"value": "$$ROOT"}, "in": "$$value"}}}');
SELECT * FROM bson_dollar_project('{ "a": 10}', '{"result": {"$let": {"vars": {"value": "$$REMOVE"}, "in": "$$value"}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$let": {"vars": {"日本語": 10}, "in": "$$日本語"}}}');

-- verify nested let work and can also override variables
SELECT * FROM bson_dollar_project('{ }', '{"result": {"$let": {"vars": {"foo": "this is my foo variable"}, "in": {"$let": {"vars": {"foo2": "$$foo"}, "in": {"a": "$$foo", "b": "$$foo2"}}}}}}');
SELECT * FROM bson_dollar_project('{ }', '{"result": {"$let": {"vars": {"foo": "this is my foo variable"}, "in": {"$let": {"vars": {"foo": "$$foo"}, "in": {"a": "$$foo"}}}}}}');
SELECT * FROM bson_dollar_project('{ }', '{"result": {"$let": {"vars": {"foo": "this is my foo variable"}, "in": {"$let": {"vars": {"foo": "this is a foo override"}, "in": {"a": "$$foo"}}}}}}');
SELECT * FROM bson_dollar_project('{ }', '{"result": {"$let": {"vars": {"foo": "this is my foo variable"}, "in": {"$let": {"vars": {"foo2": "$$foo"}, "in": {"$let": {"vars": {"foo3": "this is my foo3 variable"}, "in": {"foo": "$$foo", "foo2": "$$foo2", "foo3": "$$foo3"}}}}}}}}');

-- nested works with expressions that define variables
SELECT * FROM bson_dollar_project('{ "input": [1, 2, 3, 4, 5]}', '{"result": {"$let": {"vars": {"value": "100"}, "in": {"$filter": {"input": "$input", "as": "value", "cond": {"$gte": ["$$value", 4]}}}}}}');
SELECT * FROM bson_dollar_project('{ "input": [1, 2, 3, 4, 5]}', '{"result": {"$let": {"vars": {"value": "100"}, "in": {"$filter": {"input": "$input", "as": "this", "cond": {"$gte": ["$$value", 4]}}}}}}');
SELECT * FROM bson_dollar_project('{ "input": [1, 2, 3, 4, 5]}', '{"result": {"$filter": {"input": "$input", "as": "value", "cond": {"$let": {"vars": {"value": 100}, "in": {"$gte": ["$$value", 4]}}}}}}');

-- override current
SELECT * FROM bson_dollar_project('{ "a": 10}', '{"result": {"$let": {"vars": {"CURRENT": {"a": "this is a in new current"}}, "in": "$a"}}}');

-- use dotted paths on variable expressions
SELECT * FROM bson_dollar_project('{ "a": 10}', '{"result": {"$let": {"vars": {"value": {"a": "value a field"}}, "in": "$$value.a"}}}');
SELECT * FROM bson_dollar_project('{ "a": 10}', '{"result": {"$let": {"vars": {"value": [{"a": "nested field in array"}]}, "in": "$$value.a"}}}');
SELECT * FROM bson_dollar_project('{ "a": 10}', '{"result": {"$let": {"vars": {"value": {"a": "value a field"}}, "in": "$$value.nonExistent"}}}');

-- test with multiple rows on a collection and a non constant spec such that caching is not in the picture to test we don't have memory corruption on the variable data itself
SELECT helio_api.insert_one('db', 'dollar_let_test', '{"_id": 1, "name": "santi", "hobby": "running"}');
SELECT helio_api.insert_one('db', 'dollar_let_test', '{"_id": 2, "name": "joe", "hobby": "soccer"}');
SELECT helio_api.insert_one('db', 'dollar_let_test', '{"_id": 3, "name": "daniel", "hobby": "painting"}');
SELECT helio_api.insert_one('db', 'dollar_let_test', '{"_id": 4, "name": "lucas", "hobby": "music"}');
SELECT helio_api.insert_one('db', 'dollar_let_test', '{"_id": 5, "name": "richard", "hobby": "running"}');
SELECT helio_api.insert_one('db', 'dollar_let_test', '{"_id": 6, "name": "daniela", "hobby": "reading"}');
SELECT helio_api.insert_one('db', 'dollar_let_test', '{"_id": 7, "name": "isabella", "hobby": "video games"}');
SELECT helio_api.insert_one('db', 'dollar_let_test', '{"_id": 8, "name": "daniel II", "hobby": "board games"}');
SELECT helio_api.insert_one('db', 'dollar_let_test', '{"_id": 9, "name": "jose", "hobby": "music"}');
SELECT helio_api.insert_one('db', 'dollar_let_test', '{"_id": 10, "name": "camille", "hobby": "painting"}');

SELECT bson_dollar_project(document, FORMAT('{"result": {"$let": {"vars": {"intro": "%s", "hobby_text": " , and my hobby is: "}, "in": {"$concat": ["$$intro", "$name", "$$hobby_text", "$hobby"]}}}}', 'Hello my name is: ')::bson)
FROM helio_api.collection('db', 'dollar_let_test');

-- negative cases
SELECT * FROM bson_dollar_project('{}', '{"result": {"$let": [1, 2, 3]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$let": {"vars": true, "in": "a"}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$let": {"vars": {}}}}');
SELECT * FROM bson_dollar_project('{"a": 10}', '{"result": {"$let": {"in": "$a"}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$let": {"vars": [], "in": "$$123"}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$let": {"vars": {"123": "123 variable"}, "in": "$$123"}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$let": {"vars": {"ROOT": "new root"}, "in": "$$ROOT"}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$let": {"vars": {"REMOVE": "new remove"}, "in": "$$REMOVE"}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$let": {"vars": {"MyVariable": "MyVariable"}, "in": "$$MyVariable"}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$let": {"vars": {"_variable": "_variable"}, "in": "$$_variable"}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$let": {"vars": {"with space": "with space"}, "in": "$$with space"}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$let": {"vars": {"hello!": "with space"}, "in": "$$FOO"}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$let": {"vars": {"variable": "with space"}, "in": "$$_variable"}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$let": {"vars": {"variable": "with space"}, "in": "$$with spaces"}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$let": {"vars": {"variable": "with space"}, "in": "$$hello!"}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$let": {"vars": {"variable": "with space"}, "in": "$$FOO"}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$let": {"vars": {"variable": "with space"}, "in": "$$.a"}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$let": {"vars": {"variable": "with space"}, "in": "$$variable."}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$let": {"vars": {"a.b": "a.b"}, "in": "$$a.b"}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$let": {"vars": {"foo": "this is my foo variable"}, "in": {"$let": {"vars": {"foo": "$$foo2"}, "in": {"a": "$$foo"}}}}}}');
