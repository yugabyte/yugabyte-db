-- top level
SELECT helio_api.insert_one('db','dollaralltests','{"_id": 1, "a": "b"}', NULL);
SELECT helio_api.insert_one('db','dollaralltests','{"_id": 2, "a": ["a", "b"]}', NULL);
SELECT helio_api.insert_one('db','dollaralltests','{"_id": 3, "a": ["a", "b", "c"]}', NULL);
SELECT helio_api.insert_one('db','dollaralltests','{"_id": 4, "a": ["y", "z", "a"]}', NULL);
SELECT helio_api.insert_one('db','dollaralltests','{"_id": 5, "a": ["y", "z", "a", { "d": 1 }, 2]}', NULL);

-- nested and empty arrays
SELECT helio_api.insert_one('db','dollaralltests','{"_id": 6, "a": [[["x", "y"], ["z"]]]}', NULL);
SELECT helio_api.insert_one('db','dollaralltests','{"_id": 7, "a": []}', NULL);
SELECT helio_api.insert_one('db','dollaralltests','{"_id": 8, "a": [[]]}', NULL);

-- nested objects
SELECT helio_api.insert_one('db','dollaralltests','{"_id": 9, "a": {"b": [ ["a", "z", "b"] ]}}', NULL);
SELECT helio_api.insert_one('db','dollaralltests','{"_id": 10, "a": {"b": [ ["a", "z", "b"], ["x", "y"] ]}}', NULL);
SELECT helio_api.insert_one('db','dollaralltests','{"_id": 11, "a": {"b": [ ["a", "z", "b"], ["x", "y"], ["c"] ]}}', NULL);
SELECT helio_api.insert_one('db','dollaralltests','{"_id": 12, "a": {"b": { "c": ["d"] }}}', NULL);
SELECT helio_api.insert_one('db','dollaralltests','{"_id": 13, "a": {"b": { "c": { "d": 1 }}}}', NULL);

-- objects with elements that sort after arrays
SELECT helio_api.insert_one('db','dollaralltests','{"_id": 14, "foo": true}', NULL);
SELECT helio_api.insert_one('db','dollaralltests','{"_id": 15, "foo": false}', NULL);
SELECT helio_api.insert_one('db','dollaralltests','{"_id": 16, "foo": [ 1, "a", true]}', NULL);
SELECT helio_api.insert_one('db','dollaralltests','{"_id": 17, "foo": [true]}', NULL);

-- objects with null and NaN
SELECT helio_api.insert_one('db','dollaralltests','{"_id": 18, "other": [ 1, null]}', NULL);
SELECT helio_api.insert_one('db','dollaralltests','{"_id": 19, "other": [null]}', NULL);
SELECT helio_api.insert_one('db','dollaralltests','{"_id": 20, "other": [1, NaN]}', NULL);
SELECT helio_api.insert_one('db','dollaralltests','{"_id": 21, "other": [1, 0.0]}', NULL);
SELECT helio_api.insert_one('db','dollaralltests','{"_id": 22, "other": [1, 0.0, NaN]}', NULL);
SELECT helio_api.insert_one('db','dollaralltests','{"_id": 23, "other": [null, NaN]}', NULL);

-- documents inside array
SELECT helio_api.insert_one('db','dollaralltests','{"_id": 24, "array": [ {"x": 1}, {"x": 2}]}', NULL);
SELECT helio_api.insert_one('db','dollaralltests','{"_id": 25, "array": [ {"x": 2}, {"x": 3}]}', NULL);
SELECT helio_api.insert_one('db','dollaralltests','{"_id": 26, "array": [ {"x": 3}, {"x": 4}]}', NULL);

