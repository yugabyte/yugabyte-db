SET search_path TO documentdb_api_catalog, documentdb_api, documentdb_core, public;
SET citus.next_shard_id TO 3120000;
SET documentdb.next_collection_id TO 3120;
SET documentdb.next_collection_index_id TO 3120;

SET citus.multi_shard_modify_mode TO 'sequential';

CREATE SCHEMA aggregation_cursor_test;

DO $$
DECLARE i int;
BEGIN
-- each doc is "a": 500KB, "c": 5 MB - ~5.5 MB & there's 10 of them
FOR i IN 1..10 LOOP
PERFORM documentdb_api.insert_one('db', 'get_aggregation_cursor_test', FORMAT('{ "_id": %s, "a": "%s", "c": [ %s "d" ] }',  i, repeat('Sample', 100000), repeat('"' || repeat('a', 1000) || '", ', 5000))::documentdb_core.bson);
END LOOP;
END;
$$;

CREATE TYPE aggregation_cursor_test.drain_result AS (filteredDoc bson, docSize int, continuationFiltered bson, persistConnection bool);


CREATE FUNCTION aggregation_cursor_test.drain_find_query(
    loopCount int, pageSize int, project bson DEFAULT NULL, skipVal int4 DEFAULT NULL, limitVal int4 DEFAULT NULL,
    sort bson DEFAULT NULL, filter bson default null,
    obfuscate_id bool DEFAULT false) RETURNS SETOF aggregation_cursor_test.drain_result AS
$$
    DECLARE
        i int;
        doc bson;
        docSize int;
        cont bson;
        contProcessed bson;
        persistConn bool;
        findSpec bson;
        getMoreSpec bson;
    BEGIN

    WITH r1 AS (SELECT 'get_aggregation_cursor_test' AS "find", filter AS "filter", sort AS "sort", project AS "projection", skipVal AS "skip", limitVal as "limit", pageSize AS "batchSize")
    SELECT row_get_bson(r1) INTO findSpec FROM r1;

    WITH r1 AS (SELECT 'get_aggregation_cursor_test' AS "collection", 4294967294::int8 AS "getMore", pageSize AS "batchSize")
    SELECT row_get_bson(r1) INTO getMoreSpec FROM r1;

    SELECT cursorPage, continuation, persistConnection INTO STRICT doc, cont, persistConn FROM
                    documentdb_api.find_cursor_first_page(database => 'db', commandSpec => findSpec, cursorId => 4294967294);
    SELECT documentdb_api_catalog.bson_dollar_project(doc,
        ('{ "ok": 1, "cursor.id": 1, "cursor.ns": 1, "batchCount": { "$size": { "$ifNull": [ "$cursor.firstBatch", "$cursor.nextBatch" ] } }, ' ||
        ' "ids": { "$ifNull": [ "$cursor.firstBatch._id", "$cursor.nextBatch._id" ] } }')::documentdb_core.bson), length(doc::bytea)::int INTO STRICT doc, docSize;

    IF obfuscate_id THEN
        SELECT documentdb_api_catalog.bson_dollar_add_fields(doc, '{ "ids.a": "1" }'::documentdb_core.bson) INTO STRICT doc;
    END IF;
    
    SELECT documentdb_api_catalog.bson_dollar_project(cont, '{ "continuation.value": 0 }'::documentdb_core.bson) INTO STRICT contProcessed;
    RETURN NEXT ROW(doc, docSize, contProcessed, persistConn)::aggregation_cursor_test.drain_result;

    FOR i IN 1..loopCount LOOP
        SELECT cursorPage, continuation INTO STRICT doc, cont FROM documentdb_api.cursor_get_more(database => 'db', getMoreSpec => getMoreSpec, continuationSpec => cont);

        SELECT documentdb_api_catalog.bson_dollar_project(doc,
        ('{ "ok": 1, "cursor.id": 1, "cursor.ns": 1, "batchCount": { "$size": { "$ifNull": [ "$cursor.firstBatch", "$cursor.nextBatch" ] } }, ' ||
        ' "ids": { "$ifNull": [ "$cursor.firstBatch._id", "$cursor.nextBatch._id" ] } }')::documentdb_core.bson), length(doc::bytea)::int INTO STRICT doc, docSize;

        IF obfuscate_id THEN
            SELECT documentdb_api_catalog.bson_dollar_add_fields(doc, '{ "ids.a": "1" }'::documentdb_core.bson) INTO STRICT doc;
        END IF;

        SELECT documentdb_api_catalog.bson_dollar_project(cont, '{ "continuation.value": 0 }'::documentdb_core.bson) INTO STRICT contProcessed;
        RETURN NEXT ROW(doc, docSize, contProcessed, FALSE)::aggregation_cursor_test.drain_result;
    END LOOP;
