set search_path to helio_core,helio_api,helio_api_catalog,pg_catalog;

-- Tests on numbers.

SELECT helio_api.insert_one('db','bitwiseOperators','{"_id": 1, "a": 0}', NULL);
SELECT helio_api.insert_one('db','bitwiseOperators','{"_id": 2, "a": 1}', NULL);
SELECT helio_api.insert_one('db','bitwiseOperators','{"_id": 3, "a": 54}', NULL);
SELECT helio_api.insert_one('db','bitwiseOperators','{"_id": 4, "a": 88}', NULL);
SELECT helio_api.insert_one('db','bitwiseOperators','{"_id": 5, "a": 255}', NULL);

--Test with bitmask $bitsAllClear
SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAllClear" : 0 } }';
SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAllClear" : 1 } }';
SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAllClear" : 16 } }';
SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAllClear" : 129 } }';
SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAllClear" : 255 } }';
SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAllClear" : 12984 } }';

--Test with bitmask $bitsAnyClear
SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAnyClear" : 0 } }';
SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAnyClear" : 18 } }';
SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAnyClear" : 24 } }';
SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAnyClear" : 255 } }';

--Test with bitmask $bitsAllSet
SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAllSet" : 0 } }';
SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAllSet" : 1 } }';
SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAllSet" : 16 } }';
SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAllSet" : 54 } }';
SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAllSet" : 55 } }';
SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAllSet" : 88 } }';
SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAllSet" : 255 } }';

--Test with bitmask $bitsAnySet
SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAnySet" : 0 } }';
SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAnySet" : 9 } }';
SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAnySet" : 255 } }';

--Test with bit position $bitsAllClear

SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAllClear" : [] } }';
SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAllClear" : [ 0 ] } }';
SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAllClear" : [ 4 ] } }';
SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAllClear" : [ 1, 7 ] } }';
SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAllClear" : [ 0, 1, 2, 3, 4, 5, 6, 7  ] } }';

--Test with bit position $bitsAnyClear
SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAnyClear" : [  ] } }';
SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAnyClear" : [ 1, 4 ] } }';
SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAnyClear" : [ 3, 4 ] } }';
SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAnyClear" : [ 0, 1, 2, 3, 4, 5, 6, 7  ] } }';

--Test with bit position $bitsAllSet
SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAllSet" : [  ] } }';
SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAllSet" : [ 0 ] } }';
SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAllSet" : [ 4 ] } }';
SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAllSet" : [ 1, 2, 4, 5 ] } }';
SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAllSet" : [ 0, 1, 2, 4, 5 ] } }';
SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAllSet" : [ 3, 4, 6 ] } }';
SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAllSet" : [ 0, 1, 2, 3, 4, 5, 6, 7 ] } }';

--Test with bit position $bitsAnySet
SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAnySet" : [  ] } }';
SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAnySet" : [ 1, 3 ] } }';
SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAnySet" : [ 0, 1, 2, 3, 4, 5, 6, 7 ] } }';

--Test On negative Number
SELECT helio_api.delete('db', '{"delete":"bitwiseOperators", "deletes":[{"q":{},"limit":0}]}');
SELECT helio_api.insert_one('db','bitwiseOperators','{"_id": 6, "a": -0}', NULL);
SELECT helio_api.insert_one('db','bitwiseOperators','{"_id": 7, "a": -1}', NULL);
SELECT helio_api.insert_one('db','bitwiseOperators','{"_id": 8, "a": -54}', NULL);


--Test With BitMask $bitsAllClear
SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAllClear" : 0  } }';
SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAllClear" : 53 } }';
SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAllClear" : 127 } }';

--Test With BitMask $bitsAnyClear
SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAnyClear" : 0  } }';
SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAnyClear" : 53 } }';
SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAnyClear" : 127 } }';

--Test With BitMask $bitsAllSet
SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAllSet" : 0  } }';
SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAllSet" : 2 } }';
SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAllSet" : 127 } }';
SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAllSet" : 74 } }';

--Test With BitMask $bitsAnySet
SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAnySet" : 0  } }';
SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAnySet" : 2 } }';
SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAnySet" : 127 } }';


