/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

--
-- AGTYPE data type regression tests
--

--
-- Load extension and set path
--
LOAD 'age';
SET extra_float_digits = 0;
SET search_path TO ag_catalog;

--
-- Create a table using the AGTYPE type
--
CREATE TABLE agtype_table (type text, agtype agtype);

--
-- Insert values to exercise agtype_in/agtype_out
--
INSERT INTO agtype_table VALUES ('bool', 'true');
INSERT INTO agtype_table VALUES ('bool', 'false');

INSERT INTO agtype_table VALUES ('null', 'null');

INSERT INTO agtype_table VALUES ('string', '""');
INSERT INTO agtype_table VALUES ('string', '"This is a string"');

INSERT INTO agtype_table VALUES ('integer', '0');
INSERT INTO agtype_table VALUES ('integer', '9223372036854775807');
INSERT INTO agtype_table VALUES ('integer', '-9223372036854775808');

INSERT INTO agtype_table VALUES ('float', '0.0');
INSERT INTO agtype_table VALUES ('float', '1.0');
INSERT INTO agtype_table VALUES ('float', '-1.0');
INSERT INTO agtype_table VALUES ('float', '100000000.000001');
INSERT INTO agtype_table VALUES ('float', '-100000000.000001');
INSERT INTO agtype_table VALUES ('float', '0.00000000000000012345');
INSERT INTO agtype_table VALUES ('float', '-0.00000000000000012345');

INSERT INTO agtype_table VALUES ('numeric', '100000000000.0000000000001::numeric');
INSERT INTO agtype_table VALUES ('numeric', '-100000000000.0000000000001::numeric');

INSERT INTO agtype_table VALUES ('integer array',
	'[-9223372036854775808, -1, 0, 1, 9223372036854775807]');
INSERT INTO agtype_table VALUES('float array',
	'[-0.00000000000000012345, -100000000.000001, -1.0, 0.0, 1.0, 100000000.000001, 0.00000000000000012345]');
INSERT INTO agtype_table VALUES('mixed array', '[true, false, null, "string", 1, 1.0, {"bool":true}, -1::numeric, [1,3,5]]');

INSERT INTO agtype_table VALUES('object', '{"bool":true, "null":null, "string":"string", "integer":1, "float":1.2, "arrayi":[-1,0,1], "arrayf":[-1.0, 0.0, 1.0], "object":{"bool":true, "null":null, "string":"string", "int":1, "float":8.0}}');
INSERT INTO agtype_table VALUES ('numeric array',
        '[-5::numeric, -1::numeric, 0::numeric, 1::numeric, 9223372036854775807::numeric]');

--
-- Special float values: NaN, +/- Infinity
--
INSERT INTO agtype_table VALUES ('float  nan', 'nan');
INSERT INTO agtype_table VALUES ('float  Infinity', 'Infinity');
INSERT INTO agtype_table VALUES ('float -Infinity', '-Infinity');
INSERT INTO agtype_table VALUES ('float  inf', 'inf');
INSERT INTO agtype_table VALUES ('float -inf', '-inf');

SELECT * FROM agtype_table;

--
-- These should fail
--
INSERT INTO agtype_table VALUES ('bad integer', '9223372036854775808');
INSERT INTO agtype_table VALUES ('bad integer', '-9223372036854775809');
INSERT INTO agtype_table VALUES ('bad float', '-NaN');
INSERT INTO agtype_table VALUES ('bad float', 'Infi');
INSERT INTO agtype_table VALUES ('bad float', '-Infi');

--
-- Test agtype mathematical operator functions
-- +, -, unary -, *, /, %, and ^
--
SELECT agtype_add('1', '-1');
SELECT agtype_add('1', '-1.0');
SELECT agtype_add('1.0', '-1');
SELECT agtype_add('1.0', '-1.0');
SELECT agtype_add('1', '-1.0::numeric');
SELECT agtype_add('1.0', '-1.0::numeric');
SELECT agtype_add('1::numeric', '-1.0::numeric');

SELECT agtype_sub('-1', '-1');
SELECT agtype_sub('-1', '-1.0');
SELECT agtype_sub('-1.0', '-1');
SELECT agtype_sub('-1.0', '-1.0');
SELECT agtype_sub('1', '-1.0::numeric');
SELECT agtype_sub('1.0', '-1.0::numeric');
SELECT agtype_sub('1::numeric', '-1.0::numeric');
SELECT agtype_sub('[1, 2, 3]', '1');
SELECT agtype_sub('{"a": 1, "b": 2, "c": 3}', '"a"');

SELECT agtype_neg('-1');
SELECT agtype_neg('-1.0');
SELECT agtype_neg('0');
SELECT agtype_neg('0.0');
SELECT agtype_neg('0::numeric');
SELECT agtype_neg('-1::numeric');
SELECT agtype_neg('1::numeric');

SELECT agtype_mul('-2', '3');
SELECT agtype_mul('2', '-3.0');
SELECT agtype_mul('-2.0', '3');
SELECT agtype_mul('2.0', '-3.0');
SELECT agtype_mul('-2', '3::numeric');
SELECT agtype_mul('2.0', '-3::numeric');
SELECT agtype_mul('-2.0::numeric', '3::numeric');

SELECT agtype_div('-4', '3');
SELECT agtype_div('4', '-3.0');
SELECT agtype_div('-4.0', '3');
SELECT agtype_div('4.0', '-3.0');
SELECT agtype_div('4', '-3.0::numeric');
SELECT agtype_div('-4.0', '3::numeric');
SELECT agtype_div('4.0::numeric', '-3::numeric');

SELECT agtype_mod('-11', '3');
SELECT agtype_mod('11', '-3.0');
SELECT agtype_mod('-11.0', '3');
SELECT agtype_mod('11.0', '-3.0');
SELECT agtype_mod('11', '-3.0::numeric');
SELECT agtype_mod('-11.0', '3::numeric');
SELECT agtype_mod('11.0::numeric', '-3::numeric');

SELECT agtype_pow('-2', '3');
SELECT agtype_pow('2', '-1.0');
SELECT agtype_pow('2.0', '3');
SELECT agtype_pow('2.0', '-1.0');
SELECT agtype_pow('2::numeric', '3');
SELECT agtype_pow('2::numeric', '-1.0');
SELECT agtype_pow('-2', '3::numeric');
SELECT agtype_pow('2.0', '-1.0::numeric');
SELECT agtype_pow('2.0::numeric', '-1.0::numeric');

--
-- Test overloaded agtype any mathematical operator functions
-- +, -, *, /, and %
--
SELECT agtype_any_add('1', -1);
SELECT agtype_any_add('1.0', -1);
SELECT agtype_any_add('1::numeric', 1);
SELECT agtype_any_add('1.0::numeric', 1);

SELECT agtype_any_sub('1', -1);
SELECT agtype_any_sub('1.0', -1);
SELECT agtype_any_sub('1::numeric', 1);
SELECT agtype_any_sub('1.0::numeric', 1);

SELECT agtype_any_mul('-2', 3);
SELECT agtype_any_mul('2.0', -3);
SELECT agtype_any_mul('-2::numeric', 3);
SELECT agtype_any_mul('-2.0::numeric', 3);