END;
$$ LANGUAGE plpgsql;

CREATE FUNCTION aggregation_cursor_test.drain_aggregation_query(
    loopCount int, pageSize int, pipeline bson DEFAULT NULL, obfuscate_id bool DEFAULT false, singleBatch bool DEFAULT NULL) RETURNS SETOF aggregation_cursor_test.drain_result AS
$$
    DECLARE
        i int;
        doc bson;
        docSize int;
        cont bson;
        contProcessed bson;
        persistConn bool;
        aggregateSpec bson;
        getMoreSpec bson;
    BEGIN

    IF pipeline IS NULL THEN
        pipeline = '{ "": [] }'::bson;
    END IF;

    WITH r0 AS (SELECT pageSize AS "batchSize", singleBatch AS "singleBatch" ),
    r1 AS (SELECT 'get_aggregation_cursor_test' AS "aggregate", pipeline AS "pipeline", row_get_bson(r0) AS "cursor" FROM r0)
    SELECT row_get_bson(r1) INTO aggregateSpec FROM r1;

    WITH r1 AS (SELECT 'get_aggregation_cursor_test' AS "collection", 4294967294::int8 AS "getMore", pageSize AS "batchSize" )
    SELECT row_get_bson(r1) INTO getMoreSpec FROM r1;

    SELECT cursorPage, continuation, persistConnection INTO STRICT doc, cont, persistConn FROM
                    documentdb_api.aggregate_cursor_first_page(database => 'db', commandSpec => aggregateSpec, cursorId => 4294967294);
    SELECT documentdb_api_catalog.bson_dollar_project(doc,
        ('{ "ok": 1, "cursor.id": 1, "cursor.ns": 1, "batchCount": { "$size": { "$ifNull": [ "$cursor.firstBatch", "$cursor.nextBatch" ] } }, ' ||
        ' "ids": { "$ifNull": [ "$cursor.firstBatch._id", "$cursor.nextBatch._id" ] } }')::documentdb_core.bson), length(doc::bytea)::int INTO STRICT doc, docSize;

    IF obfuscate_id THEN
        SELECT documentdb_api_catalog.bson_dollar_add_fields(doc, '{ "ids.a": "1" }'::documentdb_core.bson) INTO STRICT doc;
    END IF;
    
    SELECT documentdb_api_catalog.bson_dollar_project(cont, '{ "continuation.value": 0 }'::documentdb_core.bson) INTO STRICT contProcessed;
    RETURN NEXT ROW(doc, docSize, contProcessed, persistConn)::aggregation_cursor_test.drain_result;

    FOR i IN 1..loopCount LOOP
        SELECT cursorPage, continuation INTO STRICT doc, cont FROM documentdb_api.cursor_get_more(database => 'db', getMoreSpec => getMoreSpec, continuationSpec => cont);

        SELECT documentdb_api_catalog.bson_dollar_project(doc,
        ('{ "ok": 1, "cursor.id": 1, "cursor.ns": 1, "batchCount": { "$size": { "$ifNull": [ "$cursor.firstBatch", "$cursor.nextBatch" ] } }, ' ||
        ' "ids": { "$ifNull": [ "$cursor.firstBatch._id", "$cursor.nextBatch._id" ] } }')::documentdb_core.bson), length(doc::bytea)::int INTO STRICT doc, docSize;

        IF obfuscate_id THEN
            SELECT documentdb_api_catalog.bson_dollar_add_fields(doc, '{ "ids.a": "1" }'::documentdb_core.bson) INTO STRICT doc;
        END IF;

        SELECT documentdb_api_catalog.bson_dollar_project(cont, '{ "continuation.value": 0 }'::documentdb_core.bson) INTO STRICT contProcessed;
        RETURN NEXT ROW(doc, docSize, contProcessed, FALSE)::aggregation_cursor_test.drain_result;
    END LOOP;