--Test With bit positions $bitsAllClear
SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAllClear" : [  ] } }';
SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAllClear" : [ 5, 4, 2, 0 ] } }';
SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAllClear" : [ 0, 1, 2, 3, 4, 5, 6, 7 ] } }';

--Test With bit positions $bitsAnyClear
SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAnyClear" : [  ] } }';
SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAnyClear" : [ 0, 2, 4, 5, 100 ] } }';
SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAnyClear" : [ 0, 1, 2, 3, 4, 5, 6, 7 ] } }';

--Test With bit positions $bitsAllSet
SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAllSet" : [  ] } }';
SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAllSet" : [ 1 ] } }';
SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAllSet" : [ 0, 1, 2, 3, 4, 5, 6, 7 ] } }';
SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAllSet" : [ 0, 7, 6, 3 ,100 ] } }';

--Test With bit positions $bitsAnySet
SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAnySet" : [  ] } }';
SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAnySet" : [ 1 ] } }';
SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAnySet" : [ 0, 1, 2, 3, 4, 5, 6, 7 ] } }';

--Test With bit positions with not int32 Type $bitsAllClear
SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAllClear" : [ { "$numberLong" : "5"}, { "$numberLong" : "4"}, { "$numberLong" : "2"}, { "$numberLong" : "0"} ] } }';
SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAllClear" : [ { "$numberDouble" : "5"}, { "$numberDouble" : "4"}, { "$numberDouble" : "2"}, { "$numberDouble" : "0"} ] } }';
SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAllClear" : [ { "$numberDecimal" : "5"}, { "$numberDecimal" : "4"}, { "$numberDecimal" : "2"}, { "$numberDecimal" : "0"} ] } }';

--Test With bit positions with not int32 Type $bitsAnyClear
SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAnyClear" : [ { "$numberLong" : "5"}, { "$numberLong" : "4"}, { "$numberLong" : "2"}, { "$numberLong" : "0"} ] } }';
SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAnyClear" : [ { "$numberDouble" : "5"}, { "$numberDouble" : "4"}, { "$numberDouble" : "2"}, { "$numberDouble" : "0"} ] } }';
SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAnyClear" : [ { "$numberDecimal" : "5"}, { "$numberDecimal" : "4"}, { "$numberDecimal" : "2"}, { "$numberDecimal" : "0"} ] } }';

--Test With bit positions with not int32 Type $bitsAllSet
SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAllSet" : [ { "$numberLong" : "5"}, { "$numberLong" : "4"}, { "$numberLong" : "2"}, { "$numberLong" : "0"} ] } }';
SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAllSet" : [ { "$numberDouble" : "5"}, { "$numberDouble" : "4"}, { "$numberDouble" : "2"}, { "$numberDouble" : "0"} ] } }';
SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAllSet" : [ { "$numberDecimal" : "5"}, { "$numberDecimal" : "4"}, { "$numberDecimal" : "2"}, { "$numberDecimal" : "0"} ] } }';

--Test With bit positions with not int32 Type $bitsAnySet
SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAnySet" : [ { "$numberLong" : "5"}, { "$numberLong" : "4"}, { "$numberLong" : "2"}, { "$numberLong" : "0"} ] } }';
SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAnySet" : [ { "$numberDouble" : "5"}, { "$numberDouble" : "4"}, { "$numberDouble" : "2"}, { "$numberDouble" : "0"} ] } }';
SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAnySet" : [ { "$numberDecimal" : "5"}, { "$numberDecimal" : "4"}, { "$numberDecimal" : "2"}, { "$numberDecimal" : "0"} ] } }';

--Test On BinData with different subtypes
SELECT helio_api.delete('db', '{"delete":"bitwiseOperators", "deletes":[{"q":{},"limit":0}]}');
SELECT helio_api.insert_one('db','bitwiseOperators','{"_id":"9", "a": {"$binary": { "base64": "AAAAAAAAAAAAAAAAAAAAAAAAAAAA", "subType": "01"}}}', NULL);
SELECT helio_api.insert_one('db','bitwiseOperators','{"_id":"10", "a": {"$binary": { "base64": "AANgAAAAAAAAAAAAAAAAAAAAAAAA", "subType": "01"}}}', NULL);
SELECT helio_api.insert_one('db','bitwiseOperators','{"_id":"11", "a": {"$binary": { "base64": "JANgqwetkqwklEWRbWERKKJREtbq", "subType": "01"}}}', NULL);
SELECT helio_api.insert_one('db','bitwiseOperators','{"_id":"12", "a": {"$binary": { "base64": "////////////////////////////", "subType": "01"}}}', NULL);
SELECT helio_api.insert_one('db','bitwiseOperators','{"_id":"13", "a": {"$binary": { "base64": "////////////////////////////", "subType": "02"}}}', NULL);
SELECT helio_api.insert_one('db','bitwiseOperators','{"_id":"14", "a": {"$binary": { "base64": "////////////////////////////", "subType": "03"}}}', NULL);


