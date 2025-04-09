
SET search_path TO documentdb_core,documentdb_api,documentdb_api_catalog,documentdb_api_internal;

SET citus.next_shard_id TO 71000;
SET documentdb.next_collection_id TO 7100;
SET documentdb.next_collection_index_id TO 7100;

-- Insert an a.b, a.c matrix
SELECT documentdb_api.insert_one('db','bsonorderaggregates', '{"_id": 1, "a" : { "b" : 1, "c" : 1 } }', NULL);
SELECT documentdb_api.insert_one('db','bsonorderaggregates', '{"_id": 2, "a" : { "b" : 1, "c" : 2 } }', NULL);
SELECT documentdb_api.insert_one('db','bsonorderaggregates', '{"_id": 3, "a" : { "b" : 1, "c" : 3 } }', NULL);
SELECT documentdb_api.insert_one('db','bsonorderaggregates', '{"_id": 4, "a" : { "b" : 2, "c" : 1 } }', NULL);
SELECT documentdb_api.insert_one('db','bsonorderaggregates', '{"_id": 5, "a" : { "b" : 2, "c" : 2 } }', NULL);
SELECT documentdb_api.insert_one('db','bsonorderaggregates', '{"_id": 6, "a" : { "b" : 2, "c" : 3 } }', NULL);
SELECT documentdb_api.insert_one('db','bsonorderaggregates', '{"_id": 7, "a" : { "b" : 3, "c" : 1 } }', NULL);
SELECT documentdb_api.insert_one('db','bsonorderaggregates', '{"_id": 8, "a" : { "b" : 3, "c" : 2 } }', NULL);
SELECT documentdb_api.insert_one('db','bsonorderaggregates', '{"_id": 9, "a" : { "b" : 3, "c" : 3 } }', NULL);


SELECT BSONLAST(document, ARRAY['{ "a.b":1}'::bson, '{"a.c":1}'::bson])
FROM documentdb_api.collection('db', 'bsonorderaggregates') 
GROUP BY bson_expression_get(document, '{ "": "$a.c" }');

SELECT BSONLAST(document, ARRAY['{ "a.b":1}', '{"a.c":-1}']::bson[]) 
FROM documentdb_api.collection('db', 'bsonorderaggregates') 
GROUP BY bson_expression_get(document, '{ "": "$a.c" }');

SELECT BSONLAST(document, ARRAY['{ "a.b":-1}', '{"a.c":1}']::bson[]) 
FROM documentdb_api.collection('db', 'bsonorderaggregates') 
GROUP BY bson_expression_get(document, '{ "": "$a.c" }');

SELECT BSONLAST(document, ARRAY['{ "a.b":-1}', '{"a.c":-1}']::bson[]) 
FROM documentdb_api.collection('db', 'bsonorderaggregates') 
GROUP BY bson_expression_get(document, '{ "": "$a.c" }');

SELECT BSONFIRST(document, ARRAY['{ "a.b":1}', '{"a.c":1}']::bson[]) 
FROM documentdb_api.collection('db', 'bsonorderaggregates') 
GROUP BY bson_expression_get(document, '{ "": "$a.b" }');

SELECT BSONFIRST(document, ARRAY['{ "a.b":1}', '{"a.c":-1}']::bson[]) 
FROM documentdb_api.collection('db', 'bsonorderaggregates') 
GROUP BY bson_expression_get(document, '{ "": "$a.b" }');

SELECT BSONFIRST(document, ARRAY['{ "a.b":-1}', '{"a.c":1}']::bson[])
FROM documentdb_api.collection('db', 'bsonorderaggregates') 
GROUP BY bson_expression_get(document, '{ "": "$a.b" }');

SELECT BSONFIRST(document, ARRAY['{ "a.b":-1}', '{"a.c":-1}']::bson[])
FROM documentdb_api.collection('db', 'bsonorderaggregates') 
GROUP BY bson_expression_get(document, '{ "": "$a.b" }');

SELECT BSONLASTONSORTED(document) 
FROM documentdb_api.collection('db', 'bsonorderaggregates') 
GROUP BY bson_expression_get(document, '{ "": "$a.c" }');

SELECT BSONFIRSTONSORTED(document) 
FROM documentdb_api.collection('db', 'bsonorderaggregates') 
GROUP BY bson_expression_get(document, '{ "": "$a.b" }');

SELECT BSONLASTONSORTED(document) 
FROM documentdb_api.collection('db', 'bsonorderaggregates') 
GROUP BY bson_expression_get(document, '{ "": "$a.b" }');

SELECT BSONFIRSTONSORTED(document) 
FROM documentdb_api.collection('db', 'bsonorderaggregates') 
GROUP BY bson_expression_get(document, '{ "": "$a.b" }');

-- last/first field not present

SELECT bson_repath_and_build('firstonsorted'::text, BSONFIRSTONSORTED(bson_expression_get(document, '{ "": "$a.f" }')))
FROM documentdb_api.collection('db', 'bsonorderaggregates') 
GROUP BY bson_expression_get(document, '{ "": "$a.c" }');

SELECT bson_repath_and_build('last'::text, BSONLAST(bson_expression_get(document, '{ "": "$a.f" }'),  ARRAY['{ "a.b":-1}', '{"a.c":-1}']::bson[]))
FROM documentdb_api.collection('db', 'bsonorderaggregates') 
GROUP BY bson_expression_get(document, '{ "": "$a.c" }');

-- Test the different order combinations
SELECT BSONFIRST(document, ARRAY['{ "a.b":1}', '{"a.c":1}']::bson[]) FROM documentdb_api.collection('db', 'bsonorderaggregates');
SELECT BSONFIRST(document, ARRAY['{ "a.b":1}', '{"a.c":-1}']::bson[]) FROM documentdb_api.collection('db', 'bsonorderaggregates');
SELECT BSONFIRST(document, ARRAY['{ "a.b":-1}', '{"a.c":1}']::bson[]) FROM documentdb_api.collection('db', 'bsonorderaggregates');
SELECT BSONFIRST(document, ARRAY['{ "a.b":-1}', '{"a.c":-1}']::bson[]) FROM documentdb_api.collection('db', 'bsonorderaggregates');