END;
$$ LANGUAGE plpgsql;

-- STREAMING BASED:
-- test getting the first page (with max page size) - should limit to 2 docs at a time.
SELECT * FROM aggregation_cursor_test.drain_find_query(loopCount => 6, pageSize => 100000);
SELECT * FROM aggregation_cursor_test.drain_aggregation_query(loopCount => 6, pageSize => 100000);


-- test smaller docs (500KB)
SELECT * FROM aggregation_cursor_test.drain_find_query(loopCount => 2, pageSize => 100000, project => '{ "a": 1 }');
SELECT * FROM aggregation_cursor_test.drain_aggregation_query(loopCount => 2, pageSize => 100000, pipeline => '{ "": [{ "$project": { "a": 1 } }]}');

-- test smaller batch size(s)
SELECT * FROM aggregation_cursor_test.drain_find_query(loopCount => 2, pageSize => 0, project => '{ "a": 1 }');
SELECT * FROM aggregation_cursor_test.drain_find_query(loopCount => 12, pageSize => 1, project => '{ "a": 1 }');
SELECT * FROM aggregation_cursor_test.drain_find_query(loopCount => 5, pageSize => 2, project => '{ "a": 1 }');
SELECT * FROM aggregation_cursor_test.drain_find_query(loopCount => 4, pageSize => 3, project => '{ "a": 1 }');

SELECT * FROM aggregation_cursor_test.drain_aggregation_query(loopCount => 2, pageSize => 0, pipeline => '{ "": [{ "$project": { "a": 1 } }]}');
SELECT * FROM aggregation_cursor_test.drain_aggregation_query(loopCount => 12, pageSize => 1, pipeline => '{ "": [{ "$project": { "a": 1 } }]}');
SELECT * FROM aggregation_cursor_test.drain_aggregation_query(loopCount => 5, pageSize => 2, pipeline => '{ "": [{ "$project": { "a": 1 } }]}');
SELECT * FROM aggregation_cursor_test.drain_aggregation_query(loopCount => 4, pageSize => 3, pipeline => '{ "": [{ "$project": { "a": 1 } }]}');

SELECT * FROM aggregation_cursor_test.drain_aggregation_query(loopCount => 4, pageSize => 3, pipeline => '{ "": [{ "$project": { "a": 1 } }, { "$skip": 0 }]}');

-- test singleBatch
SELECT * FROM aggregation_cursor_test.drain_aggregation_query(loopCount => 4, pageSize => 3, pipeline => '{ "": [{ "$project": { "a": 1 } }]}', singleBatch => TRUE);
SELECT * FROM aggregation_cursor_test.drain_aggregation_query(loopCount => 4, pageSize => 3, pipeline => '{ "": [{ "$project": { "a": 1 } }]}', singleBatch => FALSE);

-- FIND: Test streaming vs not
SELECT * FROM aggregation_cursor_test.drain_find_query(loopCount => 5, pageSize => 100000, skipVal => 2);
SELECT * FROM aggregation_cursor_test.drain_find_query(loopCount => 4, pageSize => 100000, filter => '{ "_id": { "$gt": 2 }} ');
SELECT * FROM aggregation_cursor_test.drain_find_query(loopCount => 4, pageSize => 100000, limitVal => 3);
SELECT * FROM aggregation_cursor_test.drain_find_query(loopCount => 2, pageSize => 100000, limitVal => 1);
SELECT * FROM aggregation_cursor_test.drain_find_query(loopCount => 1, pageSize => 0, limitVal => 1);
SELECT * FROM aggregation_cursor_test.drain_find_query(loopCount => 5, pageSize => 100000, sort => '{ "_id": -1 }');
SELECT * FROM aggregation_cursor_test.drain_find_query(loopCount => 4, pageSize => 100000, filter => '{ "_id": { "$gt": 2 }} ', skipVal => 0, limitVal => 0);