SELECT agtype_any_div('-4', 3);
SELECT agtype_any_div('4.0', -3);
SELECT agtype_any_div('-4::numeric', 3);
SELECT agtype_any_div('-4.0::numeric', 3);

SELECT agtype_any_mod('-11', 3);
SELECT agtype_any_mod('11.0', -3);
SELECT agtype_any_mod('-11::numeric', 3);
SELECT agtype_any_mod('-11.0::numeric', 3);
--
-- Should fail with divide by zero
--
SELECT agtype_div('1', '0');
SELECT agtype_div('1', '0.0');
SELECT agtype_div('1.0', '0');
SELECT agtype_div('1.0', '0.0');
SELECT agtype_div('1', '0::numeric');
SELECT agtype_div('1.0', '0::numeric');
SELECT agtype_div('1::numeric', '0');
SELECT agtype_div('1::numeric', '0.0');
SELECT agtype_div('1::numeric', '0::numeric');

SELECT agtype_any_div('1', 0);
SELECT agtype_any_div('1.0', 0);
SELECT agtype_any_div('-1::numeric', 0);
SELECT agtype_any_div('-1.0::numeric', 0);

--
-- Should get Infinity
--
SELECT agtype_pow('0', '-1');
SELECT agtype_pow('-0.0', '-1');

--
-- Should get - ERROR:  zero raised to a negative power is undefined
--
SELECT agtype_pow('0', '-1::numeric');
SELECT agtype_pow('-0.0', '-1::numeric');
SELECT agtype_pow('0::numeric', '-1');
SELECT agtype_pow('-0.0::numeric', '-1');
SELECT agtype_pow('-0.0::numeric', '-1');

--
-- Test operators +, -, unary -, *, /, %, and ^
--
SELECT '3.14'::agtype + '3.14'::agtype;
SELECT '3.14'::agtype - '3.14'::agtype;
SELECT -'3.14'::agtype;
SELECT '3.14'::agtype * '3.14'::agtype;
SELECT '3.14'::agtype / '3.14'::agtype;
SELECT '3.14'::agtype % '3.14'::agtype;
SELECT '3.14'::agtype ^ '2'::agtype;
SELECT '3'::agtype + '3'::agtype;
SELECT '3'::agtype + '3.14'::agtype;
SELECT '3'::agtype + '3.14::numeric'::agtype;
SELECT '3.14'::agtype + '3.14::numeric'::agtype;
SELECT '3.14::numeric'::agtype + '3.14::numeric'::agtype;

--
-- Test operator - for extended functionality
--
SELECT '{"a":1 , "b":2, "c":3}'::agtype - '"a"';
SELECT '{"a":null , "b":2, "c":3}'::agtype - '"a"';
SELECT '{"a":1 , "b":2, "c":3}'::agtype - '"b"';
SELECT '{"a":1 , "b":2, "c":3}'::agtype - '"c"';
SELECT '{"a":1 , "b":2, "c":3}'::agtype - '"d"';
SELECT '{"a":1 , "b":2, "c":3}'::agtype - '""';
SELECT '{"a":1 , "b":2, "c":3}'::agtype - '"1"';
SELECT '{"a":1 , "b":2, "c":3, "1": 4}'::agtype - '"1"';
SELECT '{"a":1 , "b":2, "c":3}'::agtype - age_tostring('a');
SELECT '{"a":1 , "b":2, "c":3}'::agtype - age_tostring(1);
SELECT '{"a":1 , "b":2, "c":3, "1": 4}'::agtype - age_tostring(1);
SELECT '{}'::agtype - '"a"';

SELECT '["a","b","c"]'::agtype - 3;
SELECT '["a","b","c"]'::agtype - 2;
SELECT '["a","b","c"]'::agtype - 1;
SELECT '["a","b","c"]'::agtype - 0;
SELECT '["a","b","c"]'::agtype - -1;
SELECT '["a","b","c"]'::agtype - -2;
SELECT '["a","b","c"]'::agtype - -3;
SELECT '["a","b","c"]'::agtype - -4;
SELECT '["a","b","c"]'::agtype - '2';
SELECT '["a","b","c"]'::agtype - -(true::int);
SELECT '[]'::agtype - 1;

SELECT '{"a":1 , "b":2, "c":3}'::agtype - '["b"]'::agtype;
SELECT '{"a":1 , "b":2, "c":3}'::agtype - '["c","b"]'::agtype;
SELECT '{"a":1 , "b":2, "c":3}'::agtype - '[]'::agtype;
SELECT '["a","b","c"]'::agtype - '[]';
SELECT '["a","b","c"]'::agtype - '[1]';
SELECT '[1, 2, 3, 4, 5, 6]'::agtype - '[9]';
SELECT '["a","b","c"]'::agtype - '[1, -1]';
SELECT '["a","b","c"]'::agtype - '[1, -1, 3, 4]';
SELECT '["a","b","c"]'::agtype - '[1, -1, 3, 4, 0]';
SELECT '["a","b","c"]'::agtype - '[-1, 1, 3, 4, 1]';
SELECT '["a","b","c"]'::agtype - '[-1, 1, 3, 4, 0]';
SELECT '["a","b","c"]'::agtype - '[1, 1]';
SELECT '["a","b","c"]'::agtype - '[1, 1, 1]';
SELECT '["a","b","c"]'::agtype - '[1, 1, -1]';
SELECT '["a","b","c"]'::agtype - '[1, 1, -1, -1]';
SELECT '["a","b","c"]'::agtype - '[-2, -4, -5, -1]';
SELECT '[1, 2, 3, 4, 5, 6]'::agtype - '[0, 4, 3, 2]';
SELECT '[1, 2, 3, 4, 5, 6]'::agtype - '[0, 4, 3, 2, -1]';
SELECT '[1, 2, 3, 4, 5, 6]'::agtype - '[3, 3, 4, 4, 6, 8, 9]';
SELECT '[1, 2, 3, 4, 5, 6]'::agtype - '[8, 9, -7, -6]';

-- multiple sub operations
SELECT '{"a":1 , "b":2, "c":3, "1": 4}'::agtype - age_tostring(1) - age_tostring(1);
SELECT '{"a":1 , "b":2, "c":3, "1": 4}'::agtype - age_tostring(1) - age_tostring('a');
SELECT '{"a":1 , "b":2, "c":3, "1": 4}'::agtype - age_tostring(1) - age_tostring('a') - age_tostring('e') - age_tostring('c');
SELECT '{"a":1 , "b":2, "c":3}'::agtype - '["c","b"]' - '["a"]';
SELECT '{"a":1 , "b":2, "c":3}'::agtype - '["c","b"]' - '["e"]' - '["a"]';
SELECT '{"a":1 , "b":2, "c":3}'::agtype - '["c","b"]' - '[]';
SELECT '["a","b","c"]'::agtype - '[-1]' - '[-1]';
SELECT '["a","b","c"]'::agtype - '[-1]' - '[-2]' - '[-2]';
SELECT '["a","b","c"]'::agtype - '[-1]' - '[]' - '[-2]';
SELECT '["a","b","c"]'::agtype - '[-1]' - '[4]' - '[-2]';
SELECT '[1, 2, 3, 4, 5, 6]'::agtype - '[8, 9, -7, -6]' - '1';
SELECT '[1, 2, 3, 4, 5, 6]'::agtype - '[8, 9, -7, -6]' - '[1, 0]';
SELECT '[1, 2, 3, 4, 5, 6]'::agtype - '[8, 9, -7, -6]' - 3 - '[]';