SELECT BSONLAST(document, ARRAY['{ "a.b":1}', '{"a.c":1}']::bson[]) FROM documentdb_api.collection('db', 'bsonorderaggregates');
SELECT BSONLAST(document, ARRAY['{ "a.b":1}', '{"a.c":-1}']::bson[]) FROM documentdb_api.collection('db', 'bsonorderaggregates');
SELECT BSONLAST(document, ARRAY['{ "a.b":-1}', '{"a.c":1}']::bson[]) FROM documentdb_api.collection('db', 'bsonorderaggregates');
SELECT BSONLAST(document, ARRAY['{ "a.b":-1}', '{"a.c":-1}']::bson[]) FROM documentdb_api.collection('db', 'bsonorderaggregates');

-- Test with null results from the sort spec
SELECT BSONFIRST(document, ARRAY['{ "f":1}', '{ "a.b":1}', '{"a.c":1}']::bson[]) FROM documentdb_api.collection('db', 'bsonorderaggregates');
SELECT BSONFIRST(document, ARRAY['{ "a.b":1}', '{ "f":1}', '{"a.c":1}']::bson[]) FROM documentdb_api.collection('db', 'bsonorderaggregates');
SELECT BSONFIRST(document, ARRAY['{ "a.b":1}', '{"a.c":1}', '{ "f":1}']::bson[]) FROM documentdb_api.collection('db', 'bsonorderaggregates');

SELECT BSONLAST(document, ARRAY['{ "f":1}', '{ "a.b":1}', '{"a.c":1}']::bson[]) FROM documentdb_api.collection('db', 'bsonorderaggregates');
SELECT BSONLAST(document, ARRAY['{ "a.b":1}', '{ "f":1}', '{"a.c":1}']::bson[]) FROM documentdb_api.collection('db', 'bsonorderaggregates');
SELECT BSONLAST(document, ARRAY['{ "a.b":1}', '{"a.c":1}', '{ "f":1}']::bson[]) FROM documentdb_api.collection('db', 'bsonorderaggregates');

SELECT BSONFIRST(document, ARRAY['{ "f":1}', '{ "a.b":1}', '{"a.c":1}']::bson[]) FROM documentdb_api.collection('db', 'bsonorderaggregates');
SELECT BSONFIRST(document, ARRAY['{ "a.b":1}', '{ "f":1}', '{"a.c":1}']::bson[]) FROM documentdb_api.collection('db', 'bsonorderaggregates');
SELECT BSONFIRST(document, ARRAY['{ "a.b":1}', '{"a.c":1}', '{ "f":1}']::bson[]) FROM documentdb_api.collection('db', 'bsonorderaggregates');

SELECT BSONLAST(document, ARRAY['{ "f":1}', '{ "a.b":1}', '{"a.c":1}']::bson[]) FROM documentdb_api.collection('db', 'bsonorderaggregates');
SELECT BSONLAST(document, ARRAY['{ "a.b":1}', '{ "f":1}', '{"a.c":1}']::bson[]) FROM documentdb_api.collection('db', 'bsonorderaggregates');
SELECT BSONLAST(document, ARRAY['{ "a.b":1}', '{"a.c":1}', '{ "f":1}']::bson[]) FROM documentdb_api.collection('db', 'bsonorderaggregates');

-- Test null ordering 
SELECT documentdb_api.insert_one('db','bsonorderaggregates', '{"_id": 11, "a" : { "c" : 3 } }', NULL);

SELECT BSONFIRST(document, ARRAY['{ "a.b":1}', '{"a.c":1}']::bson[]) FROM documentdb_api.collection('db', 'bsonorderaggregates');
SELECT BSONFIRST(document, ARRAY['{ "a.b":1}', '{"a.c":-1}']::bson[]) FROM documentdb_api.collection('db', 'bsonorderaggregates');
SELECT BSONFIRST(document, ARRAY['{ "a.b":-1}', '{"a.c":1}']::bson[]) FROM documentdb_api.collection('db', 'bsonorderaggregates');
SELECT BSONFIRST(document, ARRAY['{ "a.b":-1}', '{"a.c":-1}']::bson[]) FROM documentdb_api.collection('db', 'bsonorderaggregates');

SELECT BSONFIRST(document, ARRAY['{ "a.c":1}', '{"a.b":1}']::bson[]) FROM documentdb_api.collection('db', 'bsonorderaggregates');
SELECT BSONFIRST(document, ARRAY['{ "a.c":1}', '{"a.b":-1}']::bson[]) FROM documentdb_api.collection('db', 'bsonorderaggregates');
SELECT BSONFIRST(document, ARRAY['{ "a.c":-1}', '{"a.b":1}']::bson[]) FROM documentdb_api.collection('db', 'bsonorderaggregates');
SELECT BSONFIRST(document, ARRAY['{ "a.c":-1}', '{"a.b":-1}']::bson[]) FROM documentdb_api.collection('db', 'bsonorderaggregates');

SELECT BSONLAST(document, ARRAY['{ "a.b":1}', '{"a.c":1}']::bson[]) FROM documentdb_api.collection('db', 'bsonorderaggregates');
SELECT BSONLAST(document, ARRAY['{ "a.b":1}', '{"a.c":-1}']::bson[]) FROM documentdb_api.collection('db', 'bsonorderaggregates');
SELECT BSONLAST(document, ARRAY['{ "a.b":-1}', '{"a.c":1}']::bson[]) FROM documentdb_api.collection('db', 'bsonorderaggregates');
SELECT BSONLAST(document, ARRAY['{ "a.b":-1}', '{"a.c":-1}']::bson[]) FROM documentdb_api.collection('db', 'bsonorderaggregates');

