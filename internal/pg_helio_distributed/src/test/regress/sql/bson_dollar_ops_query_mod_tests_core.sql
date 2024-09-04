set search_path to helio_core,helio_api,helio_api_catalog,pg_catalog;

-- Basic
SELECT helio_api.insert_one('db','dollarmodtests','{"_id": 100, "a": 10}', NULL);
SELECT helio_api.insert_one('db','dollarmodtests','{"_id": 101, "a": [10, 11]}', NULL);

SELECT helio_api.insert_one('db','dollarmodtests','{"_id": 200, "a": -10}', NULL);
SELECT helio_api.insert_one('db','dollarmodtests','{"_id": 201, "a": [-10, -11]}', NULL);

-- nested and empty arrays. These docs should not be in result set as $mod can not be applied on arr of arr
SELECT helio_api.insert_one('db','dollarmodtests','{"_id": 300, "a": []}', NULL);
SELECT helio_api.insert_one('db','dollarmodtests','{"_id": 301, "a": [[]]}', NULL);
SELECT helio_api.insert_one('db','dollarmodtests','{"_id": 302, "a": [[[10, 11], [5]]]}', NULL);
SELECT helio_api.insert_one('db','dollarmodtests','{"_id": 303, "a": [[[-10, -11], [-5]]]}', NULL);

-- nested objects
SELECT helio_api.insert_one('db','dollarmodtests','{"_id": 102, "a": {"b": 10}}', NULL);
SELECT helio_api.insert_one('db','dollarmodtests','{"_id": 103, "a": {"b": [10, 11]}}', NULL);
SELECT helio_api.insert_one('db','dollarmodtests','{"_id": 104, "a": {"b": [10, 11], "c": 11}}', NULL);
SELECT helio_api.insert_one('db','dollarmodtests','{"_id": 105, "a": {"b": { "c": [10, 11] }}}', NULL);

SELECT helio_api.insert_one('db','dollarmodtests','{"_id": 202, "a": {"b": -10}}', NULL);
SELECT helio_api.insert_one('db','dollarmodtests','{"_id": 203, "a": {"b": [-10, -11]}}', NULL);
SELECT helio_api.insert_one('db','dollarmodtests','{"_id": 204, "a": {"b": [-10, -11], "c": -11}}', NULL);
SELECT helio_api.insert_one('db','dollarmodtests','{"_id": 205, "a": {"b": { "c": [-10, -11] }}}', NULL);

-- documents inside array
SELECT helio_api.insert_one('db','dollarmodtests','{"_id": 106, "a": [ {"b": 10}, {"c": 11}]}', NULL);
SELECT helio_api.insert_one('db','dollarmodtests','{"_id": 206, "a": [ {"b": -10}, {"c": -11}]}', NULL);

--various numeric types
SELECT helio_api.insert_one('db','dollarmodtests','{"_id": 107, "a": {"$numberInt" : "10"}}', NULL);
SELECT helio_api.insert_one('db','dollarmodtests','{"_id": 108, "a": {"$numberLong" : "10"}}', NULL);
SELECT helio_api.insert_one('db','dollarmodtests','{"_id": 109, "a": {"$numberDouble" : "10.4"}}', NULL);
SELECT helio_api.insert_one('db','dollarmodtests','{"_id": 110, "a": {"$numberDecimal" : "10.6"}}', NULL);

SELECT helio_api.insert_one('db','dollarmodtests','{"_id": 207, "a": {"$numberInt" : "-10"}}', NULL);
SELECT helio_api.insert_one('db','dollarmodtests','{"_id": 208, "a": {"$numberLong" : "-10"}}', NULL);
SELECT helio_api.insert_one('db','dollarmodtests','{"_id": 209, "a": {"$numberDouble" : "-10.4"}}', NULL);
SELECT helio_api.insert_one('db','dollarmodtests','{"_id": 210, "a": {"$numberDecimal" : "-10.6"}}', NULL);