-- top level simple
SELECT document FROM helio_api.collection('db', 'dollaralltests') where document @&= '{ "a" : ["b"] }';
SELECT document FROM helio_api.collection('db', 'dollaralltests') where document @&= '{ "a" : ["b", "a"] }';
SELECT document FROM helio_api.collection('db', 'dollaralltests') where document @&= '{ "a" : ["b", "c"] }';
SELECT document FROM helio_api.collection('db', 'dollaralltests') where document @&= '{ "a" : ["a", "z"] }';
SELECT document FROM helio_api.collection('db', 'dollaralltests') where document @&= '{ "a" : ["a", "y", "z"] }';
SELECT document FROM helio_api.collection('db', 'dollaralltests') where document @&= '{ "a" : [{ "d": 1 }, "a", "y", "z"] }';
SELECT document FROM helio_api.collection('db', 'dollaralltests') where document @&= '{ "a" : [{ "d": 1 }]}';
SELECT document FROM helio_api.collection('db', 'dollaralltests') where document @&= '{ "a" : [{ "d": 2 }]}';
SELECT document FROM helio_api.collection('db', 'dollaralltests') where document @&= '{ "a" : [2]}';

-- array indexes selector
SELECT document FROM helio_api.collection('db', 'dollaralltests') where document @&= '{ "a.0" : ["b"] }';
SELECT document FROM helio_api.collection('db', 'dollaralltests') where document @&= '{ "a.1" : ["b"] }';
SELECT document FROM helio_api.collection('db', 'dollaralltests') where document @&= '{ "a.0.0" : ["x"] }';
SELECT document FROM helio_api.collection('db', 'dollaralltests') where document @&= '{ "a.0.0" : ["y"] }';
SELECT document FROM helio_api.collection('db', 'dollaralltests') where document @&= '{ "a.0.0" : ["y", "x"] }';
SELECT document FROM helio_api.collection('db', 'dollaralltests') where document @&= '{ "a.0.0" : [ ["x", "y"] ] }';
SELECT document FROM helio_api.collection('db', 'dollaralltests') where document @&= '{ "a.0.1" : [ ["z"] ] }';
SELECT document FROM helio_api.collection('db', 'dollaralltests') where document @&= '{ "a.0.0.0" : ["x"] }';
SELECT document FROM helio_api.collection('db', 'dollaralltests') where document @&= '{ "a.3" : [{ "d": 1 }] }';

-- top level nested arrays
SELECT document FROM helio_api.collection('db', 'dollaralltests') where document @&= '{ "a" : [[["z"]]] }';
SELECT document FROM helio_api.collection('db', 'dollaralltests') where document @&= '{ "a" : [[["x", "y"]]] }';
SELECT document FROM helio_api.collection('db', 'dollaralltests') where document @&= '{ "a" : [[["x", "y"], ["z"]]] }';

-- empty arrays
SELECT document FROM helio_api.collection('db', 'dollaralltests') where document @&= '{ "a" : [] }';
SELECT document FROM helio_api.collection('db', 'dollaralltests') where document @&= '{ "a" : [ [] ]}';
SELECT document FROM helio_api.collection('db', 'dollaralltests') where document @&= '{ "a" : [ [ [] ] ]}';

-- nested objects and arrays
SELECT document FROM helio_api.collection('db', 'dollaralltests') where document @&= '{ "a.b" : [ ["a", "z", "b"] ] }';
SELECT document FROM helio_api.collection('db', 'dollaralltests') where document @&= '{ "a.b" : [ ["a", "z", "b"], ["x", "y"] ] }';
SELECT document FROM helio_api.collection('db', 'dollaralltests') where document @&= '{ "a.b" : [ ["a", "z", "b"], ["x", "y"], ["c"] ] }';
SELECT document FROM helio_api.collection('db', 'dollaralltests') where document @&= '{ "a.b" : [ ["a", "z", "b"], ["c"] ] }';
SELECT document FROM helio_api.collection('db', 'dollaralltests') where document @&= '{ "a.b" : [ ["x", "y"] ] }';

-- nested objects and arrays with objects
SELECT document FROM helio_api.collection('db', 'dollaralltests') where document @&= '{ "a.b.c" : [ "d" ] }';
SELECT document FROM helio_api.collection('db', 'dollaralltests') where document @&= '{ "a.b.c" : [ [ "d" ] ] }';
SELECT document FROM helio_api.collection('db', 'dollaralltests') where document @&= '{ "a.b.c" : [ { "d": 1 } ] }';
SELECT document FROM helio_api.collection('db', 'dollaralltests') where document @&= '{ "a.b.c" : [{ }] }';
SELECT document FROM helio_api.collection('db', 'dollaralltests') where document @&= '{ "a.b.c.d" : [ 1 ] }';
SELECT document FROM helio_api.collection('db', 'dollaralltests') where document @&= '{ "a.b.c.d" : [ 1, 2 ] }';

