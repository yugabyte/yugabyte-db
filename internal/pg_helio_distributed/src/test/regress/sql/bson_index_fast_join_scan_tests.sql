SET search_path TO helio_core,helio_api,helio_api_catalog,helio_api_internal;

SET citus.next_shard_id TO 340500;
SET helio_api.next_collection_id TO 34050;
SET helio_api.next_collection_index_id TO 34050;


-- create entries with accid == 1, 2, val == 3, 4
with r1 AS (SELECT FORMAT('{ "accid": %s, "val": %s }', ((s % 2) + 1), ((s % 2) + 3))::json as textVal from generate_series(1, 25000) s),
r2 AS (SELECT json_build_object('insert', 'fast_join_scan_tests', 'documents', json_agg(r1.textVal)) AS jsonObj FROM r1)
SELECT helio_api.insert('db', (r2.jsonObj)::text::bson) FROM r2;

SELECT helio_api.insert_one('db', 'fast_join_scan_tests', '{ "accid": 1, "val": 5 }');
DO $$
DECLARE v_output record;
BEGIN
    FOR i IN 1..5 LOOP        
        with r1 AS (SELECT FORMAT('{ "accid": %s, "val": %s }', ((s % 2) + 1), ((s % 2) + 3))::json from generate_series(1, 15000) s),
        r2 AS (SELECT json_build_object('insert', 'fast_join_scan_tests', 'documents', json_agg(r1)) AS jsonObj FROM r1)
        SELECT helio_api.insert('db', (r2.jsonObj)::text::bson) INTO v_output FROM r2;
    END LOOP;
END;
$$ LANGUAGE plpgsql;

SELECT helio_api.insert_one('db', 'fast_join_scan_tests', '{ "accid": 1, "val": 5 }');

-- create entries with accid == 3, 4, val == 5, 6

SELECT helio_api.insert_one('db', 'fast_join_scan_tests', '{ "accid": 2, "val": 6 }');

DO $$
DECLARE v_output record;
BEGIN
    FOR i IN 1..5 LOOP        
        with r1 AS (SELECT FORMAT('{ "accid": %s, "val": %s }', ((s % 2) + 3), ((s % 2) + 5))::json from generate_series(1, 15000) s),
        r2 AS (SELECT json_build_object('insert', 'fast_join_scan_tests', 'documents', json_agg(r1)) AS jsonObj FROM r1)
        SELECT helio_api.insert('db', (r2.jsonObj)::text::bson) INTO v_output FROM r2;
    END LOOP;
END;
$$ LANGUAGE plpgsql;

SELECT helio_api.insert_one('db', 'fast_join_scan_tests', '{ "accid": 2, "val": 6 }');

SELECT helio_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "fast_join_scan_tests", "indexes": [ { "key": { "accid": 1 }, "name": "myIdx1" }]}', true);
SELECT helio_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "fast_join_scan_tests", "indexes": [ { "key": { "val": 1 }, "name": "myIdx2" }]}', true);

SELECT document FROM bson_aggregation_find('db', '{ "find": "fast_join_scan_tests", "filter": { "accid": { "$in": [ 1, 2 ] }, "val": { "$in": [ 5, 6 ] } }, "projection": { "_id": 0 }, "limit": 10 }');

BEGIN;
set local client_min_messages to DEBUG3;
set local helio_api.enableMultiIndexRumJoin to off;
SELECT document FROM bson_aggregation_find('db', '{ "find": "fast_join_scan_tests", "filter": { "accid": { "$in": [ 1, 2 ] }, "val": { "$in": [ 5, 6 ] } }, "projection": { "_id": 0 }, "limit": 10 }');
set local helio_api.enableMultiIndexRumJoin to on;
SELECT document FROM bson_aggregation_find('db', '{ "find": "fast_join_scan_tests", "filter": { "accid": { "$in": [ 1, 2 ] }, "val": { "$in": [ 5, 6 ] } }, "projection": { "_id": 0 }, "limit": 10 }');
ROLLBACK;

-- add 1 doc that matches against the docs above
SELECT helio_api.insert_one('db', 'fast_join_scan_tests', '{ "accid": 2, "val": 200 }');

BEGIN;
set local client_min_messages to DEBUG3;
set local helio_api.enableMultiIndexRumJoin to on;
SELECT document FROM bson_aggregation_find('db', '{ "find": "fast_join_scan_tests", "filter": { "val": 200, "accid": { "$ne": null } }, "projection": { "_id": 0 }, "limit": 10 }');
ROLLBACK;

-- create a query to force a partialMatch scan
BEGIN;
set local client_min_messages to DEBUG3;
SELECT document FROM bson_aggregation_find('db', '{ "find": "fast_join_scan_tests", "filter": { "val": 200, "accid": { "$gt": { "$minKey": 1 } } }, "projection": { "_id": 0 }, "limit": 10 }');
ROLLBACK;

-- insert 1K docs each with a unique value
DO $$
DECLARE v_output record;
BEGIN
    FOR i IN 1..5 LOOP        
        with r1 AS (SELECT FORMAT('{ "accid": %s, "val": %s }', s, s)::json AS doc from generate_series(20000, 21000) s),
        r2 AS (SELECT json_build_object('insert', 'fast_join_scan_tests', 'documents', json_agg(r1.doc)) AS jsonObj FROM r1)
        SELECT helio_api.insert('db', (r2.jsonObj)::text::bson) INTO v_output FROM r2;
    END LOOP;
END;
$$ LANGUAGE plpgsql;

-- now generate a $in query that matches 1K items of accid and 1 item of val.
SELECT FORMAT('{ "find": "fast_join_scan_tests", "filter": %s, "projection": { "_id": 0 }, "limit": 10 }', json_build_object('accid', json_build_object('$in', json_agg(s)), 'val', 20500)) FROM generate_series(20400, 20600) s \gset

BEGIN;
set local client_min_messages to DEBUG2;
set local helio_api.enableMultiIndexRumJoin to off;
SELECT document FROM bson_aggregation_find('db', :'format');
set local helio_api.enableMultiIndexRumJoin to on;
SELECT document FROM bson_aggregation_find('db', :'format');
ROLLBACK;