SELECT BSONLAST(document, ARRAY['{ "a.c":1}', '{"a.b":1}']::bson[]) FROM documentdb_api.collection('db', 'bsonorderaggregates');
SELECT BSONLAST(document, ARRAY['{ "a.c":1}', '{"a.b":-1}']::bson[]) FROM documentdb_api.collection('db', 'bsonorderaggregates');
SELECT BSONLAST(document, ARRAY['{ "a.c":-1}', '{"a.b":1}']::bson[]) FROM documentdb_api.collection('db', 'bsonorderaggregates');
SELECT BSONLAST(document, ARRAY['{ "a.c":-1}', '{"a.b":-1}']::bson[]) FROM documentdb_api.collection('db', 'bsonorderaggregates');


BEGIN;
SET LOCAL enable_seqscan TO OFF;
SELECT documentdb_distributed_test_helpers.mask_plan_id_from_distributed_subplan($Q$
EXPLAIN(costs off)  
SELECT BSONLAST(document, ARRAY['{"a.b":1}']::bson[]) FROM documentdb_api.collection('db', 'bsonorderaggregates');
$Q$);
END;

SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "bsonorderaggregates", "indexes": [{"key": {"a.b": 1}, "name": "a_dot_b"}]}', true);

BEGIN;
SET LOCAL enable_seqscan TO OFF;
SELECT documentdb_distributed_test_helpers.mask_plan_id_from_distributed_subplan($Q$
EXPLAIN(costs off)  
SELECT BSONLAST(document, ARRAY['{"a.b":-1}', '{"a.b":-1}']::bson[]) FROM documentdb_api.collection('db', 'bsonorderaggregates');
$Q$);
END;

-- Shard the collection
SELECT documentdb_api.shard_collection('db','bsonorderaggregates', '{"_id":"hashed"}', false);

SELECT BSONLAST(document, ARRAY['{ "a.b":1}'::bson,'{"a.c":1}'::bson])
FROM documentdb_api.collection('db', 'bsonorderaggregates') 
GROUP BY bson_expression_get(document, '{ "": "$a.c" }');

SELECT BSONLAST(document, ARRAY['{ "a.b":1}', '{"a.c":-1}']::bson[]) 
FROM documentdb_api.collection('db', 'bsonorderaggregates') 
GROUP BY bson_expression_get(document, '{ "": "$a.c" }');

SELECT BSONLAST(document, ARRAY['{ "a.b":-1}', '{"a.c":1}']::bson[]) 
FROM documentdb_api.collection('db', 'bsonorderaggregates') 
GROUP BY bson_expression_get(document, '{ "": "$a.c" }');

SELECT BSONLAST(document, ARRAY['{ "a.b":-1}', '{"a.c":-1}']::bson[]) 
FROM documentdb_api.collection('db', 'bsonorderaggregates') 
GROUP BY bson_expression_get(document, '{ "": "$a.c" }');

SELECT BSONFIRST(document, ARRAY['{ "a.b":1}', '{"a.c":1}']::bson[]) 
FROM documentdb_api.collection('db', 'bsonorderaggregates') 
GROUP BY bson_expression_get(document, '{ "": "$a.b" }');

SELECT BSONFIRST(document, ARRAY['{ "a.b":1}', '{"a.c":-1}']::bson[]) 
FROM documentdb_api.collection('db', 'bsonorderaggregates') 
GROUP BY bson_expression_get(document, '{ "": "$a.b" }');

SELECT BSONFIRST(document, ARRAY['{ "a.b":-1}', '{"a.c":1}']::bson[]) 
FROM documentdb_api.collection('db', 'bsonorderaggregates') 
GROUP BY bson_expression_get(document, '{ "": "$a.b" }');

SELECT BSONFIRST(document, ARRAY['{ "a.b":-1}', '{"a.c":-1}']::bson[]) 
FROM documentdb_api.collection('db', 'bsonorderaggregates') 
GROUP BY bson_expression_get(document, '{ "": "$a.b" }');


SELECT BSONLASTONSORTED(document) 
FROM documentdb_api.collection('db', 'bsonorderaggregates') 
GROUP BY bson_expression_get(document, '{ "": "$a.c" }');

SELECT BSONFIRSTONSORTED(document) 
FROM documentdb_api.collection('db', 'bsonorderaggregates') 
GROUP BY bson_expression_get(document, '{ "": "$a.b" }');

SELECT BSONLASTONSORTED(document) 
FROM documentdb_api.collection('db', 'bsonorderaggregates') 
GROUP BY bson_expression_get(document, '{ "": "$a.b" }');

SELECT BSONFIRSTONSORTED(document) 
FROM documentdb_api.collection('db', 'bsonorderaggregates') 
GROUP BY bson_expression_get(document, '{ "": "$a.b" }');

-- last/first field not present

SELECT bson_repath_and_build('firstonsorted'::text, BSONFIRSTONSORTED(bson_expression_get(document, '{ "": "$a.f" }')))
FROM documentdb_api.collection('db', 'bsonorderaggregates') 
GROUP BY bson_expression_get(document, '{ "": "$a.c" }');

SELECT bson_repath_and_build('last'::text, BSONLAST(bson_expression_get(document, '{ "": "$a.f" }'),  ARRAY['{ "a.b":-1}', '{"a.c":-1}']::bson[]))
FROM documentdb_api.collection('db', 'bsonorderaggregates') 
GROUP BY bson_expression_get(document, '{ "": "$a.c" }');

SELECT bson_repath_and_build(
    'lastNull'::text, bson_expression_get(BSONLAST(document,  ARRAY['{ "a.b":-1}', '{"a.c":-1}']::bson[]), '{ "": "$a.f" }', true),
    'first'::text, bson_expression_get(BSONFIRST(document,  ARRAY['{ "a.b":-1}', '{"a.c":-1}']::bson[]), '{ "": "$a.b" }', true),
    'firstNull'::text, bson_expression_get(BSONFIRST(document,  ARRAY['{ "a.b":-1}', '{"a.c":-1}']::bson[]), '{ "": "$a.x" }', true)
    )
