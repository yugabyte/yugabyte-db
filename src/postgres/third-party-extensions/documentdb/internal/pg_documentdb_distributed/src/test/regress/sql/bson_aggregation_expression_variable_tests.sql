SET search_path TO documentdb_core,documentdb_api,documentdb_api_catalog,documentdb_api_internal;
SET citus.next_shard_id TO 4910000;
SET documentdb.next_collection_id TO 4910;
SET documentdb.next_collection_index_id TO 4910;


-- $$ROOT as a variable.

SELECT documentdb_api_catalog.bson_expression_get('{ "a": 1 }', '{ "c": "$$ROOT" }');
SELECT documentdb_api_catalog.bson_expression_get('{ "a": { "b": [ 1, 2, 3 ] } }', '{ "c": "$$ROOT.a" }');
SELECT documentdb_api_catalog.bson_expression_get('{ "a": { "b": [ 1, 2, 3 ] } }', '{ "c": "$$ROOT.a.b" }');
SELECT documentdb_api_catalog.bson_expression_get('{ "a": { "b": [ 1, 2, 3 ] } }', '{ "c": "$$ROOT.c" }');
SELECT documentdb_api_catalog.bson_expression_get('{ "a": { "b": [ 1, 2, 3 ] } }', '{ "c": { "$isArray": "$$ROOT.a.b" } }');
SELECT documentdb_api_catalog.bson_expression_get('{ "a": { "b": [ 1, 2, 3 ] } }', '{ "c": { "$isArray": "$$ROOT.a" } }');
SELECT documentdb_api_catalog.bson_expression_get('{ "a": { "b": [ 1, 2, 3 ] } }', '{ "c": { "$size": "$$ROOT.a.b" } }');

SELECT documentdb_api_catalog.bson_expression_get('{ "a": { "b": 2 } }', '{ "c": { "$eq": [ "$$ROOT.a.b", 2 ] } }');
SELECT documentdb_api_catalog.bson_expression_get('{ "a": { "b": 2 } }', '{ "c": { "$eq": [ "$$ROOT.a.b", 3 ] } }');

-- override $$CURRENT should change the meaning of path expressions since $a -> $$CURRENT.a
SELECT * FROM bson_dollar_project('{"a": 1}', '{"b": {"$filter": {"input": [{"a": 10, "b": 8}, {"a": 7, "b": 8}], "as": "CURRENT", "cond": {"$and": [{"$gt": ["$a", 8]}, {"$gt": ["$b", 7]}]}}}}');

-- $reduce with multiple documents in a collection to ensure variable context is reset between every row evaluation
SELECT documentdb_api.insert_one('db', 'remove_variable_tests', '{"_id": 1, "a": ["a", "b", "c"]}');
SELECT documentdb_api.insert_one('db', 'remove_variable_tests', '{"_id": 2, "a": ["d", "e", "f"]}');
SELECT documentdb_api.insert_one('db', 'remove_variable_tests', '{"_id": 3, "a": ["g", "h", "i"]}');

select bson_dollar_project(document, '{"result": { "$reduce": { "input": "$a", "initialValue": "", "in": { "$concat": ["$$value", "$$this"] } } } }') from documentdb_api.collection('db', 'remove_variable_tests');