--Test with bin data $bitsAllClear
 SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAllClear" : { "$binary": { "base64": "AAAAAAAAAAAAAAAAAAAAAAAAAAAA", "subType": "01"} } } }';
 SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAllClear" : { "$binary": { "base64": "AAyfAAAAAAAAAAAAAAAAAAAAAAAA", "subType": "01"} } } }';
 SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAllClear" : { "$binary": { "base64": "JAyfqwetkqwklEWRbWERKKJREtbq", "subType": "01"} } } }';
 SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAllClear" : { "$binary": { "base64": "////////////////////////////", "subType": "01"} } } }';
 SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAllClear" : { "$binary": { "base64": "////////////////////////////", "subType": "02"} } } }';
 SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAllClear" : { "$binary": { "base64": "////////////////////////////", "subType": "03"} } } }';

--Test with bin data $bitsAnyClear
 SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAnyClear" : { "$binary": { "base64": "AAAAAAAAAAAAAAAAAAAAAAAAAAAA", "subType": "01"} } } }';
 SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAnyClear" : { "$binary": { "base64": "AANgAAAAAAAAAAAAAAAAAAAAAAAA", "subType": "01"} } } }';
 SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAnyClear" : { "$binary": { "base64": "JANgqwetkqwklEWRbWERKKJREtbq", "subType": "02"} } } }';
 SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAnyClear" : { "$binary": { "base64": "////////////////////////////", "subType": "03"} } } }';

--Test with bin data $bitsAllSet
 SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAllSet" : { "$binary": { "base64": "AAAAAAAAAAAAAAAAAAAAAAAAAAAA", "subType": "01"} } } }';
 SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAllSet" : { "$binary": { "base64": "AANgAAAAAAAAAAAAAAAAAAAAAAAA", "subType": "01"} } } }';
 SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAllSet" : { "$binary": { "base64": "JANgqwetkqwklEWRbWERKKJREtbq", "subType": "02"} } } }';
 SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAllSet" : { "$binary": { "base64": "////////////////////////////", "subType": "03"} } } }';

--Test with bin data $bitsAnySet
 SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAnySet" : { "$binary": { "base64": "AAAAAAAAAAAAAAAAAAAAAAAAAAAA", "subType": "01"} } } }';
 SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAnySet" : { "$binary": { "base64": "AAyfAAAAAAAAAAAAAAAAAAAAAAAA", "subType": "01"} } } }';
 SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAnySet" : { "$binary": { "base64": "JAyfqwetkqwklEWRbWERKKJREtbq", "subType": "02"} } } }';
 SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAnySet" : { "$binary": { "base64": "////////////////////////////", "subType": "03"} } } }';

 --Sign Extension Check
SELECT helio_api.delete('db', '{"delete":"bitwiseOperators", "deletes":[{"q":{},"limit":0}]}');
SELECT helio_api.insert_one('db','bitwiseOperators','{"_id": 6, "a": -2}', NULL);
SELECT helio_api.insert_one('db','bitwiseOperators','{"_id": 7, "a": 2}', NULL);
SELECT helio_api.insert_one('db','bitwiseOperators','{"_id": 8, "a": -1}', NULL);
SELECT helio_api.insert_one('db','bitwiseOperators','{"_id": 9, "a": -8}', NULL);
SELECT helio_api.insert_one('db','bitwiseOperators','{"_id": 10, "a": 5}', NULL);

--Sign Extension check with $bitsAllClear
SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAllClear" : [ 1000 ] } }';
SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAllClear" : [ 0 ] } }';
SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAllClear" : [ 0, 2000 ] } }';
SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAllClear" : [ 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63, 64, 65, 66, 67, 68, 69, 70] } }';

