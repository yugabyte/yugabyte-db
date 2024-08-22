SET search_path TO mongo_catalog;
SET citus.next_shard_id TO 400000;
SET helio_api.next_collection_id TO 4000;
SET helio_api.next_collection_index_id TO 4000;

-- test explicit equality operator
SELECT helio_distributed_test_helpers.evaluate_query_expression('{ "$eq": 2 }', '{ "": 2 }');
SELECT helio_distributed_test_helpers.evaluate_query_expression('{ "$eq": 2 }', '{ "": 1 }');
SELECT helio_distributed_test_helpers.evaluate_query_expression('{ "$eq": 2 }', '{ "": 3 }');

-- test $in/$nin
SELECT helio_distributed_test_helpers.evaluate_query_expression('{ "$in": [ 2, 3, 4] }', '{ "": 1 }');
SELECT helio_distributed_test_helpers.evaluate_query_expression('{ "$in": [ 2, 3, 4] }', '{ "": 3 }');
SELECT helio_distributed_test_helpers.evaluate_query_expression('{ "$nin": [ 2, 3, 4] }', '{ "": 1 }');
SELECT helio_distributed_test_helpers.evaluate_query_expression('{ "$nin": [ 2, 3, 4] }', '{ "": 3 }');

-- test $gte/$lte
SELECT helio_distributed_test_helpers.evaluate_query_expression('{ "$gte": 4 }', '{ "": 3 }');
SELECT helio_distributed_test_helpers.evaluate_query_expression('{ "$gte": 4 }', '{ "": 4 }');
SELECT helio_distributed_test_helpers.evaluate_query_expression('{ "$gte": 4 }', '{ "": 5 }');

SELECT helio_distributed_test_helpers.evaluate_query_expression('{ "$gt": 4 }', '{ "": 3 }');
SELECT helio_distributed_test_helpers.evaluate_query_expression('{ "$gt": 4 }', '{ "": 4 }');
SELECT helio_distributed_test_helpers.evaluate_query_expression('{ "$gt": 4 }', '{ "": 5 }');

SELECT helio_distributed_test_helpers.evaluate_query_expression('{ "$lte": 4 }', '{ "": 3 }');
SELECT helio_distributed_test_helpers.evaluate_query_expression('{ "$lte": 4 }', '{ "": 4 }');
SELECT helio_distributed_test_helpers.evaluate_query_expression('{ "$lte": 4 }', '{ "": 5 }');

SELECT helio_distributed_test_helpers.evaluate_query_expression('{ "$lt": 4 }', '{ "": 3 }');
SELECT helio_distributed_test_helpers.evaluate_query_expression('{ "$lt": 4 }', '{ "": 4 }');
SELECT helio_distributed_test_helpers.evaluate_query_expression('{ "$lt": 4 }', '{ "": 5 }');

SELECT helio_distributed_test_helpers.evaluate_query_expression('{ "$type": "string" }', '{ "": 5 }');
SELECT helio_distributed_test_helpers.evaluate_query_expression('{ "$type": "string" }', '{ "": "hello" }');

SELECT helio_distributed_test_helpers.evaluate_query_expression('{ "$size": 3 }', '{ "": "hello" }');
SELECT helio_distributed_test_helpers.evaluate_query_expression('{ "$size": 3 }', '{ "": [ 1, 2 ] }');
SELECT helio_distributed_test_helpers.evaluate_query_expression('{ "$size": 3 }', '{ "": [ 1, 2, 3 ] }');


SELECT helio_distributed_test_helpers.evaluate_query_expression('{ "$ne": 4 }', '{ "": 3 }');
SELECT helio_distributed_test_helpers.evaluate_query_expression('{ "$ne": 4 }', '{ "": 4 }');

SELECT helio_distributed_test_helpers.evaluate_query_expression('{ "$exists": false }', '{ "": 3 }');
SELECT helio_distributed_test_helpers.evaluate_query_expression('{ "$exists": true }', '{ "": 4 }');

SELECT helio_distributed_test_helpers.evaluate_query_expression('{ "$regex": "a.*b", "$options": "" }', '{ "": 4 }');
SELECT helio_distributed_test_helpers.evaluate_query_expression('{ "$regex": "a.*b", "$options": "" }', '{ "": "someString" }');
SELECT helio_distributed_test_helpers.evaluate_query_expression('{ "$regex": "a.*b", "$options": "" }', '{ "": "aaab" }');
SELECT helio_distributed_test_helpers.evaluate_query_expression('{ "$regex": "a.*b", "$options": "" }', '{ "": "asomethingb" }');
-- negative test to validate options
SELECT helio_distributed_test_helpers.evaluate_query_expression('{ "$regex": "a.*b", "$options": "g" }', '{ "": 4 }');
SELECT helio_distributed_test_helpers.evaluate_query_expression('{ "$regex": "a.*b", "$options": "hw" }', '{ "": 4 }');
SELECT helio_distributed_test_helpers.evaluate_query_expression('{ "$regex": {"$regex": "\\d[3]", "$options": "s"} }', '{ "": 4 }');
SELECT helio_distributed_test_helpers.evaluate_query_expression('{ "$regex": {"$regex": "\\d[3]", "$options": "x"} }', '{ "": 4 }');