-- $$REMOVE should not be written
select bson_dollar_project('{}', '{"result": "$$REMOVE" }');
select bson_dollar_project(document, '{"result": [ "$a", "$$REMOVE", "final" ]  }') from documentdb_api.collection('db', 'remove_variable_tests');
select bson_dollar_project(document, '{"result": { "$reduce": { "input": "$a", "initialValue": "", "in": { "$concat": ["$$REMOVE", "$$this"] } } } }') from documentdb_api.collection('db', 'remove_variable_tests');

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
SELECT documentdb_api.insert_one('db', 'dollar_let_test', '{"_id": 1, "name": "santi", "hobby": "running"}');
SELECT documentdb_api.insert_one('db', 'dollar_let_test', '{"_id": 2, "name": "joe", "hobby": "soccer"}');
SELECT documentdb_api.insert_one('db', 'dollar_let_test', '{"_id": 3, "name": "daniel", "hobby": "painting"}');
SELECT documentdb_api.insert_one('db', 'dollar_let_test', '{"_id": 4, "name": "lucas", "hobby": "music"}');
SELECT documentdb_api.insert_one('db', 'dollar_let_test', '{"_id": 5, "name": "richard", "hobby": "running"}');
SELECT documentdb_api.insert_one('db', 'dollar_let_test', '{"_id": 6, "name": "daniela", "hobby": "reading"}');
SELECT documentdb_api.insert_one('db', 'dollar_let_test', '{"_id": 7, "name": "isabella", "hobby": "video games"}');
SELECT documentdb_api.insert_one('db', 'dollar_let_test', '{"_id": 8, "name": "daniel II", "hobby": "board games"}');
SELECT documentdb_api.insert_one('db', 'dollar_let_test', '{"_id": 9, "name": "jose", "hobby": "music"}');
SELECT documentdb_api.insert_one('db', 'dollar_let_test', '{"_id": 10, "name": "camille", "hobby": "painting"}');

SELECT bson_dollar_project(document, FORMAT('{"result": {"$let": {"vars": {"intro": "%s", "hobby_text": " , and my hobby is: "}, "in": {"$concat": ["$$intro", "$name", "$$hobby_text", "$hobby"]}}}}', 'Hello my name is: ')::bson)
FROM documentdb_api.collection('db', 'dollar_let_test');

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

-- $$NOW
SELECT documentdb_api.insert_one('db','now_test_1','{"_id":"1", "a": "umbor" }', NULL);
SELECT documentdb_api.insert_one('db','now_test_1','{"_id":"2", "a": "gabby" }', NULL);
SELECT documentdb_api.insert_one('db','now_test_1','{"_id":"3", "a": "jo" }', NULL);

-- $$NOW with dotted expression should evaluate to EOD
SELECT document FROM bson_aggregation_find('db', '{ "find": "now_test_1", "projection": { "nowField" : "$$NOW.foo" }, "filter": {} }');

CREATE SCHEMA time_system_variables_test;

-- Function to replace the value of $$NOW with NOW_SYS_VARIABLE
CREATE OR REPLACE FUNCTION time_system_variables_test.mask_time_value(query text, out result text)
RETURNS SETOF TEXT AS $$
BEGIN
  FOR result IN EXECUTE query LOOP
    RETURN QUERY 
    SELECT REGEXP_REPLACE(
      result, 
      '\{\s*"\$date"\s*:\s*\{\s*"\$numberLong"\s*:\s*"[0-9]+"\s*\}\s*\}', 
      'NOW_SYS_VARIABLE', 
      'g'
    );
  END LOOP;
  RETURN;
END; $$ LANGUAGE plpgsql;

SELECT time_system_variables_test.mask_time_value($Q$ SELECT document FROM bson_aggregation_find('db', '{ "find": "now_test_1", "projection": { "nowField" : "$$NOW" }, "filter": {} }') $Q$);

