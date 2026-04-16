-- Based on andor.js
CREATE SCHEMA andor;

SET search_path TO documentdb_core,documentdb_api,documentdb_api_catalog,documentdb_api_internal,public,andor;
SET citus.next_shard_id TO 548000;
SET documentdb.next_collection_id TO 5480;
SET documentdb.next_collection_index_id TO 5480;

CREATE OR REPLACE FUNCTION andor.ok(query documentdb_core.bson)
 RETURNS void
 LANGUAGE plpgsql
AS $$
BEGIN
	IF NOT EXISTS (SELECT 1 FROM collection('db','andor') WHERE document @@ query) THEN
		RAISE 'query return no rows: %', query::text;
	END IF;
END;
$$;

SELECT 1 FROM drop_collection('db','andor');
SELECT 1 FROM insert_one('db','andor','{"a": 1}');
SELECT documentdb_distributed_test_helpers.drop_primary_key('db', 'andor');

CREATE OR REPLACE FUNCTION andor.test1()
 RETURNS void
 LANGUAGE plpgsql
AS $$
BEGIN
    PERFORM ok('{"a": 1}');

    PERFORM ok('{"$and": [{"a": 1}]}');
    PERFORM ok('{"$or": [{"a": 1}]}');

    PERFORM ok('{"$and": [{"$and": [{"a": 1}]}]}');
    PERFORM ok('{"$or": [{"$or": [{"a": 1}]}]}');

    PERFORM ok('{"$and": [{"$or": [{"a": 1}]}]}');
    PERFORM ok('{"$or": [{"$and": [{"a": 1}]}]}');

    PERFORM ok('{"$and": [{"$and": [{"$or": [{"a": 1}]}]}]}');
    PERFORM ok('{"$and": [{"$or": [{"$and": [{"a": 1}]}]}]}');
    PERFORM ok('{"$or": [{"$and": [{"$and": [{"a": 1}]}]}]}');

    PERFORM ok('{"$or": [{"$and": [{"$or": [{"a": 1}]}]}]}');

    -- now test $nor

    PERFORM ok('{"$and": [{"a": 1}]}');
    PERFORM ok('{"$nor": [{"a": 2}]}');

    PERFORM ok('{"$and": [{"$and": [{"a": 1}]}]}');
    PERFORM ok('{"$nor": [{"$nor": [{"a": 1}]}]}');

    PERFORM ok('{"$and": [{"$nor": [{"a": 2}]}]}');
    PERFORM ok('{"$nor": [{"$and": [{"a": 2}]}]}');

    PERFORM ok('{"$and": [{"$and": [{"$nor": [{"a": 2}]}]}]}');
    PERFORM ok('{"$and": [{"$nor": [{"$and": [{"a": 2}]}]}]}');
    PERFORM ok('{"$nor": [{"$and": [{"$and": [{"a": 2}]}]}]}');

    PERFORM ok('{"$nor": [{"$and": [{"$nor": [{"a": 1}]}]}]}');
END;
$$;

SELECT test1();

SELECT documentdb_api_internal.create_indexes_non_concurrently('db', documentdb_distributed_test_helpers.generate_create_index_arg('andor', 'index_1', '{"a.$**": 1}'), true);

BEGIN;
set local enable_seqscan TO OFF;

SELECT test1();

ROLLBACK;

-- Test an inequality base match.

CREATE OR REPLACE FUNCTION andor.test2()
 RETURNS void
 LANGUAGE plpgsql
AS $$
BEGIN

    PERFORM ok('{"a": {"$ne": 2}}');

    PERFORM ok('{"$and": [{"a": {"$ne": 2}}]}');
    PERFORM ok('{"$or": [{"a": {"$ne": 2}}]}');

    PERFORM ok('{"$and": [{"$and": [{"a": {"$ne": 2}}]}]}');
    PERFORM ok('{"$or": [{"$or": [{"a": {"$ne": 2}}]}]}');

    PERFORM ok('{"$and": [{"$or": [{"a": {"$ne": 2}}]}]}');
    PERFORM ok('{"$or": [{"$and": [{"a": {"$ne": 2}}]}]}');

    PERFORM ok('{"$and": [{"$and": [{"$or": [{"a": {"$ne": 2}}]}]}]}');
    PERFORM ok('{"$and": [{"$or": [{"$and": [{"a": {"$ne": 2}}]}]}]}');
    PERFORM ok('{"$or": [{"$and": [{"$and": [{"a": {"$ne": 2}}]}]}]}');

    PERFORM ok('{"$or": [{"$and": [{"$or": [{"a": {"$ne": 2}}]}]}]}');

    -- now test $nor

    PERFORM ok('{"$and": [{"a": {"$ne": 2}}]}');
    PERFORM ok('{"$nor": [{"a": {"$ne": 1}}]}');

    PERFORM ok('{"$and": [{"$and": [{"a": {"$ne": 2}}]}]}');
    PERFORM ok('{"$nor": [{"$nor": [{"a": {"$ne": 2}}]}]}');

    PERFORM ok('{"$and": [{"$nor": [{"a": {"$ne": 1}}]}]}');
    PERFORM ok('{"$nor": [{"$and": [{"a": {"$ne": 1}}]}]}');

    PERFORM ok('{"$and": [{"$and": [{"$nor": [{"a": {"$ne": 1}}]}]}]}');
    PERFORM ok('{"$and": [{"$nor": [{"$and": [{"a": {"$ne": 1}}]}]}]}');
    PERFORM ok('{"$nor": [{"$and": [{"$and": [{"a": {"$ne": 1}}]}]}]}');

    PERFORM ok('{"$nor": [{"$and": [{"$nor": [{"a": {"$ne": 2}}]}]}]}');
END;
$$;

SELECT 1 FROM drop_collection('db','andor');
SELECT 1 FROM insert_one('db','andor','{"a": 1}');

SELECT test2();

SELECT documentdb_api_internal.create_indexes_non_concurrently('db', documentdb_distributed_test_helpers.generate_create_index_arg('andor', 'index_2', '{"a.$**": 1}'), true);

BEGIN;
set local enable_seqscan TO OFF;

SELECT test2();

ROLLBACK;

SELECT drop_collection('db','andor');
DROP SCHEMA andor CASCADE;