--Sign Extension check with $bitsAnyClear
SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAnyClear" : [ 1000 ] } }';
SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAnyClear" : [ 0 ] } }';
SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAnyClear" : [ 0, 2000 ] } }';
SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAnyClear" : [ 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63, 64, 65, 66, 67, 68, 69, 70] } }';

--Sign Extension check with $bitsAllSet
SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAllSet" : [ 1000 ] } }';
SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAllSet" : [ 0 ] } }';
SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAllSet" : [ 0, 2000 ] } }';
SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAllSet" : [ 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63, 64, 65, 66, 67, 68, 69, 70] } }';

--Sign Extension check with $bitsAnySet
SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAnySet" : [ 1000 ] } }';
SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAnySet" : [ 0 ] } }';
SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAnySet" : [ 0, 2000 ] } }';
SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAnySet" : [ 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63, 64, 65, 66, 67, 68, 69, 70] } }';

--Test when Document has neither bindata nor Number
SELECT helio_api.delete('db', '{"delete":"bitwiseOperators", "deletes":[{"q":{},"limit":0}]}');
SELECT helio_api.insert_one('db','bitwiseOperators','{"_id": 6, "a": "B"}', NULL);
SELECT helio_api.insert_one('db','bitwiseOperators','{"_id": 7, "a": 1.2832}', NULL);
SELECT helio_api.insert_one('db','bitwiseOperators','{"_id": 8, "a": "apple"}', NULL);
SELECT helio_api.insert_one('db','bitwiseOperators','{"_id": 9, "a": "A"}', NULL);

SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAllClear" : 1 } }';
SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAllClear" : 0 } }';
SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAllClear" : 2 } }';
SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAllClear" : 3 } }';
SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAllClear" : 10 } }';
SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAnyClear" : 3 } }';
SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAnyClear" : 10 } }';
SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAllSet" : 3 } }';
SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAllSet" : 10 } }';
SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAnySet" : 3 } }';
SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAnySet" : 10 } }';

--Test on Double , Int64, Decimal128
SELECT helio_api.delete('db', '{"delete":"bitwiseOperators", "deletes":[{"q":{},"limit":0}]}');
SELECT helio_api.insert_one('db','bitwiseOperators','{"_id": 1, "a": {"$numberDouble" : "1.00000000"}}', NULL);
SELECT helio_api.insert_one('db','bitwiseOperators','{"_id": 3, "a": {"$numberDouble" : "2.00000000"}}', NULL);
SELECT helio_api.insert_one('db','bitwiseOperators','{"_id": 5, "a": {"$numberLong" : "6"}}', NULL);
SELECT helio_api.insert_one('db','bitwiseOperators','{"_id": 6, "a": {"$numberLong" : "9999999999999999"}}', NULL);

--Test with bitmask $bitsAllClear
SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAllClear" : 0 } }';
SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAllClear" : 1 } }';
SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAllClear" : 2 } }';
SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAllClear" : 3 } }';

--Test with bitmask $bitsAnyClear
SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAnyClear" : 0 } }';
SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAnyClear" : 1 } }';
SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAnyClear" : 2 } }';
SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAnyClear" : 3 } }';

--Test with bitmask $bitsAllSet
SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAllSet" : 0 } }';
SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAllSet" : 1 } }';
SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAllSet" : 2 } }';
SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAllSet" : 3 } }';

--Test with bitmask $bitsAnySet
SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAnySet" : 0 } }';
SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAnySet" : 1 } }';
SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAnySet" : 2 } }';
SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAnySet" : 3 } }';