SELECT time_system_variables_test.mask_time_value($Q$ SELECT document FROM bson_aggregation_find('db', '{ "find": "now_test_1", "projection": { "nowField" : "$$varRef" }, "filter": {}, "let": { "varRef": "$$NOW" } }') $Q$);
SELECT time_system_variables_test.mask_time_value($Q$ EXPLAIN (COSTS OFF, VERBOSE ON ) SELECT document FROM bson_aggregation_find('db', '{ "find": "now_test_1", "projection": { "nowField" : "$$varRef" }, "filter": {}, "let": { "varRef": "$$NOW" } }') $Q$);
SELECT time_system_variables_test.mask_time_value($Q$ SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "now_test_1", "pipeline": [ { "$addFields": { "nowField" : "$$varRef", "c": { "$lt": [ "$_id", "$$varRef" ] } }} ], "let": { "varRef": "$$NOW" } }') $Q$);
SELECT time_system_variables_test.mask_time_value($Q$ EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "now_test_1", "pipeline": [ { "$addFields": { "nowField" : "$$varRef", "c": { "$lt": [ "$_id", "$$varRef" ] } }} ], "let": { "varRef": "$$NOW" } }') $Q$);
SELECT time_system_variables_test.mask_time_value($Q$ SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "now_test_1", "pipeline": [ { "$addFields": { "nowField" : "$$NOW" }} ] }') $Q$);
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "now_test_1", "pipeline": [ { "$addFields": { "nowField" : "$$NOW" }} ] }');
EXPLAIN (COSTS OFF, VERBOSE ON) SELECT document FROM bson_aggregation_find('db', '{ "find": "now_test_1", "projection": { "other": "$$varRef" }, "filter": { "$expr": { "$lt": [ "$_id", "$$NOW" ]} }, "let": { "varRef": "3" } }');

-- $$NOW in $lookup 
SELECT documentdb_api.insert_one('db','now_test_from','{"_id":1,"a":"alpha","b":["x","y"],"c":10,"d":["m","n"]}',NULL);
SELECT documentdb_api.insert_one('db','now_test_from','{"_id":2,"a":"beta","b":["p","q"],"c":20,"d":["z"]}',NULL);

SELECT documentdb_api.insert_one('db','now_test_to','{"_id":1,"x":"x","a":"alpha","c":10}',NULL);
SELECT documentdb_api.insert_one('db','now_test_to','{"_id":2,"x":"p","a":"beta","y":"t","c":20}',NULL);
SELECT documentdb_api.insert_one('db','now_test_to','{"_id":3,"x":"p","a":"beta","y":"z","c":30}',NULL);

SELECT documentdb_api.insert_one('db','now_test_from_2','{"_id":1,"z":"val1","c":10}',NULL);
SELECT documentdb_api.insert_one('db','now_test_from_2','{"_id":2,"z":"val2","c":20}',NULL);

SELECT time_system_variables_test.mask_time_value($q$ SELECT document FROM bson_aggregation_pipeline('db','{"aggregate":"now_test_to","pipeline":[{"$lookup":{"from":"now_test_from","pipeline":[{"$match":{"$expr":{"$eq":["$c","$$c"]}}},{"$addFields":{"newC":"$$c","now":"$$NOW"}}],"as":"joined","localField":"a","foreignField":"a","let":{"c":"$c"}}}],"cursor":{}}') $q$);

SELECT time_system_variables_test.mask_time_value($q$ SELECT document FROM bson_aggregation_pipeline('db','{"aggregate":"now_test_to","pipeline":[{"$lookup":{"from":"now_test_from","pipeline":[{"$sort":{"_id":1}},{"$match":{"$expr":{"$eq":["$c","$$NOW"]}}}],"as":"joined","localField":"a","foreignField":"a","let":{"c":"$c"}}}],"cursor":{}}') $q$);

SELECT time_system_variables_test.mask_time_value($Q$ SELECT document FROM bson_aggregation_pipeline('db','{"aggregate":"now_test_to","pipeline":[{"$lookup":{"from":"now_test_from","pipeline":[{"$addFields":{"extra":"$$NOW"}},{"$lookup":{"from":"now_test_from_2","localField":"z","foreignField":"c","as":"inner","let":{"v":"$c"},"pipeline":[{"$match":{"$expr":{"$eq":["$z","$$v"]}}}]}}],"as":"joined","localField":"a","foreignField":"a"}}],"cursor":{}}') $Q$);

