/* validate explain */
EXPLAIN (COSTS OFF) SELECT object_id, document FROM helio_api.collection('db','queryregexopstest') WHERE document @~ '{ "a.b": "^.+$" }';