-- Min Max Boundary
-- Int32
SELECT helio_api.insert_one('db','dollarmodtests','{"_id": 130, "a": {"$numberInt" : "2147483647"}}', NULL);
SELECT helio_api.insert_one('db','dollarmodtests','{"_id": 131, "a": {"$numberInt" : "-2147483648"}}', NULL);
-- Int64
SELECT helio_api.insert_one('db','dollarmodtests','{"_id": 132, "a": {"$numberLong" : "9223372036854775807"}}', NULL);
SELECT helio_api.insert_one('db','dollarmodtests','{"_id": 133, "a": {"$numberLong" : "-9223372036854775808"}}', NULL);
-- Double - (double only takes 15 significand digits)
SELECT helio_api.insert_one('db','dollarmodtests','{"_id": 134, "a": {"$numberDouble" : "922337203685477e4"}}', NULL);
SELECT helio_api.insert_one('db','dollarmodtests','{"_id": 135, "a": {"$numberDouble" : "-922337203685477e4"}}', NULL);
-- Decimal128
SELECT helio_api.insert_one('db','dollarmodtests','{"_id": 136, "a": {"$numberDecimal" : "9223372036854775807"}}', NULL);
SELECT helio_api.insert_one('db','dollarmodtests','{"_id": 137, "a": {"$numberDecimal" : "-9223372036854775808"}}', NULL);
-- Decimal128 - values that are more than 64 bits
SELECT helio_api.insert_one('db','dollarmodtests','{"_id": 140, "a": {"$numberDecimal" : "9223372036854775807.5"}}', NULL);
SELECT helio_api.insert_one('db','dollarmodtests','{"_id": 141, "a": {"$numberDecimal" : "9223372036854775808"}}', NULL);
SELECT helio_api.insert_one('db','dollarmodtests','{"_id": 142, "a": {"$numberDecimal" : "-9223372036854775808.5"}}', NULL);
SELECT helio_api.insert_one('db','dollarmodtests','{"_id": 143, "a": {"$numberDecimal" : "-9223372036854775809"}}', NULL);

-- non-numeric fields
SELECT helio_api.insert_one('db','dollarmodtests','{"_id": 310, "a": true}', NULL);
SELECT helio_api.insert_one('db','dollarmodtests','{"_id": 311, "a": false}', NULL);
SELECT helio_api.insert_one('db','dollarmodtests','{"_id": 312, "a": "Hello"}', NULL);
SELECT helio_api.insert_one('db','dollarmodtests','{"_id": 313, "a": ["Hello", "World"]}', NULL);
SELECT helio_api.insert_one('db','dollarmodtests','{"_id": 314, "a": { "$timestamp": { "t": 1234567890, "i": 1 }}}', NULL);
SELECT helio_api.insert_one('db','dollarmodtests','{"_id": 315, "a": { "$date": { "$numberLong" : "1234567890000" }}}', NULL);

-- objects with null and NaN
SELECT helio_api.insert_one('db','dollarmodtests','{"_id": 121, "a": 0}', NULL);
SELECT helio_api.insert_one('db','dollarmodtests','{"_id": 122, "a": null}', NULL);
SELECT helio_api.insert_one('db','dollarmodtests','{"_id": 123, "a": NaN}', NULL);
SELECT helio_api.insert_one('db','dollarmodtests','{"_id": 124, "a": [10, null]}', NULL);
SELECT helio_api.insert_one('db','dollarmodtests','{"_id": 125, "a": [null]}', NULL);
SELECT helio_api.insert_one('db','dollarmodtests','{"_id": 126, "a": [null, NaN]}', NULL);
SELECT helio_api.insert_one('db','dollarmodtests','{"_id": 127, "a": {"$numberDecimal" : "NaN"}}', NULL);

SELECT helio_api.insert_one('db','dollarmodtests','{"_id": 221, "a": [-10, NaN]}', NULL);
SELECT helio_api.insert_one('db','dollarmodtests','{"_id": 222, "a": [-0, -0.0]}', NULL);

