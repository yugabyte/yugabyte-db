SET search_path TO documentdb_api_catalog, documentdb_api, documentdb_core, public;
SET citus.next_shard_id TO 3160000;
SET documentdb.next_collection_id TO 3160;
SET documentdb.next_collection_index_id TO 3160;

SET citus.multi_shard_modify_mode TO 'sequential';
set documentdb.useFileBasedPersistedCursors to on;

CREATE SCHEMA aggregation_cursor_test_file;

DO $$
DECLARE i int;
BEGIN
-- each doc is "a": 500KB, "c": 5 MB - ~5.5 MB & there's 10 of them
FOR i IN 1..10 LOOP
PERFORM documentdb_api.insert_one('db', 'get_aggregation_cursor_test_file', FORMAT('{ "_id": %s, "a": "%s", "c": [ %s "d" ] }',  i, repeat('Sample', 100000), repeat('"' || repeat('a', 1000) || '", ', 5000))::documentdb_core.bson);
END LOOP;
END;
$$;

CREATE TYPE aggregation_cursor_test_file.drain_result AS (filteredDoc bson, docSize int, continuationFiltered bson, persistConnection bool);


CREATE FUNCTION aggregation_cursor_test_file.drain_find_query(
    loopCount int, pageSize int, project bson DEFAULT NULL, skipVal int4 DEFAULT NULL, limitVal int4 DEFAULT NULL,
    sort bson DEFAULT NULL, filter bson default null,
    obfuscate_id bool DEFAULT false) RETURNS SETOF aggregation_cursor_test_file.drain_result AS
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

    WITH r1 AS (SELECT 'get_aggregation_cursor_test_file' AS "find", filter AS "filter", sort AS "sort", project AS "projection", skipVal AS "skip", limitVal as "limit", pageSize AS "batchSize")
    SELECT row_get_bson(r1) INTO findSpec FROM r1;

    WITH r1 AS (SELECT 'get_aggregation_cursor_test_file' AS "collection", 4294967294::int8 AS "getMore", pageSize AS "batchSize")
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
    RETURN NEXT ROW(doc, docSize, contProcessed, persistConn)::aggregation_cursor_test_file.drain_result;

    FOR i IN 1..loopCount LOOP
        SELECT cursorPage, continuation INTO STRICT doc, cont FROM documentdb_api.cursor_get_more(database => 'db', getMoreSpec => getMoreSpec, continuationSpec => cont);

        SELECT documentdb_api_catalog.bson_dollar_project(doc,
        ('{ "ok": 1, "cursor.id": 1, "cursor.ns": 1, "batchCount": { "$size": { "$ifNull": [ "$cursor.firstBatch", "$cursor.nextBatch" ] } }, ' ||
        ' "ids": { "$ifNull": [ "$cursor.firstBatch._id", "$cursor.nextBatch._id" ] } }')::documentdb_core.bson), length(doc::bytea)::int INTO STRICT doc, docSize;

        IF obfuscate_id THEN
            SELECT documentdb_api_catalog.bson_dollar_add_fields(doc, '{ "ids.a": "1" }'::documentdb_core.bson) INTO STRICT doc;
        END IF;

        SELECT documentdb_api_catalog.bson_dollar_project(cont, '{ "continuation.value": 0 }'::documentdb_core.bson) INTO STRICT contProcessed;
        RETURN NEXT ROW(doc, docSize, contProcessed, FALSE)::aggregation_cursor_test_file.drain_result;
    END LOOP;
END;
$$ LANGUAGE plpgsql;