-- AGGREGATE: Test streaming vs not
SELECT * FROM aggregation_cursor_test.drain_aggregation_query(loopCount => 5, pageSize => 100000, pipeline => '{ "": [{ "$skip": 2 }]}');
SELECT * FROM aggregation_cursor_test.drain_aggregation_query(loopCount => 4, pageSize => 100000, pipeline => '{ "": [{ "$match": { "_id": { "$gt": 2 }} }]}');
SELECT * FROM aggregation_cursor_test.drain_aggregation_query(loopCount => 4, pageSize => 100000, pipeline => '{ "": [{ "$limit": 3 }]}');
SELECT * FROM aggregation_cursor_test.drain_aggregation_query(loopCount => 2, pageSize => 100000, pipeline => '{ "": [{ "$limit": 1 }]}');
SELECT * FROM aggregation_cursor_test.drain_aggregation_query(loopCount => 1, pageSize => 0, pipeline => '{ "": [{ "$limit": 1 }]}');
SELECT * FROM aggregation_cursor_test.drain_aggregation_query(loopCount => 5, pageSize => 100000, pipeline => '{ "": [{ "$sort": { "_id": -1 } }]}');
SELECT * FROM aggregation_cursor_test.drain_aggregation_query(loopCount => 5, pageSize => 2, pipeline => '{ "": [{ "$group": { "_id": "$_id", "c": { "$max": "$a" } } }] }');

SELECT * FROM aggregation_cursor_test.drain_aggregation_query(loopCount => 1, pageSize => 100000, pipeline => '{ "": [{ "$match": { "_id": { "$gt": 2 }} }, { "$limit": 1 }]}');
SELECT * FROM aggregation_cursor_test.drain_aggregation_query(loopCount => 1, pageSize => 100000, pipeline => '{ "": [{ "$match": { "_id": { "$gt": 2 }} }, { "$limit": 1 }, { "$addFields": { "c": "$a" }}]}');

BEGIN;
SELECT * FROM aggregation_cursor_test.drain_aggregation_query(loopCount => 1, pageSize => 0, pipeline => '{ "": [{ "$match": { "_id": { "$gt": 2 }} }, { "$limit": 1 }, { "$addFields": { "c": "$a" }}]}');
ROLLBACK;

-- inside a transaction block
BEGIN;
SELECT * FROM aggregation_cursor_test.drain_find_query(loopCount => 5, pageSize => 100000, skipVal => 2);
SELECT * FROM aggregation_cursor_test.drain_find_query(loopCount => 4, pageSize => 100000, filter => '{ "_id": { "$gt": 2 }} ');
SELECT * FROM aggregation_cursor_test.drain_find_query(loopCount => 4, pageSize => 100000, limitVal => 3);
SELECT * FROM aggregation_cursor_test.drain_find_query(loopCount => 2, pageSize => 100000, limitVal => 1);
SELECT * FROM aggregation_cursor_test.drain_find_query(loopCount => 1, pageSize => 0, limitVal => 1);
SELECT * FROM aggregation_cursor_test.drain_find_query(loopCount => 5, pageSize => 100000, sort => '{ "_id": -1 }');
SELECT * FROM aggregation_cursor_test.drain_aggregation_query(loopCount => 5, pageSize => 100000, pipeline => '{ "": [{ "$skip": 2 }]}');
SELECT * FROM aggregation_cursor_test.drain_aggregation_query(loopCount => 4, pageSize => 100000, pipeline => '{ "": [{ "$match": { "_id": { "$gt": 2 }} }]}');
SELECT * FROM aggregation_cursor_test.drain_aggregation_query(loopCount => 4, pageSize => 100000, pipeline => '{ "": [{ "$limit": 3 }]}');
SELECT * FROM aggregation_cursor_test.drain_aggregation_query(loopCount => 2, pageSize => 100000, pipeline => '{ "": [{ "$limit": 1 }]}');
SELECT * FROM aggregation_cursor_test.drain_aggregation_query(loopCount => 1, pageSize => 0, pipeline => '{ "": [{ "$limit": 1 }]}');
SELECT * FROM aggregation_cursor_test.drain_aggregation_query(loopCount => 5, pageSize => 100000, pipeline => '{ "": [{ "$sort": { "_id": -1 } }]}');
SELECT * FROM aggregation_cursor_test.drain_aggregation_query(loopCount => 5, pageSize => 2, pipeline => '{ "": [{ "$group": { "_id": "$_id", "c": { "$max": "$a" } } }] }');
ROLLBACK;

