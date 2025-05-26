CREATE SCHEMA regex5;
SET search_path TO documentdb_core,documentdb_api,documentdb_api_catalog,documentdb_api_internal,public,regex5;

SET citus.next_shard_id TO 1010000;
SET documentdb.next_collection_id TO 101000;
SET documentdb.next_collection_index_id TO 101000;

SELECT insert_one('db','regex5', '{"x": "ayc"}');
SELECT insert_one('db','regex5', '{"x": ["abc", "xyz1"]}');
SELECT insert_one('db','regex5', '{"x": ["acd", "xyz23"]}');
SELECT insert_one('db','regex5', '{"F1" : "F1_value",  "x": ["first regular expression", "second expression", "third value for x"]}');
SELECT insert_one('db','regex5', '{"F1" : "F1_value2"}');

-- DROP PRIMARY KEY
SELECT documentdb_distributed_test_helpers.drop_primary_key('db', 'regex5');

\set prevEcho :ECHO
\set ECHO none
\o /dev/null
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', documentdb_distributed_test_helpers.generate_create_index_arg('regex5', 'index_wc', '{"$**": 1}'), true);
\o
\set ECHO :prevEcho

--
-- FORCING INDEX PATH
--

BEGIN;
set local enable_seqscan TO OFF;
set local documentdb.forceRumIndexScantoBitmapHeapScan TO OFF;

-- When x is non-array
EXPLAIN (COSTS OFF) SELECT document FROM collection('db', 'regex5') WHERE document @@ '{"x": {"$in": [{"$regex" : ".*Yc", "$options": "i"}]}}';

-- When x's value is array and regex matches one of the array elements, specifically the first element (3rd record).
EXPLAIN (COSTS OFF) SELECT document FROM collection('db', 'regex5') WHERE document @@ '{"x": {"$in": [{"$regex" : "^.*cd", "$options": ""}]}}';

-- When x'z value is array and regex matches second element of 2nd record and 3rd element in the 4th record.
EXPLAIN (COSTS OFF) SELECT document FROM collection('db', 'regex5') WHERE document @@ '{"x": {"$in": [{"$regex" : "x.+1", "$options": ""}, {"$regex" : "value .+ x", "$options": ""}]}}';

-- Without any regex
EXPLAIN (COSTS OFF) SELECT document FROM collection('db', 'regex5') WHERE document @@ '{"x": {"$in": ["acd", "first regular expression"]}}';

-- Mix of Regex and text
EXPLAIN (COSTS OFF) SELECT document FROM collection('db', 'regex5') WHERE document @@ '{"x": {"$in": [  "second expression", {"$regex" : "xy.1", "$options": ""}  ]  }}';

-- Test for hasNull (10 filler records and 3 actual records to match)
EXPLAIN (COSTS OFF) SELECT document FROM collection('db', 'regex5') WHERE document @@ '{"x": {"$in": [  "second expression", null, {"$regex" : "xy.1", "$options": ""}  ]  }}';

-- Test for $all
EXPLAIN (COSTS OFF) SELECT document FROM collection('db','regex5') WHERE document @@ '{"x": {"$all": [{"$regex" : "expression", "$options": ""}, {"$regex" : "value .+ x", "$options": ""}]}}';

ROLLBACK;

--
-- RUN TIME PATH
--

-- When x is non-array
EXPLAIN (COSTS OFF) SELECT document FROM collection('db', 'regex5') WHERE document @@ '{"x": {"$in": [{"$regex" : ".*yc", "$options": ""}]}}';

-- When x's value is array and regex matches one of the array elements, specifically the first element (3rd record).
EXPLAIN (COSTS OFF) SELECT document FROM collection('db', 'regex5') WHERE document @@ '{"x": {"$in": [{"$regex" : "^.*cd", "$options": ""}]}}';

-- When x'z value is array and regex matches second element of 2nd record and 3rd element in the 4th record.
EXPLAIN (COSTS OFF) SELECT document FROM collection('db', 'regex5') WHERE document @@ '{"x": {"$in": [{"$regex" : "x.+1", "$options": ""}, {"$regex" : "value .+ x", "$options": ""}]}}';

-- Without any regex
EXPLAIN (COSTS OFF) SELECT document FROM collection('db', 'regex5') WHERE document @@ '{"x": {"$in": ["acd", "first regular expression"]}}';

-- Mix of Regex and text
EXPLAIN (COSTS OFF) SELECT document FROM collection('db', 'regex5') WHERE document @@ '{"x": {"$in": [  "second expression", {"$regex" : "xy.1", "$options": ""}  ]  }}';

-- Test for hasNull (10 filler records and 3 actual records to match)
EXPLAIN (COSTS OFF) SELECT document FROM collection('db', 'regex5') WHERE document @@ '{"x": {"$in": [  "second expression", null, {"$regex" : "xy.1", "$options": ""}  ]  }}';

-- Test for $all
EXPLAIN (COSTS OFF) SELECT document FROM collection('db','regex5') WHERE document @@ '{"x": {"$all": [{"$regex" : "expression", "$options": ""}, {"$regex" : "value .+ x", "$options": ""}]}}';

SELECT drop_collection('db','regex5');
DROP SCHEMA regex5 CASCADE;
