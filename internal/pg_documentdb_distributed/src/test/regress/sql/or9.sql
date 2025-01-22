-- Based on or9.js
CREATE SCHEMA or9;
SET search_path TO documentdb_core,documentdb_api,documentdb_api_catalog,documentdb_api_internal,public,or9;
SET citus.next_shard_id TO 10000;
SET documentdb.next_collection_id TO 1000;
SET documentdb.next_collection_index_id TO 1000;

SELECT 1 FROM drop_collection('db','or9');
SELECT 1 FROM insert_one('db','or9', '{"a":2,"b":2}');

SELECT documentdb_distributed_test_helpers.drop_primary_key('db', 'or9');

-- Since we cannot define compound wildcard indexes, let's define two
-- different wildcard indexes here.
--
-- Indeed, our current implementation always creates a wildcard index
-- whether it's compound or not, but let's be convenient with vanilla mongo.
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', documentdb_distributed_test_helpers.generate_create_index_arg('or9', 'index_1', '{"a.$**": 1}'), true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', documentdb_distributed_test_helpers.generate_create_index_arg('or9', 'index_2', '{"b.$**": 1}'), true);

CREATE OR REPLACE FUNCTION or9.assert_count(expected_row_count int, query bson)
 RETURNS void
 LANGUAGE plpgsql
AS $$
DECLARE
	returned_row_count int;
BEGIN
	SELECT count(*) INTO returned_row_count
	FROM collection('db','or9') WHERE document @@ query;

	IF returned_row_count <> expected_row_count THEN
		RAISE 'query % returned % rows instead of %', query, returned_row_count, expected_row_count;
	END IF;
END;
$$;

BEGIN;
set local enable_seqscan TO OFF;

-- "SERVER-12594": there are two clauses in this case, because we do
-- not yet collapse OR of ANDs to a single ixscan.
SELECT assert_count(1, '{"$or": [{"a": {"$gte": 1, "$lte": 3}}, {"a": 2}]}');

SELECT assert_count(1, '{"$or": [{"a": {"$gt": 2, "$lte": 3}}, {"a": 2}]}');

SELECT assert_count(1, '{"$or": [{"b": {"$gte": 1, "$lte": 3}}, {"b": 2}]}');
SELECT assert_count(1, '{"$or": [{"b": {"$gte": 2, "$lte": 3}}, {"b": 2}]}');
SELECT assert_count(1, '{"$or": [{"b": {"$gt": 2, "$lte": 3}}, {"b": 2}]}');

-- "SERVER-12594": there are two clauses in this case, because we do
-- not yet collapse OR of ANDs to a single ixscan.
SELECT assert_count(1, '{"$or": [{"a": {"$gte": 1, "$lte": 3}}, {"a": 2, "b": 2}]}');

SELECT assert_count(1, '{"$or": [{"a": {"$gte": 1, "$lte": 3}, "b": 3}, {"a": 2}]}');

SELECT assert_count(1, '{"$or": [{"b": {"$gte": 1, "$lte": 3}}, {"b": 2, "a": 2}]}');

SELECT assert_count(1, '{"$or": [{"b": {"$gte": 1, "$lte": 3}, "a": 3}, {"b": 2}]}');

SELECT assert_count(1, '{"$or": [{"a": {"$gte": 1, "$lte": 3}, "b": 3}, {"a": 2, "b": 2}]}');
SELECT assert_count(1, '{"$or": [{"a": {"$gte": 2, "$lte": 3}, "b": 3}, {"a": 2, "b": 2}]}');
-- "SERVER-12594": there are two clauses in this case, because we do
-- not yet collapse OR of ANDs to a single ixscan.
SELECT assert_count(1, '{"$or": [{"a": {"$gte": 1, "$lte": 3}, "b": 2}, {"a": 2, "b": 2}]}');

SELECT assert_count(1, '{"$or": [{"b": {"$gte": 1, "$lte": 3}, "a": 3}, {"a": 2, "b": 2}]}');
SELECT assert_count(1, '{"$or": [{"b": {"$gte": 2, "$lte": 3}, "a": 3}, {"a": 2, "b": 2}]}');
-- "SERVER-12594": there are two clauses in this case, because we do
-- not yet collapse OR of ANDs to a single ixscan.
SELECT assert_count(1, '{"$or": [{"b": {"$gte": 1, "$lte": 3}, "a": 2}, {"a": 2, "b": 2}]}');

ROLLBACK;

DO $$
DECLARE
  v_collection_id bigint;
BEGIN
	SELECT collection_id INTO v_collection_id FROM documentdb_api_catalog.collections
  		WHERE database_name = 'db' AND collection_name = 'or9';
	EXECUTE format('TRUNCATE documentdb_data.documents_%s', v_collection_id);
END
$$;

SELECT 1 FROM insert_one('db','or9', '{"a": 1, "b": 5}');
SELECT 1 FROM insert_one('db','or9', '{"a": 5, "b": 1}');

BEGIN;
set local enable_seqscan to OFF;

-- "SERVER-12594": there are two clauses in the case below, because we do
-- not yet collapse OR of ANDs to a single ixscan.
SELECT assert_count(2, '{"$or": [{"a": {"$in": [1, 5]}, "b": {"$in": [1, 5]}}, {"a": {"$in": [1, 5]}, "b": {"$in": [1, 5]}}]}');

SELECT assert_count(2, '{"$or": [{"a": {"$in": [1]}, "b": {"$in": [1, 5]}}, {"a": {"$in": [1, 5]}, "b": {"$in": [1, 5]}}]}');
SELECT assert_count(2, '{"$or": [{"a": {"$in": [1]}, "b": {"$in": [1]}}, {"a": {"$in": [1, 5]}, "b": {"$in": [1, 5]}}]}');

ROLLBACK;