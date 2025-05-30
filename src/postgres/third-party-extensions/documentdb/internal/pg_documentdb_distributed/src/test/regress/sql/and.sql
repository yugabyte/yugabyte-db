-- Based on and.js
CREATE SCHEMA and1;
SET search_path TO documentdb_core,documentdb_api,documentdb_api_catalog,documentdb_api_internal,public,and1;
SET citus.next_shard_id TO 549000;
SET documentdb.next_collection_id TO 5490;
SET documentdb.next_collection_index_id TO 5490;

SELECT 1 FROM drop_collection('db','and1');
SELECT 1 FROM insert_one('db','and1', '{"a":[1,2]}');
SELECT 1 FROM insert_one('db','and1', '{"a":"foo"}');
SELECT documentdb_distributed_test_helpers.drop_primary_key('db', 'and1');

CREATE OR REPLACE FUNCTION and1.assert_count(expected_row_count int, query bson)
 RETURNS void
 LANGUAGE plpgsql
AS $$
DECLARE
	returned_row_count int;
BEGIN
	SELECT count(*) INTO returned_row_count
	FROM collection('db','and1') WHERE document @@ query;

	IF returned_row_count <> expected_row_count THEN
		RAISE 'query % returned % rows instead of %', query, returned_row_count, expected_row_count;
	END IF;
END;
$$;

CREATE OR REPLACE FUNCTION and1.check1()
 RETURNS void
 LANGUAGE plpgsql
AS $$
BEGIN
    /*
    -- $and must be an array
    assert.throws(function() {
        '{"$and": 4}).toArray();
    });
    -- $and array must not be empty
    assert.throws(function() {
        '{"$and": []}).toArray();
    });
    -- $and elements must be objects
    assert.throws(function() {
        '{"$and": [4]}).toArray();
    });
    */

    -- Check equality matching
    PERFORM assert_count(1, '{"$and": [{"a": 1}]}');
    PERFORM assert_count(1, '{"$and": [{"a": 1}, {"a": 2}]}');
    PERFORM assert_count(0, '{"$and": [{"a": 1}, {"a": 3}]}');
    PERFORM assert_count(0, '{"$and": [{"a": 1}, {"a": 2}, {"a": 3}]}');
    PERFORM assert_count(1, '{"$and": [{"a": "foo"}]}');
    PERFORM assert_count(0, '{"$and": [{"a": "foo"}, {"a": "g"}]}');

    -- Check $and with other fields
    PERFORM assert_count(1, '{"a": 2, "$and": [{"a": 1}]}');
    PERFORM assert_count(0, '{"a": 0, "$and": [{"a": 1}]}');
    PERFORM assert_count(0, '{"a": 2, "$and": [{"a": 0}]}');
    PERFORM assert_count(1, '{"a": 1, "$and": [{"a": 1}]}');

    -- Check recursive $and
    PERFORM assert_count(1, '{"a": 2, "$and": [{"$and": [{"a": 1}]}]}');
    PERFORM assert_count(0, '{"a": 0, "$and": [{"$and": [{"a": 1}]}]}');
    PERFORM assert_count(0, '{"a": 2, "$and": [{"$and": [{"a": 0}]}]}');
    PERFORM assert_count(1, '{"a": 1, "$and": [{"$and": [{"a": 1}]}]}');

    PERFORM assert_count(1, '{"$and": [{"a": 2}, {"$and": [{"a": 1}]}]}');
    PERFORM assert_count(0, '{"$and": [{"a": 0}, {"$and": [{"a": 1}]}]}');
    PERFORM assert_count(0, '{"$and": [{"a": 2}, {"$and": [{"a": 0}]}]}');
    PERFORM assert_count(1, '{"$and": [{"a": 1}, {"$and": [{"a": 1}]}]}');

    -- Some of these cases were more important with an alternative $and syntax
    -- that was rejected, but they're still valid check1s.

    -- Check simple regex
    PERFORM assert_count(1, '{"$and": [{"a": {"$regularExpression":{"pattern":"foo","options":""}}}]}');
    -- Check multiple regexes
    PERFORM assert_count(1, '{"$and": [{"a": {"$regularExpression":{"pattern":"foo","options":""}}}, {"a": {"$regularExpression":{"pattern":"^f","options":""}}}, {"a": {"$regularExpression":{"pattern":"o","options":""}}}]}');
    PERFORM assert_count(0, '{"$and": [{"a": {"$regularExpression":{"pattern":"foo","options":""}}}, {"a": {"$regularExpression":{"pattern":"^g","options":""}}}]}');
    PERFORM assert_count(1, '{"$and": [{"a": {"$regularExpression":{"pattern":"^f","options":""}}}, {"a": "foo"}]}');
    -- Check regex flags
    PERFORM assert_count(0, '{"$and": [{"a": {"$regularExpression":{"pattern":"^F","options":""}}}, {"a": "foo"}]}');
    --PERFORM assert_count(1, '{"$and": [{"a": {"$regularExpression":{"pattern":"^F","options":"i"}}}, {"a": "foo"}]}');

    -- Check operator
    PERFORM assert_count(1, '{"$and": [{"a": {"$gt": 0}}]}');
	/*
    -- Check where
    PERFORM assert_count(1, '{"a": "foo", "$where": "this.a==\"foo\""}');
    PERFORM assert_count(1, '{"$and": [{"a": "foo"}], "$where": "this.a==\"foo\""}');
    PERFORM assert_count(1, '{"$and": [{"a": "foo"}], "$where": "this.a==\"foo\""}');

    -- Nested where ok
    PERFORM assert_count(1, '{"$and": [{"$where": "this.a==\"foo\""}]}');
    PERFORM assert_count(1, '{"$and": [{"a": "foo"}, {"$where": "this.a==\"foo\""}]}');
    PERFORM assert_count(1, '{"$and": [{"$where": "this.a==\"foo\""}], "$where": "this.a==\"foo\""}');
	*/
END; $$;

SELECT check1();

SELECT documentdb_api_internal.create_indexes_non_concurrently('db', documentdb_distributed_test_helpers.generate_create_index_arg('and1', 'index_1', '{"a.$**": 1}'), true);

BEGIN;
set local enable_seqscan TO OFF;
SELECT check1();
ROLLBACK;

SELECT assert_count(1, '{"a": 1, "$and": [{"a": 2}]}');
SELECT assert_count(1, '{"$and": [{"a": 1}, {"a": 2}]}');

-- with regex options
SELECT assert_count(1, '{"$and": [{"a": {"$regularExpression":{"pattern":"^F","options":"i"}}}, {"a": "foo"}]}');

SELECT drop_collection('db','and1');
DROP SCHEMA and1 CASCADE;