-- Tests on array.
SELECT helio_api.delete('db', '{"delete":"bitwiseOperators", "deletes":[{"q":{},"limit":0}]}');
SELECT helio_api.insert_one('db','bitwiseOperators','{"_id": 1, "a": [10,12,14,16]}', NULL);
SELECT helio_api.insert_one('db','bitwiseOperators','{"_id": 2, "a": [10,12,14,16,1]}', NULL);
SELECT helio_api.insert_one('db','bitwiseOperators','{"_id": 3, "a": [1,3,5,7]}', NULL);
SELECT helio_api.insert_one('db','bitwiseOperators','{"_id": 4, "a": [1,3,4]}', NULL);
SELECT helio_api.insert_one('db','bitwiseOperators','{"_id": 5, "a": [10.01, "hello", "Test", 11, 2]}', NULL);
SELECT helio_api.insert_one('db','bitwiseOperators','{"_id": 6, "a": [10.01, "hello", "Test"]}', NULL);
SELECT helio_api.insert_one('db','bitwiseOperators','{"_id": 7, "a": ["hello", "Test"]}', NULL);
SELECT helio_api.insert_one('db','bitwiseOperators','{"_id": 8, "a": [10.11, 11.12, 13.24]}', NULL);
SELECT helio_api.insert_one('db','bitwiseOperators','{"_id": 9, "a": [10.0, 11.0, 13.0]}', NULL);
SELECT helio_api.insert_one('db','bitwiseOperators','{"_id": 10, "a": [1, 1, 1, 1]}', NULL);

--Test with bitmask $bitsAllClear
SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAllClear" : 0 } }';
SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAllClear" : 1 } }';
SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a":{ "$elemMatch" : { "$bitsAllClear" : 1, "$gt" : 5 } } }';
SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a":{ "$elemMatch" : { "$bitsAllClear" : 1, "$lte" : 5 } } }';

--Test with bitmask $bitsAnyClear
SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAnyClear" : 0 } }';
SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAnyClear" : 1 } }';
SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a":{ "$elemMatch" : { "$bitsAnyClear" : 1, "$gt" : 5 } } }';
SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a":{ "$elemMatch" : { "$bitsAnyClear" : 1, "$lte" : 5 } } }';

--Test with bitmask $bitsAllSet
SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAllSet" : 0 } }';
SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAllSet" : 1 } }';
SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a":{ "$elemMatch" : { "$bitsAllSet" : 1, "$gt" : 5 } } }';
SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a":{ "$elemMatch" : { "$bitsAllSet" : 1, "$lte" : 5 } } }';

--Test with bitmask $bitsAnySet
SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAnySet" : 0 } }';
SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAnySet" : 1 } }';
SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a":{ "$elemMatch" : { "$bitsAnySet" : 1, "$gt" : 5 } } }';
SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a":{ "$elemMatch" : { "$bitsAnySet" : 1, "$lte" : 5 } } }';

--insert multiple field in document
SELECT helio_api.delete('db', '{"delete":"bitwiseOperators", "deletes":[{"q":{},"limit":0}]}');
SELECT helio_api.insert_one('db','bitwiseOperators','{"_id": 1, "a": 0, "b":3}', NULL);
SELECT helio_api.insert_one('db','bitwiseOperators','{"_id": 2, "a": 1, "b":2}', NULL);
SELECT helio_api.insert_one('db','bitwiseOperators','{"_id": 3, "a": 2, "b":1}', NULL);
SELECT helio_api.insert_one('db','bitwiseOperators','{"_id": 3, "a": 3, "b":0}', NULL);

--Test with non exist field or on multi field document $bitsAllClear
SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAllClear" : 0 } }';
SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "c": { "$bitsAllClear" : 2 } }';

--Test with non exist field or on multi field document $bitsAllClear
SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAnyClear" : 0 } }';
SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "c": { "$bitsAnyClear" : 2 } }';

--Test with non exist field or on multi field document $bitsAllSet
SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAllSet" : 0 } }';
SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "c": { "$bitsAllSet" : 2 } }';

--Test with non exist field or on multi field document $bitsAnySet
SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAnySet" : 0 } }';
SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "c": { "$bitsAnySet" : 2 } }';

--Test on array of array
SELECT helio_api.delete('db', '{"delete":"bitwiseOperators", "deletes":[{"q":{},"limit":0}]}');
SELECT helio_api.insert_one('db','bitwiseOperators','{"_id": 1, "a": [[1,2,3],[3,2,1]]}', NULL);
SELECT helio_api.insert_one('db','bitwiseOperators','{"_id": 2, "a": [[1,3,5],[5,3,1]]}', NULL);
SELECT helio_api.insert_one('db','bitwiseOperators','{"_id": 3, "a": [[0,2,4],[4,2,0]]}', NULL);
SELECT helio_api.insert_one('db','bitwiseOperators','{"_id": 3, "a": [[0,0,0],[0,0,0]]}', NULL);