-- errors out
SELECT '["a","b","c"]'::agtype - '["1"]';
SELECT '["a","b","c"]'::agtype - '[null]';
SELECT '["a","b","c"]'::agtype - '"1"';
SELECT '["a","b","c"]'::agtype - 'null';
SELECT '["a","b","c"]'::agtype - '[-1]' - '["-2"]' - '[-2]';
SELECT '{"a":1 , "b":2, "c":3}'::agtype - '[1]';
SELECT '{"a":1 , "b":2, "c":3}'::agtype - '[null]';
SELECT '{"a":1 , "b":2, "c":3}'::agtype - '1';
SELECT '{"a":1 , "b":2, "c":3}'::agtype - 'null';
SELECT '{"a":1 , "b":2, "c":3}'::agtype - '["c","b"]' - '[1]' - '["a"]';
SELECT 'null'::agtype - '1';
SELECT 'null'::agtype - '[1]';
SELECT '{"id": 1125899906842625, "label": "Vertex", "properties": {"a": "xyz", "b": true, "c": -19.888, "e": {"f": "abcdef", "g": {}, "h": [[], {}]}, "i": {"j": 199, "k": {"l": "mnopq"}}}}::vertex'::agtype - '"a"';
SELECT '{"id": 1125899906842625, "label": "Vertex", "properties": {"a": "xyz", "b": true, "c": -19.888, "e": {"f": "abcdef", "g": {}, "h": [[], {}]}, "i": {"j": 199, "k": {"l": "mnopq"}}}}::vertex'::agtype - '["a"]';
SELECT '{"id": 1125899906842625, "label": "Vertex", "properties": {"a": "xyz", "b": true, "c": -19.888, "e": {"f": "abcdef", "g": {}, "h": [[], {}]}, "i": {"j": 199, "k": {"l": "mnopq"}}}}::vertex'::agtype - '[1]';
SELECT '{"id": 1688849860263951, "label": "e_var", "end_id": 281474976710664, "start_id": 281474976710663, "properties": {"id": 0}}::edge'::agtype - '{"id": 1688849860263951, "label": "e_var", "end_id": 281474976710664, "start_id": 281474976710663, "properties": {"id": 0}}::edge'::agtype;
SELECT '{"id": 1125899906842625, "label": "Vertex", "properties": {"a": "xyz", "b": true, "c": -19.888, "e": {"f": "abcdef", "g": {}, "h": [[], {}]}, "i": {"j": 199, "k": {"l": "mnopq"}}}}::vertex'::agtype - '[]';

--
-- Test operator + for extended functionality
--
SELECT '[1, 2, 3]'::agtype + '[4, 5]'::agtype;
SELECT '[1, 2, true, "string", 1.4::numeric]'::agtype + '[4.5, -5::numeric, {"a": true}]'::agtype;
SELECT '[{"a":1 , "b":2, "c":3}]'::agtype + '[]';
SELECT '[{"a":1 , "b":2, "c":3}]'::agtype + '[{"d": 4}]';

SELECT '{"a":1 , "b":2, "c":3}'::agtype + '["b", 2, {"d": 4}]'::agtype;
SELECT '["b", 2, {"d": 4}]'::agtype + '{"a":1 , "b":2, "c":3}'::agtype;
SELECT '{"id": 1125899906842625, "label": "Vertex", "properties": {"a": "xyz", "b": true, "c": -19.888, "e": {"f": "abcdef", "g": {}, "h": [[], {}]}, "i": {"j": 199, "k": {"l": "mnopq"}}}}::vertex'::agtype + '[1]';
SELECT '{"id": 1125899906842625, "label": "Vertex", "properties": {"a": "xyz", "b": true, "c": -19.888}}::vertex'::agtype + '[1, "e", true]';
SELECT '[]'::agtype + '{}';
SELECT '[]'::agtype + '{"a": 1}';

SELECT '{"a":1 , "b":2, "c":3}'::agtype + '{"b": 2}';
SELECT '{"a":1 , "b":2, "c":3}'::agtype + '{"": 2}';
SELECT '{"a":1 , "b":2, "c":3}'::agtype + '{"a":1 , "b":2, "c":3}';
SELECT '{"a":1 , "b":2, "c":3}'::agtype + '{}';
SELECT '{}'::agtype + '{}';

SELECT '1'::agtype + '[{"a":1 , "b":2, "c":3}]'::agtype;

SELECT '{"id": 1125899906842625, "label": "Vertex", "properties": {"a": "xyz", "b": true, "c": -19.888}}::vertex'::agtype + '{"d": 4}';

SELECT '{"id": 1125899906842625, "label": "Vertex", "properties": {"a": "xyz", "b": true, "c": -19.888}}::vertex'::agtype + '{"id": 1125899906842625, "label": "Vertex", "properties": {"a": "xyz", "b": true, "c": -19.888}}::vertex'::agtype;
SELECT '[{"id": 1688849860263938, "label": "v2", "properties": {"id": "middle"}}::vertex, {"id": 1970324836974594, "label": "e2", "end_id": 1688849860263937, "start_id": 1688849860263938, "properties": {}}::edge, {"id": 1688849860263937, "label": "v2", "properties": {"id": "initial"}}::vertex]::path'::agtype + ' [{"id": 1688849860263938, "label": "v2", "properties": {"id": "middle"}}::vertex, {"id": 1970324836974594, "label": "e2", "end_id": 1688849860263937, "start_id": 1688849860263938, "properties": {}}::edge, {"id": 1688849860263937, "label": "v2", "properties": {"id": "initial"}}::vertex]::path'::agtype;
SELECT '[{"id": 1688849860263938, "label": "v2", "properties": {"id": "middle"}}::vertex, {"id": 1970324836974594, "label": "e2", "end_id": 1688849860263937, "start_id": 1688849860263938, "properties": {}}::edge, {"id": 1688849860263937, "label": "v2", "properties": {"id": "initial"}}::vertex]::path'::agtype + '{"id": 1125899906842625, "label": "Vertex", "properties": {"a": "xyz", "b": true, "c": -19.888, "e": {"f": "abcdef", "g": {}, "h": [[], {}]}, "i": {"j": 199, "k": {"l": "mnopq"}}}}::vertex'::agtype;
SELECT '{"id": 1688849860263951, "label": "e_var", "end_id": 281474976710664, "start_id": 281474976710663, "properties": {"id": 0}}::edge'::agtype + '{"id": 1688849860263951, "label": "e_var", "end_id": 281474976710664, "start_id": 281474976710663, "properties": {"id": 0}}::edge'::agtype;
SELECT '{"id": 1688849860263951, "label": "e_var", "end_id": 281474976710664, "start_id": 281474976710663, "properties": {"id": 0}}::edge'::agtype + '{"id": 1125899906842625, "label": "Vertex", "properties": {"a": "xyz", "b": true, "c": -19.888}}::vertex'::agtype;

-- errors out
SELECT '1'::agtype + '{"a":1 , "b":2, "c":3}'::agtype;
SELECT '{"a":1 , "b":2, "c":3}'::agtype + '"string"';