CREATE FUNCTION aggregation_cursor_test_file.drain_aggregation_query(
    loopCount int, pageSize int, pipeline bson DEFAULT NULL, obfuscate_id bool DEFAULT false, singleBatch bool DEFAULT NULL) RETURNS SETOF aggregation_cursor_test_file.drain_result AS
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
    r1 AS (SELECT 'get_aggregation_cursor_test_file' AS "aggregate", pipeline AS "pipeline", row_get_bson(r0) AS "cursor" FROM r0)
    SELECT row_get_bson(r1) INTO aggregateSpec FROM r1;

    WITH r1 AS (SELECT 'get_aggregation_cursor_test_file' AS "collection", 4294967294::int8 AS "getMore", pageSize AS "batchSize" )
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
    RETURN NEXT ROW(doc, docSize, contProcessed, persistConn)::aggregation_cursor_test_file.drain_result;

    FOR i IN 1..loopCount LOOP
        SELECT cursorPage, continuation INTO STRICT doc, cont FROM documentdb_api.cursor_get_more(database => 'db', getMoreSpec => getMoreSpec, continuationSpec => cont);

        SELECT documentdb_api_catalog.bson_dollar_project(doc,
        ('{ "ok": 1, "cursor.id": 1, "cursor.ns": 1, "batchCount": { "$size": { "$ifNull": [ "$cursor.firstBatch", "$cursor.nextBatch" ] } }, ' ||
        ' "ids": { "$ifNull": [ "$cursor.firstBatch._id", "$cursor.nextBatch._id" ] } }')::documentdb_core.bson), length(doc::bytea)::int INTO STRICT doc, docSize;

        IF obfuscate_id THEN
            SELECT documentdb_api_catalog.bson_dollar_add_fields(doc, '{ "ids.a": "1" }'::documentdb_core.bson) INTO STRICT doc;
        END IF;

        SELECT documentdb_api_catalog.bson_dollar_project(cont, '{ "continuation.value": 0 }'::documentdb_core.bson) INTO STRICT contProcessed;
        RETURN NEXT ROW(doc, docSize, contProcessed, FALSE)::aggregation_cursor_test_file.drain_result;
    END LOOP;
END;
$$ LANGUAGE plpgsql;

-- PERSISTED BASED:
-- test getting the first page (with max page size) - should limit to 2 docs at a time.
SELECT * FROM aggregation_cursor_test_file.drain_find_query(loopCount => 6, limitVal => 300000, pageSize => 100000);
SELECT * FROM aggregation_cursor_test_file.drain_aggregation_query(loopCount => 6, pageSize => 100000, pipeline => '{ "": [{ "$limit": 300000 }]}');


-- test smaller docs (500KB)
SELECT * FROM aggregation_cursor_test_file.drain_find_query(loopCount => 2, limitVal => 300000, pageSize => 100000, project => '{ "a": 1 }');
SELECT * FROM aggregation_cursor_test_file.drain_aggregation_query(loopCount => 2, pageSize => 100000, pipeline => '{ "": [{ "$project": { "a": 1 } }, { "$limit": 300000 }]}');

-- test smaller batch size(s)
SELECT * FROM aggregation_cursor_test_file.drain_find_query(loopCount => 2, pageSize => 0, project => '{ "a": 1 }', limitVal => 300000);

-- this will fail (with cursor in use)
SELECT * FROM aggregation_cursor_test_file.drain_find_query(loopCount => 12, pageSize => 1, project => '{ "a": 1 }', limitVal => 300000);

SELECT documentdb_api_internal.delete_cursors(ARRAY[4294967294::int8]);

-- now this works
SELECT * FROM aggregation_cursor_test_file.drain_find_query(loopCount => 5, pageSize => 2, project => '{ "a": 1 }', limitVal => 300000);
SELECT * FROM aggregation_cursor_test_file.drain_find_query(loopCount => 4, pageSize => 3, project => '{ "a": 1 }', limitVal => 300000);

SELECT * FROM aggregation_cursor_test_file.drain_aggregation_query(loopCount => 2, pageSize => 0, pipeline => '{ "": [{ "$project": { "a": 1 } }, { "$limit": 300000 }]}');