--Test with bitmask $bitsAllClear
SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAllClear" : 0 } }';
SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAllClear" : 1 } }';
SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAllClear" : 2 } }';

--Test with bitmask $bitsAnyClear
SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAnyClear" : 0 } }';
SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAnyClear" : 1 } }';
SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAnyClear" : 2 } }';

--Test with bitmask $bitsAllSet
SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAnyClear" : 0 } }';
SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAnyClear" : 1 } }';
SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAnyClear" : 2 } }';

--Test with bitmask $bitsAnySet
SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAnySet" : 0 } }';
SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAnySet" : 1 } }';
SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAnySet" : 2 } }';

--Multiple Bits Operator Test
SELECT helio_api.delete('db', '{"delete":"bitwiseOperators", "deletes":[{"q":{},"limit":0}]}');
SELECT helio_api.insert_one('db','bitwiseOperators','{"_id": 1, "a": 0}', NULL);
SELECT helio_api.insert_one('db','bitwiseOperators','{"_id": 2, "a": 1}', NULL);
SELECT helio_api.insert_one('db','bitwiseOperators','{"_id": 3, "a": 2}', NULL);
SELECT helio_api.insert_one('db','bitwiseOperators','{"_id": 4, "a": 3}', NULL);
SELECT helio_api.insert_one('db','bitwiseOperators','{"_id": 5, "a": 4}', NULL);
SELECT helio_api.insert_one('db','bitwiseOperators','{"_id": 6, "a": 5}', NULL);
SELECT helio_api.insert_one('db','bitwiseOperators','{"_id": 7, "a": 6}', NULL);
SELECT helio_api.insert_one('db','bitwiseOperators','{"_id": 8, "a": 7}', NULL);
SELECT helio_api.insert_one('db','bitwiseOperators','{"_id": 9, "a": 8}', NULL);
SELECT helio_api.insert_one('db','bitwiseOperators','{"_id": 10, "a": 9}', NULL);

SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAnySet" : 1, "$bitsAllSet" : 1 } }';
SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAnyClear" : 1, "$bitsAllClear" : 1 } }';
SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAnySet" : 3, "$bitsAnyClear" : 3 } }';

--Decimal 128 Test
SELECT helio_api.delete('db', '{"delete":"bitwiseOperators", "deletes":[{"q":{},"limit":0}]}');
SELECT helio_api.insert_one('db','bitwiseOperators','{"_id": 0, "a": { "$numberDecimal" : "0" }}', NULL);
SELECT helio_api.insert_one('db','bitwiseOperators','{"_id": 1, "a": { "$numberDecimal" : "1" }}', NULL);
SELECT helio_api.insert_one('db','bitwiseOperators','{"_id": 2, "a": { "$numberDecimal" : "1.1234" }}', NULL);
SELECT helio_api.insert_one('db','bitwiseOperators','{"_id": 3, "a": { "$numberDecimal" : "1.0000" }}', NULL);
SELECT helio_api.insert_one('db','bitwiseOperators','{"_id": 4, "a": { "$numberDecimal" : "2.0020" }}', NULL);
SELECT helio_api.insert_one('db','bitwiseOperators','{"_id": 5, "a": { "$numberDecimal" : "3.0000" }}', NULL);
SELECT helio_api.insert_one('db','bitwiseOperators','{"_id": 6, "a": { "$numberDecimal" : "NaN" }}', NULL);
SELECT helio_api.insert_one('db','bitwiseOperators','{"_id": 7, "a": { "$numberDecimal" : "Infinity" }}', NULL);

SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAllSet" : [ { "$numberDecimal"  : "0" }, { "$numberDecimal"  : "1" }] } }';
SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAllSet" : [ { "$numberDecimal"  : "0.112"}, { "$numberDecimal"  : "1.321" }] } }';
SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAllClear" : { "$numberDecimal"  : "1"} } }';
SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAnyClear" : { "$numberDecimal"  : "1.000"} } }';
SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAnySet" : { "$numberDecimal"  : "1.000"} } }';
SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAllClear" : { "$numberDecimal"  : "0"} } }';
SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAnyClear" : { "$numberDecimal"  : "0.000"} } }';
SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAnySet" : { "$numberDecimal"  : "0.000"} } }';


