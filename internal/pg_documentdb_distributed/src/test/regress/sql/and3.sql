-- Based on and3.js
CREATE SCHEMA and3;
SET search_path TO documentdb_core,documentdb_api,documentdb_api_catalog,documentdb_api_internal,public,and3;
SET citus.next_shard_id TO 547000;
SET documentdb.next_collection_id TO 5470;
SET documentdb.next_collection_index_id TO 5470;

SELECT drop_collection('db','and3');
SELECT 1 FROM insert_one('db','and3', '{"a":3}');
SELECT 1 FROM insert_one('db','and3', '{"a":"foo"}');

SELECT documentdb_distributed_test_helpers.drop_primary_key('db', 'and3');

SELECT documentdb_api_internal.create_indexes_non_concurrently('db', documentdb_distributed_test_helpers.generate_create_index_arg('and3', 'index_1', '{"a.$**": 1}'), true);

-- examined_row_count is currently ignored
CREATE OR REPLACE FUNCTION and3.checkScanMatch(query bson, examined_row_count int, expected_row_count int)
 RETURNS void
 LANGUAGE plpgsql
AS $$
DECLARE
	returned_row_count int;
BEGIN
	SELECT count(*) INTO returned_row_count
	FROM collection('db','and3') WHERE document @@ query;

	IF returned_row_count <> expected_row_count THEN
		RAISE 'query % returned % rows instead of %', query, returned_row_count, expected_row_count;
	END IF;
END;
$$;

SELECT checkScanMatch('{"a": {"$regularExpression":{"pattern":"o","options":""}}}', 1, 1);
SELECT checkScanMatch('{"a": {"$regularExpression":{"pattern":"a","options":""}}}', 0, 0);
SELECT checkScanMatch('{"a": {"$not": {"$regularExpression":{"pattern":"o","options":""}}}}', 2, 1);
SELECT checkScanMatch('{"a": {"$not": {"$regularExpression":{"pattern":"a","options":""}}}}', 2, 2);

SELECT checkScanMatch('{"$and": [{"a": {"$regularExpression":{"pattern":"o","options":""}}}]}', 1, 1);
SELECT checkScanMatch('{"$and": [{"a": {"$regularExpression":{"pattern":"a","options":""}}}]}', 0, 0);
SELECT checkScanMatch('{"$and": [{"a": {"$not": {"$regularExpression":{"pattern":"o","options":""}}}}]}', 2, 1);
SELECT checkScanMatch('{"$and": [{"a": {"$not": {"$regularExpression":{"pattern":"a","options":""}}}}]}', 2, 2);
SELECT checkScanMatch('{"$and": [{"a": {"$regularExpression":{"pattern":"o","options":""}}}, {"a": {"$not": {"$regularExpression":{"pattern":"o","options":""}}}}]}', 1, 0);
SELECT checkScanMatch('{"$and": [{"a": {"$regularExpression":{"pattern":"o","options":""}}}, {"a": {"$not": {"$regularExpression":{"pattern":"a","options":""}}}}]}', 1, 1);
SELECT checkScanMatch('{"$or": [{"a": {"$regularExpression":{"pattern":"o","options":""}}}]}', 1, 1);
SELECT checkScanMatch('{"$or": [{"a": {"$regularExpression":{"pattern":"a","options":""}}}]}', 0, 0);
SELECT checkScanMatch('{"$nor": [{"a": {"$regularExpression":{"pattern":"o","options":""}}}]}', 2, 1);
SELECT checkScanMatch('{"$nor": [{"a": {"$regularExpression":{"pattern":"a","options":""}}}]}', 2, 2);

SELECT checkScanMatch('{"$and": [{"$and": [{"a": {"$regularExpression":{"pattern":"o","options":""}}}]}]}', 1, 1);
SELECT checkScanMatch('{"$and": [{"$and": [{"a": {"$regularExpression":{"pattern":"a","options":""}}}]}]}', 0, 0);
SELECT checkScanMatch('{"$and": [{"$and": [{"a": {"$not": {"$regularExpression":{"pattern":"o","options":""}}}}]}]}', 2, 1);
SELECT checkScanMatch('{"$and": [{"$and": [{"a": {"$not": {"$regularExpression":{"pattern":"a","options":""}}}}]}]}', 2, 2);
SELECT checkScanMatch('{"$and": [{"$or": [{"a": {"$regularExpression":{"pattern":"o","options":""}}}]}]}', 1, 1);
SELECT checkScanMatch('{"$and": [{"$or": [{"a": {"$regularExpression":{"pattern":"a","options":""}}}]}]}', 0, 0);
SELECT checkScanMatch('{"$or": [{"a": {"$not": {"$regularExpression":{"pattern":"o","options":""}}}}]}', 2, 1);
SELECT checkScanMatch('{"$and": [{"$or": [{"a": {"$not": {"$regularExpression":{"pattern":"o","options":""}}}}]}]}', 2, 1);
SELECT checkScanMatch('{"$and": [{"$or": [{"a": {"$not": {"$regularExpression":{"pattern":"a","options":""}}}}]}]}', 2, 2);
SELECT checkScanMatch('{"$and": [{"$nor": [{"a": {"$regularExpression":{"pattern":"o","options":""}}}]}]}', 2, 1);
SELECT checkScanMatch('{"$and": [{"$nor": [{"a": {"$regularExpression":{"pattern":"a","options":""}}}]}]}', 2, 2);

-- $where is not yet supported
SELECT checkScanMatch('{"$where": "this.a==1"}', 2, 1);
SELECT checkScanMatch('{"$and": [{"$where": "this.a==1"}]}', 2, 1);

SELECT checkScanMatch('{"a": 1, "$where": "this.a==1"}', 1, 1);
SELECT checkScanMatch('{"a": 1, "$and": [{"$where": "this.a==1"}]}', 1, 1);
SELECT checkScanMatch('{"$and": [{"a": 1}, {"$where": "this.a==1"}]}', 1, 1);
SELECT checkScanMatch('{"$and": [{"a": 1, "$where": "this.a==1"}]}', 1, 1);
SELECT checkScanMatch('{"a": 1, "$and": [{"a": 1}, {"a": 1, "$where": "this.a==1"}]}', 1, 1);

-- these are supported
SELECT checkScanMatch('{"a": 1, "$and": [{"a": 2}]}', NULL, 0);
SELECT checkScanMatch('{"$and": [{"a": 1}, {"a": 2}]}', NULL, 0);

SELECT drop_collection('db','and3');
DROP SCHEMA and3 CASCADE;
