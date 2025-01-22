SET search_path TO documentdb_core,documentdb_api,documentdb_api_catalog,documentdb_api_internal;


SET citus.next_shard_id TO 335000;
SET documentdb.next_collection_id TO 3350;
SET documentdb.next_collection_index_id TO 3350;

SELECT create_collection('db','regex');

SELECT insert_one('db','regex', '{"_id" : 99, "sku": "abc123"}');
SELECT insert_one('db','regex', '{"_id" : 100, "sku" : "abc123", "description" : "Single line description." }');
SELECT insert_one('db','regex', '{"_id" : 101, "sku" : "abc789", "description" : "single line\nSecond line" }');
SELECT insert_one('db','regex', '{"_id" : 102, "sku" : "xyz456", "description" : "Many spaces before     line" }');
SELECT insert_one('db','regex', '{"_id" : 103, "sku" : "xyz789", "description" : "Multiple\nline description" }');
SELECT insert_one('db','regex', '{"_id" : 104, "sku" : "xyz790", "description" : "Multiple\n incline description" }');
SELECT insert_one('db','regex', '{"_id" : 105, "sku" : "xyz800", "description" : "Multiple\n in\bcline \bdescription" }');
SELECT insert_one('db','regex', '{"_id" : 106, "a" : "hello a\bcde world" }');
SELECT insert_one('db','regex', '{"_id" : 107, "a" : {"$regularExpression" : {"pattern" : "a\bcde\b", "options" : ""}}}');
SELECT insert_one('db','regex', '{"_id" : 108, "a" : {"$regularExpression" : {"pattern" : "hello", "options" : ""}} }');
SELECT insert_one('db','regex', '{"_id" : 109, "sku" : "xyz810", "description" : "Multiple\n in\\ycline \bdescription"}');
SELECT insert_one('db','regex', '{"_id" : 110, "a" : {"$regularExpression" : {"pattern" : "a\\ycde\\y", "options" : ""}}}');
SELECT insert_one('db','regex', '{"_id" : 111, "a" : "a value", "b": "b value"}');
SELECT insert_one('db','regex', '{"_id" : 112, "a" : "v2", "b": "bv2"}');
SELECT insert_one('db','regex', '{"_id" : 113, "a" : "v3", "b": "bv3"}');
SELECT insert_one('db','regex', '{"_id" : 114, "a" : "v4", "b": "bv4"}');
SELECT insert_one('db','regex', '{"_id" : 115, "a" : "v5", "b": "bv5"}');
SELECT insert_one('db','regex', '{"_id" : 116, "a" : "v6", "b": "bv6"}');
SELECT insert_one('db','regex', '{"_id" : 117, "a" : "a value7", "b": "b value7"}');

-- Some unicodes & mixed numerical
SELECT insert_one('db','regex', '{"_id" : 120, "number" : "༢༣༤༥"}');
SELECT insert_one('db','regex', '{"_id" : 121, "number" : "02191996"}');
SELECT insert_one('db','regex', '{"_id" : 122, "number" : "9୩୪୬୯6"}');
-- Some unicode text
SELECT insert_one('db','regex', '{"_id" : 130, "text" : "kyle"}');
SELECT insert_one('db','regex', '{"_id" : 131, "text" : "박정수"}');
SELECT insert_one('db','regex', '{"_id" : 132, "text" : "suárez"}');

SELECT document from collection('db', 'regex') where document @@ '{"sku": {"$regex": true} }';

SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "regex", "indexes": [{"key": {"sku": 1}, "name": "index_on_sku"}]}', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "regex", "indexes": [{"key": {"x": 1},   "name": "index_on_x"  }]}', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "regex", "indexes": [{"key": {"F1": 1},  "name": "index_on_F1"  }]}', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "regex", "indexes": [{"key": {"a": 1, "b": 1},  "name": "Compound_index_on_a_and_b"  }]}', true);

-- avoid plans that use the primary key index
SELECT documentdb_distributed_test_helpers.drop_primary_key('db','regex');

SELECT document from collection('db', 'regex') where document @@ '{"sku": {"$regex": true} }';