--
-- Test overloaded agytype any operators +, -, *, /, %
--
SELECT '3'::agtype + 3;
SELECT '3.14'::agtype + 3;
SELECT '3.14::numeric'::agtype + 3;
SELECT 3 + '3'::agtype;
SELECT 3 + '3.14'::agtype;
SELECT 3 + '3.14::numeric'::agtype;

SELECT '3'::agtype - 3;
SELECT '3.14'::agtype - 3;
SELECT '3.14::numeric'::agtype - 3;
SELECT 3 - '3'::agtype;
SELECT 3 - '3.14'::agtype;
SELECT 3 - '3.14::numeric'::agtype;

SELECT '3'::agtype * 3;
SELECT '3.14'::agtype * 3;
SELECT '3.14::numeric'::agtype * 3;
SELECT 3 * '3'::agtype;
SELECT 3 * '3.14'::agtype;
SELECT 3 * '3.14::numeric'::agtype;

SELECT '3'::agtype / 3;
SELECT '3.14'::agtype / 3;
SELECT '3.14::numeric'::agtype / 3;
SELECT 3 / '3'::agtype;
SELECT 3 / '3.14'::agtype;
SELECT 3 / '3.14::numeric'::agtype;

SELECT '3'::agtype % 3;
SELECT '3.14'::agtype % 3;
SELECT '3.14::numeric'::agtype % 3;
SELECT 3 % '3'::agtype;
SELECT 3 % '3.14'::agtype;
SELECT 3 % '3.14::numeric'::agtype;

--
-- Test overloaded agytype any functions and operators for NULL input
-- +, -, *, /, %, =, <>, <, >, <=, >=
-- These should all return null
SELECT agtype_any_add('null'::agtype, 1);
SELECT agtype_any_sub('null'::agtype, 1);
SELECT agtype_any_mul('null'::agtype, 1);
SELECT agtype_any_div('null'::agtype, 1);
SELECT agtype_any_mod('null'::agtype, 1);
SELECT agtype_any_add(null, '1'::agtype);
SELECT agtype_any_sub(null, '1'::agtype);
SELECT agtype_any_mul(null, '1'::agtype);
SELECT agtype_any_div(null, '1'::agtype);
SELECT agtype_any_mod(null, '1'::agtype);

SELECT 1 + 'null'::agtype;
SELECT 1 - 'null'::agtype;
SELECT 1 * 'null'::agtype;
SELECT 1 / 'null'::agtype;
SELECT 1 % 'null'::agtype;
SELECT '1'::agtype + null;
SELECT '1'::agtype - null;
SELECT '1'::agtype * null;
SELECT '1'::agtype / null;
SELECT '1'::agtype % null;

SELECT 1 = 'null'::agtype;
SELECT 1 <> 'null'::agtype;
SELECT 1 < 'null'::agtype;
SELECT 1 > 'null'::agtype;
SELECT 1 <= 'null'::agtype;
SELECT 1 >= 'null'::agtype;
SELECT '1'::agtype = null;
SELECT '1'::agtype <> null;
SELECT '1'::agtype < null;
SELECT '1'::agtype > null;
SELECT '1'::agtype <= null;
SELECT '1'::agtype >= null;

SELECT agtype_any_eq('null'::agtype, 1);
SELECT agtype_any_ne('null'::agtype, 1);
SELECT agtype_any_lt('null'::agtype, 1);
SELECT agtype_any_gt('null'::agtype, 1);
SELECT agtype_any_le('null'::agtype, 1);
SELECT agtype_any_ge('null'::agtype, 1);
SELECT agtype_any_eq(null, '1'::agtype);
SELECT agtype_any_ne(null, '1'::agtype);
SELECT agtype_any_lt(null, '1'::agtype);
SELECT agtype_any_gt(null, '1'::agtype);
SELECT agtype_any_le(null, '1'::agtype);
SELECT agtype_any_ge(null, '1'::agtype);

--
-- Test orderability of comparison operators =, <>, <, >, <=, >=
-- These should all return true
-- Integer
SELECT agtype_in('1') = agtype_in('1');
SELECT agtype_in('1') <> agtype_in('2');
SELECT agtype_in('1') <> agtype_in('-2');
SELECT agtype_in('1') < agtype_in('2');
SELECT agtype_in('1') > agtype_in('-2');
SELECT agtype_in('1') <= agtype_in('2');
SELECT agtype_in('1') >= agtype_in('-2');

-- Float
SELECT agtype_in('1.01') = agtype_in('1.01');
SELECT agtype_in('1.01') <> agtype_in('1.001');
SELECT agtype_in('1.01') <> agtype_in('1.011');
SELECT agtype_in('1.01') < agtype_in('1.011');
SELECT agtype_in('1.01') > agtype_in('1.001');
SELECT agtype_in('1.01') <= agtype_in('1.011');
SELECT agtype_in('1.01') >= agtype_in('1.001');
SELECT agtype_in('1.01') < agtype_in('Infinity');
SELECT agtype_in('1.01') > agtype_in('-Infinity');
-- NaN, under ordering, is considered to be the biggest numeric value
-- greater than positive infinity. So, greater than any other number.
SELECT agtype_in('1.01') < agtype_in('NaN');
SELECT agtype_in('NaN') > agtype_in('Infinity');
SELECT agtype_in('NaN') > agtype_in('-Infinity');
SELECT agtype_in('NaN') = agtype_in('NaN');

-- Mixed Integer and Float
SELECT agtype_in('1') = agtype_in('1.0');
SELECT agtype_in('1') <> agtype_in('1.001');
SELECT agtype_in('1') <> agtype_in('0.999999');
SELECT agtype_in('1') < agtype_in('1.001');
SELECT agtype_in('1') > agtype_in('0.999999');
SELECT agtype_in('1') <= agtype_in('1.001');
SELECT agtype_in('1') >= agtype_in('0.999999');
SELECT agtype_in('1') < agtype_in('Infinity');
SELECT agtype_in('1') > agtype_in('-Infinity');
SELECT agtype_in('1') < agtype_in('NaN');

-- Mixed Float and Integer
SELECT agtype_in('1.0') = agtype_in('1');
SELECT agtype_in('1.001') <> agtype_in('1');
SELECT agtype_in('0.999999') <> agtype_in('1');
SELECT agtype_in('1.001') > agtype_in('1');
SELECT agtype_in('0.999999') < agtype_in('1');

-- Mixed Integer and Numeric
SELECT agtype_in('1') = agtype_in('1::numeric');
SELECT agtype_in('1') <> agtype_in('2::numeric');
SELECT agtype_in('1') <> agtype_in('-2::numeric');
SELECT agtype_in('1') < agtype_in('2::numeric');
SELECT agtype_in('1') > agtype_in('-2::numeric');
SELECT agtype_in('1') <= agtype_in('2::numeric');
SELECT agtype_in('1') >= agtype_in('-2::numeric');

-- Mixed Float and Numeric
SELECT agtype_in('1.01') = agtype_in('1.01::numeric');
SELECT agtype_in('1.01') <> agtype_in('1.001::numeric');
SELECT agtype_in('1.01') <> agtype_in('1.011::numeric');
SELECT agtype_in('1.01') < agtype_in('1.011::numeric');
SELECT agtype_in('1.01') > agtype_in('1.001::numeric');
SELECT agtype_in('1.01') <= agtype_in('1.011::numeric');
SELECT agtype_in('1.01') >= agtype_in('1.001::numeric');