-- With local execution off.
BEGIN;
set local citus.enable_local_execution to off;
SELECT * FROM aggregation_cursor_test.drain_find_query(loopCount => 5, pageSize => 100000, skipVal => 2);
SELECT * FROM aggregation_cursor_test.drain_find_query(loopCount => 4, pageSize => 100000, filter => '{ "_id": { "$gt": 2 }} ');
SELECT * FROM aggregation_cursor_test.drain_find_query(loopCount => 4, pageSize => 100000, limitVal => 3);
SELECT * FROM aggregation_cursor_test.drain_find_query(loopCount => 2, pageSize => 100000, limitVal => 1);
SELECT * FROM aggregation_cursor_test.drain_find_query(loopCount => 1, pageSize => 0, limitVal => 1);
SELECT * FROM aggregation_cursor_test.drain_find_query(loopCount => 5, pageSize => 100000, sort => '{ "_id": -1 }');
SELECT * FROM aggregation_cursor_test.drain_aggregation_query(loopCount => 5, pageSize => 100000, pipeline => '{ "": [{ "$skip": 2 }]}');
SELECT * FROM aggregation_cursor_test.drain_aggregation_query(loopCount => 4, pageSize => 100000, pipeline => '{ "": [{ "$match": { "_id": { "$gt": 2 }} }]}');
SELECT * FROM aggregation_cursor_test.drain_aggregation_query(loopCount => 4, pageSize => 100000, pipeline => '{ "": [{ "$limit": 3 }]}');
SELECT * FROM aggregation_cursor_test.drain_aggregation_query(loopCount => 2, pageSize => 100000, pipeline => '{ "": [{ "$limit": 1 }]}');
SELECT * FROM aggregation_cursor_test.drain_aggregation_query(loopCount => 1, pageSize => 0, pipeline => '{ "": [{ "$limit": 1 }]}');
SELECT * FROM aggregation_cursor_test.drain_aggregation_query(loopCount => 5, pageSize => 100000, pipeline => '{ "": [{ "$sort": { "_id": -1 } }]}');
SELECT * FROM aggregation_cursor_test.drain_aggregation_query(loopCount => 5, pageSize => 2, pipeline => '{ "": [{ "$group": { "_id": "$_id", "c": { "$max": "$a" } } }] }');
ROLLBACK;

-- with sharded
SELECT documentdb_api.shard_collection('db', 'get_aggregation_cursor_test', '{ "_id": "hashed" }', false);
SELECT * FROM aggregation_cursor_test.drain_find_query(loopCount => 6, pageSize => 100000);
SELECT * FROM aggregation_cursor_test.drain_aggregation_query(loopCount => 6, pageSize => 100000);

-- FIND: Test streaming vs not
SELECT * FROM aggregation_cursor_test.drain_find_query(loopCount => 5, pageSize => 100000, skipVal => 2);
SELECT * FROM aggregation_cursor_test.drain_find_query(loopCount => 4, pageSize => 100000, filter => '{ "_id": { "$gt": 2 }} ');
SELECT * FROM aggregation_cursor_test.drain_find_query(loopCount => 4, pageSize => 100000, limitVal => 3);
SELECT * FROM aggregation_cursor_test.drain_find_query(loopCount => 2, pageSize => 100000, limitVal => 1);
SELECT * FROM aggregation_cursor_test.drain_find_query(loopCount => 1, pageSize => 0, limitVal => 1);
SELECT * FROM aggregation_cursor_test.drain_find_query(loopCount => 5, pageSize => 100000, sort => '{ "_id": -1 }');

