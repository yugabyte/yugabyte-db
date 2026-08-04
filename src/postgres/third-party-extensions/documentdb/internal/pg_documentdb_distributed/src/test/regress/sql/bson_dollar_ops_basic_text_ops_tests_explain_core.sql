/* validate explain */
EXPLAIN (COSTS OFF) SELECT object_id, document FROM documentdb_api.collection('db','queryregexopstest') WHERE document @~ '{ "a.b": "^.+$" }';