-- Strings
SELECT agtype_in('"a"') = agtype_in('"a"');
SELECT agtype_in('"a"') <> agtype_in('"b"');
SELECT agtype_in('"a"') < agtype_in('"aa"');
SELECT agtype_in('"b"') > agtype_in('"aa"');
SELECT agtype_in('"a"') <= agtype_in('"aa"');
SELECT agtype_in('"b"') >= agtype_in('"aa"');

-- Lists
SELECT agtype_in('[0, 1, null, 2]') = agtype_in('[0, 1, null, 2]');
SELECT agtype_in('[0, 1, null, 2]') <> agtype_in('[2, null, 1, 0]');
SELECT agtype_in('[0, 1, null]') < agtype_in('[0, 1, null, 2]');
SELECT agtype_in('[1, 1, null, 2]') > agtype_in('[0, 1, null, 2]');

-- Objects (Maps)
SELECT agtype_in('{"bool":true, "null": null}') = agtype_in('{"null":null, "bool":true}');
SELECT agtype_in('{"bool":true}') < agtype_in('{"bool":true, "null": null}');

-- Comparisons between types
-- Path < Edge < Vertex < Object < List < String < Boolean < Integer = Float = Numeric < Null
SELECT agtype_in('1') < agtype_in('null');
SELECT agtype_in('NaN') < agtype_in('null');
SELECT agtype_in('Infinity') < agtype_in('null');
SELECT agtype_in('true') < agtype_in('1');
SELECT agtype_in('true') < agtype_in('NaN');
SELECT agtype_in('true') < agtype_in('Infinity');
SELECT agtype_in('"string"') < agtype_in('true');
SELECT agtype_in('[1,3,5,7,9,11]') < agtype_in('"string"');
SELECT agtype_in('{"bool":true, "integer":1}') < agtype_in('[1,3,5,7,9,11]');
SELECT agtype_in('[1, "string"]') < agtype_in('[1, 1]');
SELECT agtype_in('{"bool":true, "integer":1}') < agtype_in('{"bool":true, "integer":null}');
SELECT agtype_in('{"id":0, "label": "v", "properties":{"i":0}}::vertex') < agtype_in('{"bool":true, "i":0}');
SELECT agtype_in('{"id":2, "start_id":0, "end_id":1, "label": "e", "properties":{"i":0}}::edge') < agtype_in('{"id":0, "label": "v", "properties":{"i":0}}::vertex');
SELECT agtype_in('[{"id": 0, "label": "v", "properties": {"i": 0}}::vertex, {"id": 2, "start_id": 0, "end_id": 1, "label": "e", "properties": {"i": 0}}::edge, {"id": 1, "label": "v", "properties": {"i": 0}}::vertex]::path') < agtype_in('{"id":2, "start_id":0, "end_id":1, "label": "e", "properties":{"i":0}}::edge');
SELECT agtype_in('1::numeric') < agtype_in('null');
SELECT agtype_in('true') < agtype_in('1::numeric');
-- Testing orderability between types
SELECT * FROM create_graph('orderability_graph');
SELECT * FROM cypher('orderability_graph', $$ CREATE (:vertex {prop: null}), (:vertex {prop: 1}), (:vertex {prop: 1.01}),(:vertex {prop: true}), (:vertex {prop:"string"}),(:vertex {prop:"string_2"}), (:vertex {prop:[1, 2, 3]}), (:vertex {prop:[1, 2, 3, 4, 5]}), (:vertex {prop:{bool:true, i:0}}), (:vertex {prop:{bool:true, i:null}}), (:vertex {prop: {id:0, label: "v", properties:{i:0}}::vertex}),  (:vertex {prop: {id: 2, start_id: 0, end_id: 1, label: "e", properties: {i: 0}}::edge}), (:vertex {prop: [{id: 0, label: "v", properties: {i: 0}}::vertex, {id: 2, start_id: 0, end_id: 1, label: "e", properties: {i: 0}}::edge, {id: 1, label: "v", properties: {i: 0}}::vertex]::path}) $$)  AS (x agtype);
SELECT * FROM cypher('orderability_graph', $$ MATCH (n) RETURN n ORDER BY n.prop $$) AS (sorted agtype);
SELECT * FROM cypher('orderability_graph', $$ MATCH (n) RETURN n ORDER BY n.prop DESC $$) AS (sorted agtype);
SELECT * FROM drop_graph('orderability_graph', true);

--
-- Test overloaded agytype any comparison operators =, <>, <, >, <=, >=,
--
-- Integer
SELECT agtype_in('1') = 1;
SELECT agtype_in('1') <> 2;
SELECT agtype_in('1') <> -2;
SELECT agtype_in('1') < 2;
SELECT agtype_in('1') > -2;
SELECT agtype_in('1') <= 2;
SELECT agtype_in('1') >= -2;

-- Float
SELECT agtype_in('1.01') = 1.01;
SELECT agtype_in('1.01') <> 1.001;
SELECT agtype_in('1.01') <> 1.011;
SELECT agtype_in('1.01') < 1.011;
SELECT agtype_in('1.01') > 1.001;
SELECT agtype_in('1.01') <= 1.011;
SELECT agtype_in('1.01') >= 1.001;
SELECT agtype_in('1.01') < 'Infinity';
SELECT agtype_in('1.01') > '-Infinity';
-- NaN, under ordering, is considered to be the biggest numeric value
-- greater than positive infinity. So, greater than any other number.
SELECT agtype_in('1.01') < 'NaN';
SELECT agtype_in('NaN') > 'Infinity';
SELECT agtype_in('NaN') > '-Infinity';
SELECT agtype_in('NaN') = 'NaN';

-- Mixed Integer and Float
SELECT agtype_in('1') = 1.0;
SELECT agtype_in('1') <> 1.001;
SELECT agtype_in('1') <> 0.999999;
SELECT agtype_in('1') < 1.001;
SELECT agtype_in('1') > 0.999999;
SELECT agtype_in('1') <= 1.001;
SELECT agtype_in('1') >= 0.999999;
SELECT agtype_in('1') < 'Infinity';
SELECT agtype_in('1') > '-Infinity';
SELECT agtype_in('1') < 'NaN';

-- Mixed Float and Integer
SELECT agtype_in('1.0') = 1;
SELECT agtype_in('1.001') <> 1;
SELECT agtype_in('0.999999') <> 1;
SELECT agtype_in('1.001') > 1;
SELECT agtype_in('0.999999') < 1;

-- Mixed Integer and Numeric
SELECT agtype_in('1') = 1::numeric;
SELECT agtype_in('1') <> 2::numeric;
SELECT agtype_in('1') <> -2::numeric;
SELECT agtype_in('1') < 2::numeric;
SELECT agtype_in('1') > -2::numeric;
SELECT agtype_in('1') <= 2::numeric;
SELECT agtype_in('1') >= -2::numeric;

-- Mixed Float and Numeric
SELECT agtype_in('1.01') = 1.01::numeric;
SELECT agtype_in('1.01') <> 1.001::numeric;
SELECT agtype_in('1.01') <> 1.011::numeric;
SELECT agtype_in('1.01') < 1.011::numeric;
SELECT agtype_in('1.01') > 1.001::numeric;
SELECT agtype_in('1.01') <= 1.011::numeric;
SELECT agtype_in('1.01') >= 1.001::numeric;