-- AGGREGATE: Test streaming vs not
SELECT * FROM aggregation_cursor_test.drain_aggregation_query(loopCount => 5, pageSize => 100000, pipeline => '{ "": [{ "$skip": 2 }]}');
SELECT * FROM aggregation_cursor_test.drain_aggregation_query(loopCount => 4, pageSize => 100000, pipeline => '{ "": [{ "$match": { "_id": { "$gt": 2 }} }]}');
SELECT * FROM aggregation_cursor_test.drain_aggregation_query(loopCount => 4, pageSize => 100000, pipeline => '{ "": [{ "$limit": 3 }]}');
SELECT * FROM aggregation_cursor_test.drain_aggregation_query(loopCount => 2, pageSize => 100000, pipeline => '{ "": [{ "$limit": 1 }]}');
SELECT * FROM aggregation_cursor_test.drain_aggregation_query(loopCount => 1, pageSize => 0, pipeline => '{ "": [{ "$limit": 1 }]}');
SELECT * FROM aggregation_cursor_test.drain_aggregation_query(loopCount => 5, pageSize => 100000, pipeline => '{ "": [{ "$sort": { "_id": -1 } }]}');
SELECT * FROM aggregation_cursor_test.drain_aggregation_query(loopCount => 5, pageSize => 2, pipeline => '{ "": [{ "$group": { "_id": "$_id", "c": { "$max": "$a" } } }] }');


-- inside a transaction block
BEGIN;
SELECT * FROM aggregation_cursor_test.drain_find_query(loopCount => 5, pageSize => 100000, skipVal => 2);
SELECT * FROM aggregation_cursor_test.drain_find_query(loopCount => 4, pageSize => 100000, filter => '{ "_id": { "$gt": 2 }} ');
SELECT * FROM aggregation_cursor_test.drain_find_query(loopCount => 4, pageSize => 100000, limitVal => 3);
SELECT * FROM aggregation_cursor_test.drain_find_query(loopCount => 2, pageSize => 100000, limitVal => 1);
SELECT * FROM aggregation_cursor_test.drain_find_query(loopCount => 1, pageSize => 0, limitVal => 1);
SELECT * FROM aggregation_cursor_test.drain_find_query(loopCount => 5, pageSize => 100000, sort => '{ "_id": -1 }');
SELECT * FROM aggregation_cursor_test.drain_aggregation_query(loopCount => 5, pageSize => 100000, pipeline => '{ "": [{ "$skip": 2 }]}');
SELECT * FROM aggregation_cursor_test.drain_aggregation_query(loopCount => 4, pageSize => 100000, pipeline => '{ "": [{ "$match": { "_id": { "$gt": 2 }} }]}');
SELECT * FROM aggregation_cursor_test.drain_aggregation_query(loopCount => 4, pageSize => 100000, pipeline => '{ "": [{ "$limit": 3 }]}');
SELECT * FROM aggregation_cursor_test.drain_aggregation_query(loopCount => 2, pageSize => 100000, pipeline => '{ "": [{ "$limit": 1 }]}');
SELECT * FROM aggregation_cursor_test.drain_aggregation_query(loopCount => 1, pageSize => 0, pipeline => '{ "": [{ "$limit": 1 }]}');
SELECT * FROM aggregation_cursor_test.drain_aggregation_query(loopCount => 5, pageSize => 100000, pipeline => '{ "": [{ "$sort": { "_id": -1 } }]}');
SELECT * FROM aggregation_cursor_test.drain_aggregation_query(loopCount => 5, pageSize => 2, pipeline => '{ "": [{ "$group": { "_id": "$_id", "c": { "$max": "$a" } } }] }');
ROLLBACK;