FROM documentdb_api.collection('db', 'bsonorderaggregates') 
GROUP BY bson_expression_get(document, '{ "": "$a.c" }');

-- regression test for crash in BSONFIRST/BSONLAST when the first documents are small and we need to choose a larger document in byte size
SELECT documentdb_api.insert_one('db', 'bsonFirstLastCrash', '{"_id": 1, "a": "z", "b": "z", "c": "z"}');
SELECT documentdb_api.insert_one('db', 'bsonFirstLastCrash', FORMAT('{"_id": 2, "a": "%s", "b": "%s", "c": "%s"}', repeat('aaa', 10), repeat('bbb', 10), repeat('dd', 10))::bson);

SELECT BSONFIRST(document, ARRAY['{ "a": 1}', '{"c": 1}']::bson[]) FROM documentdb_api.collection('db', 'bsonFirstLastCrash');
SELECT BSONLAST(document, ARRAY['{ "a": -1}', '{"c": -1}']::bson[]) FROM documentdb_api.collection('db', 'bsonFirstLastCrash');

--
-- BEGIN FIRSTN/LASTN Testing
--
-- Insert an a.b, a.c matrix
SELECT documentdb_api.insert_one('db','bsonordernaggregates', '{"_id": 1, "a" : { "b" : 1, "c" : 1 } }', NULL);
SELECT documentdb_api.insert_one('db','bsonordernaggregates', '{"_id": 2, "a" : { "b" : 1, "c" : 2 } }', NULL);
SELECT documentdb_api.insert_one('db','bsonordernaggregates', '{"_id": 3, "a" : { "b" : 1, "c" : 3 } }', NULL);
SELECT documentdb_api.insert_one('db','bsonordernaggregates', '{"_id": 4, "a" : { "b" : 2, "c" : 1 } }', NULL);
SELECT documentdb_api.insert_one('db','bsonordernaggregates', '{"_id": 5, "a" : { "b" : 2, "c" : 2 } }', NULL);
SELECT documentdb_api.insert_one('db','bsonordernaggregates', '{"_id": 6, "a" : { "b" : 2, "c" : 3 } }', NULL);
SELECT documentdb_api.insert_one('db','bsonordernaggregates', '{"_id": 7, "a" : { "b" : 3, "c" : 1 } }', NULL);
SELECT documentdb_api.insert_one('db','bsonordernaggregates', '{"_id": 8, "a" : { "b" : 3, "c" : 2 } }', NULL);
SELECT documentdb_api.insert_one('db','bsonordernaggregates', '{"_id": 9, "a" : { "b" : 3, "c" : 3 } }', NULL);

SELECT BSONLASTN(document, 2, ARRAY['{ "a.b":1}'::bson, '{"a.c":1}'::bson])
FROM documentdb_api.collection('db', 'bsonordernaggregates') 
GROUP BY bson_expression_get(document, '{ "": "$a.c" }');

SELECT BSONLASTN(document, 2, ARRAY['{ "a.b":1}', '{"a.c":1}']::bson[])
FROM documentdb_api.collection('db', 'bsonordernaggregates') 
GROUP BY bson_expression_get(document, '{ "": "$a.c" }');

SELECT BSONLASTN(document, 3, ARRAY['{ "a.b":1}', '{"a.c":1}']::bson[]) 
FROM documentdb_api.collection('db', 'bsonordernaggregates') 
GROUP BY bson_expression_get(document, '{ "": "$a.c" }');

SELECT BSONLASTN(document, 4, ARRAY['{ "a.b":1}', '{"a.c":1}']::bson[]) 
FROM documentdb_api.collection('db', 'bsonordernaggregates') 
GROUP BY bson_expression_get(document, '{ "": "$a.c" }');

SELECT BSONLASTN(document, 2, ARRAY['{ "a.b":1}', '{"a.c":-1}']::bson[]) 
FROM documentdb_api.collection('db', 'bsonordernaggregates') 
GROUP BY bson_expression_get(document, '{ "": "$a.c" }');

SELECT BSONLASTN(document, 1, ARRAY['{ "a.b":-1}', '{"a.c":1}']::bson[]) 
FROM documentdb_api.collection('db', 'bsonordernaggregates') 
GROUP BY bson_expression_get(document, '{ "": "$a.c" }');

SELECT BSONLASTN(document, 1, ARRAY['{ "a.b":-1}', '{"a.c":-1}']::bson[]) 
FROM documentdb_api.collection('db', 'bsonordernaggregates') 
GROUP BY bson_expression_get(document, '{ "": "$a.c" }');

SELECT BSONLASTN(document, 2, ARRAY['{ "a.b":-1}', '{"a.c":-1}']::bson[]) 
FROM documentdb_api.collection('db', 'bsonordernaggregates') 
GROUP BY bson_expression_get(document, '{ "": "$a.c" }');

SELECT BSONLASTN(document, 3, ARRAY['{ "a.b":-1}', '{"a.c":-1}']::bson[]) 
FROM documentdb_api.collection('db', 'bsonordernaggregates') 
GROUP BY bson_expression_get(document, '{ "": "$a.c" }');

SELECT BSONLASTN(document, 4, ARRAY['{ "a.b":-1}', '{"a.c":-1}']::bson[]) 
FROM documentdb_api.collection('db', 'bsonordernaggregates') 
GROUP BY bson_expression_get(document, '{ "": "$a.c" }');

SELECT BSONLASTN(document, 4, ARRAY['{ "a.b":-1}', '{"a.c":-1}']::bson[]) 
FROM documentdb_api.collection('db', 'bsonordernaggregates') 
GROUP BY bson_expression_get(document, '{ "": "$a.c" }');