-- Strings
SELECT agtype_in('"a"') = '"a"';
SELECT agtype_in('"a"') <> '"b"';
SELECT agtype_in('"a"') < '"aa"';
SELECT agtype_in('"b"') > '"aa"';
SELECT agtype_in('"a"') <= '"aa"';
SELECT agtype_in('"b"') >= '"aa"';

-- Lists
SELECT agtype_in('[0, 1, null, 2]') = '[0, 1, null, 2]';
SELECT agtype_in('[0, 1, null, 2]') <> '[2, null, 1, 0]';
SELECT agtype_in('[0, 1, null]') < '[0, 1, null, 2]';
SELECT agtype_in('[1, 1, null, 2]') > '[0, 1, null, 2]';

-- Objects (Maps)
SELECT agtype_in('{"bool":true, "null": null}') = '{"null":null, "bool":true}';
SELECT agtype_in('{"bool":true}') < '{"bool":true, "null": null}';

-- Comparisons between types
-- Object < List < String < Boolean < Integer = Float = Numeric < Null
SELECT agtype_in('1') < 'null';
SELECT agtype_in('NaN') < 'null';
SELECT agtype_in('Infinity') < 'null';
SELECT agtype_in('true') < '1';
SELECT agtype_in('true') < 'NaN';
SELECT agtype_in('true') < 'Infinity';
SELECT agtype_in('"string"') < 'true';
SELECT agtype_in('[1,3,5,7,9,11]') < '"string"';
SELECT agtype_in('{"bool":true, "integer":1}') < '[1,3,5,7,9,11]';
SELECT agtype_in('[1, "string"]') < '[1, 1]';
SELECT agtype_in('{"bool":true, "integer":1}') < '{"bool":true, "integer":null}';
SELECT agtype_in('1::numeric') < 'null';
SELECT agtype_in('true') < '1::numeric';

--
-- Test agtype to boolean cast
--
SELECT agtype_to_bool(agtype_in('true'));
SELECT agtype_to_bool(agtype_in('false'));
-- These should all fail
SELECT agtype_to_bool(agtype_in('1'));
SELECT agtype_to_bool(agtype_in('null'));
SELECT agtype_to_bool(agtype_in('1.0'));
SELECT agtype_to_bool(agtype_in('"string"'));
SELECT agtype_to_bool(agtype_in('[1,2,3]'));
SELECT agtype_to_bool(agtype_in('{"bool":true}'));

--
-- Test boolean to agtype cast
--
SELECT bool_to_agtype(true);
SELECT bool_to_agtype(false);
SELECT bool_to_agtype(null);
SELECT bool_to_agtype(true) = bool_to_agtype(true);
SELECT bool_to_agtype(true) <> bool_to_agtype(false);

--
-- Test boolean to pg_bigint cast
--
SELECT agtype_to_int8(agtype_in('true'));
SELECT agtype_to_int8(agtype_in('false'));

--
-- Test boolean to integer cast
--
SELECT agtype_to_int4(agtype_in('true'));
SELECT agtype_to_int4(agtype_in('false'));
SELECT agtype_to_int4(agtype_in('null'));

--
-- Test agtype to integer cast
--
SELECT agtype_to_int4(agtype_in('1'));
SELECT agtype_to_int4(agtype_in('1.45'));
SELECT agtype_to_int4(agtype_in('1.444::numeric'));
-- These should all fail
SELECT agtype_to_int4(agtype_in('"string"'));
SELECT agtype_to_int4(agtype_in('[1, 2, 3]'));
SELECT agtype_to_int4(agtype_in('{"int":1}'));

--
-- Test agtype to int[]
--
SELECT agtype_to_int4_array(agtype_in('[1,2,3]'));
SELECT agtype_to_int4_array(agtype_in('[1.6,2.3,3.66]'));
SELECT agtype_to_int4_array(agtype_in('["6","7",3.66]'));

--
-- Map Literal
--

--Invalid Map Key (should fail)
SELECT agtype_build_map('[0]'::agtype, null);

--
-- Test agtype object/array access operators object.property, object["property"], and array[element]
-- Note: At this point, object.property and object["property"] are equivalent.
--
SELECT agtype_access_operator('{"bool":true, "array":[1,3,{"bool":false, "int":3, "float":3.14},7], "float":3.14}','"array"','2', '"float"');
-- empty map access
SELECT agtype_access_operator('{}', '"array"');
-- empty array access
SELECT agtype_access_operator('[]', '0');
-- out of bounds array access
SELECT agtype_access_operator('[0, 1]', '2');
SELECT agtype_access_operator('[0, 1]', '-3');
-- array AGTV_NULL element
SELECT agtype_access_operator('[1, 3, 5, 7]', 'null');
-- map AGTV_NULL key
SELECT agtype_access_operator('{"bool":false, "int":3, "float":3.14}', 'null');
-- invalid map key types
SELECT agtype_access_operator('{"bool":false, "int":3, "float":3.14}', 'true');
SELECT agtype_access_operator('{"bool":false, "int":3, "float":3.14}', '2');
SELECT agtype_access_operator('{"bool":false, "int":3, "float":3.14}', '2.0');

-- Test duplicate keys and null value
-- expected: only the latest key, among duplicates, will be kept; and null will be removed
SELECT * FROM create_graph('agtype_null_duplicate_test');
SELECT * FROM cypher('agtype_null_duplicate_test', $$ CREATE (a { a:NULL, b:'bb', c:'cc', d:'dd' }) RETURN a $$) AS (a agtype);
SELECT * FROM cypher('agtype_null_duplicate_test', $$ CREATE (a { a:'aa', b:'bb', c:'cc', d:'dd' }) RETURN a $$) AS (a agtype);
SELECT * FROM cypher('agtype_null_duplicate_test', $$ CREATE (a { a:'aa', b:'bb', b:NULL , d:NULL }) RETURN a $$) AS (a agtype);
SELECT * FROM cypher('agtype_null_duplicate_test', $$ CREATE (a { a:'aa', b:'bb', b:'xx', d:NULL }) RETURN a $$) AS (a agtype);
SELECT * FROM cypher('agtype_null_duplicate_test', $$ CREATE (a { a:NULL }) RETURN a $$) AS (a agtype);
SELECT * FROM drop_graph('agtype_null_duplicate_test', true);

--
-- Vertex
--
--Basic Vertex Creation
SELECT _agtype_build_vertex('1'::graphid, $$label_name$$, agtype_build_map());
SELECT _agtype_build_vertex('1'::graphid, $$label$$, agtype_build_map('id', 2));

--Null properties
SELECT _agtype_build_vertex('1'::graphid, $$label_name$$, NULL);

--Test access operator
SELECT agtype_access_operator(_agtype_build_vertex('1'::graphid, $$label$$,
                              agtype_build_map('id', 2)), '"id"');
SELECT _agtype_build_vertex('1'::graphid, $$label$$, agtype_build_list());

--Vertex in a map
SELECT agtype_build_map(
	'vertex',
	_agtype_build_vertex('1'::graphid, $$label_name$$, agtype_build_map()));


