
set search_path to documentdb_api_catalog, documentdb_core, documentdb_api_internal;

/* insert paths with nested objects arrays */
SELECT documentdb_api.insert_one('db','bsoexplainnorderby', '{"_id": 9, "a" : { "b" : 1 } }', NULL);
SELECT documentdb_api.insert_one('db','bsoexplainnorderby', '{"_id": 10, "a" : { "b" : [ 0, 1, 2 ] } }', NULL);
SELECT documentdb_api.insert_one('db','bsoexplainnorderby', '{"_id": 11, "a" : [ { "b": 0 }, { "b": 1 }, { "b": 3.0 }] }', NULL);
SELECT documentdb_api.insert_one('db','bsoexplainnorderby', '{"_id": 12, "a" : [ { "b": [-1, 1, 2] }, { "b": [0, 1, 2] }, { "b": [0, 1, 7] }] }', NULL);
SELECT documentdb_api.insert_one('db','bsoexplainnorderby', '{"_id": 13, "a" : [ { "b": [[-1, 1, 2]] }, { "b": [[0, 1, 2]] }, { "b": [[0, 1, 7]] }] }', NULL);

EXPLAIN (COSTS OFF) SELECT object_id, document FROM documentdb_api.collection('db', 'bsoexplainnorderby') ORDER BY bson_orderby(document, '{ "a.b": 1 }');

EXPLAIN (COSTS OFF) SELECT object_id, document FROM documentdb_api.collection('db', 'bsoexplainnorderby') ORDER BY bson_orderby(document, '{ "a.b": -1 }') DESC;

EXPLAIN (COSTS OFF) SELECT object_id, document FROM documentdb_api.collection('db', 'bsoexplainnorderby') ORDER BY bson_orderby(document, '{ "a.b.0": -1 }') DESC;

EXPLAIN (COSTS OFF) SELECT object_id, document FROM documentdb_api.collection('db', 'bsoexplainnorderby') ORDER BY bson_orderby(document, '{ "a.b.1": 1 }');

EXPLAIN (COSTS OFF) SELECT object_id, document FROM documentdb_api.collection('db', 'bsoexplainnorderby') ORDER BY bson_orderby(document, '{ "a.b": 1 }'), bson_orderby(document, '{ "a.b.0": 1 }');

EXPLAIN (COSTS OFF) SELECT object_id, document FROM documentdb_api.collection('db', 'bsoexplainnorderby') ORDER BY bson_orderby(document, '{ "a.b": 1 }'), bson_orderby(document, '{ "a.b.0": -1 }') DESC;


EXPLAIN (COSTS OFF) SELECT object_id, document FROM documentdb_api.collection('db', 'bsoexplainnorderby') WHERE document @@ '{ "a.b": { "$gt": 0 } }' ORDER BY bson_orderby(document, '{ "a.b": 1 }');

EXPLAIN (COSTS OFF) SELECT object_id, document FROM documentdb_api.collection('db', 'bsoexplainnorderby') WHERE document @@ '{ "a.b.0": { "$gte": 0 } }' ORDER BY bson_orderby(document, '{ "a.b": 1 }');

EXPLAIN (COSTS OFF) SELECT object_id, document FROM documentdb_api.collection('db', 'bsoexplainnorderby') WHERE document @@ '{ "a.b.0": { "$gte": 0 } }' ORDER BY bson_orderby(document, '{ "a.b": 1 }'), bson_orderby(document, '{ "a.b.0": 1 }');

EXPLAIN (COSTS OFF) SELECT object_id, document FROM documentdb_api.collection('db', 'bsoexplainnorderby') WHERE document @@ '{ "a.b.0": { "$gte": 0 } }' ORDER BY bson_orderby(document, '{ "a.b": 1 }'), bson_orderby(document, '{ "a.b.1": 1 }');

EXPLAIN (COSTS OFF) SELECT object_id, document FROM documentdb_api.collection('db', 'bsoexplainnorderby') WHERE document @@ '{ "a.b": { "$gte": 0 } }' ORDER BY bson_orderby(document, '{ "a.b": 1 }'), bson_orderby(document, '{ "a.b.1": 1 }');

EXPLAIN (COSTS OFF) SELECT object_id, document FROM documentdb_api.collection('db', 'bsoexplainnorderby') WHERE document @@ '{ "a": { "$gte": { "b": 0 } } }' ORDER BY bson_orderby(document, '{ "a.b": 1 }');

