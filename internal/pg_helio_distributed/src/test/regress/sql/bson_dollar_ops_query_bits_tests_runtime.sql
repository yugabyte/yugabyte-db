set search_path to helio_core,helio_api,helio_api_catalog,pg_catalog;
SET citus.next_shard_id TO 420000;
SET helio_api.next_collection_id TO 4200;
SET helio_api.next_collection_index_id TO 4200;

\set prevEcho :ECHO
\set ECHO none
\o /dev/null
SELECT helio_api.create_collection('db', 'bitwiseOperators');
SELECT helio_distributed_test_helpers.drop_primary_key('db','bitwiseOperators');
\o
\set ECHO :prevEcho

BEGIN;
set local enable_seqscan TO on;
\i sql/bson_dollar_ops_query_bits_tests_core.sql;
ROLLBACK;

--Negative Test Cases $bitsAllClear
SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAllClear" : -1  } }';
SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAllClear" : 23.04  } }';
SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAllClear" : "NegTest"  } }';
SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAllClear" : 99999999999 } }';
SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAllClear" : [ -1 ] } }';
SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAllClear" : [ 99999999999 ] } }';
SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAllClear" : [ "NegTest" ] } }';
SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAllClear" : { "$numberDecimal" : "99999999999" }} }';
SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAllClear" : { "$numberDecimal" : "1.0232" }} }';
SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAllClear" : { "$numberDecimal" : "-1" }} }';
SELECT helio_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "bitwiseOperators",
      "indexes": [
        {
          "key": {"c.d": 1},
          "name": "new_idx",
          "partialFilterExpression": {"a": {"$bitsAllClear": 1}}
        }
      ]
    }', TRUE);

--Negative Test Cases $bitsAnyClear
SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAnyClear" : -1  } }';
SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAnyClear" : 23.04  } }';
SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAnyClear" : "NegTest"  } }';
SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAnyClear" : 99999999999 } }';
SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAnyClear" : [ -1 ] } }';
SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAnyClear" : [ 99999999999 ] } }';
SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAnyClear" : [ "NegTest" ] } }';
SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAnyClear" : { "$numberDecimal" : "99999999999" }} }';
SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAnyClear" : { "$numberDecimal" : "1.0232" }} }';
SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAnyClear" : { "$numberDecimal" : "-1" }} }';
SELECT helio_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "bitwiseOperators",
      "indexes": [
        {
          "key": {"c.d": 1},
          "name": "new_idx",
          "partialFilterExpression": {"a": {"$bitsAnyClear": 1}}
        }
      ]
    }', TRUE);

--Negative Test Cases $bitsAllSet
SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAllSet" : -1  } }';
SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAllSet" : 23.04  } }';
SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAllSet" : "NegTest"  } }';
SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAllSet" : 99999999999 } }';
SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAllSet" : [ -1 ] } }';
SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAllSet" : [ 99999999999 ] } }';
SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAllSet" : [ "NegTest" ] } }';
SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAllSet" : { "$numberDecimal" : "99999999999" }} }';
SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAllSet" : { "$numberDecimal" : "1.0232" }} }';
SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAllSet" : { "$numberDecimal" : "-1" }} }';
SELECT helio_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "bitwiseOperators",
      "indexes": [
        {
          "key": {"c.d": 1},
          "name": "new_idx",
          "partialFilterExpression": {"a": {"$bitsAllSet": 1}}
        }
      ]
    }', TRUE);

--Negative Test Cases $bitsAnySet
SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAnySet" : -1  } }';
SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAnySet" : 23.04  } }';
SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAnySet" : "NegTest"  } }';
SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAnySet" : 99999999999 } }';
SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAnySet" : [ -1 ] } }';
SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAnySet" : [ 99999999999 ] } }';
SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAnySet" : [ "NegTest" ] } }';
SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAnySet" : { "$numberDecimal" : "99999999999" }} }';
SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAnySet" : { "$numberDecimal" : "1.0232" }} }';
SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAnySet" : { "$numberDecimal" : "-1" }} }';
SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAnySet" : { "$numberDecimal" : "NaN" }} }';
SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAllSet" : { "$numberDecimal" : "Infinity" }} }';

SELECT helio_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "bitwiseOperators",
      "indexes": [
        {
          "key": {"c.d": 1},
          "name": "new_idx",
          "partialFilterExpression": {"a": {"$bitsAnySet": 1}}
        }
      ]
    }', TRUE);

--drop collection
SELECT drop_collection('db','bitwiseOperators');