SELECT BSONFIRSTN(document, 2, ARRAY['{ "a.b":1}'::bson, '{"a.c":1}'::bson])
FROM documentdb_api.collection('db', 'bsonordernaggregates') 
GROUP BY bson_expression_get(document, '{ "": "$a.c" }');

SELECT BSONFIRSTN(document, 2, ARRAY['{ "a.b":1}', '{"a.c":1}']::bson[]) 
FROM documentdb_api.collection('db', 'bsonordernaggregates') 
GROUP BY bson_expression_get(document, '{ "": "$a.c" }');

SELECT BSONFIRSTN(document, 3, ARRAY['{ "a.b":1}', '{"a.c":1}']::bson[]) 
FROM documentdb_api.collection('db', 'bsonordernaggregates') 
GROUP BY bson_expression_get(document, '{ "": "$a.c" }');

SELECT BSONFIRSTN(document, 4, ARRAY['{ "a.b":1}', '{"a.c":1}']::bson[]) 
FROM documentdb_api.collection('db', 'bsonordernaggregates') 
GROUP BY bson_expression_get(document, '{ "": "$a.c" }');

SELECT BSONFIRSTN(document, 2, ARRAY['{ "a.b":1}', '{"a.c":-1}']::bson[]) 
FROM documentdb_api.collection('db', 'bsonordernaggregates') 
GROUP BY bson_expression_get(document, '{ "": "$a.c" }');

SELECT BSONFIRSTN(document, 1, ARRAY['{ "a.b":-1}', '{"a.c":1}']::bson[]) 
FROM documentdb_api.collection('db', 'bsonordernaggregates') 
GROUP BY bson_expression_get(document, '{ "": "$a.c" }');

SELECT BSONFIRSTN(document, 1, ARRAY['{ "a.b":-1}', '{"a.c":-1}']::bson[]) 
FROM documentdb_api.collection('db', 'bsonordernaggregates') 
GROUP BY bson_expression_get(document, '{ "": "$a.c" }');

SELECT BSONFIRSTN(document, 2, ARRAY['{ "a.b":-1}', '{"a.c":-1}']::bson[]) 
FROM documentdb_api.collection('db', 'bsonordernaggregates') 
GROUP BY bson_expression_get(document, '{ "": "$a.c" }');

SELECT BSONFIRSTN(document, 3, ARRAY['{ "a.b":-1}', '{"a.c":-1}']::bson[]) 
FROM documentdb_api.collection('db', 'bsonordernaggregates') 
GROUP BY bson_expression_get(document, '{ "": "$a.c" }');

SELECT BSONFIRSTN(document, 4, ARRAY['{ "a.b":-1}', '{"a.c":-1}']::bson[]) 
FROM documentdb_api.collection('db', 'bsonordernaggregates') 
GROUP BY bson_expression_get(document, '{ "": "$a.c" }');

SELECT BSONFIRSTN(document, 4, ARRAY['{ "a.b":-1}', '{"a.c":-1}']::bson[]) 
FROM documentdb_api.collection('db', 'bsonordernaggregates') 
GROUP BY bson_expression_get(document, '{ "": "$a.c" }');

SELECT BSONLASTNONSORTED(document, 2) 
FROM documentdb_api.collection('db', 'bsonordernaggregates') 
GROUP BY bson_expression_get(document, '{ "": "$a.c" }');

SELECT BSONFIRSTNONSORTED(document, 2) 
FROM documentdb_api.collection('db', 'bsonordernaggregates') 
GROUP BY bson_expression_get(document, '{ "": "$a.b" }');

SELECT BSONLASTNONSORTED(document, 4) 
FROM documentdb_api.collection('db', 'bsonordernaggregates') 
GROUP BY bson_expression_get(document, '{ "": "$a.b" }');

SELECT BSONFIRSTNONSORTED(document, 4) 
FROM documentdb_api.collection('db', 'bsonordernaggregates') 
GROUP BY bson_expression_get(document, '{ "": "$a.b" }');

-- last/first field not present

SELECT bson_repath_and_build('firstonsorted'::text, BSONFIRSTNONSORTED(bson_expression_get(document, '{ "": "$a.f" }'), 1))
FROM documentdb_api.collection('db', 'bsonordernaggregates') 
GROUP BY bson_expression_get(document, '{ "": "$a.c" }');

SELECT bson_repath_and_build('last'::text, BSONLASTN(bson_expression_get(document, '{ "": "$a.f" }'), 1,  ARRAY['{ "a.b":-1}', '{"a.c":-1}']::bson[]))
FROM documentdb_api.collection('db', 'bsonordernaggregates') 
GROUP BY bson_expression_get(document, '{ "": "$a.c" }');

-- Test the different order combinations
SELECT BSONFIRSTN(document, 3, ARRAY['{ "a.b":1}', '{"a.c":1}']::bson[]) FROM documentdb_api.collection('db', 'bsonordernaggregates');
SELECT BSONFIRSTN(document, 3, ARRAY['{ "a.b":1}', '{"a.c":-1}']::bson[]) FROM documentdb_api.collection('db', 'bsonordernaggregates');
SELECT BSONFIRSTN(document, 3, ARRAY['{ "a.b":-1}', '{"a.c":1}']::bson[]) FROM documentdb_api.collection('db', 'bsonordernaggregates');
SELECT BSONFIRSTN(document, 3, ARRAY['{ "a.b":-1}', '{"a.c":-1}']::bson[]) FROM documentdb_api.collection('db', 'bsonordernaggregates');

SELECT BSONLASTN(document, 3, ARRAY['{ "a.b":1}', '{"a.c":1}']::bson[]) FROM documentdb_api.collection('db', 'bsonordernaggregates');
SELECT BSONLASTN(document, 3, ARRAY['{ "a.b":1}', '{"a.c":-1}']::bson[]) FROM documentdb_api.collection('db', 'bsonordernaggregates');
SELECT BSONLASTN(document, 3, ARRAY['{ "a.b":-1}', '{"a.c":1}']::bson[]) FROM documentdb_api.collection('db', 'bsonordernaggregates');
SELECT BSONLASTN(document, 3, ARRAY['{ "a.b":-1}', '{"a.c":-1}']::bson[]) FROM documentdb_api.collection('db', 'bsonordernaggregates');

