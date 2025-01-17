
SELECT helio_api.insert_one('db','negation_tests', '{"_id": 1, "a" : { "b" : 0 }}', NULL);
SELECT helio_api.insert_one('db','negation_tests', '{"_id": 2, "a" : { "b" : 1 }}', NULL);
SELECT helio_api.insert_one('db','negation_tests', '{"_id": 3, "a" : { "b" : 2.0 }}', NULL);
SELECT helio_api.insert_one('db','negation_tests', '{"_id": 4, "a" : { "b" : null }}', NULL);
SELECT helio_api.insert_one('db','negation_tests', '{"_id": 5, "a" : { "b" : true }}', NULL);
SELECT helio_api.insert_one('db','negation_tests', '{"_id": 6, "a" : { "b" : { "$numberDouble": "Infinity" } }}', NULL);
SELECT helio_api.insert_one('db','negation_tests', '{"_id": 7, "a" : { "b" : { "$numberDouble": "NaN" } }}', NULL);

SELECT helio_api.insert_one('db','negation_tests', '{"_id": 8, "a" : { "b" : "someString" }}', NULL);
SELECT helio_api.insert_one('db','negation_tests', '{"_id": 9, "a" : { "b" : "earlierString" }}', NULL);
SELECT helio_api.insert_one('db','negation_tests', '{"_id": 10, "a" : { "b" : "xafterString" }}', NULL);

-- do a search
SELECT document FROM helio_api.collection('db', 'negation_tests') WHERE document @@ '{ "a.b": { "$gt": 2 }}';

-- do the complement
SELECT document FROM helio_api.collection('db', 'negation_tests') WHERE document @@ '{ "a.b": { "$lte": 2 }}';

-- do the NOTs
SELECT document FROM helio_api.collection('db', 'negation_tests') WHERE document @@ '{ "a.b": { "$not": { "$gt": 2 } }}';
SELECT document FROM helio_api.collection('db', 'negation_tests') WHERE document @@ '{ "a.b": { "$not": { "$lte": 2 } }}';

-- Now try $gte/$lt
SELECT document FROM helio_api.collection('db', 'negation_tests') WHERE document @@ '{ "a.b": { "$gte": 2 }}';
SELECT document FROM helio_api.collection('db', 'negation_tests') WHERE document @@ '{ "a.b": { "$lt": 2 }}';
SELECT document FROM helio_api.collection('db', 'negation_tests') WHERE document @@ '{ "a.b": { "$not": { "$gte": 2 } }}';
SELECT document FROM helio_api.collection('db', 'negation_tests') WHERE document @@ '{ "a.b": { "$not": { "$lt": 2 } }}';

-- $gte: Minkey (Exists doesn't factor in this)
SELECT document FROM helio_api.collection('db', 'negation_tests') WHERE document @@ '{ "a.b": { "$exists": true } }';
SELECT document FROM helio_api.collection('db', 'negation_tests') WHERE document @@ '{ "a.b": { "$exists": false } }';

SELECT document FROM helio_api.collection('db', 'negation_tests') WHERE document @@ '{ "a.b": { "$not": { "$exists": true } } }';
SELECT document FROM helio_api.collection('db', 'negation_tests') WHERE document @@ '{ "a.b": { "$not": { "$exists": false } } }';