SELECT time_system_variables_test.mask_time_value($Q$ SELECT document FROM bson_aggregation_pipeline('db','{"aggregate":"now_test_to","pipeline":[{"$lookup":{"from":"now_test_from","pipeline":[{"$addFields":{"extra":"$$NOW"}},{"$lookup":{"from":"now_test_from_2","localField":"z","foreignField":"c","as":"inner","let":{"v":"$$NOW"},"pipeline":[{"$match":{"$expr":{"$eq":["$z","$$v"]}}}]}}],"as":"joined","localField":"a","foreignField":"a"}}],"cursor":{}}') $Q$);

SELECT document FROM bson_aggregation_pipeline('db','{ "aggregate":"now_test_to", "pipeline":[{ "$match":{ "_id":3 }},{ "$addFields":{ "nowField1":"$$NOW" }},{ "$lookup":{ "from":"now_test_from", "pipeline":[{ "$addFields":{ "nowField2":"$$NOW" }},{ "$lookup":{ "from":"now_test_from_2", "localField":"z", "foreignField":"c", "as":"inner", "let":{ "v":"$c" }, "pipeline":[{ "$match":{ "$expr":{ "$eq":["$z","$$v"] }}}] }}], "as":"joined", "localField":"a", "foreignField":"a" }}], "cursor":{} }') \gset
\set result0 :document
SELECT :'result0'::json->>'nowField1' = :'result0'::json->'joined'->0->>'nowField2';

-- $$NOW in transactions. We expect same values.
SELECT documentdb_api.insert_one('db','now_test_2','{"_id": 0}', NULL);

BEGIN;
SELECT document FROM bson_aggregation_find('db', '{ "find": "now_test_2", "projection": { "nowField" : "$$NOW" }, "filter": {} }') as document \gset
\set result1 :document

SELECT pg_sleep(2);

SELECT document FROM bson_aggregation_find('db', '{ "find": "now_test_2", "projection": { "nowField" : "$$NOW" }, "filter": {} }') as document \gset
\set result2 :document

SELECT pg_sleep(2);

SELECT document FROM bson_aggregation_find('db', '{ "find": "now_test_2", "projection": { "nowField" : "$$NOW" }, "filter": {} }') as document \gset
\set result3 :document

SELECT :'result1'::bson->>'nowField' = :'result2'::bson->>'nowField' AND :'result2'::bson->>'nowField' = :'result3'::bson->>'nowField';
END;

-- $$NOW outside transaction block. We expect different values.
SELECT document FROM bson_aggregation_find('db', '{ "find": "now_test_2", "projection": { "nowField" : "$$NOW" }, "filter": {} }') as document \gset
\set result4 :document

SELECT pg_sleep(2);

SELECT document FROM bson_aggregation_find('db', '{ "find": "now_test_2", "projection": { "nowField" : "$$NOW" }, "filter": {} }') as document \gset
\set result5 :document

SELECT pg_sleep(2);

SELECT document FROM bson_aggregation_find('db', '{ "find": "now_test_2", "projection": { "nowField" : "$$NOW" }, "filter": {} }') as document \gset
\set result6 :document

SELECT :'result4'::bson->>'nowField' <> :'result5'::bson->>'nowField' AND :'result5'::bson->>'nowField' <> :'result6'::bson->>'nowField';

-- Earlier queries should have higher $$NOW values
SELECT :'result4'::bson->>'nowField' < :'result5'::bson->>'nowField' AND :'result5'::bson->>'nowField' < :'result6'::bson->>'nowField';

-- $$NOW with cursors
CREATE TYPE time_system_variables_test.drain_result AS (filteredDoc bson, nowCompareResult bool, persistConnection bool);

DO $$
DECLARE i int;
BEGIN
FOR i IN 1..10 LOOP
PERFORM documentdb_api.insert_one('db', 'now_cursor_tests', FORMAT('{ "_id": %s }',  i)::documentdb_core.bson);
END LOOP;
END;
$$;

-- Drains a find query and compares the value of 'nowField' across all batches returned by the cursor.
CREATE FUNCTION time_system_variables_test.drain_find_query(
    loopCount int, pageSize int, project bson DEFAULT NULL, skipVal int4 DEFAULT NULL, limitVal int4 DEFAULT NULL) RETURNS SETOF time_system_variables_test.drain_result AS