-- With local execution off.
BEGIN;
set local citus.enable_local_execution to off;
SELECT * FROM aggregation_cursor_test.drain_find_query(loopCount => 5, pageSize => 100000, skipVal => 2, obfuscate_id => true);
SELECT * FROM aggregation_cursor_test.drain_find_query(loopCount => 4, pageSize => 100000, filter => '{ "_id": { "$gt": 2 }} ', obfuscate_id => true);
SELECT * FROM aggregation_cursor_test.drain_find_query(loopCount => 4, pageSize => 100000, limitVal => 3, obfuscate_id => true);
SELECT * FROM aggregation_cursor_test.drain_find_query(loopCount => 2, pageSize => 100000, limitVal => 1, obfuscate_id => true);
SELECT * FROM aggregation_cursor_test.drain_find_query(loopCount => 1, pageSize => 0, limitVal => 1, obfuscate_id => true);
SELECT * FROM aggregation_cursor_test.drain_find_query(loopCount => 5, pageSize => 100000, sort => '{ "_id": -1 }');
SELECT * FROM aggregation_cursor_test.drain_aggregation_query(loopCount => 5, pageSize => 100000, pipeline => '{ "": [{ "$skip": 2 }]}', obfuscate_id => true);
SELECT * FROM aggregation_cursor_test.drain_aggregation_query(loopCount => 4, pageSize => 100000, pipeline => '{ "": [{ "$match": { "_id": { "$gt": 2 }} }]}', obfuscate_id => true);
SELECT * FROM aggregation_cursor_test.drain_aggregation_query(loopCount => 4, pageSize => 100000, pipeline => '{ "": [{ "$limit": 3 }]}', obfuscate_id => true);
SELECT * FROM aggregation_cursor_test.drain_aggregation_query(loopCount => 2, pageSize => 100000, pipeline => '{ "": [{ "$limit": 1 }]}', obfuscate_id => true);
SELECT * FROM aggregation_cursor_test.drain_aggregation_query(loopCount => 1, pageSize => 0, pipeline => '{ "": [{ "$limit": 1 }]}', obfuscate_id => true);
SELECT * FROM aggregation_cursor_test.drain_aggregation_query(loopCount => 5, pageSize => 100000, pipeline => '{ "": [{ "$sort": { "_id": -1 } }]}', obfuscate_id => true);
SELECT * FROM aggregation_cursor_test.drain_aggregation_query(loopCount => 5, pageSize => 2, pipeline => '{ "": [{ "$group": { "_id": "$_id", "c": { "$max": "$a" } } }] }', obfuscate_id => true);
ROLLBACK;

-- test for errors when returnKey is set to true
SELECT cursorPage FROM documentdb_api.find_cursor_first_page('db', '{ "find" : "movies", "filter" : { "title" : "a" }, "limit" : 1, "singleBatch" : true, "batchSize" : 1, "returnKey" : true, "lsid" : { "id" : { "$binary" : { "base64": "apfUje6LTzKH9YfO3smIGA==", "subType" : "04" } } }, "$db" : "test" }');

-- test for no errors when returnKey is set to false
SELECT cursorPage FROM documentdb_api.find_cursor_first_page('db', '{ "find" : "movies", "filter" : { "title" : "a" }, "limit" : 1, "singleBatch" : true, "batchSize" : 1, "returnKey" : false, "lsid" : { "id" : { "$binary" : { "base64": "apfUje6LTzKH9YfO3smIGA==", "subType" : "04" } } }, "$db" : "test" }');

-- test for errors when returnKey and showRecordId are set to true
SELECT cursorPage FROM documentdb_api.find_cursor_first_page('db', '{ "find" : "movies", "filter" : { "title" : "a" }, "limit" : 1, "singleBatch" : true, "batchSize" : 1, "showRecordId": true, "returnKey" : true, "lsid" : { "id" : { "$binary" : { "base64": "apfUje6LTzKH9YfO3smIGA==", "subType" : "04" } } }, "$db" : "test" }');

-- test for ntoreturn in find command with unset documentdb.version
SELECT cursorPage FROM documentdb_api.find_cursor_first_page('db', '{ "find" : "movies",  "limit" : 1,  "batchSize" : 1, "ntoreturn":1 ,"$db" : "test" }');
SELECT cursorPage FROM documentdb_api.find_cursor_first_page('db', '{ "find" : "movies", "ntoreturn":1 ,"$db" : "test" }');
SELECT cursorPage FROM documentdb_api.find_cursor_first_page('db', '{ "find" : "movies", "ntoreturn":1 , "batchSize":1, "$db" : "test" }');
SELECT cursorPage FROM documentdb_api.find_cursor_first_page('db', '{ "find" : "movies", "ntoreturn":1 , "limit":1, "$db" : "test" }');