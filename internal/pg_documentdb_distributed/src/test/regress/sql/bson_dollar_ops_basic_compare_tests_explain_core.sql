
/* validate explain */
EXPLAIN (COSTS OFF) SELECT object_id, document FROM helio_api.collection('db', 'querydollartest') WHERE document @= '{ "a.b": 1 }';
EXPLAIN (COSTS OFF) SELECT object_id, document FROM helio_api.collection('db', 'querydollartest') WHERE document @!= '{ "a.b": 1 }';
EXPLAIN (COSTS OFF) SELECT object_id, document FROM helio_api.collection('db', 'querydollartest') WHERE document @> '{ "a.b": 1 }';
EXPLAIN (COSTS OFF) SELECT object_id, document FROM helio_api.collection('db', 'querydollartest') WHERE document @>= '{ "a.b": 1 }';
EXPLAIN (COSTS OFF) SELECT object_id, document FROM helio_api.collection('db', 'querydollartest') WHERE document @< '{ "a.b": 1 }';
EXPLAIN (COSTS OFF) SELECT object_id, document FROM helio_api.collection('db', 'querydollartest') WHERE document @<= '{ "a.b": 1 }';
EXPLAIN (COSTS OFF) SELECT object_id, document FROM helio_api.collection('db', 'querydollartest') WHERE document @*= '{ "a.b": [ 1, 2, true ]}'::bson;

/* validate explain for the individual items as well */
EXPLAIN (COSTS OFF) SELECT object_id, document FROM helio_api.collection('db', 'querydollartest') WHERE document @> '{ "a": { "b": 1 } }';
EXPLAIN (COSTS OFF) SELECT object_id, document FROM helio_api.collection('db', 'querydollartest') WHERE document @> '{ "a": { "b": [ true, false ] } }';
EXPLAIN (COSTS OFF) SELECT object_id, document FROM helio_api.collection('db', 'querydollartest') WHERE document @> '{ "a": [ { "b": [ 2, 3, 4 ] } ] }';

EXPLAIN (COSTS OFF) SELECT object_id, document FROM helio_api.collection('db', 'querydollartest') WHERE document @>= '{ "a": { "b": 1 } }';
EXPLAIN (COSTS OFF) SELECT object_id, document FROM helio_api.collection('db', 'querydollartest') WHERE document @>= '{ "a": { "b": [ true, false ] } }';
EXPLAIN (COSTS OFF) SELECT object_id, document FROM helio_api.collection('db', 'querydollartest') WHERE document @>= '{ "a": [ { "b": [ 2, 3, 4 ] } ] }';

EXPLAIN (COSTS OFF) SELECT object_id, document FROM helio_api.collection('db', 'querydollartest') WHERE document @< '{ "a": { "b": 1 } }';
EXPLAIN (COSTS OFF) SELECT object_id, document FROM helio_api.collection('db', 'querydollartest') WHERE document @< '{ "a": { "b": [ true, false ] } }';
EXPLAIN (COSTS OFF) SELECT object_id, document FROM helio_api.collection('db', 'querydollartest') WHERE document @< '{ "a": [ { "b": [ 2, 3, 4 ] } ] }';

EXPLAIN (COSTS OFF) SELECT object_id, document FROM helio_api.collection('db', 'querydollartest') WHERE document @<= '{ "a": { "b": 1 } }';
EXPLAIN (COSTS OFF) SELECT object_id, document FROM helio_api.collection('db', 'querydollartest') WHERE document @<= '{ "a": { "b": [ true, false ] } }';
EXPLAIN (COSTS OFF) SELECT object_id, document FROM helio_api.collection('db', 'querydollartest') WHERE document @<= '{ "a": [ { "b": [ 2, 3, 4 ] } ] }';

EXPLAIN (COSTS OFF) SELECT object_id, document FROM helio_api.collection('db', 'querydollartest') WHERE document @= '{ "a": { "b": 1 } }';
EXPLAIN (COSTS OFF) SELECT object_id, document FROM helio_api.collection('db', 'querydollartest') WHERE document @= '{ "a": { "b": [ true, false ] } }';
EXPLAIN (COSTS OFF) SELECT object_id, document FROM helio_api.collection('db', 'querydollartest') WHERE document @= '{ "a": [ { "b": [ 2, 3, 4 ] } ] }';

EXPLAIN (COSTS OFF) SELECT object_id, document FROM helio_api.collection('db', 'querydollartest') WHERE document @!= '{ "a": { "b": 1 } }';
EXPLAIN (COSTS OFF) SELECT object_id, document FROM helio_api.collection('db', 'querydollartest') WHERE document @!= '{ "a": { "b": [ true, false ] } }';
EXPLAIN (COSTS OFF) SELECT object_id, document FROM helio_api.collection('db', 'querydollartest') WHERE document @!= '{ "a": [ { "b": [ 2, 3, 4 ] } ] }';

EXPLAIN (COSTS OFF) SELECT object_id, document FROM helio_api.collection('db', 'querydollartest') WHERE document @*= '{ "a.b": [ { "b": [ 2, 3, 4 ] }, 2, true ]}';
EXPLAIN (COSTS OFF) SELECT object_id, document FROM helio_api.collection('db', 'querydollartest') WHERE document @!*= '{ "a.b": [ { "b": [ 2, 3, 4 ] }, 2, true ]}';

EXPLAIN (COSTS OFF) SELECT object_id, document FROM helio_api.collection('db', 'querydollartest') WHERE document @? '{ "a.b": 1 }';
EXPLAIN (COSTS OFF) SELECT object_id, document FROM helio_api.collection('db', 'querydollartest') WHERE document @? '{ "a.b": 0 }';

EXPLAIN (COSTS OFF) SELECT object_id, document FROM helio_api.collection('db', 'querydollartest') WHERE document @&= '{ "a.b": [1] }';
EXPLAIN (COSTS OFF) SELECT object_id, document FROM helio_api.collection('db', 'querydollartest') WHERE document @&= '{ "a.b": [0, 1] }';
EXPLAIN (COSTS OFF) SELECT object_id, document FROM helio_api.collection('db', 'querydollartest') WHERE document @&= '{ "a.b": [[0]] }';
EXPLAIN (COSTS OFF) SELECT object_id, document FROM helio_api.collection('db', 'querydollartest') WHERE document @&= '{ "a.b": [] }';