-- Test with null results from the sort spec
SELECT BSONFIRSTN(document, 3, ARRAY['{ "f":1}', '{ "a.b":1}', '{"a.c":1}']::bson[]) FROM documentdb_api.collection('db', 'bsonordernaggregates');
SELECT BSONFIRSTN(document, 3, ARRAY['{ "a.b":1}', '{ "f":1}', '{"a.c":1}']::bson[]) FROM documentdb_api.collection('db', 'bsonordernaggregates');
SELECT BSONFIRSTN(document, 3, ARRAY['{ "a.b":1}', '{"a.c":1}', '{ "f":1}']::bson[]) FROM documentdb_api.collection('db', 'bsonordernaggregates');

SELECT BSONLASTN(document, 3, ARRAY['{ "f":1}', '{ "a.b":1}', '{"a.c":1}']::bson[]) FROM documentdb_api.collection('db', 'bsonordernaggregates');
SELECT BSONLASTN(document, 3, ARRAY['{ "a.b":1}', '{ "f":1}', '{"a.c":1}']::bson[]) FROM documentdb_api.collection('db', 'bsonordernaggregates');
SELECT BSONLASTN(document, 3, ARRAY['{ "a.b":1}', '{"a.c":1}', '{ "f":1}']::bson[]) FROM documentdb_api.collection('db', 'bsonordernaggregates');

SELECT BSONFIRSTN(document, 3, ARRAY['{ "f":1}', '{ "a.b":1}', '{"a.c":1}']::bson[]) FROM documentdb_api.collection('db', 'bsonordernaggregates');
SELECT BSONFIRSTN(document, 3, ARRAY['{ "a.b":1}', '{ "f":1}', '{"a.c":1}']::bson[]) FROM documentdb_api.collection('db', 'bsonordernaggregates');
SELECT BSONFIRSTN(document, 3, ARRAY['{ "a.b":1}', '{"a.c":1}', '{ "f":1}']::bson[]) FROM documentdb_api.collection('db', 'bsonordernaggregates');

SELECT BSONLASTN(document, 3, ARRAY['{ "f":1}', '{ "a.b":1}', '{"a.c":1}']::bson[]) FROM documentdb_api.collection('db', 'bsonordernaggregates');
SELECT BSONLASTN(document, 3, ARRAY['{ "a.b":1}', '{ "f":1}', '{"a.c":1}']::bson[]) FROM documentdb_api.collection('db', 'bsonordernaggregates');
SELECT BSONLASTN(document, 3, ARRAY['{ "a.b":1}', '{"a.c":1}', '{ "f":1}']::bson[]) FROM documentdb_api.collection('db', 'bsonordernaggregates');

-- Test null ordering 
SELECT documentdb_api.insert_one('db','bsonordernaggregates', '{"_id": 11, "a" : { "c" : 3 } }', NULL);

SELECT BSONFIRSTN(document, 3, ARRAY['{ "a.b":1}', '{"a.c":1}']::bson[]) FROM documentdb_api.collection('db', 'bsonordernaggregates');
SELECT BSONFIRSTN(document, 3, ARRAY['{ "a.b":1}', '{"a.c":-1}']::bson[]) FROM documentdb_api.collection('db', 'bsonordernaggregates');
SELECT BSONFIRSTN(document, 3, ARRAY['{ "a.b":-1}', '{"a.c":1}']::bson[]) FROM documentdb_api.collection('db', 'bsonordernaggregates');
SELECT BSONFIRSTN(document, 3, ARRAY['{ "a.b":-1}', '{"a.c":-1}']::bson[]) FROM documentdb_api.collection('db', 'bsonordernaggregates');

SELECT BSONFIRSTN(document, 3, ARRAY['{ "a.c":1}', '{"a.b":1}']::bson[]) FROM documentdb_api.collection('db', 'bsonordernaggregates');
SELECT BSONFIRSTN(document, 3, ARRAY['{ "a.c":1}', '{"a.b":-1}']::bson[]) FROM documentdb_api.collection('db', 'bsonordernaggregates');
SELECT BSONFIRSTN(document, 3, ARRAY['{ "a.c":-1}', '{"a.b":1}']::bson[]) FROM documentdb_api.collection('db', 'bsonordernaggregates');
SELECT BSONFIRSTN(document, 3, ARRAY['{ "a.c":-1}', '{"a.b":-1}']::bson[]) FROM documentdb_api.collection('db', 'bsonordernaggregates');

SELECT BSONLASTN(document, 3, ARRAY['{ "a.b":1}', '{"a.c":1}']::bson[]) FROM documentdb_api.collection('db', 'bsonordernaggregates');
SELECT BSONLASTN(document, 3, ARRAY['{ "a.b":1}', '{"a.c":-1}']::bson[]) FROM documentdb_api.collection('db', 'bsonordernaggregates');
SELECT BSONLASTN(document, 3, ARRAY['{ "a.b":-1}', '{"a.c":1}']::bson[]) FROM documentdb_api.collection('db', 'bsonordernaggregates');
SELECT BSONLASTN(document, 3, ARRAY['{ "a.b":-1}', '{"a.c":-1}']::bson[]) FROM documentdb_api.collection('db', 'bsonordernaggregates');

SELECT BSONLASTN(document, 3, ARRAY['{ "a.c":1}', '{"a.b":1}']::bson[]) FROM documentdb_api.collection('db', 'bsonordernaggregates');
SELECT BSONLASTN(document, 3, ARRAY['{ "a.c":1}', '{"a.b":-1}']::bson[]) FROM documentdb_api.collection('db', 'bsonordernaggregates');
SELECT BSONLASTN(document, 3, ARRAY['{ "a.c":-1}', '{"a.b":1}']::bson[]) FROM documentdb_api.collection('db', 'bsonordernaggregates');
SELECT BSONLASTN(document, 3, ARRAY['{ "a.c":-1}', '{"a.b":-1}']::bson[]) FROM documentdb_api.collection('db', 'bsonordernaggregates');