$$
    DECLARE
        i int;
        doc bson;
        cont bson;
        contProcessed bson;
        persistConn bool;
        findSpec bson;
        getMoreSpec bson;
        currentBatch jsonb[];
        currentDoc jsonb;
        currentNowValue text;
        finalResult bool;
    BEGIN

    WITH r1 AS (SELECT 'now_cursor_tests' AS "find", project AS "projection", skipVal AS "skip", limitVal as "limit", pageSize AS "batchSize")
    SELECT row_get_bson(r1) INTO findSpec FROM r1;

    WITH r1 AS (SELECT 'now_cursor_tests' AS "collection", 4294967295::int8 AS "getMore", pageSize AS "batchSize")
    SELECT row_get_bson(r1) INTO getMoreSpec FROM r1;

    SELECT cursorPage, continuation, persistConnection INTO STRICT doc, cont, persistConn FROM
                    documentdb_api.find_cursor_first_page(database => 'db', commandSpec => findSpec, cursorId => 4294967295);

    finalResult := TRUE;
    currentNowValue := ((doc->>'cursor')::bson->>'firstBatch')::jsonb->0->>'nowField';

    SELECT documentdb_api_catalog.bson_dollar_project(doc,
        ('{ "ok": 1, "cursor.id": 1, "cursor.ns": 1, "batchCount": { "$size": { "$ifNull": [ "$cursor.firstBatch", "$cursor.nextBatch" ] } }, ' || 
        ' "ids": { "$ifNull": [ "$cursor.firstBatch._id", "$cursor.nextBatch._id" ] } }')::documentdb_core.bson) INTO STRICT doc;

    RETURN NEXT ROW(doc, finalResult, persistConn)::time_system_variables_test.drain_result;

    FOR i IN 1..loopCount LOOP
        SELECT cursorPage, continuation INTO STRICT doc, cont FROM documentdb_api.cursor_get_more(database => 'db', getMoreSpec => getMoreSpec, continuationSpec => cont);

        currentBatch := (
        SELECT ARRAY(
            SELECT jsonb_array_elements( ((doc->>'cursor')::bson->>'nextBatch')::jsonb)
            )
        );

        IF currentBatch IS NOT NULL AND array_length(currentBatch, 1) > 0 THEN
          FOR i IN 1..array_length(currentBatch, 1) LOOP
              currentDoc := currentBatch[i];
              finalResult := finalResult AND (currentNowValue = currentDoc->>'nowField');
              currentNowValue := currentDoc->>'nowField';
          END LOOP;
        END IF;

        SELECT documentdb_api_catalog.bson_dollar_project(doc,
        ('{ "ok": 1, "cursor.id": 1, "cursor.ns": 1, "batchCount": { "$size": { "$ifNull": [ "$cursor.firstBatch", "$cursor.nextBatch" ] } }, ' ||
        ' "ids": { "$ifNull": [ "$cursor.firstBatch._id", "$cursor.nextBatch._id" ] } }')::documentdb_core.bson) INTO STRICT doc;

        RETURN NEXT ROW(doc, finalResult, FALSE)::time_system_variables_test.drain_result;
    END LOOP;
END;
$$ LANGUAGE plpgsql;

-- Drains an aggregate query and compares the value of 'nowField' across all batches returned by the cursor.
CREATE FUNCTION time_system_variables_test.drain_aggregation_query(
    loopCount int, pageSize int, pipeline bson DEFAULT NULL, singleBatch bool DEFAULT NULL) RETURNS SETOF time_system_variables_test.drain_result AS