-- objects with elements that sort after arrays
SELECT document FROM helio_api.collection('db', 'dollaralltests') where document @&= '{ "foo" : [ true ] }';
SELECT document FROM helio_api.collection('db', 'dollaralltests') where document @&= '{ "foo" : [ false ] }';
SELECT document FROM helio_api.collection('db', 'dollaralltests') where document @&= '{ "foo" : [ true, false ] }';
SELECT document FROM helio_api.collection('db', 'dollaralltests') where document @&= '{ "foo" : [ 1, "a" ] }';
SELECT document FROM helio_api.collection('db', 'dollaralltests') where document @&= '{ "foo" : [ 1, true ] }';


-- null and NaN
SELECT document FROM helio_api.collection('db', 'dollaralltests') where document @&= '{ "other" : [ null ] }';
SELECT document FROM helio_api.collection('db', 'dollaralltests') where document @&= '{ "a" : [ null ] }';
SELECT document FROM helio_api.collection('db', 'dollaralltests') where document @&= '{ "a" : [ null, null, null, null ] }';
SELECT document FROM helio_api.collection('db', 'dollaralltests') where document @&= '{ "a" : [ null, null, null, null, "foo" ] }';
SELECT document FROM helio_api.collection('db', 'dollaralltests') where document @&= '{ "a" : [ "foo", null, null, null ] }';
SELECT document FROM helio_api.collection('db', 'dollaralltests') where document @&= '{ "other" : [ [ null ] ] }';
SELECT document FROM helio_api.collection('db', 'dollaralltests') where document @&= '{ "other" : [ [ null ], [null] ] }';
SELECT document FROM helio_api.collection('db', 'dollaralltests') where document @&= '{ "other" : [ null, NaN ] }';
SELECT document FROM helio_api.collection('db', 'dollaralltests') where document @&= '{ "other" : [ 0.0 ] }';
SELECT document FROM helio_api.collection('db', 'dollaralltests') where document @&= '{ "other" : [ 0.0, NaN ] }';
SELECT document FROM helio_api.collection('db', 'dollaralltests') where document @&= '{ "other" : [ NaN ] }';

-- all with repeated elements
SELECT document FROM helio_api.collection('db', 'dollaralltests') where document @&= '{ "foo" : [ true, true, true, true ] }';
SELECT document FROM helio_api.collection('db', 'dollaralltests') where document @&= '{ "foo" : [ true, true, true, true, false ] }';
SELECT document FROM helio_api.collection('db', 'dollaralltests') where document @&= '{ "foo" : [[true], [true], [true], [true]] }';
SELECT document FROM helio_api.collection('db', 'dollaralltests') where document @&= '{ "foo" : [ false, false, false, false ] }';
SELECT document FROM helio_api.collection('db', 'dollaralltests') where document @&= '{ "foo" : [ true, false, true, false, true ] }';
SELECT document FROM helio_api.collection('db', 'dollaralltests') where document @&= '{ "foo" : [ 1, "a", "a", "a", "a", "a", 1 ] }';
SELECT document FROM helio_api.collection('db', 'dollaralltests') where document @&= '{ "foo" : [ 1, 1, 1, 1, 1, true ] }';

-- query for path inside array
SELECT document FROM helio_api.collection('db', 'dollaralltests') where document @&= '{ "array.x" : [ 1, 2 ] }';
SELECT document FROM helio_api.collection('db', 'dollaralltests') where document @&= '{ "array.x" : [ 2, 3 ] }';
SELECT document FROM helio_api.collection('db', 'dollaralltests') where document @&= '{ "array.x" : [ 3 ] }';
SELECT document FROM helio_api.collection('db', 'dollaralltests') where document @&= '{ "array.x" : [ 1, 3 ] }';