SELECT agtype_access_operator(
        agtype_build_map(
            'vertex', _agtype_build_vertex('1'::graphid, $$label_name$$,
                                           agtype_build_map('key', 'value')),
            'other_vertex', _agtype_build_vertex('1'::graphid, $$label_name$$,
                                           agtype_build_map('key', 'other_value'))),
        '"vertex"');
--Vertex in a list
SELECT agtype_build_list(
	_agtype_build_vertex('1'::graphid, $$label_name$$, agtype_build_map()),
	_agtype_build_vertex('2'::graphid, $$label_name$$, agtype_build_map()));

SELECT agtype_access_operator(
	agtype_build_list(
		_agtype_build_vertex('1'::graphid, $$label_name$$,
                                     agtype_build_map('id', 3)),
		_agtype_build_vertex('2'::graphid, $$label_name$$,
                                     agtype_build_map('id', 4))), '0');

--
-- Edge
--
--Basic Edge Creation
SELECT _agtype_build_edge('1'::graphid, '2'::graphid, '3'::graphid,
			  $$label_name$$, agtype_build_map());

SELECT _agtype_build_edge('1'::graphid, '2'::graphid, '3'::graphid,
			  $$label$$, agtype_build_map('id', 2));

--Null properties
SELECT _agtype_build_edge('1'::graphid, '2'::graphid, '3'::graphid,
			  $$label_name$$, NULL);

--Test access operator
SELECT agtype_access_operator(_agtype_build_edge('1'::graphid, '2'::graphid,
			      '3'::graphid, $$label$$, agtype_build_map('id', 2)),'"id"');



--Edge in a map
SELECT agtype_build_map(
	'edge',
	_agtype_build_edge('1'::graphid, '2'::graphid, '3'::graphid,
			   $$label_name$$, agtype_build_map()));


SELECT agtype_access_operator(
        agtype_build_map(
            'edge', _agtype_build_edge('1'::graphid, '2'::graphid, '3'::graphid,
				       $$label_name$$, agtype_build_map('key', 'value')),
            'other_edge', _agtype_build_edge('1'::graphid, '2'::graphid, '3'::graphid,
					     $$label_name$$, agtype_build_map('key', 'other_value'))),
        '"edge"');

--Edge in a list
SELECT agtype_build_list(
	_agtype_build_edge('1'::graphid, '2'::graphid, '3'::graphid,
			   $$label_name$$, agtype_build_map()),
	_agtype_build_edge('2'::graphid, '2'::graphid, '3'::graphid,
			   $$label_name$$, agtype_build_map()));

SELECT agtype_access_operator(
	agtype_build_list(
		_agtype_build_edge('1'::graphid, '2'::graphid, '3'::graphid, $$label_name$$,
                                     agtype_build_map('id', 3)),
		_agtype_build_edge('2'::graphid, '2'::graphid, '3'::graphid, $$label_name$$,
                                     agtype_build_map('id', 4))), '0');

-- Path
SELECT _agtype_build_path(
	_agtype_build_vertex('2'::graphid, $$label_name$$, agtype_build_map()),
	_agtype_build_edge('1'::graphid, '2'::graphid, '3'::graphid,
			  $$label$$, agtype_build_map('id', 2)),
	_agtype_build_vertex('3'::graphid, $$label_name$$, agtype_build_map())
);

--All these paths should produce Errors
SELECT _agtype_build_path(
	_agtype_build_vertex('2'::graphid, $$label_name$$, agtype_build_map()),
	_agtype_build_edge('1'::graphid, '2'::graphid, '3'::graphid,
			  $$label$$, agtype_build_map('id', 2))
);

SELECT _agtype_build_path(
       _agtype_build_vertex('2'::graphid, $$label_name$$, agtype_build_map()),
       _agtype_build_edge('1'::graphid, '2'::graphid, '3'::graphid,
                         $$label$$, agtype_build_map('id', 2)),
       _agtype_build_vertex('3'::graphid, $$label_name$$, agtype_build_map()),
       _agtype_build_edge('1'::graphid, '4'::graphid, '5'::graphid,
                         $$label$$, agtype_build_map('id', 2))
);

SELECT _agtype_build_path(
	_agtype_build_vertex('2'::graphid, $$label_name$$, agtype_build_map()),
	_agtype_build_edge('1'::graphid, '2'::graphid, '3'::graphid,
			  $$label$$, agtype_build_map('id', 2)),
	NULL
);

SELECT _agtype_build_path(
	_agtype_build_vertex('2'::graphid, $$label_name$$, agtype_build_map()),
	_agtype_build_edge('1'::graphid, '2'::graphid, '3'::graphid,
			  $$label$$, agtype_build_map('id', 2)),
	1
);

SELECT _agtype_build_path(
	_agtype_build_vertex('2'::graphid, $$label_name$$, agtype_build_map()),
	_agtype_build_edge('1'::graphid, '2'::graphid, '3'::graphid,
			  $$label$$, agtype_build_map('id', 2)),
	_agtype_build_edge('1'::graphid, '2'::graphid, '3'::graphid,
			  $$label$$, agtype_build_map('id', 2))
);


--
-- id, startid, endid
--
SELECT age_id(_agtype_build_vertex('1'::graphid, $$label_name$$, agtype_build_map()));
SELECT age_id(_agtype_build_edge('1'::graphid, '2'::graphid, '3'::graphid,
			  $$label_name$$, agtype_build_map('id', 2)));

SELECT age_start_id(_agtype_build_edge('1'::graphid, '2'::graphid, '3'::graphid,
			  $$label_name$$, agtype_build_map('id', 2)));

SELECT age_end_id(_agtype_build_edge('1'::graphid, '2'::graphid, '3'::graphid,
			  $$label_name$$, agtype_build_map('id', 2)));


SELECT age_id(_agtype_build_path(
	_agtype_build_vertex('2'::graphid, $$label_name$$, agtype_build_map()),
	_agtype_build_edge('1'::graphid, '2'::graphid, '3'::graphid,
			  $$label$$, agtype_build_map('id', 2)),
	_agtype_build_vertex('3'::graphid, $$label$$, agtype_build_map('id', 2))
));

SELECT age_id(agtype_in('1'));

SELECT age_id(NULL);
SELECT age_start_id(NULL);
SELECT age_end_id(NULL);

SELECT age_id(agtype_in('null'));
SELECT age_start_id(agtype_in('null'));
SELECT age_end_id(agtype_in('null'));

SELECT agtype_contains('{"id": 1}','{"id": 1}');
SELECT agtype_contains('{"id": 1}','{"id": 2}');

SELECT '{"id": 1}'::agtype @> '{"id": 1}';
SELECT '{"id": 1}'::agtype @> '{"id": 2}';

--
-- Test STARTS WITH, ENDS WITH, and CONTAINS
--
SELECT agtype_string_match_starts_with('"abcdefghijklmnopqrstuvwxyz"', '"abcd"');
SELECT agtype_string_match_ends_with('"abcdefghijklmnopqrstuvwxyz"', '"wxyz"');
SELECT agtype_string_match_contains('"abcdefghijklmnopqrstuvwxyz"', '"abcd"');
SELECT agtype_string_match_contains('"abcdefghijklmnopqrstuvwxyz"', '"hijk"');
SELECT agtype_string_match_contains('"abcdefghijklmnopqrstuvwxyz"', '"wxyz"');
-- should all fail
SELECT agtype_string_match_starts_with('"abcdefghijklmnopqrstuvwxyz"', '"bcde"');
SELECT agtype_string_match_ends_with('"abcdefghijklmnopqrstuvwxyz"', '"vwxy"');
SELECT agtype_string_match_contains('"abcdefghijklmnopqrstuvwxyz"', '"hijl"');