SELECT documentdb_api_internal.delete_cursors(ARRAY[4294967294::int8]);
SELECT * FROM aggregation_cursor_test_file.drain_aggregation_query(loopCount => 12, pageSize => 1, pipeline => '{ "": [{ "$project": { "a": 1 } }, { "$limit": 300000 }]}');
SELECT * FROM aggregation_cursor_test_file.drain_aggregation_query(loopCount => 5, pageSize => 2, pipeline => '{ "": [{ "$project": { "a": 1 } }, { "$limit": 300000 }]}');
SELECT * FROM aggregation_cursor_test_file.drain_aggregation_query(loopCount => 4, pageSize => 3, pipeline => '{ "": [{ "$project": { "a": 1 } }, { "$limit": 300000 }]}');

SELECT * FROM aggregation_cursor_test_file.drain_aggregation_query(loopCount => 4, pageSize => 3, pipeline => '{ "": [{ "$project": { "a": 1 } }, { "$skip": 1 }]}');

-- FIND: Test streaming vs not
SELECT * FROM aggregation_cursor_test_file.drain_find_query(loopCount => 5, pageSize => 100000, skipVal => 2);
SELECT * FROM aggregation_cursor_test_file.drain_find_query(loopCount => 4, pageSize => 100000, filter => '{ "_id": { "$gt": 2 }} ', limitVal => 300000);
SELECT * FROM aggregation_cursor_test_file.drain_find_query(loopCount => 4, pageSize => 100000, limitVal => 3);
SELECT * FROM aggregation_cursor_test_file.drain_find_query(loopCount => 2, pageSize => 100000, limitVal => 1);
SELECT * FROM aggregation_cursor_test_file.drain_find_query(loopCount => 1, pageSize => 0, limitVal => 1);
SELECT * FROM aggregation_cursor_test_file.drain_find_query(loopCount => 5, pageSize => 100000, sort => '{ "_id": -1 }');
SELECT * FROM aggregation_cursor_test_file.drain_find_query(loopCount => 4, pageSize => 100000, filter => '{ "_id": { "$gt": 2 }} ', skipVal => 0, limitVal => 300000);

-- AGGREGATE: Test streaming vs not
SELECT * FROM aggregation_cursor_test_file.drain_aggregation_query(loopCount => 5, pageSize => 100000, pipeline => '{ "": [{ "$skip": 2 }]}');
SELECT * FROM aggregation_cursor_test_file.drain_aggregation_query(loopCount => 4, pageSize => 100000, pipeline => '{ "": [{ "$match": { "_id": { "$gt": 2 }} }, { "$limit": 300000 }]}');
SELECT * FROM aggregation_cursor_test_file.drain_aggregation_query(loopCount => 4, pageSize => 100000, pipeline => '{ "": [{ "$limit": 3 }]}');
SELECT * FROM aggregation_cursor_test_file.drain_aggregation_query(loopCount => 2, pageSize => 100000, pipeline => '{ "": [{ "$limit": 1 }]}');
SELECT * FROM aggregation_cursor_test_file.drain_aggregation_query(loopCount => 1, pageSize => 0, pipeline => '{ "": [{ "$limit": 1 }]}');
SELECT * FROM aggregation_cursor_test_file.drain_aggregation_query(loopCount => 5, pageSize => 100000, pipeline => '{ "": [{ "$sort": { "_id": -1 } }]}');
SELECT * FROM aggregation_cursor_test_file.drain_aggregation_query(loopCount => 5, pageSize => 2, pipeline => '{ "": [{ "$group": { "_id": "$_id", "c": { "$max": "$a" } } }] }');

SELECT * FROM aggregation_cursor_test_file.drain_aggregation_query(loopCount => 1, pageSize => 100000, pipeline => '{ "": [{ "$match": { "_id": { "$gt": 2 }} }, { "$limit": 1 }]}');
SELECT * FROM aggregation_cursor_test_file.drain_aggregation_query(loopCount => 1, pageSize => 100000, pipeline => '{ "": [{ "$match": { "_id": { "$gt": 2 }} }, { "$limit": 1 }, { "$addFields": { "c": "$a" }}]}');

