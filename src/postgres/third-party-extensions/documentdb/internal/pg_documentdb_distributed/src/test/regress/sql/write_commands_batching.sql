SET search_path TO documentdb_core,documentdb_api,documentdb_api_catalog,documentdb_api_internal;
SET citus.next_shard_id TO 662000;
SET documentdb.next_collection_id TO 6620;
SET documentdb.next_collection_index_id TO 6620;

CREATE SCHEMA bulk_write;

SELECT documentdb_api.create_collection('db', 'write_batching');

CREATE FUNCTION bulk_write.do_bulk_insert(numIterations int, ordered bool)
RETURNS bson
SET search_path TO documentdb_core,documentdb_api_catalog, pg_catalog
AS $fn$
DECLARE
    v_insertSpec bson;
    v_resultDocs bson;
BEGIN

WITH r1 AS ( SELECT array_agg(FORMAT('{ "_id": %s, "a": %s}', g, g)::bson) AS "documents" FROM generate_series(1, numIterations) g),
    r2 AS (SELECT 'write_batching' AS "insert", r1.documents AS "documents", ordered AS "ordered" FROM r1)
    SELECT row_get_bson(r2) INTO v_insertSpec FROM r2;

    SELECT p_result INTO v_resultDocs FROM documentdb_api.insert('db', v_insertSpec);
    RETURN v_resultDocs;
END;
$fn$ LANGUAGE plpgsql;

CREATE FUNCTION bulk_write.do_bulk_update(numIterations int, ordered bool)
RETURNS bson
SET search_path TO documentdb_core,documentdb_api_catalog, pg_catalog
AS $fn$
DECLARE
    v_updateSpec bson;
    v_resultDocs bson;
BEGIN

WITH r1 AS ( SELECT array_agg(FORMAT('{ "q": { "_id": %s}, "u": { "$inc": { "a": 1 } } }', MOD(g, 10) + 1)::bson) AS "documents" FROM generate_series(1, numIterations) g),
    r2 AS (SELECT 'write_batching' AS "update", r1.documents AS "updates", ordered AS "ordered" FROM r1)
    SELECT row_get_bson(r2) INTO v_updateSpec FROM r2;

    SELECT p_result INTO v_resultDocs FROM documentdb_api.update('db', v_updateSpec);
    RETURN v_resultDocs;
END;
$fn$ LANGUAGE plpgsql;

-- try a small amount.
BEGIN;
SELECT bulk_write.do_bulk_insert(10, false);
ROLLBACK;

-- try an extremely large batch (maxBatchSize)
BEGIN;
SELECT bulk_write.do_bulk_insert(25000, false);
ROLLBACK;

-- try an extremely large batch (maxBatchSize + 1 ) fails
BEGIN;
SELECT bulk_write.do_bulk_insert(25001, false);
ROLLBACK;

BEGIN;
SELECT bulk_write.do_bulk_insert(25001, true);
ROLLBACK;

-- introduce a failure in the 432'th position (Everything before that succeeds)
BEGIN;
SELECT documentdb_api.insert_one('db', 'write_batching', '{ "_id": 432, "a": 600 }');
SELECT bulk_write.do_bulk_insert(5000, true);
SELECT COUNT(*) FROM documentdb_api.collection('db', 'write_batching');
ROLLBACK;

-- introduce a failure in the 432'th position (Everything except that succeeds)
BEGIN;
SELECT documentdb_api.insert_one('db', 'write_batching', '{ "_id": 432, "a": 600 }');
SELECT bulk_write.do_bulk_insert(5000, false);
SELECT COUNT(*) FROM documentdb_api.collection('db', 'write_batching');
ROLLBACK;

BEGIN;
set local documentdb.batchWriteSubTransactionCount TO 40;
SELECT documentdb_api.insert_one('db', 'write_batching', '{ "_id": 31, "a": 600 }');
SELECT bulk_write.do_bulk_insert(35, false);
SELECT COUNT(*) FROM documentdb_api.collection('db', 'write_batching');
ROLLBACK;

BEGIN;
set local documentdb.batchWriteSubTransactionCount TO 40;
SELECT documentdb_api.insert_one('db', 'write_batching', '{ "_id": 31, "a": 600 }');
SELECT bulk_write.do_bulk_insert(39, false);
SELECT COUNT(*) FROM documentdb_api.collection('db', 'write_batching');
ROLLBACK;

BEGIN;
set local documentdb.batchWriteSubTransactionCount TO 40;
SELECT documentdb_api.insert_one('db', 'write_batching', '{ "_id": 31, "a": 600 }');
SELECT bulk_write.do_bulk_insert(40, false);
SELECT COUNT(*) FROM documentdb_api.collection('db', 'write_batching');
ROLLBACK;

BEGIN;
set local documentdb.batchWriteSubTransactionCount TO 40;
SELECT documentdb_api.insert_one('db', 'write_batching', '{ "_id": 31, "a": 600 }');
SELECT bulk_write.do_bulk_insert(41, false);
SELECT COUNT(*) FROM documentdb_api.collection('db', 'write_batching');
ROLLBACK;

-- now insert 10 docs and commit
SELECT bulk_write.do_bulk_insert(10, false);

-- do a small bulk update
BEGIN;
SELECT COUNT(*) FROM documentdb_api.collection('db', 'write_batching') WHERE document->'_id' = document->'a';
SELECT bulk_write.do_bulk_update(10, false);
SELECT COUNT(*) FROM documentdb_api.collection('db', 'write_batching') WHERE document->'_id' = document->'a';
ROLLBACK;

BEGIN;
SELECT bulk_write.do_bulk_update(10, true);
ROLLBACK;

BEGIN;
-- do a large number (fail)
SELECT bulk_write.do_bulk_update(25001, false);
ROLLBACK;

BEGIN;
-- do a large number (fail)
SELECT bulk_write.do_bulk_update(25001, true);
ROLLBACK;

BEGIN;
-- do a large number
SELECT bulk_write.do_bulk_update(10000, false);
ROLLBACK;


-- introduce an error in one document
BEGIN;
SELECT documentdb_api.update('db', '{ "update": "write_batching", "updates": [{ "q": { "_id": 5 }, "u": { "$set": { "a": "this is a string" } } }] }');
set local documentdb.batchWriteSubTransactionCount TO 40;
SELECT bulk_write.do_bulk_update(50, false);
ROLLBACK;