-- Test for $mod with positive divisor and 0 remainder
SELECT document FROM helio_api.collection('db', 'dollarmodtests') where document @@ '{ "a" : {"$mod" : [5,0]} }';
SELECT document FROM helio_api.collection('db', 'dollarmodtests') where document @@ '{ "a.b" : {"$mod" : [5,0]} }';
SELECT document FROM helio_api.collection('db', 'dollarmodtests') where document @@ '{ "a.c" : {"$mod" : [5,0]} }';
SELECT document FROM helio_api.collection('db', 'dollarmodtests') where document @@ '{ "a.b.c" : {"$mod" : [5,0]} }';

-- Test for $mod with negative divisor and 0 remainder
SELECT document FROM helio_api.collection('db', 'dollarmodtests') where document @@ '{ "a" : {"$mod" : [-5,0]} }';
SELECT document FROM helio_api.collection('db', 'dollarmodtests') where document @@ '{ "a.b" : {"$mod" : [-5,0]} }';
SELECT document FROM helio_api.collection('db', 'dollarmodtests') where document @@ '{ "a.c" : {"$mod" : [-5,0]} }';
SELECT document FROM helio_api.collection('db', 'dollarmodtests') where document @@ '{ "a.b.c" : {"$mod" : [-5,0]} }';


-- Test for $mod with positive divisor and positive remainder
SELECT document FROM helio_api.collection('db', 'dollarmodtests') where document @@ '{ "a" : {"$mod" : [3,2]} }';
SELECT document FROM helio_api.collection('db', 'dollarmodtests') where document @@ '{ "a.b" : {"$mod" : [3,2]} }';
SELECT document FROM helio_api.collection('db', 'dollarmodtests') where document @@ '{ "a.c" : {"$mod" : [3,2]} }';
SELECT document FROM helio_api.collection('db', 'dollarmodtests') where document @@ '{ "a.b.c" : {"$mod" : [3,2]} }';

-- Test for $mod with positive divisor and negative remainder
SELECT document FROM helio_api.collection('db', 'dollarmodtests') where document @@ '{ "a" : {"$mod" : [3,-2]} }';
SELECT document FROM helio_api.collection('db', 'dollarmodtests') where document @@ '{ "a.b" : {"$mod" : [3,-2]} }';
SELECT document FROM helio_api.collection('db', 'dollarmodtests') where document @@ '{ "a.c" : {"$mod" : [3,-2]} }';
SELECT document FROM helio_api.collection('db', 'dollarmodtests') where document @@ '{ "a.b.c" : {"$mod" : [3,-2]} }';


-- Test for $mod with negative divisor and positive remainder
SELECT document FROM helio_api.collection('db', 'dollarmodtests') where document @@ '{ "a" : {"$mod" : [-3,2]} }';
SELECT document FROM helio_api.collection('db', 'dollarmodtests') where document @@ '{ "a.b" : {"$mod" : [-3,2]} }';
SELECT document FROM helio_api.collection('db', 'dollarmodtests') where document @@ '{ "a.c" : {"$mod" : [-3,2]} }';
SELECT document FROM helio_api.collection('db', 'dollarmodtests') where document @@ '{ "a.b.c" : {"$mod" : [-3,2]} }';

-- Test for $mod with negative divisor and negative remainder
SELECT document FROM helio_api.collection('db', 'dollarmodtests') where document @@ '{ "a" : {"$mod" : [-3,-2]} }';
SELECT document FROM helio_api.collection('db', 'dollarmodtests') where document @@ '{ "a.b" : {"$mod" : [-3,-2]} }';
SELECT document FROM helio_api.collection('db', 'dollarmodtests') where document @@ '{ "a.c" : {"$mod" : [-3,-2]} }';
SELECT document FROM helio_api.collection('db', 'dollarmodtests') where document @@ '{ "a.b.c" : {"$mod" : [-3,-2]} }';