BEGIN;
SELECT * FROM aggregation_cursor_test_file.drain_aggregation_query(loopCount => 1, pageSize => 0, pipeline => '{ "": [{ "$match": { "_id": { "$gt": 2 }} }, { "$limit": 1 }, { "$addFields": { "c": "$a" }}]}');
ROLLBACK;

-- With local execution off.
set citus.enable_local_execution to off;
SELECT * FROM aggregation_cursor_test_file.drain_find_query(loopCount => 5, pageSize => 100000, skipVal => 2);
SELECT * FROM aggregation_cursor_test_file.drain_find_query(loopCount => 4, pageSize => 100000, filter => '{ "_id": { "$gt": 2 }} ', limitVal => 300000);
SELECT * FROM aggregation_cursor_test_file.drain_find_query(loopCount => 4, pageSize => 100000, limitVal => 3);
SELECT * FROM aggregation_cursor_test_file.drain_find_query(loopCount => 2, pageSize => 100000, limitVal => 1);
SELECT * FROM aggregation_cursor_test_file.drain_find_query(loopCount => 1, pageSize => 0, limitVal => 1);
SELECT * FROM aggregation_cursor_test_file.drain_find_query(loopCount => 5, pageSize => 100000, sort => '{ "_id": -1 }');
SELECT * FROM aggregation_cursor_test_file.drain_aggregation_query(loopCount => 5, pageSize => 100000, pipeline => '{ "": [{ "$skip": 2 }]}');
SELECT * FROM aggregation_cursor_test_file.drain_aggregation_query(loopCount => 4, pageSize => 100000, pipeline => '{ "": [{ "$match": { "_id": { "$gt": 2 }} }]}');
SELECT * FROM aggregation_cursor_test_file.drain_aggregation_query(loopCount => 4, pageSize => 100000, pipeline => '{ "": [{ "$limit": 3 }]}');
SELECT * FROM aggregation_cursor_test_file.drain_aggregation_query(loopCount => 2, pageSize => 100000, pipeline => '{ "": [{ "$limit": 1 }]}');
SELECT * FROM aggregation_cursor_test_file.drain_aggregation_query(loopCount => 1, pageSize => 0, pipeline => '{ "": [{ "$limit": 1 }]}');
SELECT * FROM aggregation_cursor_test_file.drain_aggregation_query(loopCount => 5, pageSize => 100000, pipeline => '{ "": [{ "$sort": { "_id": -1 } }]}');
SELECT * FROM aggregation_cursor_test_file.drain_aggregation_query(loopCount => 5, pageSize => 2, pipeline => '{ "": [{ "$group": { "_id": "$_id", "c": { "$max": "$a" } } }] }');
RESET citus.enable_local_execution;

-- with sharded
SELECT documentdb_api.shard_collection('db', 'get_aggregation_cursor_test_file', '{ "_id": "hashed" }', false);
SELECT * FROM aggregation_cursor_test_file.drain_find_query(loopCount => 6, pageSize => 100000);
SELECT * FROM aggregation_cursor_test_file.drain_aggregation_query(loopCount => 6, pageSize => 100000);

-- FIND: Test streaming vs not
SELECT * FROM aggregation_cursor_test_file.drain_find_query(loopCount => 5, pageSize => 100000, skipVal => 2);
SELECT * FROM aggregation_cursor_test_file.drain_find_query(loopCount => 4, pageSize => 100000, filter => '{ "_id": { "$gt": 2 }} ');
SELECT * FROM aggregation_cursor_test_file.drain_find_query(loopCount => 4, pageSize => 100000, limitVal => 3);
SELECT * FROM aggregation_cursor_test_file.drain_find_query(loopCount => 2, pageSize => 100000, limitVal => 1);
SELECT * FROM aggregation_cursor_test_file.drain_find_query(loopCount => 1, pageSize => 0, limitVal => 1);
SELECT * FROM aggregation_cursor_test_file.drain_find_query(loopCount => 5, pageSize => 100000, sort => '{ "_id": -1 }');