$$
    DECLARE
        i int;
        doc bson;
        cont bson;
        contProcessed bson;
        persistConn bool;
        aggregateSpec bson;
        getMoreSpec bson;
        currentBatch jsonb[];
        currentDoc jsonb;
        currentNowValue text;
        finalResult bool;
    BEGIN

    IF pipeline IS NULL THEN
        pipeline = '{ "": [] }'::bson;
    END IF;

    WITH r0 AS (SELECT pageSize AS "batchSize", singleBatch AS "singleBatch" ),
    r1 AS (SELECT 'now_cursor_tests' AS "aggregate", pipeline AS "pipeline", row_get_bson(r0) AS "cursor" FROM r0)
    SELECT row_get_bson(r1) INTO aggregateSpec FROM r1;

    WITH r1 AS (SELECT 'now_cursor_tests' AS "collection", 4294967294::int8 AS "getMore", pageSize AS "batchSize" )
    SELECT row_get_bson(r1) INTO getMoreSpec FROM r1;

    SELECT cursorPage, continuation, persistConnection INTO STRICT doc, cont, persistConn FROM
                    documentdb_api.aggregate_cursor_first_page(database => 'db', commandSpec => aggregateSpec, cursorId => 4294967294);

    finalResult := TRUE;
    currentNowValue := ((doc->>'cursor')::bson->>'firstBatch')::jsonb->0->>'nowField';

    SELECT documentdb_api_catalog.bson_dollar_project(doc,
        ('{ "ok": 1, "cursor.id": 1, "cursor.ns": 1, "batchCount": { "$size": { "$ifNull": [ "$cursor.firstBatch", "$cursor.nextBatch" ] } }, ' ||
        ' "ids": { "$ifNull": [ "$cursor.firstBatch._id", "$cursor.nextBatch._id" ] } }')::documentdb_core.bson) INTO STRICT doc;

    RETURN NEXT ROW(doc, finalResult, FALSE)::time_system_variables_test.drain_result;

    FOR i IN 1..loopCount LOOP
        SELECT cursorPage, continuation INTO STRICT doc, cont FROM documentdb_api.cursor_get_more(database => 'db', getMoreSpec => getMoreSpec, continuationSpec => cont);

        currentBatch := (
        SELECT ARRAY(
            SELECT jsonb_array_elements( ((doc->>'cursor')::bson->>'nextBatch')::jsonb)
            )
        );

        IF currentBatch IS NOT NULL AND array_length(currentBatch, 1) > 0 THEN
          FOR i IN 1..array_length(currentBatch, 1) LOOP
              currentDoc := currentBatch[i];
              finalResult := finalResult AND (currentNowValue = currentDoc->>'nowField');
              currentNowValue := currentDoc->>'nowField';
          END LOOP;
        END IF;

        SELECT documentdb_api_catalog.bson_dollar_project(doc,
        ('{ "ok": 1, "cursor.id": 1, "cursor.ns": 1, "batchCount": { "$size": { "$ifNull": [ "$cursor.firstBatch", "$cursor.nextBatch" ] } }, ' ||
        ' "ids": { "$ifNull": [ "$cursor.firstBatch._id", "$cursor.nextBatch._id" ] } }')::documentdb_core.bson) INTO STRICT doc;

        RETURN NEXT ROW(doc, finalResult, FALSE)::time_system_variables_test.drain_result;
    END LOOP;
END;
$$ LANGUAGE plpgsql;

SELECT time_system_variables_test.mask_time_value($Q$ SELECT * FROM aggregate_cursor_first_page('db', '{ "aggregate": "now_cursor_tests", "pipeline": [ { "$addFields": { "nowField" : "$$NOW" }} ], "cursor": { "batchSize": 2 } }', 4294967294); $Q$);

SELECT * FROM time_system_variables_test.drain_find_query(loopCount => 12, pageSize => 1, project => '{ "nowField": "$$NOW" }');
SELECT * FROM time_system_variables_test.drain_find_query(loopCount => 12, pageSize => 1, project => '{ "a": 1, "nowField": "$$NOW" }');

