set search_path to helio_core,helio_api,helio_api_catalog,pg_catalog;
SET citus.next_shard_id TO 410000;
SET helio_api.next_collection_id TO 4100;
SET helio_api.next_collection_index_id TO 4100;

\set prevEcho :ECHO
\set ECHO none
\o /dev/null
SELECT helio_api.drop_collection('db', 'dollarmodtests');
SELECT helio_api.create_collection('db', 'dollarmodtests');
-- avoid plans that use the primary key index
SELECT helio_distributed_test_helpers.drop_primary_key('db','dollarmodtests');
\o
\set ECHO :prevEcho

BEGIN;
set local enable_seqscan TO ON;
\i sql/bson_dollar_ops_query_mod_tests_core.sql
ROLLBACK;

-- Error case : Divisor Zero
SELECT document FROM helio_api.collection('db', 'dollarmodtests') WHERE document @@ '{ "a" : {"$mod" : [0,2]} }';

-- Error case : Less than 2 args
SELECT document FROM helio_api.collection('db', 'dollarmodtests') WHERE document @@ '{ "a" : {"$mod" : []} }';
SELECT document FROM helio_api.collection('db', 'dollarmodtests') WHERE document @@ '{ "a" : {"$mod" : [2]} }';

-- Error case : More than 2 args
SELECT document FROM helio_api.collection('db', 'dollarmodtests') WHERE document @@ '{ "a" : {"$mod" : [1,2,3]} }';
SELECT document FROM helio_api.collection('db', 'dollarmodtests') WHERE document @@ '{ "a" : {"$mod" : [-1,-2,3,4]} }';

-- Error case : NaN, Inf and overflow (Double & Decimal128) in Divisor
SELECT document FROM helio_api.collection('db', 'dollarmodtests') WHERE document @@ '{ "a" : {"$mod" : [{"$numberDecimal" : "9223372036854775808"}, 2]} }';
SELECT document FROM helio_api.collection('db', 'dollarmodtests') WHERE document @@ '{ "a" : {"$mod" : [{"$numberDecimal" : "NaN"}, 2]} }';
SELECT document FROM helio_api.collection('db', 'dollarmodtests') WHERE document @@ '{ "a" : {"$mod" : [{"$numberDecimal" : "Inf"}, 2]} }';
SELECT document FROM helio_api.collection('db', 'dollarmodtests') WHERE document @@ '{ "a" : {"$mod" : [{"$numberDecimal" : "-Inf"}, 2]} }';
SELECT document FROM helio_api.collection('db', 'dollarmodtests') WHERE document @@ '{ "a" : {"$mod" : [{"$numberDouble" : "9223372036854775808"}, 2]} }';
SELECT document FROM helio_api.collection('db', 'dollarmodtests') WHERE document @@ '{ "a" : {"$mod" : [{"$numberDouble" : "NaN"}, 0]} }';
SELECT document FROM helio_api.collection('db', 'dollarmodtests') WHERE document @@ '{ "a" : {"$mod" : [{"$numberDouble" : "Inf"}, 0]} }';
SELECT document FROM helio_api.collection('db', 'dollarmodtests') WHERE document @@ '{ "a" : {"$mod" : [{"$numberDouble" : "-Inf"}, 1]} }';

-- Error case : NaN, Inf and overflow (Double & Decimal128) in Remainder
SELECT document FROM helio_api.collection('db', 'dollarmodtests') WHERE document @@ '{ "a" : {"$mod" : [3, {"$numberDecimal" : "9223372036854775808"}]} }';
SELECT document FROM helio_api.collection('db', 'dollarmodtests') WHERE document @@ '{ "a" : {"$mod" : [3, {"$numberDecimal" : "NaN"}]} }';
SELECT document FROM helio_api.collection('db', 'dollarmodtests') WHERE document @@ '{ "a" : {"$mod" : [-3, {"$numberDecimal" : "Inf"}]} }';
SELECT document FROM helio_api.collection('db', 'dollarmodtests') WHERE document @@ '{ "a" : {"$mod" : [-3, {"$numberDecimal" : "-Inf"}]} }';
SELECT document FROM helio_api.collection('db', 'dollarmodtests') WHERE document @@ '{ "a" : {"$mod" : [3, {"$numberDouble" : "9223372036854775808"}]} }';
SELECT document FROM helio_api.collection('db', 'dollarmodtests') WHERE document @@ '{ "a" : {"$mod" : [3, {"$numberDouble" : "NaN"}]} }';
SELECT document FROM helio_api.collection('db', 'dollarmodtests') WHERE document @@ '{ "a" : {"$mod" : [-3, {"$numberDouble" : "Inf"}]} }';
SELECT document FROM helio_api.collection('db', 'dollarmodtests') WHERE document @@ '{ "a" : {"$mod" : [-3, {"$numberDouble" : "-Inf"}]} }';