-- AGGREGATE: Test streaming vs not
SELECT * FROM aggregation_cursor_test_file.drain_aggregation_query(loopCount => 5, pageSize => 100000, pipeline => '{ "": [{ "$skip": 2 }]}');
SELECT * FROM aggregation_cursor_test_file.drain_aggregation_query(loopCount => 4, pageSize => 100000, pipeline => '{ "": [{ "$match": { "_id": { "$gt": 2 }} }]}');
SELECT * FROM aggregation_cursor_test_file.drain_aggregation_query(loopCount => 4, pageSize => 100000, pipeline => '{ "": [{ "$limit": 3 }]}');
SELECT * FROM aggregation_cursor_test_file.drain_aggregation_query(loopCount => 2, pageSize => 100000, pipeline => '{ "": [{ "$limit": 1 }]}');
SELECT * FROM aggregation_cursor_test_file.drain_aggregation_query(loopCount => 1, pageSize => 0, pipeline => '{ "": [{ "$limit": 1 }]}');
SELECT * FROM aggregation_cursor_test_file.drain_aggregation_query(loopCount => 5, pageSize => 100000, pipeline => '{ "": [{ "$sort": { "_id": -1 } }]}');
SELECT * FROM aggregation_cursor_test_file.drain_aggregation_query(loopCount => 5, pageSize => 2, pipeline => '{ "": [{ "$group": { "_id": "$_id", "c": { "$max": "$a" } } }] }');

-- With local execution off.
set citus.enable_local_execution to off;
SELECT * FROM aggregation_cursor_test_file.drain_find_query(loopCount => 5, pageSize => 100000, skipVal => 2, obfuscate_id => true);
SELECT * FROM aggregation_cursor_test_file.drain_find_query(loopCount => 4, pageSize => 100000, filter => '{ "_id": { "$gt": 2 }} ', obfuscate_id => true);
SELECT * FROM aggregation_cursor_test_file.drain_find_query(loopCount => 4, pageSize => 100000, limitVal => 3, obfuscate_id => true);
SELECT * FROM aggregation_cursor_test_file.drain_find_query(loopCount => 2, pageSize => 100000, limitVal => 1, obfuscate_id => true);
SELECT * FROM aggregation_cursor_test_file.drain_find_query(loopCount => 1, pageSize => 0, limitVal => 1, obfuscate_id => true);
SELECT * FROM aggregation_cursor_test_file.drain_find_query(loopCount => 5, pageSize => 100000, sort => '{ "_id": -1 }');
SELECT * FROM aggregation_cursor_test_file.drain_aggregation_query(loopCount => 5, pageSize => 100000, pipeline => '{ "": [{ "$skip": 2 }]}', obfuscate_id => true);
SELECT * FROM aggregation_cursor_test_file.drain_aggregation_query(loopCount => 4, pageSize => 100000, pipeline => '{ "": [{ "$match": { "_id": { "$gt": 2 }} }]}', obfuscate_id => true);
SELECT * FROM aggregation_cursor_test_file.drain_aggregation_query(loopCount => 4, pageSize => 100000, pipeline => '{ "": [{ "$limit": 3 }]}', obfuscate_id => true);
SELECT * FROM aggregation_cursor_test_file.drain_aggregation_query(loopCount => 2, pageSize => 100000, pipeline => '{ "": [{ "$limit": 1 }]}', obfuscate_id => true);
SELECT * FROM aggregation_cursor_test_file.drain_aggregation_query(loopCount => 1, pageSize => 0, pipeline => '{ "": [{ "$limit": 1 }]}', obfuscate_id => true);
SELECT * FROM aggregation_cursor_test_file.drain_aggregation_query(loopCount => 5, pageSize => 100000, pipeline => '{ "": [{ "$sort": { "_id": -1 } }]}', obfuscate_id => true);
SELECT * FROM aggregation_cursor_test_file.drain_aggregation_query(loopCount => 5, pageSize => 2, pipeline => '{ "": [{ "$group": { "_id": "$_id", "c": { "$max": "$a" } } }] }', obfuscate_id => true);
RESET citus.enable_local_execution;