--Agtype Hash Comparison Function
SELECT agtype_hash_cmp(NULL);
SELECT agtype_hash_cmp('1'::agtype);
SELECT agtype_hash_cmp('1.0'::agtype);
SELECT agtype_hash_cmp('"1"'::agtype);
SELECT agtype_hash_cmp('[1]'::agtype);
SELECT agtype_hash_cmp('[1, 1]'::agtype);
SELECT agtype_hash_cmp('[1, 1, 1]'::agtype);
SELECT agtype_hash_cmp('[1, 1, 1, 1]'::agtype);
SELECT agtype_hash_cmp('[1, 1, 1, 1, 1]'::agtype);
SELECT agtype_hash_cmp('[[1]]'::agtype);
SELECT agtype_hash_cmp('[[1, 1]]'::agtype);
SELECT agtype_hash_cmp('[[1], 1]'::agtype);
SELECT agtype_hash_cmp('[1543872]'::agtype);
SELECT agtype_hash_cmp('[1, "abcde", 2.0]'::agtype);
SELECT agtype_hash_cmp(agtype_in('null'));
SELECT agtype_hash_cmp(agtype_in('[null]'));
SELECT agtype_hash_cmp(agtype_in('[null, null]'));
SELECT agtype_hash_cmp(agtype_in('[null, null, null]'));
SELECT agtype_hash_cmp(agtype_in('[null, null, null, null]'));
SELECT agtype_hash_cmp(agtype_in('[null, null, null, null, null]'));
SELECT agtype_hash_cmp('{"id":1, "label":"test", "properties":{"id":100}}'::agtype);
SELECT agtype_hash_cmp('{"id":1, "label":"test", "properties":{"id":100}}::vertex'::agtype);

SELECT agtype_hash_cmp('{"id":2, "start_id":1, "end_id": 3, "label":"elabel", "properties":{}}'::agtype);
SELECT agtype_hash_cmp('{"id":2, "start_id":1, "end_id": 3, "label":"elabel", "properties":{}}::edge'::agtype);

SELECT agtype_hash_cmp('
	[{"id":1, "label":"test", "properties":{"id":100}}::vertex,
	 {"id":2, "start_id":1, "end_id": 3, "label":"elabel", "properties":{}}::edge,
	 {"id":5, "label":"vlabel", "properties":{}}::vertex]'::agtype);

SELECT agtype_hash_cmp('
	[{"id":1, "label":"test", "properties":{"id":100}}::vertex,
	 {"id":2, "start_id":1, "end_id": 3, "label":"elabel", "properties":{}}::edge,
	 {"id":5, "label":"vlabel", "properties":{}}::vertex]::path'::agtype);

--Agtype BTree Comparison Function
SELECT agtype_btree_cmp('1'::agtype, '1'::agtype);
SELECT agtype_btree_cmp('1'::agtype, '1.0'::agtype);
SELECT agtype_btree_cmp('1'::agtype, '"1"'::agtype);

SELECT agtype_btree_cmp('"string"'::agtype, '"string"'::agtype);
SELECT agtype_btree_cmp('"string"'::agtype, '"string "'::agtype);

SELECT agtype_btree_cmp(NULL, NULL);
SELECT agtype_btree_cmp(NULL, '1'::agtype);
SELECT agtype_btree_cmp('1'::agtype, NULL);
SELECT agtype_btree_cmp(agtype_in('null'), NULL);

SELECT agtype_btree_cmp(
	'1'::agtype,
	'{"id":1, "label":"test", "properties":{"id":100}}::vertex'::agtype);
SELECT agtype_btree_cmp(
	'{"id":1, "label":"test", "properties":{"id":100}}'::agtype,
	'{"id":1, "label":"test", "properties":{"id":100}}'::agtype);
SELECT agtype_btree_cmp(
	'{"id":1, "label":"test", "properties":{"id":100}}'::agtype,
	'{"id":1, "label":"test", "properties":{"id":200}}'::agtype);
SELECT agtype_btree_cmp(
	'{"id":1, "label":"test", "properties":{"id":100}}::vertex'::agtype,
	'{"id":1, "label":"test", "properties":{"id":100}}::vertex'::agtype);
SELECT agtype_btree_cmp(
	'{"id":1, "label":"test", "properties":{"id":100}}::vertex'::agtype,
	'{"id":1, "label":"test", "properties":{"id":200}}::vertex'::agtype);
SELECT agtype_btree_cmp(
	'{"id":2, "start_id":1, "end_id": 3, "label":"elabel", "properties":{}}::edge'::agtype,
	'{"id":2, "start_id":1, "end_id": 3, "label":"elabel", "properties":{}}::edge'::agtype);
SELECT agtype_btree_cmp(
	'{"id":2, "start_id":1, "end_id": 3, "label":"elabel", "properties":{"prop1": 1}}::edge'::agtype,
	'{"id":2, "start_id":4, "end_id": 5, "label":"elabel", "properties":{"prop2": 2}}::edge'::agtype);
SELECT agtype_btree_cmp(
	'{"id":2, "start_id":1, "end_id": 3, "label":"elabel", "properties":{"prop1": 1}}::edge'::agtype,
	'{"id":8, "start_id":4, "end_id": 5, "label":"elabel", "properties":{"prop2": 2}}::edge'::agtype);

SELECT agtype_btree_cmp(
	'[{"id":1, "label":"test", "properties":{"id":100}}::vertex,
	  {"id":2, "start_id":1, "end_id": 3, "label":"elabel", "properties":{}}::edge,
	  {"id":3, "label":"vlabel", "properties":{}}::vertex]::path'::agtype,
	'[{"id":1, "label":"test", "properties":{"id":100}}::vertex,
	  {"id":2, "start_id":1, "end_id": 3, "label":"elabel", "properties":{}}::edge,
	  {"id":3, "label":"vlabel", "properties":{}}::vertex]::path'::agtype);

SELECT agtype_btree_cmp(
	'[{"id":1, "label":"test", "properties":{"id":100}}::vertex,
	  {"id":2, "start_id":1, "end_id": 3, "label":"elabel", "properties":{}}::edge,
	  {"id":3, "label":"vlabel", "properties":{}}::vertex]::path'::agtype,
	'[{"id":1, "label":"test", "properties":{"id":100}}::vertex,
	  {"id":2, "start_id":1, "end_id": 3, "label":"elabel", "properties":{}}::edge,
	  {"id":4, "label":"vlabel", "properties":{}}::vertex]::path'::agtype);

--Int2 to Agtype in agtype_volatile_wrapper
SELECT ag_catalog.agtype_volatile_wrapper(1::int2);
SELECT ag_catalog.agtype_volatile_wrapper(32767::int2);
SELECT ag_catalog.agtype_volatile_wrapper(-32767::int2);

-- These should fail
SELECT ag_catalog.agtype_volatile_wrapper(32768::int2);
SELECT ag_catalog.agtype_volatile_wrapper(-32768::int2);
--
-- Cleanup
--
DROP TABLE agtype_table;

--
-- End of AGTYPE data type regression tests
--