-- double range test
SELECT helio_api.delete('db', '{"delete":"bitwiseOperators", "deletes":[{"q":{},"limit":0}]}');
SELECT helio_api.insert_one('db','bitwiseOperators','{ "_id": "double-whole", "a" : {  "$numberDouble" : "42.0" } }', NULL);
SELECT helio_api.insert_one('db','bitwiseOperators','{ "_id": "double-zero", "a" : {  "$numberDouble" : "0.0"} }', NULL);
SELECT helio_api.insert_one('db','bitwiseOperators','{ "_id": "double-big", "a" : {  "$numberDouble" : "2305843009213693952.0"} }', NULL);
SELECT helio_api.insert_one('db','bitwiseOperators','{ "_id": "double-1", "a" : {  "$numberDouble" : "-9223372036854775808"} }', NULL);
SELECT helio_api.insert_one('db','bitwiseOperators','{ "_id": "double-2", "a" : {  "$numberDouble" : "-123456789"} }', NULL);
SELECT helio_api.insert_one('db','bitwiseOperators','{ "_id": "double-3", "a" : {  "$numberDouble" : "123456789"} }', NULL);
SELECT helio_api.insert_one('db','bitwiseOperators','{ "_id": "double-4", "a" : {  "$numberDouble" : "9223372036854775807"} }', NULL);
SELECT helio_api.insert_one('db','bitwiseOperators','{ "_id": "int64-max", "a" : {  "$numberDouble" : "9223372036854775807"}}', NULL);
SELECT helio_api.insert_one('db','bitwiseOperators','{ "_id": "double-max", "a" : {  "$numberDouble" : "1.7976931348623157e+308"} }', NULL);
SELECT helio_api.insert_one('db','bitwiseOperators','{ "_id": "double-smallest", "a" : {  "$numberDouble" : "5e-324"} }', NULL);

SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAllSet" : { "$numberDecimal"  : "0" } } }';
SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAllClear" : [1,5] } }';

-- decimal range test
SELECT helio_api.delete('db', '{"delete":"bitwiseOperators", "deletes":[{"q":{},"limit":0}]}');
SELECT helio_api.insert_one('db','bitwiseOperators','{ "_id": "decimal-whole", "a" : {  "$numberDecimal" : "42.0" } }', NULL);
SELECT helio_api.insert_one('db','bitwiseOperators','{ "_id": "decimal-zero", "a" : {  "$numberDecimal" : "0.0"} }', NULL);
SELECT helio_api.insert_one('db','bitwiseOperators','{ "_id": "decimal-big", "a" : {  "$numberDecimal" : "2305843009213693952.0"} }', NULL);
SELECT helio_api.insert_one('db','bitwiseOperators','{ "_id": "decimal-1", "a" : {  "$numberDecimal" : "-9223372036854775808"} }', NULL);
SELECT helio_api.insert_one('db','bitwiseOperators','{ "_id": "decimal-2", "a" : {  "$numberDecimal" : "-123456789"} }', NULL);
SELECT helio_api.insert_one('db','bitwiseOperators','{ "_id": "decimal-3", "a" : {  "$numberDecimal" : "123456789"} }', NULL);
SELECT helio_api.insert_one('db','bitwiseOperators','{ "_id": "decimal-4", "a" : {  "$numberDecimal" : "9223372036854775807"} }', NULL);
SELECT helio_api.insert_one('db','bitwiseOperators','{ "_id": "int64-max", "a" : {  "$numberDecimal" : "9223372036854775807"}}', NULL);
SELECT helio_api.insert_one('db','bitwiseOperators','{ "_id": "double-max", "a" : {  "$numberDecimal" : "1.7976931348623157e+308"} }', NULL);
SELECT helio_api.insert_one('db','bitwiseOperators','{ "_id": "double-smallest", "a" : {  "$numberDecimal" : "5e-324"} }', NULL);

SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAllSet" : { "$numberDecimal"  : "0" } } }';
SELECT document FROM helio_api.collection('db', 'bitwiseOperators') WHERE document @@ '{ "a": { "$bitsAllClear" : [1,5] } }';