-- Tests where Positive Divisor is different types of numeric
SELECT document FROM helio_api.collection('db', 'dollarmodtests') where document @@ '{ "a" : {"$mod" : [{"$numberInt" : "5"}, 0]} }';
SELECT document FROM helio_api.collection('db', 'dollarmodtests') where document @@ '{ "a" : {"$mod" : [{"$numberLong" : "-5"}, 0]} }';
SELECT document FROM helio_api.collection('db', 'dollarmodtests') where document @@ '{ "a" : {"$mod" : [{"$numberDouble" : "5.0"}, 0]} }';
SELECT document FROM helio_api.collection('db', 'dollarmodtests') where document @@ '{ "a" : {"$mod" : [{"$numberDecimal" : "-5.0"}, 0]} }';
SELECT document FROM helio_api.collection('db', 'dollarmodtests') where document @@ '{ "a" : {"$mod" : [{"$numberInt" : "3"}, 2]} }';
SELECT document FROM helio_api.collection('db', 'dollarmodtests') where document @@ '{ "a" : {"$mod" : [{"$numberLong" : "-3"}, -2]} }';
SELECT document FROM helio_api.collection('db', 'dollarmodtests') where document @@ '{ "a" : {"$mod" : [{"$numberDouble" : "3.0"}, 2]} }';
SELECT document FROM helio_api.collection('db', 'dollarmodtests') where document @@ '{ "a" : {"$mod" : [{"$numberDecimal" : "-3.0"}, -2]} }';

-- Tests where remainder is different types of numeric
SELECT document FROM helio_api.collection('db', 'dollarmodtests') where document @@ '{ "a" : {"$mod" : [3, {"$numberInt" : "-2"}]} }';
SELECT document FROM helio_api.collection('db', 'dollarmodtests') where document @@ '{ "a" : {"$mod" : [3, {"$numberLong" : "2"}]} }';
SELECT document FROM helio_api.collection('db', 'dollarmodtests') where document @@ '{ "a" : {"$mod" : [3, {"$numberDouble" : "-2.0"}]} }';
SELECT document FROM helio_api.collection('db', 'dollarmodtests') where document @@ '{ "a" : {"$mod" : [3, {"$numberDecimal" : "2.0"}]} }';

-- Tests where Divisor is not a decimal number
SELECT document FROM helio_api.collection('db', 'dollarmodtests') where document @@ '{ "a" : {"$mod" : [{"$numberDouble" : "5.2"}, 0]} }';
SELECT document FROM helio_api.collection('db', 'dollarmodtests') where document @@ '{ "a" : {"$mod" : [{"$numberDouble" : "-3.5"}, 2]} }';
SELECT document FROM helio_api.collection('db', 'dollarmodtests') where document @@ '{ "a" : {"$mod" : [{"$numberDouble" : "3.9"}, {"$numberDouble" : "-2.0"}]} }';

-- Tests for overflow check. If Dividend is INT_MAX and divisor is -1, mod operation should not overflow
SELECT document FROM helio_api.collection('db', 'dollarmodtests') where document @@ '{ "a" : {"$mod" : [{"$numberInt" : "1"}, 0]} }';
SELECT document FROM helio_api.collection('db', 'dollarmodtests') where document @@ '{ "a" : {"$mod" : [{"$numberInt" : "-1"}, 0]} }';
SELECT document FROM helio_api.collection('db', 'dollarmodtests') where document @@ '{ "a" : {"$mod" : [{"$numberLong" : "-1"}, 0]} }';
SELECT document FROM helio_api.collection('db', 'dollarmodtests') where document @@ '{ "a" : {"$mod" : [{"$numberDouble" : "-1.0"}, 0]} }';
SELECT document FROM helio_api.collection('db', 'dollarmodtests') where document @@ '{ "a" : {"$mod" : [{"$numberDecimal" : "-1.0"}, 0]} }';

-- Tests for overflow check. Divison if INT_MIN/MAX value
SELECT document FROM helio_api.collection('db', 'dollarmodtests') where document @@ '{ "a" : {"$mod" : [{"$numberInt" : "2147483647"}, 0]} }';
SELECT document FROM helio_api.collection('db', 'dollarmodtests') where document @@ '{ "a" : {"$mod" : [{"$numberInt" : "-2147483648"}, 0]} }';
SELECT document FROM helio_api.collection('db', 'dollarmodtests') where document @@ '{ "a" : {"$mod" : [{"$numberLong" : "9223372036854775807"}, 0]} }';
SELECT document FROM helio_api.collection('db', 'dollarmodtests') where document @@ '{ "a" : {"$mod" : [{"$numberLong" : "-9223372036854775808"}, 0]} }';