BEGIN;
SET LOCAL enable_seqscan TO OFF;
SELECT documentdb_distributed_test_helpers.mask_plan_id_from_distributed_subplan($Q$
EXPLAIN(costs off)  
SELECT BSONLASTN(document, 1, ARRAY['{"a.b":1}']::bson[]) FROM documentdb_api.collection('db', 'bsonordernaggregates');
$Q$);
END;

SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "bsonordernaggregates", "indexes": [{"key": {"a.b": 1}, "name": "a_dot_b"}]}', true);

BEGIN;
SET LOCAL enable_seqscan TO OFF;
SELECT documentdb_distributed_test_helpers.mask_plan_id_from_distributed_subplan($Q$
EXPLAIN(costs off)  
SELECT BSONLASTN(document, 1, ARRAY['{"a.b":-1}', '{"a.b":-1}']::bson[]) FROM documentdb_api.collection('db', 'bsonordernaggregates');
$Q$);
END;

-- Shard the collection
SELECT documentdb_api.shard_collection('db','bsonordernaggregates', '{"_id":"hashed"}', false);

SELECT BSONLASTN(document, 2, ARRAY['{ "a.b":1}'::bson, '{"a.c":1}'::bson])
FROM documentdb_api.collection('db', 'bsonordernaggregates') 
GROUP BY bson_expression_get(document, '{ "": "$a.c" }');

SELECT BSONLASTN(document, 2, ARRAY['{ "a.b":1}', '{"a.c":1}']::bson[]) 
FROM documentdb_api.collection('db', 'bsonordernaggregates') 
GROUP BY bson_expression_get(document, '{ "": "$a.c" }');

SELECT BSONLASTN(document, 3, ARRAY['{ "a.b":1}', '{"a.c":1}']::bson[]) 
FROM documentdb_api.collection('db', 'bsonordernaggregates') 
GROUP BY bson_expression_get(document, '{ "": "$a.c" }');

SELECT BSONLASTN(document, 4, ARRAY['{ "a.b":1}', '{"a.c":1}']::bson[]) 
FROM documentdb_api.collection('db', 'bsonordernaggregates') 
GROUP BY bson_expression_get(document, '{ "": "$a.c" }');

SELECT BSONLASTN(document, 2, ARRAY['{ "a.b":1}', '{"a.c":-1}']::bson[]) 
FROM documentdb_api.collection('db', 'bsonordernaggregates') 
GROUP BY bson_expression_get(document, '{ "": "$a.c" }');

SELECT BSONLASTN(document, 1, ARRAY['{ "a.b":-1}', '{"a.c":1}']::bson[]) 
FROM documentdb_api.collection('db', 'bsonordernaggregates') 
GROUP BY bson_expression_get(document, '{ "": "$a.c" }');

SELECT BSONLASTN(document, 1, ARRAY['{ "a.b":-1}', '{"a.c":-1}']::bson[]) 
FROM documentdb_api.collection('db', 'bsonordernaggregates') 
GROUP BY bson_expression_get(document, '{ "": "$a.c" }');

SELECT BSONLASTN(document, 2, ARRAY['{ "a.b":-1}', '{"a.c":-1}']::bson[]) 
FROM documentdb_api.collection('db', 'bsonordernaggregates') 
GROUP BY bson_expression_get(document, '{ "": "$a.c" }');

SELECT BSONLASTN(document, 3, ARRAY['{ "a.b":-1}', '{"a.c":-1}']::bson[]) 
FROM documentdb_api.collection('db', 'bsonordernaggregates') 
GROUP BY bson_expression_get(document, '{ "": "$a.c" }');

SELECT BSONLASTN(document, 4, ARRAY['{ "a.b":-1}', '{"a.c":-1}']::bson[]) 
FROM documentdb_api.collection('db', 'bsonordernaggregates') 
GROUP BY bson_expression_get(document, '{ "": "$a.c" }');

SELECT BSONLASTN(document, 4, ARRAY['{ "a.b":-1}', '{"a.c":-1}']::bson[]) 
FROM documentdb_api.collection('db', 'bsonordernaggregates') 
GROUP BY bson_expression_get(document, '{ "": "$a.c" }');

SELECT BSONFIRSTN(document, 2, ARRAY['{ "a.b":1}'::bson, '{"a.c":1}'::bson])
FROM documentdb_api.collection('db', 'bsonordernaggregates') 
GROUP BY bson_expression_get(document, '{ "": "$a.c" }');

SELECT BSONFIRSTN(document, 2, ARRAY['{ "a.b":1}', '{"a.c":1}']::bson[]) 
FROM documentdb_api.collection('db', 'bsonordernaggregates') 
GROUP BY bson_expression_get(document, '{ "": "$a.c" }');

SELECT BSONFIRSTN(document, 3, ARRAY['{ "a.b":1}', '{"a.c":1}']::bson[]) 
FROM documentdb_api.collection('db', 'bsonordernaggregates') 
GROUP BY bson_expression_get(document, '{ "": "$a.c" }');

SELECT BSONFIRSTN(document, 4, ARRAY['{ "a.b":1}', '{"a.c":1}']::bson[]) 
FROM documentdb_api.collection('db', 'bsonordernaggregates') 
GROUP BY bson_expression_get(document, '{ "": "$a.c" }');

SELECT BSONFIRSTN(document, 2, ARRAY['{ "a.b":1}', '{"a.c":-1}']::bson[]) 
FROM documentdb_api.collection('db', 'bsonordernaggregates') 
GROUP BY bson_expression_get(document, '{ "": "$a.c" }');