-- start draining first page first (should not persist connection and have a query file)
SELECT cursorPage, continuation, persistConnection FROM
    documentdb_api.find_cursor_first_page(database => 'db', commandSpec => '{ "find": "get_aggregation_cursor_test_file", "skip": 1, "batchSize": 2, "projection": { "_id": 1 } }', cursorId => 4294967291);

SELECT * FROM pg_ls_dir('pg_documentdb_cursor_files') f WHERE f LIKE '%4294967291%' ORDER BY 1;

-- can't reuse the same cursorId
SELECT cursorPage, continuation, persistConnection FROM
    documentdb_api.find_cursor_first_page(database => 'db', commandSpec => '{ "find": "get_aggregation_cursor_test_file", "skip": 1, "batchSize": 2, "projection": { "_id": 1 } }', cursorId => 4294967291);

SELECT documentdb_api_internal.delete_cursors(ARRAY[4294967291::int8]);
SELECT * FROM pg_ls_dir('pg_documentdb_cursor_files') f WHERE f LIKE '%4294967291%' ORDER BY 1;

SELECT cursorPage, continuation, persistConnection FROM
    documentdb_api.find_cursor_first_page(database => 'db', commandSpec => '{ "find": "get_aggregation_cursor_test_file", "skip": 1, "batchSize": 2, "projection": { "_id": 1 } }', cursorId => 4294967291);

-- set max cursors to 5
set documentdb.maxCursorFileCount to 5;

SELECT cursorPage, continuation, persistConnection FROM
    documentdb_api.find_cursor_first_page(database => 'db', commandSpec => '{ "find": "get_aggregation_cursor_test_file", "skip": 1, "batchSize": 2, "projection": { "_id": 1 } }', cursorId => 4294967292);
SELECT cursorPage, continuation, persistConnection FROM
    documentdb_api.find_cursor_first_page(database => 'db', commandSpec => '{ "find": "get_aggregation_cursor_test_file", "skip": 1, "batchSize": 2, "projection": { "_id": 1 } }', cursorId => 4294967293);
SELECT cursorPage, continuation, persistConnection FROM
    documentdb_api.find_cursor_first_page(database => 'db', commandSpec => '{ "find": "get_aggregation_cursor_test_file", "skip": 1, "batchSize": 2, "projection": { "_id": 1 } }', cursorId => 4294967294);
SELECT cursorPage, continuation, persistConnection FROM
    documentdb_api.find_cursor_first_page(database => 'db', commandSpec => '{ "find": "get_aggregation_cursor_test_file", "skip": 1, "batchSize": 2, "projection": { "_id": 1 } }', cursorId => 4294967295);
SELECT cursorPage, continuation, persistConnection FROM
    documentdb_api.find_cursor_first_page(database => 'db', commandSpec => '{ "find": "get_aggregation_cursor_test_file", "skip": 1, "batchSize": 2, "projection": { "_id": 1 } }', cursorId => 4294967296);

-- delete a cursor and see that it succeeds one time.
SELECT * FROM pg_ls_dir('pg_documentdb_cursor_files') f WHERE f LIKE '%429496729%' ORDER BY 1;
SELECT documentdb_api_internal.delete_cursors(ARRAY[4294967295::int8]);
SELECT * FROM pg_ls_dir('pg_documentdb_cursor_files') f WHERE f LIKE '%429496729%' ORDER BY 1;

SELECT cursorPage, continuation, persistConnection FROM
    documentdb_api.find_cursor_first_page(database => 'db', commandSpec => '{ "find": "get_aggregation_cursor_test_file", "skip": 1, "batchSize": 2, "projection": { "_id": 1 } }', cursorId => 4294967295);
SELECT cursorPage, continuation, persistConnection FROM
    documentdb_api.find_cursor_first_page(database => 'db', commandSpec => '{ "find": "get_aggregation_cursor_test_file", "skip": 1, "batchSize": 2, "projection": { "_id": 1 } }', cursorId => 4294967298);