SELECT helio_distributed_test_helpers.evaluate_query_expression('{ "$regex": "a.*b", "$options": "" }', '{ "": [ "asomethingb" ] }');
SELECT helio_distributed_test_helpers.evaluate_query_expression('{ "b": { "$regex": "a.*b", "$options": "" } }', '{ "": [ "asomethingb" ] }');
SELECT helio_distributed_test_helpers.evaluate_query_expression('{ "b": { "$regex": "a.*b", "$options": "" } }', '{ "": { "c": "asomethingb" } }');
SELECT helio_distributed_test_helpers.evaluate_query_expression('{ "b": { "$regex": "a.*b", "$options": "" } }', '{ "": { "b": 1 } }');
SELECT helio_distributed_test_helpers.evaluate_query_expression('{ "b": { "$regex": "a.*b", "$options": "" } }', '{ "": { "b": "asomethingb" } }');

SELECT helio_distributed_test_helpers.evaluate_query_expression('{ "$bitsAllClear": [ 0, 1, 2 ] }', '{ "": 7 }');
SELECT helio_distributed_test_helpers.evaluate_query_expression('{ "$bitsAllClear": [ 0, 1, 2 ] }', '{ "": 8 }');
SELECT helio_distributed_test_helpers.evaluate_query_expression('{ "$bitsAllClear": [ 0, 1, 2 ] }', '{ "": 9 }');

SELECT helio_distributed_test_helpers.evaluate_query_expression('{ "$bitsAnyClear": [ 0, 1, 2 ] }', '{ "": 7 }');
SELECT helio_distributed_test_helpers.evaluate_query_expression('{ "$bitsAnyClear": [ 0, 1, 2 ] }', '{ "": 8 }');
SELECT helio_distributed_test_helpers.evaluate_query_expression('{ "$bitsAnyClear": [ 0, 1, 2 ] }', '{ "": 9 }');

SELECT helio_distributed_test_helpers.evaluate_query_expression('{ "$bitsAllSet": [ 0, 1, 2 ] }', '{ "": 7 }');
SELECT helio_distributed_test_helpers.evaluate_query_expression('{ "$bitsAllSet": [ 0, 1, 2 ] }', '{ "": 8 }');
SELECT helio_distributed_test_helpers.evaluate_query_expression('{ "$bitsAllSet": [ 0, 1, 2 ] }', '{ "": 9 }');

SELECT helio_distributed_test_helpers.evaluate_query_expression('{ "$bitsAnySet": [ 0, 1, 2 ] }', '{ "": 7 }');
SELECT helio_distributed_test_helpers.evaluate_query_expression('{ "$bitsAnySet": [ 0, 1, 2 ] }', '{ "": 8 }');
SELECT helio_distributed_test_helpers.evaluate_query_expression('{ "$bitsAnySet": [ 0, 1, 2 ] }', '{ "": 9 }');

-- test $and/$or/$nor
SELECT helio_distributed_test_helpers.evaluate_query_expression('{ "$or": [{ "b": {"$lt": 1} }, { "b": { "$gt": 2 }}] }', '{ "": 1 }');
SELECT helio_distributed_test_helpers.evaluate_query_expression('{ "$or": [{ "b": {"$lt": 1} }, { "b": { "$gt": 2 }}] }', '{ "": 0 }');
SELECT helio_distributed_test_helpers.evaluate_query_expression('{ "$or": [{ "b": {"$lt": 1} }, { "b": { "$gt": 2 }}] }', '{ "": 2 }');
SELECT helio_distributed_test_helpers.evaluate_query_expression('{ "$or": [{ "b": {"$lt": 1} }, { "b": { "$gt": 2 }}] }', '{ "": 3 }');

SELECT helio_distributed_test_helpers.evaluate_query_expression('{ "$or": [{ "b": {"$lt": 1} }, { "b": { "$gt": 2 }}] }', '{ "": { "a": 1 } }');
SELECT helio_distributed_test_helpers.evaluate_query_expression('{ "$or": [{ "b": {"$lt": 1} }, { "b": { "$gt": 2 }}] }', '{ "": { "a": 3 } }');
SELECT helio_distributed_test_helpers.evaluate_query_expression('{ "$or": [{ "b": {"$lt": 1} }, { "b": { "$gt": 2 }}] }', '{ "": { "a": 0 } }');