SELECT BSONFIRSTN(document, 1, ARRAY['{ "a.b":-1}', '{"a.c":1}']::bson[]) 
FROM documentdb_api.collection('db', 'bsonordernaggregates') 
GROUP BY bson_expression_get(document, '{ "": "$a.c" }');

SELECT BSONFIRSTN(document, 1, ARRAY['{ "a.b":-1}', '{"a.c":-1}']::bson[]) 
FROM documentdb_api.collection('db', 'bsonordernaggregates') 
GROUP BY bson_expression_get(document, '{ "": "$a.c" }');

SELECT BSONFIRSTN(document, 2, ARRAY['{ "a.b":-1}', '{"a.c":-1}']::bson[]) 
FROM documentdb_api.collection('db', 'bsonordernaggregates') 
GROUP BY bson_expression_get(document, '{ "": "$a.c" }');

SELECT BSONFIRSTN(document, 3, ARRAY['{ "a.b":-1}', '{"a.c":-1}']::bson[]) 
FROM documentdb_api.collection('db', 'bsonordernaggregates') 
GROUP BY bson_expression_get(document, '{ "": "$a.c" }');

SELECT BSONFIRSTN(document, 4, ARRAY['{ "a.b":-1}', '{"a.c":-1}']::bson[]) 
FROM documentdb_api.collection('db', 'bsonordernaggregates') 
GROUP BY bson_expression_get(document, '{ "": "$a.c" }');

SELECT BSONFIRSTN(document, 4, ARRAY['{ "a.b":-1}', '{"a.c":-1}']::bson[]) 
FROM documentdb_api.collection('db', 'bsonordernaggregates') 
GROUP BY bson_expression_get(document, '{ "": "$a.c" }');

SELECT BSONLASTNONSORTED(document, 2) 
FROM documentdb_api.collection('db', 'bsonordernaggregates') 
GROUP BY bson_expression_get(document, '{ "": "$a.c" }');

SELECT BSONFIRSTNONSORTED(document, 2) 
FROM documentdb_api.collection('db', 'bsonordernaggregates') 
GROUP BY bson_expression_get(document, '{ "": "$a.b" }');

SELECT BSONLASTNONSORTED(document, 4) 
FROM documentdb_api.collection('db', 'bsonordernaggregates') 
GROUP BY bson_expression_get(document, '{ "": "$a.b" }');

SELECT BSONFIRSTNONSORTED(document, 4) 
FROM documentdb_api.collection('db', 'bsonordernaggregates') 
GROUP BY bson_expression_get(document, '{ "": "$a.b" }');

-- last/first field not present

SELECT bson_repath_and_build('firstonsorted'::text, BSONFIRSTNONSORTED(bson_expression_get(document, '{ "": "$a.f" }'), 1))
FROM documentdb_api.collection('db', 'bsonordernaggregates') 
GROUP BY bson_expression_get(document, '{ "": "$a.c" }');

SELECT bson_repath_and_build('last'::text, BSONLASTN(bson_expression_get(document, '{ "": "$a.f" }'), 1,  ARRAY['{ "a.b":-1}', '{"a.c":-1}']::bson[]))
FROM documentdb_api.collection('db', 'bsonordernaggregates') 
GROUP BY bson_expression_get(document, '{ "": "$a.c" }');

SELECT bson_repath_and_build(
    'lastNull'::text, BSONLASTN(document, 2,  ARRAY['{ "a.b":-1}', '{"a.c":-1}']::bson[]),
    'first'::text, BSONFIRSTN(document, 2,  ARRAY['{ "a.b":-1}', '{"a.c":-1}']::bson[]),
    'firstNull'::text, BSONFIRSTN(document, 2,  ARRAY['{ "a.b":-1}', '{"a.c":-1}']::bson[])
    )
FROM documentdb_api.collection('db', 'bsonordernaggregates') 
GROUP BY bson_expression_get(document, '{ "": "$a.c" }');

-- regression test for crash in BSONFIRSTN/BSONLASTN when the first documents are small and we need to choose a larger document in byte size
SELECT documentdb_api.insert_one('db', 'bsonFirstNLastNCrash', '{"_id": 1, "a": "z", "b": "z", "c": "z"}');
SELECT documentdb_api.insert_one('db', 'bsonFirstNLastNCrash', FORMAT('{"_id": 2, "a": "%s", "b": "%s", "c": "%s"}', repeat('aaa', 10), repeat('bbb', 10), repeat('dd', 10))::bson);

SELECT BSONFIRSTN(document, 1, ARRAY['{ "a": 1}', '{"c": 1}']::bson[]) FROM documentdb_api.collection('db', 'bsonFirstNLastNCrash');
SELECT BSONLASTN(document, 1, ARRAY['{ "a": -1}', '{"c": -1}']::bson[]) FROM documentdb_api.collection('db', 'bsonFirstNLastNCrash');

-- regression test for allocating too much memory
SELECT BSONFIRSTN(document, 9999999999999, ARRAY['{ "a": 1}', '{"c": 1}']::bson[]) FROM documentdb_api.collection('db', 'bsonFirstNLastNCrash');
SELECT BSONLASTN(document, 9999999999999, ARRAY['{ "a": -1}', '{"c": -1}']::bson[]) FROM documentdb_api.collection('db', 'bsonFirstNLastNCrash');

-- regression test for NULL pointer
SELECT BSONFIRSTN(NULL, 1, ARRAY['{ "a": 1}', '{"c": 1}']::bson[]);
SELECT BSONFIRSTNONSORTED(NULL, 1);
SELECT BSONLASTN(NULL, 3, ARRAY['{ "a": 1}', '{"c": 1}']::bson[]) FROM documentdb_api.collection('db', 'bsonFirstNLastNCrash');
SELECT BSONLASTNONSORTED(NULL, 3) FROM documentdb_api.collection('db', 'bsonFirstNLastNCrash');
