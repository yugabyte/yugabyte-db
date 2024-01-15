
SET search_path TO helio_core;

CREATE TABLE test (document bson);

-- insert int32
INSERT INTO test (document) VALUES ('{"_id":"1", "value": { "$numberInt" : "11" }, "valueMax": { "$numberInt" : "2147483647" }, "valueMin": { "$numberInt" : "-2147483648" }}');

-- insert int64
INSERT INTO test (document) VALUES ('{"_id":"2", "value":{"$numberLong" : "134311"}, "valueMax": { "$numberLong" : "9223372036854775807" }, "valueMin": { "$numberLong" : "-9223372036854775808" }}');

-- insert double
INSERT INTO test (document) VALUES ('{"_id":"3", "value":{"$numberDouble" : "0"}, "valueMax": { "$numberDouble" : "1.7976931348623157E+308" }, "valueMin": { "$numberDouble" : "-1.7976931348623157E+308" }, "valueEpsilon": { "$numberDouble": "4.94065645841247E-324"}, "valueinfinity": {"$numberDouble":"Infinity"}}');

-- insert string
INSERT INTO test (document) VALUES ('{"_id":"4", "value": "A quick brown fox jumps over the lazy dog."}');

-- insert binary
INSERT INTO test (document) VALUES ('{"_id":"5", "value": {"$binary": { "base64": "U29tZVRleHRUb0VuY29kZQ==", "subType": "02"}}}');

-- minKey/maxKey
INSERT INTO test (document) VALUES ('{"_id":"6", "valueMin": { "$minKey": 1 }, "valueMax": { "$maxKey": 1 }}');

-- oid, date, time
INSERT INTO test (document) VALUES ('{"_id":"7", "tsField": {"$timestamp":{"t":1565545664,"i":1}}, "dateBefore1970": {"$date":{"$numberLong":"-1577923200000"}}, "dateField": {"$date":{"$numberLong":"1565546054692"}}, "oidField": {"$oid":"5d505646cf6d4fe581014ab2"}}');

-- array & nested object
INSERT INTO test (document) VALUES ('{"_id":"8", "arrayOfObject": [ { "ola": "ola"}, { "tudo bem?": "tudo bem!"}, { "o que tu fizeste essa semana?" : "nada" } ]}');

-- fetch all rows
SELECT * FROM test;

\copy test to 'test.bin' with (format 'binary')
CREATE TABLE tmp_test_table (LIKE test);
\copy tmp_test_table from 'test.bin' with (format 'binary')

-- verify that all records are same after serialization/deserialization
SELECT COUNT(*)=0 FROM (
    (TABLE test EXCEPT TABLE tmp_test_table)
    UNION
    (TABLE tmp_test_table EXCEPT TABLE test)
) q;

-- verify output via hex strings and json
BEGIN;
set local helio_core.bsonUseEJson TO true;
SELECT document FROM test;
ROLLBACK;

BEGIN;
set local helio_core.bsonUseEJson TO false;
SELECT document FROM test;
ROLLBACK;

BEGIN;
set local helio_core.bsonUseEJson TO true;
SELECT document FROM test;
ROLLBACK;

BEGIN;
set local helio_core.bsonUseEJson TO false;
SELECT document FROM test;
ROLLBACK;

BEGIN;
set local helio_core.bsonUseEJson TO true;
SELECT bson_hex_to_bson(bson_to_bson_hex(document)) FROM test;
ROLLBACK;

BEGIN;

-- test that hex strings can be coerced to bson (bson_in accepts both)
set local helio_core.bsonUseEJson TO true;
SELECT bson_to_bson_hex(document)::text::bson FROM test;
ROLLBACK;

BEGIN;
set local helio_core.bsonUseEJson TO false;
SELECT COUNT(1) FROM test WHERE bson_hex_to_bson(bson_out(document)) != document;
ROLLBACK;