SELECT helio_distributed_test_helpers.evaluate_query_expression('{ "$or": [{ "b": {"$lt": 1} }, { "b": { "$gt": 2 }}] }', '{ "": { "b": 1 } }');
SELECT helio_distributed_test_helpers.evaluate_query_expression('{ "$or": [{ "b": {"$lt": 1} }, { "b": { "$gt": 2 }}] }', '{ "": { "b": 3 } }');
SELECT helio_distributed_test_helpers.evaluate_query_expression('{ "$or": [{ "b": {"$lt": 1} }, { "b": { "$gt": 2 }}] }', '{ "": { "b": 0 } }');


SELECT helio_distributed_test_helpers.evaluate_query_expression('{ "$and": [{ "b": {"$gte": 1} }, { "b": { "$lte": 2 }}] }', '{ "": 1 }');
SELECT helio_distributed_test_helpers.evaluate_query_expression('{ "$and": [{ "b": {"$gte": 1} }, { "b": { "$lte": 2 }}] }', '{ "": 0 }');
SELECT helio_distributed_test_helpers.evaluate_query_expression('{ "$and": [{ "b": {"$gte": 1} }, { "b": { "$lte": 2 }}] }', '{ "": 2 }');
SELECT helio_distributed_test_helpers.evaluate_query_expression('{ "$and": [{ "b": {"$gte": 1} }, { "b": { "$lte": 2 }}] }', '{ "": 3 }');

SELECT helio_distributed_test_helpers.evaluate_query_expression('{ "$and": [{ "b": {"$gte": 1} }, { "b": { "$lte": 2 }}] }', '{ "": { "a": 1 } }');
SELECT helio_distributed_test_helpers.evaluate_query_expression('{ "$and": [{ "b": {"$gte": 1} }, { "b": { "$lte": 2 }}] }', '{ "": { "a": 3 } }');
SELECT helio_distributed_test_helpers.evaluate_query_expression('{ "$and": [{ "b": {"$gte": 1} }, { "b": { "$lte": 2 }}] }', '{ "": { "a": 0 } }');

SELECT helio_distributed_test_helpers.evaluate_query_expression('{ "$and": [{ "b": {"$gte": 1} }, { "b": { "$lte": 2 }}] }', '{ "": { "b": 1 } }');
SELECT helio_distributed_test_helpers.evaluate_query_expression('{ "$and": [{ "b": {"$gte": 1} }, { "b": { "$lte": 2 }}] }', '{ "": { "b": 3 } }');
SELECT helio_distributed_test_helpers.evaluate_query_expression('{ "$and": [{ "b": {"$gte": 1} }, { "b": { "$lte": 2 }}] }', '{ "": { "b": 0 } }');


SELECT helio_distributed_test_helpers.evaluate_query_expression('{ "$nor": [{ "b": {"$lt": 1} }, { "b": { "$gt": 2 }}] }', '{ "": 1 }');
SELECT helio_distributed_test_helpers.evaluate_query_expression('{ "$nor": [{ "b": {"$lt": 1} }, { "b": { "$gt": 2 }}] }', '{ "": 0 }');
SELECT helio_distributed_test_helpers.evaluate_query_expression('{ "$nor": [{ "b": {"$lt": 1} }, { "b": { "$gt": 2 }}] }', '{ "": 2 }');
SELECT helio_distributed_test_helpers.evaluate_query_expression('{ "$nor": [{ "b": {"$lt": 1} }, { "b": { "$gt": 2 }}] }', '{ "": 3 }');

SELECT helio_distributed_test_helpers.evaluate_query_expression('{ "$nor": [{ "b": {"$lt": 1} }, { "b": { "$gt": 2 }}] }', '{ "": { "a": 1 } }');
SELECT helio_distributed_test_helpers.evaluate_query_expression('{ "$nor": [{ "b": {"$lt": 1} }, { "b": { "$gt": 2 }}] }', '{ "": { "a": 3 } }');
SELECT helio_distributed_test_helpers.evaluate_query_expression('{ "$nor": [{ "b": {"$lt": 1} }, { "b": { "$gt": 2 }}] }', '{ "": { "a": 0 } }');

SELECT helio_distributed_test_helpers.evaluate_query_expression('{ "$nor": [{ "b": {"$lt": 1} }, { "b": { "$gt": 2 }}] }', '{ "": { "b": 1 } }');
SELECT helio_distributed_test_helpers.evaluate_query_expression('{ "$nor": [{ "b": {"$lt": 1} }, { "b": { "$gt": 2 }}] }', '{ "": { "b": 3 } }');
SELECT helio_distributed_test_helpers.evaluate_query_expression('{ "$nor": [{ "b": {"$lt": 1} }, { "b": { "$gt": 2 }}] }', '{ "": { "b": 0 } }');