-- SELECT sku
SELECT count(*) sku from collection('db', 'regex') where document @@ '{"sku": {"$regex": "", "$options": ""} }';

-- 100 to 103
SELECT document from collection('db', 'regex') where document @@ '{ "description": { "$regex": "\\bline\\b", "$options" : "" } }';


-- No records. Will try to match \\y (as a char) in the document
SELECT document from collection('db', 'regex') where document @@ '{ "description": { "$regex": "\\\\yline\\\\y", "$options" : "" } }';

-- 109 Match \y as a char. For libbson \y is a special char. So to \y it needs to be escaped as \\y and for \\y, we need to provide as \\\\y
SELECT document from collection('db', 'regex') where document @@ '{ "description": { "$regex": "in\\\\ycline", "$options" : "" } }';

-- 107 Match a where a's value was inserted as a Regular expression object. Here \b will not be used as a flag. This entire regular expression object (/a\bcde\b/) will be compared (as such) against the regular expression object that was inserted. This is like regex is compared with another regex.
SELECT document from collection('db', 'regex') where document @@ '{"a" : { "$regularExpression" : { "pattern" : "a\bcde\b", "options" : "" } }}';

-- 110 (Same as 107 match above.)
--SELECT document from collection('db', 'regex') where document @@ '{"a" : { "$regularExpression" : { "pattern": "a\\ycde\\y", "options" : ""} } }';

-- 100 and 101
SELECT document from collection('db', 'regex') where document @@ '{ "description": { "$regex": "le.*\\bline\\b", "$options" : "" } }';

-- 105 Matching \b as a normal char inside the string.
SELECT document from collection('db', 'regex') where document @@ '{ "description": { "$regex": "in\bcline \bdescription", "$options" : "" } }';

-- 100
SELECT document from collection('db', 'regex') where document @@ '{"description": {  "$options": "i",   "$regex": " line " } }';

-- 100
SELECT document from collection('db', 'regex') where document @@ '{"description": {"$regex": " line ","$options": "i"}}';

-- 106 108
SELECT document from collection('db', 'regex') where document @@ '{ "a" : { "$regularExpression" : { "pattern" : "hello", "options" : "" } } } ';

-- 120
SELECT document from collection('db', 'regex') where document @@ '{ "number" : { "$regularExpression" : { "pattern" : "༣", "options" : "" } } } ';

-- 121 122 if ascii digits are present
SELECT document from collection('db', 'regex') where document @@ '{ "number" : { "$regularExpression" : { "pattern" : "[[:digit:]]", "options" : "" } } } ';

-- 131
SELECT document from collection('db', 'regex') where document @@ '{ "text" : { "$regularExpression" : { "pattern" : "(*UTF)정", "options" : "" } } } ';

-- 131
SELECT document from collection('db', 'regex') where document @@ '{ "text" : { "$regularExpression" : { "pattern" : "\\p{Hangul}", "options" : "" } } } ';

-- 130 131 132
SELECT document from collection('db', 'regex') where document @@ '{ "text" : { "$regularExpression" : { "pattern" : "^\\p{Xan}+$", "options" : "" } } } ';

-- 130 132
SELECT document from collection('db', 'regex') where document @@ '{ "text" : { "$regularExpression" : { "pattern" : "^\\p{Latin}", "options" : "" } } } ';
 
-- 111 117   Multiple regex in a single query. Ensuring multiple regexes, in the same query, are cached as separate entry in the cache.
SELECT document from collection('db', 'regex') where document @@ '{ "a": {"$regex": "a.vaLue", "$options": "i"}, "b": {"$regex": "b va.ue", "$options": ""}}';

BEGIN;
/* Make use of Index */
SET LOCAL enable_seqscan to OFF;
SET LOCAL documentdb.forceRumIndexScantoBitmapHeapScan TO OFF;
-- 111 117   Multiple regex in a single query. Ensuring multiple regexes, in the same query, are cached as separate entry in the cache
SELECT document from collection('db', 'regex') where document @@ '{ "a": {"$regex": "a.vaLue", "$options": "i"}, "b": {"$regex": "b va.ue", "$options": ""}}';
EXPLAIN (COSTS OFF) SELECT document from collection('db', 'regex') where document @@ '{ "a": {"$regex": "a.vaLue", "$options": "i"}, "b": {"$regex": "b va.ue", "$options": ""}}';
ROLLBACK;