SELECT * FROM time_system_variables_test.drain_find_query(loopCount => 12, pageSize => 2, project => '{ "a": 1, "nowField": "$$NOW" }');
SELECT * FROM time_system_variables_test.drain_aggregation_query(loopCount => 12, pageSize => 2, pipeline => '{ "": [{ "$project": { "a": 1, "nowField": "$$NOW" } }]}');

SELECT * FROM time_system_variables_test.drain_aggregation_query(loopCount => 12, pageSize => 1, pipeline => '{ "": [{ "$project": { "a": 1, "nowField": "$$NOW" } }]}');
SELECT * FROM time_system_variables_test.drain_aggregation_query(loopCount => 4, pageSize => 3, pipeline => '{ "": [{ "$project": { "a": 1, "nowField": "$$NOW" } }]}', singleBatch => TRUE);
SELECT * FROM time_system_variables_test.drain_aggregation_query(loopCount => 4, pageSize => 100000, pipeline => '{ "": [{ "$project": { "nowField": "$$NOW" } }, { "$limit": 3 }]}');


-- Should be more than 1 document in collection
SELECT documentdb_api.insert_one('db','coll001','{"_id":1,"a":1}');
SELECT documentdb_api.insert_one('db','coll001','{"_id":2,"a":1}');
SELECT documentdb_api.insert_one('db','coll001','{"_id":3,"a":1}');

SELECT document
FROM bson_aggregation_pipeline(
  'db',
  '{
    "aggregate": "coll001", 
    "pipeline": [
      {
        "$project": {
          "result": {
            "$let": {
              "vars": { "g": "ab", "u": "mb" },
              "in":  "$$g"
            }
          }
        }
      }
    ]
  }'
);

SELECT document
FROM bson_aggregation_pipeline(
  'db',
  '{
    "aggregate": "coll001", 
    "pipeline": [
      {
        "$project": {
          "result": {
            "$let": {
              "vars": { "g": "ab", "u": "mb" },
              "in": { "$eq": ["$$g", "$$u"] }
            }
          }
        }
      }
    ]
  }'
);


SELECT document
FROM bson_aggregation_pipeline(
  'db',
  '{
    "aggregate": "coll001", 
    "pipeline": [
      {
        "$project": {
          "result": {
            "$let": {
              "vars": { "va": "$a", "vb": "$b" },
              "in": { "$and": ["$$va"] }
            }
          }
        }
      }
    ]
  }'
);

-- nested $let
SELECT document
FROM bson_aggregation_pipeline(
  'db',
  '{
    "aggregate": "coll001", 
    "pipeline": [
      {
        "$project": {
          "result": {
            "$let": {
              "vars": { "var1": "outer", "var2": "inner" },
              "in": {
                "$let": {
                  "vars": { "var3": "$$var2" },
                  "in": { "$concat": ["$$var1", "-", "$$var3"] }
                }
              }
            }
          }
        }
      }
    ]
  }'
);

--- nested $let with $map
SELECT document
FROM bson_aggregation_pipeline(
  'db',
  '{
    "aggregate": "coll001", 
    "pipeline": [
      {
        "$project": {
          "result": {
            "$let": {
              "vars": { "prefix": "item-" },
              "in": {
                "$map": {
                  "input": [1, 2, 3],
                  "as": "num",
                  "in": { "$concat": ["$$prefix", { "$toString": "$$num" }] }
                }
              }
            }
          }
        }
      }
    ]
  }'
);

--- nested $let with $filter
SELECT document
FROM bson_aggregation_pipeline(
  'db',
  '{
    "aggregate": "coll001", 
    "pipeline": [
      {
        "$project": {
          "result": {
            "$let": {
              "vars": { "threshold": 1 },
              "in": {
                "$filter": {
                  "input": [0, 1, 2, 3, 4],
                  "as": "num",
                  "cond": { "$gt": ["$$num", "$$threshold"] }
                }
              }
            }
          }
        }
      }
    ]
  }'
);  