SELECT helio_distributed_test_helpers.evaluate_query_expression('{ "$not": { "$eq": 2 } }', '{ "": 0 }');
SELECT helio_distributed_test_helpers.evaluate_query_expression('{ "$not": { "$eq": 2 } }', '{ "": { "a": 0 } }');
SELECT helio_distributed_test_helpers.evaluate_query_expression('{ "$not": { "$eq": 2 } }', '{ "": 2 }');
SELECT helio_distributed_test_helpers.evaluate_query_expression('{ "b": { "$not": { "$eq": 2 } } }', '{ "": 2 }');
SELECT helio_distributed_test_helpers.evaluate_query_expression('{ "b": { "$not": { "$eq": 2 } } }', '{ "": { "b": 2 } }');
SELECT helio_distributed_test_helpers.evaluate_query_expression('{ "b": { "$not": { "$eq": 2 } } }', '{ "": { "b": 1 } }');

-- $elemMatch
SELECT helio_distributed_test_helpers.evaluate_query_expression('{ "b": { "$elemMatch": { "$gte": 2 } } }', '{ "": { "b": 1 } }');
SELECT helio_distributed_test_helpers.evaluate_query_expression('{ "b": { "$elemMatch": { "$gte": 2 } } }', '{ "": { "b": 2 } }');
SELECT helio_distributed_test_helpers.evaluate_query_expression('{ "b": { "$elemMatch": { "$gte": 2 } } }', '{ "": { "b": [ 1 ] } }');
SELECT helio_distributed_test_helpers.evaluate_query_expression('{ "b": { "$elemMatch": { "$gte": 2 } } }', '{ "": { "b": [ [ 2 ] ] } }');
SELECT helio_distributed_test_helpers.evaluate_query_expression('{ "b": { "$elemMatch": { "$gte": 2 } } }', '{ "": { "b": [ 2 ] } }');

SELECT helio_distributed_test_helpers.evaluate_query_expression('{ "$elemMatch": { "$gte": 2 } }', '{ "": 1 }');
SELECT helio_distributed_test_helpers.evaluate_query_expression('{ "$elemMatch": { "$gte": 2 } }', '{ "": 2 }');
SELECT helio_distributed_test_helpers.evaluate_query_expression('{ "$elemMatch": { "$gte": 2 } }', '{ "": [ 1 ] }');
SELECT helio_distributed_test_helpers.evaluate_query_expression('{ "$elemMatch": { "$gte": 2 } }', '{ "": [ [ 2 ] ] }');
SELECT helio_distributed_test_helpers.evaluate_query_expression('{ "$elemMatch": { "$gte": 2 } }', '{ "": [ 2 ] }');

-- $all
SELECT helio_distributed_test_helpers.evaluate_query_expression('{ "b": { "$all": [ 1, 3, 5 ] } }', '{ "": { "b": [ 2 ] } }');
SELECT helio_distributed_test_helpers.evaluate_query_expression('{ "b": { "$all": [ 1, 3, 5 ] } }', '{ "": { "b": 2 } }');
SELECT helio_distributed_test_helpers.evaluate_query_expression('{ "b": { "$all": [ 1, 3, 5 ] } }', '{ "": { "b": [ 1, 3, 7 ] } }');
SELECT helio_distributed_test_helpers.evaluate_query_expression('{ "b": { "$all": [ 1, 3, 5 ] } }', '{ "": { "b": [ 1, 3, 5, 8 ] } }');
SELECT helio_distributed_test_helpers.evaluate_query_expression('{ "b": { "$all": [ 1, 3, 5 ] } }', '{ "": { "b": [ 1, 1, 3, 5 ] } }');

-- projection of the value
SELECT helio_distributed_test_helpers.evaluate_expression_get_first_match('{ "$gt": 2 }', '{ "": [ 0, 1, 2 ] }');
SELECT helio_distributed_test_helpers.evaluate_expression_get_first_match('{ "$gt": 2 }', '{ "": [ 0, 5, 3, 8 ] }');

SELECT helio_distributed_test_helpers.evaluate_expression_get_first_match('{ "$in": [ 6, 7, 8] }', '{ "": [ 0, 5, 3, 8 ] }');
SELECT helio_distributed_test_helpers.evaluate_expression_get_first_match('{ "$in": [ 6, 7, 8] }', '{ "": [ 0, 5, 3, 1 ] }');