-- shard the collection by "_id"
select documentdb_api.shard_collection('db', 'regex', '{"_id": "hashed"}', false);

BEGIN;
SET LOCAL enable_seqscan to OFF;
SET LOCAL documentdb.forceRumIndexScantoBitmapHeapScan TO OFF;
-- 111 117   Multiple regex in a single query on sharded collection. Index Path. Ensuring multiple regexes, in the same query, are cached as separate entry in the cache
SELECT document from collection('db', 'regex') where document @@ '{ "a": {"$regex": "a.vaLue", "$options": "i"}, "b": {"$regex": "b va.ue", "$options": ""}}';
EXPLAIN (COSTS OFF) SELECT document from collection('db', 'regex') where document @@ '{ "a": {"$regex": "a.vaLue", "$options": "i"}, "b": {"$regex": "b va.ue", "$options": ""}}';
ROLLBACK;

BEGIN;
SET LOCAL enable_seqscan to ON;
-- 111 117   Multiple regex in a single query on sharded collection. Seq Scan Path. Ensuring multiple regexes, in the same query, are cached as separate entry in the cache
SELECT document from collection('db', 'regex') where document @@ '{ "a": {"$regex": "a.vaLue", "$options": "i"}, "b": {"$regex": "b va.ue", "$options": ""}}';
EXPLAIN (COSTS OFF) SELECT document from collection('db', 'regex') where document @@ '{ "a": {"$regex": "a.vaLue", "$options": "i"}, "b": {"$regex": "b va.ue", "$options": ""}}';
ROLLBACK;

SELECT drop_collection('db','regex') IS NOT NULL;

SELECT create_collection('db','regex');

SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "regex", "indexes": [{"key": {"a": 1}, "name": "index_on_a"}]}', true);

-- shard the collection by "a"
select documentdb_api.shard_collection('db', 'regex', '{"a": "hashed"}', false);


SELECT insert_one('db','regex', '{"_id" : 106, "a" : "hello a\bcde world" }');
SELECT insert_one('db','regex', '{"_id" : 108, "a" : {"$regularExpression" : {"pattern" : "hello", "options" : ""}} }');
SELECT insert_one('db','regex', '{"_id" : 111, "a" : "a value", "b": "b value"}');
SELECT insert_one('db','regex', '{"_id" : 116, "a" : "v6", "b": "bv6"}');
SELECT insert_one('db','regex', '{"_id" : 117, "a" : "a value7", "b": "b value7"}');

BEGIN;
SET LOCAL enable_seqscan to OFF;
-- 111 117   Multiple regex in a single query on sharded collection, where query is on the shard id column. Index Path
SELECT document from collection('db', 'regex') where document @@ '{ "a": {"$regex": "a.vaLue", "$options": "i"}, "b": {"$regex": "b va.ue", "$options": ""}}';
EXPLAIN (COSTS OFF) SELECT document from collection('db', 'regex') where document @@ '{ "a": {"$regex": "a.vaLue", "$options": "i"}, "b": {"$regex": "b va.ue", "$options": ""}}';
ROLLBACK;

BEGIN;
SET LOCAL enable_seqscan to ON;
SET LOCAL documentdb.ForceUseIndexIfAvailable to OFF;
-- 111 117   Multiple regex in a single query on sharded collection, where query is on the shard id column. Seq Scan Path
SELECT document from collection('db', 'regex') where document @@ '{ "a": {"$regex": "a.vaLue", "$options": "i"}, "b": {"$regex": "b va.ue", "$options": ""}}';
EXPLAIN (COSTS OFF) SELECT document from collection('db', 'regex') where document @@ '{ "a": {"$regex": "a.vaLue", "$options": "i"}, "b": {"$regex": "b va.ue", "$options": ""}}';
ROLLBACK;

SELECT drop_collection('db','regex') IS NOT NULL;