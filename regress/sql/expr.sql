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

SET extra_float_digits = 0;
LOAD 'age';
SET search_path TO ag_catalog;

SELECT * FROM create_graph('expr');

--
-- map literal
--

-- empty map
SELECT * FROM cypher('expr', $$RETURN {}$$) AS r(c agtype);

-- map of scalar values
SELECT * FROM cypher('expr', $$
RETURN {s: 's', i: 1, f: 1.0, b: true, z: null}
$$) AS r(c agtype);

-- nested maps
SELECT * FROM cypher('expr', $$
RETURN {s: {s: 's'}, t: {i: 1, e: {f: 1.0}, s: {a: {b: true}}}, z: null}
$$) AS r(c agtype);

--
-- list literal
--

-- empty list
SELECT * FROM cypher('expr', $$RETURN []$$) AS r(c agtype);

-- list of scalar values
SELECT * FROM cypher('expr', $$
RETURN ['str', 1, 1.0, true, null]
$$) AS r(c agtype);

-- nested lists
SELECT * FROM cypher('expr', $$
RETURN [['str'], [1, [1.0], [[true]]], null]
$$) AS r(c agtype);

--
-- parameter
--

PREPARE cypher_parameter(agtype) AS
SELECT * FROM cypher('expr', $$
RETURN $var
$$, $1) AS t(i agtype);
EXECUTE cypher_parameter('{"var": 1}');

PREPARE cypher_parameter_object(agtype) AS
SELECT * FROM cypher('expr', $$
RETURN $var.innervar
$$, $1) AS t(i agtype);
EXECUTE cypher_parameter_object('{"var": {"innervar": 1}}');

PREPARE cypher_parameter_array(agtype) AS
SELECT * FROM cypher('expr', $$
RETURN $var[$indexvar]
$$, $1) AS t(i agtype);
EXECUTE cypher_parameter_array('{"var": [1, 2, 3], "indexvar": 1}');

-- missing parameter
PREPARE cypher_parameter_missing_argument(agtype) AS
SELECT * FROM cypher('expr', $$
RETURN $var, $missingvar
$$, $1) AS t(i agtype, j agtype);
EXECUTE cypher_parameter_missing_argument('{"var": 1}');

-- invalid parameter
PREPARE cypher_parameter_invalid_argument(agtype) AS
SELECT * FROM cypher('expr', $$
RETURN $var
$$, $1) AS t(i agtype);
EXECUTE cypher_parameter_invalid_argument('[1]');

-- missing parameters argument

PREPARE cypher_missing_params_argument(int) AS
SELECT $1, * FROM cypher('expr', $$
RETURN $var
$$) AS t(i agtype);

SELECT * FROM cypher('expr', $$
RETURN $var
$$) AS t(i agtype);

--list concatenation
SELECT * FROM cypher('expr',
$$RETURN ['str', 1, 1.0] + [true, null]$$) AS r(c agtype);

--list IN (contains), should all be true
SELECT * FROM cypher('expr',
$$RETURN 1 IN ['str', 1, 1.0, true, null]$$) AS r(c boolean);
SELECT * FROM cypher('expr',
$$RETURN 'str' IN ['str', 1, 1.0, true, null]$$) AS r(c boolean);
SELECT * FROM cypher('expr',
$$RETURN 1.0 IN ['str', 1, 1.0, true, null]$$) AS r(c boolean);
SELECT * FROM cypher('expr',
$$RETURN true IN ['str', 1, 1.0, true, null]$$) AS r(c boolean);
SELECT * FROM cypher('expr',
$$RETURN [1,3,5,[2,4,6]] IN ['str', 1, 1.0, true, null, [1,3,5,[2,4,6]]]$$) AS r(c boolean);
SELECT * FROM cypher('expr',
$$RETURN {bool: true, int: 1} IN ['str', 1, 1.0, true, null, {bool: true, int: 1}, [1,3,5,[2,4,6]]]$$) AS r(c boolean);
SELECT * FROM cypher('expr',
$$RETURN 1 IN [1.0, [NULL]]$$) AS r(c boolean);
SELECT * FROM cypher('expr',
$$RETURN [NULL] IN [1.0, [NULL]]$$) AS r(c boolean);
-- should return SQL null, nothing
SELECT * FROM cypher('expr',
$$RETURN true IN NULL $$) AS r(c boolean);
SELECT * FROM cypher('expr',
$$RETURN null IN ['str', 1, 1.0, true, null]$$) AS r(c boolean);
SELECT * FROM cypher('expr',
$$RETURN null IN ['str', 1, 1.0, true]$$) AS r(c boolean);
SELECT * FROM cypher('expr',
$$RETURN 'str' IN null $$) AS r(c boolean);
SELECT * FROM cypher('expr',
$$RETURN 0 IN ['str', 1, 1.0, true, null]$$) AS r(c boolean);
SELECT * FROM cypher('expr',
$$RETURN 1.1 IN ['str', 1, 1.0, true, null]$$) AS r(c boolean);
SELECT * FROM cypher('expr',
$$RETURN 'Str' IN ['str', 1, 1.0, true, null]$$) AS r(c boolean);
SELECT * FROM cypher('expr',
$$RETURN [1,3,5,[2,4,5]] IN ['str', 1, 1.0, true, null, [1,3,5,[2,4,6]]]$$) AS r(c boolean);
SELECT * FROM cypher('expr',
$$RETURN {bool: true, int: 2} IN ['str', 1, 1.0, true, null, {bool: true, int: 1}, [1,3,5,[2,4,6]]]$$) AS r(c boolean);
-- should return false
SELECT * FROM cypher('expr',
$$RETURN 'str' IN ['StR', 1, true]$$) AS r(c boolean);
SELECT * FROM cypher('expr',
$$RETURN 2 IN ['StR', 1, true]$$) AS r(c boolean);
SELECT * FROM cypher('expr',
$$RETURN false IN ['StR', 1, true]$$) AS r(c boolean);
SELECT * FROM cypher('expr',
$$RETURN [1,2] IN ['StR', 1, 2, true]$$) AS r(c boolean);
SELECT * FROM cypher('expr',
$$RETURN 1 in [[1]]$$) AS r(c boolean);
SELECT * FROM cypher('expr',
$$RETURN 1 IN [[null]]$$) AS r(c boolean);
-- should error - ERROR:  object of IN must be a list
SELECT * FROM cypher('expr',
$$RETURN null IN 'str' $$) AS r(c boolean);
SELECT * FROM cypher('expr',
$$RETURN 'str' IN 'str' $$) AS r(c boolean);

-- list access
SELECT * FROM cypher('expr',
$$RETURN [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10][0]$$) AS r(c agtype);
SELECT * FROM cypher('expr',
$$RETURN [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10][5]$$) AS r(c agtype);
SELECT * FROM cypher('expr',
$$RETURN [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10][10]$$) AS r(c agtype);
SELECT * FROM cypher('expr',
$$RETURN [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10][-1]$$) AS r(c agtype);
SELECT * FROM cypher('expr',
$$RETURN [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10][-3]$$) AS r(c agtype);
-- should return null
SELECT * FROM cypher('expr',
$$RETURN [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10][11]$$) AS r(c agtype);

-- list slice
SELECT * FROM cypher('expr',
$$RETURN [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10][0..]$$) AS r(c agtype);
SELECT * FROM cypher('expr',
$$RETURN [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10][..11]$$) AS r(c agtype);
SELECT * FROM cypher('expr',
$$RETURN [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10][0..0]$$) AS r(c agtype);
SELECT * FROM cypher('expr',
$$RETURN [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10][10..10]$$) AS r(c agtype);
SELECT * FROM cypher('expr',
$$RETURN [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10][0..1]$$) AS r(c agtype);
SELECT * FROM cypher('expr',
$$RETURN [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10][9..10]$$) AS r(c agtype);
SELECT * FROM cypher('expr',
$$RETURN [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10][-1..]$$) AS r(c agtype);
SELECT * FROM cypher('expr',
$$RETURN [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10][-1..11]$$) AS r(c agtype);
SELECT * FROM cypher('expr',
$$RETURN [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10][-3..11]$$) AS r(c agtype);
-- this one should return null
SELECT * FROM cypher('expr',
$$RETURN [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10][-1..10]$$) AS r(c agtype);
SELECT agtype_access_slice('[0]'::agtype, 'null'::agtype, '1'::agtype);
SELECT agtype_access_slice('[0]'::agtype, '0'::agtype, 'null'::agtype);
-- should error - ERROR:  slice must access a list
SELECT * from cypher('expr',
$$RETURN 0[0..1]$$) as r(a agtype);
SELECT * from cypher('expr',
$$RETURN 0[[0]..[1]]$$) as r(a agtype);
-- should return nothing
SELECT * from cypher('expr',
$$RETURN [0][0..-2147483649]$$) as r(a agtype);

-- access and slice operators nested
SELECT * from cypher('expr', $$ WITH [0, 1, [2, 3, 4], 5, [6, 7, 8], 9] as l RETURN l[0] $$) as (results agtype);
SELECT * from cypher('expr', $$ WITH [0, 1, [2, 3, 4], 5, [6, 7, 8], 9] as l RETURN l[2] $$) as (results agtype);
SELECT * from cypher('expr', $$ WITH [0, 1, [2, 3, 4], 5, [6, 7, 8], 9] as l RETURN l[-1] $$) as (results agtype);
SELECT * from cypher('expr', $$ WITH [0, 1, [2, 3, 4], 5, [6, 7, 8], 9] as l RETURN l[2][-2] $$) as (results agtype);
SELECT * from cypher('expr', $$ WITH [0, 1, [2, 3, 4], 5, [6, 7, 8], 9] as l RETURN l[2][-2..] $$) as (results agtype);
SELECT * from cypher('expr', $$ WITH [0, 1, [2, 3, 4], 5, [6, 7, 8], 9] as l RETURN l[-2..] $$) as (results agtype);
SELECT * from cypher('expr', $$ WITH [0, 1, [2, 3, 4], 5, [6, 7, 8], 9] as l RETURN l[-2..][-1..][-1..] $$) as (results agtype);
SELECT * from cypher('expr', $$ WITH [0, 1, [2, 3, 4], 5, [6, 7, 8], 9] as l RETURN l[-2..][-1..][0] $$) as (results agtype);
SELECT * from cypher('expr', $$ WITH [0, 1, [2, 3, 4], 5, [6, 7, 8], 9] as l RETURN l[-2..][-1..][-1] $$) as (results agtype);
SELECT * from cypher('expr', $$ WITH [0, 1, [2, 3, 4], 5, [6, 7, 8], 9] as l RETURN l[-2..][-2..-1] $$) as (results agtype);
SELECT * from cypher('expr', $$ WITH [0, 1, [2, 3, 4], 5, [6, 7, 8], 9] as l RETURN l[-4..-2] $$) as (results agtype);
SELECT * from cypher('expr', $$ WITH [0, 1, [2, 3, 4], 5, [6, 7, 8], 9] as l RETURN l[-4..-2][-2] $$) as (results agtype);
SELECT * from cypher('expr', $$ WITH [0, 1, [2, 3, 4], 5, [6, 7, 8], 9] as l RETURN l[-4..-2][0] $$) as (results agtype);
SELECT * from cypher('expr', $$ WITH [0, 1, [2, 3, 4], 5, [6, 7, 8], 9] as l RETURN l[-4..-2][-2][-2..] $$) as (results agtype);
SELECT * from cypher('expr', $$ WITH [0, 1, [2, 3, 4], 5, [6, 7, 8], 9] as l RETURN l[-4..-2][-2][-2..][0] $$) as (results agtype);

-- empty list
SELECT * from cypher('expr', $$ WITH [0, 1, [2, 3, 4], 5, [6, 7, 8], 9] as l RETURN l[-2..][-1..][-2..-2] $$) as (results agtype);

-- should return null
SELECT * from cypher('expr', $$ WITH [0, 1, [2, 3, 4], 5, [6, 7, 8], 9] as l RETURN l[2][3] $$) as (results agtype);
SELECT * from cypher('expr', $$ WITH [0, 1, [2, 3, 4], 5, [6, 7, 8], 9] as l RETURN l[-2..][-1..][-2] $$) as (results agtype);

--
-- String operators
--

-- String LHS + String RHS
SELECT * FROM cypher('expr', $$RETURN 'str' + 'str'$$) AS r(c agtype);

-- String LHS + Integer RHS
SELECT * FROM cypher('expr', $$RETURN 'str' + 1$$) AS r(c agtype);

-- String LHS + Float RHS
SELECT * FROM cypher('expr', $$RETURN 'str' + 1.0$$) AS r(c agtype);

-- Integer LHS + String LHS
SELECT * FROM cypher('expr', $$RETURN 1 + 'str'$$) AS r(c agtype);

-- Float LHS + String RHS
SELECT * FROM cypher('expr', $$RETURN 1.0 + 'str'$$) AS r(c agtype);

--
-- Test transform logic for operators
--

SELECT * FROM cypher('expr', $$
RETURN (-(3 * 2 - 4.0) ^ ((10 / 5) + 1)) % -3
$$) AS r(result agtype);

--
-- Test transform logic for comparison operators
--

SELECT * FROM cypher('expr', $$
RETURN 1 = 1.0
$$) AS r(result boolean);

SELECT * FROM cypher('expr', $$
RETURN 1 > -1.0
$$) AS r(result boolean);

SELECT * FROM cypher('expr', $$
RETURN -1.0 < 1
$$) AS r(result boolean);

SELECT * FROM cypher('expr', $$
RETURN "aaa" < "z"
$$) AS r(result boolean);

SELECT * FROM cypher('expr', $$
RETURN "z" > "aaa"
$$) AS r(result boolean);

SELECT * FROM cypher('expr', $$
RETURN false = false
$$) AS r(result boolean);

SELECT * FROM cypher('expr', $$
RETURN ("string" < true)
$$) AS r(result boolean);

SELECT * FROM cypher('expr', $$
RETURN true < 1
$$) AS r(result boolean);

SELECT * FROM cypher('expr', $$
RETURN (1 + 1.0) = (7 % 5)
$$) AS r(result boolean);

--
-- Test chained comparisons
--

SELECT * FROM create_graph('chained');
SELECT * FROM cypher('chained', $$ CREATE (:people {name: "Jason", age:50}) $$) AS (result agtype);
SELECT * FROM cypher('chained', $$ CREATE (:people {name: "Amy", age:25}) $$) AS (result agtype);
SELECT * FROM cypher('chained', $$ CREATE (:people {name: "Samantha", age:35}) $$) AS (result agtype);
SELECT * FROM cypher('chained', $$ CREATE (:people {name: "Mark", age:40}) $$) AS (result agtype);
SELECT * FROM cypher('chained', $$ CREATE (:people {name: "David", age:15}) $$) AS (result agtype);
-- should return 1
SELECT * FROM cypher('chained', $$ MATCH (u:people) WHERE 35 < u.age <= 49  RETURN u $$) AS (result agtype);
SELECT * FROM cypher('chained', $$ MATCH (u:people) WHERE 25 <= u.age <= 25  RETURN u $$) AS (result agtype);
SELECT * FROM cypher('chained', $$ MATCH (u:people) WHERE 35 = u.age = 35  RETURN u $$) AS (result agtype);
SELECT * FROM cypher('chained', $$ MATCH (u:people) WHERE 50 > u.age > 35  RETURN u $$) AS (result agtype);
-- should return 3
SELECT * FROM cypher('chained', $$ MATCH (u:people) WHERE 40 <> u.age <> 35 RETURN u $$) AS (result agtype);
-- should return 2
SELECT * FROM cypher('chained', $$ MATCH (u:people) WHERE 30 <= u.age <= 49 > u.age RETURN u $$) AS (result agtype);
-- should return 0
SELECT * FROM cypher('chained', $$ MATCH (u:people) WHERE 30 <= u.age <= 49 = u.age RETURN u $$) AS (result agtype);
-- should return 2
SELECT * FROM cypher('chained', $$ MATCH (u:people) WHERE 35 < u.age + 1 <= 50 RETURN u $$) AS (result agtype);
-- should return 3
SELECT * FROM cypher('chained', $$ MATCH (u:people) WHERE NOT 35 < u.age + 1 <= 50 RETURN u $$) AS (result agtype);

--
-- Test transform logic for IS NULL & IS NOT NULL
--

SELECT * FROM cypher('expr', $$
RETURN null IS NULL
$$) AS r(result boolean);

SELECT * FROM cypher('expr', $$
RETURN 1 IS NULL
$$) AS r(result boolean);

SELECT * FROM cypher('expr', $$
RETURN 1 IS NOT NULL
$$) AS r(result boolean);

SELECT * FROM cypher('expr', $$
RETURN null IS NOT NULL
$$) AS r(result boolean);

--
-- Test transform logic for AND, OR, NOT and XOR
--

SELECT * FROM cypher('expr', $$
RETURN NOT false
$$) AS r(result boolean);

SELECT * FROM cypher('expr', $$
RETURN NOT true
$$) AS r(result boolean);

SELECT * FROM cypher('expr', $$
RETURN true AND true
$$) AS r(result boolean);

SELECT * FROM cypher('expr', $$
RETURN true AND false
$$) AS r(result boolean);

SELECT * FROM cypher('expr', $$
RETURN false AND true
$$) AS r(result boolean);

SELECT * FROM cypher('expr', $$
RETURN false AND false
$$) AS r(result boolean);

SELECT * FROM cypher('expr', $$
RETURN true OR true
$$) AS r(result boolean);

SELECT * FROM cypher('expr', $$
RETURN true OR false
$$) AS r(result boolean);

SELECT * FROM cypher('expr', $$
RETURN false OR true
$$) AS r(result boolean);

SELECT * FROM cypher('expr', $$
RETURN false OR false
$$) AS r(result boolean);

SELECT * FROM cypher('expr', $$
RETURN NOT ((true OR false) AND (false OR true))
$$) AS r(result boolean);

SELECT * FROM cypher('expr', $$
RETURN true XOR true
$$) AS r(result boolean);

SELECT * FROM cypher('expr', $$
RETURN true XOR false
$$) AS r(result boolean);

SELECT * FROM cypher('expr', $$
RETURN false XOR true
$$) AS r(result boolean);

SELECT * FROM cypher('expr', $$
RETURN false XOR false
$$) AS r(result boolean);

SELECT * FROM cypher('expr', $$
RETURN false OR 1::bool
$$) AS (result boolean);

SELECT * FROM cypher('expr', $$
RETURN false AND NOT 1::bool
$$) AS (result boolean);

SELECT * FROM cypher('expr', $$
RETURN NOT 1::bool::int::bool
$$) AS (result boolean);

-- Invalid operands for AND, OR, NOT, XOR
SELECT * FROM cypher('expr', $$
RETURN 1 AND true
$$) AS r(result boolean);

SELECT * FROM cypher('expr', $$
RETURN true AND 0
$$) AS r(result boolean);

SELECT * FROM cypher('expr', $$
RETURN false OR 1
$$) AS r(result boolean);

SELECT * FROM cypher('expr', $$
RETURN 0 OR true
$$) AS r(result boolean);

SELECT * FROM cypher('expr', $$
RETURN NOT 1
$$) AS r(result boolean);

SELECT * FROM cypher('expr', $$
RETURN true XOR 0
$$) AS r(result boolean);

SELECT * FROM cypher('expr', $$
RETURN 1 XOR 0
$$) AS r(result boolean);

SELECT * FROM cypher('expr', $$
RETURN NOT ((1 OR 0) AND (0 OR 1))
$$) AS r(result boolean);

SELECT * FROM cypher('expr', $$
RETURN 1.0 AND true
$$) AS (result agtype);

SELECT * FROM cypher('expr', $$
RETURN false OR 'string'
$$) AS (result agtype);

SELECT * FROM cypher('expr', $$
RETURN false XOR 1::numeric
$$) AS (result agtype);

SELECT * FROM cypher('expr', $$
RETURN false OR 1::bool::int
$$) AS (result boolean);
--
-- Test indirection transform logic for object.property, object["property"],
-- and array[element]
--

SELECT * FROM cypher('expr', $$
RETURN [
  1,
  {
    bool: true,
    int: 3,
    array: [
      9,
      11,
      {
        boom: false,
        float: 3.14
      },
      13
    ]
  },
  5,
  7,
  9
][1].array[2]["float"]
$$) AS r(result agtype);

SELECT *
FROM cypher('expr', $$
    WITH {bool_val: false, int_val: 3, float_val: 3.14, array: [1, 2, 3]} as a
    RETURN a - 'array'
$$) as (a agtype);

SELECT *
FROM cypher('expr', $$
    WITH {bool_val: false, int_val: 3, float_val: 3.14, array: [1, 2, 3]} as a
    RETURN a - 'int_val'
$$) as (a agtype);

SELECT *
FROM cypher('expr', $$
    WITH {bool_val: false, int_val: 3, float_val: 3.14, array: [1, 2, 3]} as a
    RETURN a.array - 1
$$) as (a agtype);

SELECT *
FROM cypher('expr', $$
    WITH {bool_val: false, int_val: 3, float_val: 3.14, array: [1, 2, 3]} as a
    RETURN a.array + 1
$$) as (a agtype);

--
-- Test STARTS WITH, ENDS WITH, and CONTAINS transform logic
--

SELECT * FROM cypher('expr', $$
RETURN "abcdefghijklmnopqrstuvwxyz" STARTS WITH "abcd"
$$) AS r(result agtype);
SELECT * FROM cypher('expr', $$
RETURN "abcdefghijklmnopqrstuvwxyz" ENDS WITH "wxyz"
$$) AS r(result agtype);
SELECT * FROM cypher('expr', $$
RETURN "abcdefghijklmnopqrstuvwxyz" CONTAINS "klmn"
$$) AS r(result agtype);

-- these should return false
SELECT * FROM cypher('expr', $$
RETURN "abcdefghijklmnopqrstuvwxyz" STARTS WITH "bcde"
$$) AS r(result agtype);
SELECT * FROM cypher('expr', $$
RETURN "abcdefghijklmnopqrstuvwxyz" ENDS WITH "vwxy"
$$) AS r(result agtype);
SELECT * FROM cypher('expr', $$
RETURN "abcdefghijklmnopqrstuvwxyz" CONTAINS "klmo"
$$) AS r(result agtype);
-- these should return SQL NULL
SELECT * FROM cypher('expr', $$
RETURN "abcdefghijklmnopqrstuvwxyz" STARTS WITH NULL
$$) AS r(result agtype);
SELECT * FROM cypher('expr', $$
RETURN "abcdefghijklmnopqrstuvwxyz" ENDS WITH NULL
$$) AS r(result agtype);
SELECT * FROM cypher('expr', $$
RETURN "abcdefghijklmnopqrstuvwxyz" CONTAINS NULL
$$) AS r(result agtype);

--
-- Test =~ aka regular expression comparisons
--
SELECT create_graph('regex');
SELECT * FROM cypher('regex', $$
CREATE (n:Person {name: 'John'}) RETURN n
$$) AS r(result agtype);
SELECT * FROM cypher('regex', $$
CREATE (n:Person {name: 'Jeff'}) RETURN n
$$) AS r(result agtype);
SELECT * FROM cypher('regex', $$
CREATE (n:Person {name: 'Joan'}) RETURN n
$$) AS r(result agtype);

SELECT * FROM cypher('regex', $$
MATCH (n:Person) WHERE n.name =~ 'JoHn' RETURN n
$$) AS r(result agtype);
SELECT * FROM cypher('regex', $$
MATCH (n:Person) WHERE n.name =~ '(?i)JoHn' RETURN n
$$) AS r(result agtype);
SELECT * FROM cypher('regex', $$
MATCH (n:Person) WHERE n.name =~ 'Jo.n' RETURN n
$$) AS r(result agtype);
SELECT * FROM cypher('regex', $$
MATCH (n:Person) WHERE n.name =~ 'J.*' RETURN n
$$) AS r(result agtype);

--
--Coerce to Postgres 3 int types (smallint, int, bigint)
--
SELECT create_graph('type_coercion');
SELECT * FROM cypher('type_coercion', $$
	RETURN NULL
$$) AS (i bigint);

SELECT * FROM cypher('type_coercion', $$
	RETURN 1
$$) AS (i smallint);

SELECT * FROM cypher('type_coercion', $$
	RETURN 1
$$) AS (i int);

SELECT * FROM cypher('type_coercion', $$
	RETURN 1
$$) AS (i bigint);

SELECT * FROM cypher('type_coercion', $$
	RETURN 1.0
$$) AS (i bigint);

SELECT * FROM cypher('type_coercion', $$
	RETURN 1.0::numeric
$$) AS (i bigint);

SELECT * FROM cypher('type_coercion', $$
	RETURN '1'
$$) AS (i bigint);

SELECT * FROM cypher('type_coercion', $$
	RETURN true
$$) AS (i bigint);

SELECT * FROM cypher('type_coercion', $$
	RETURN true
$$) AS (i int);

--Invalid String Format
SELECT * FROM cypher('type_coercion', $$
	RETURN '1.0'
$$) AS (i bigint);

-- Casting to ints that will cause overflow
SELECT * FROM cypher('type_coercion', $$
	RETURN 10000000000000000000
$$) AS (i smallint);

SELECT * FROM cypher('type_coercion', $$
	RETURN 10000000000000000000
$$) AS (i int);

--Invalid types

SELECT * FROM cypher('type_coercion', $$
	RETURN {key: 1}
$$) AS (i bigint);

SELECT * FROM cypher('type_coercion', $$
	RETURN [1]
$$) AS (i bigint);

SELECT * FROM cypher('type_coercion', $$
    RETURN 1
$$) AS (i bool);

SELECT * FROM cypher('type_coercion', $$CREATE ()-[:edge]->()$$) AS (result agtype);
SELECT * FROM cypher('type_coercion', $$
	MATCH (v)
	RETURN v
$$) AS (i bigint);
SELECT * FROM cypher('type_coercion', $$
	MATCH ()-[e]-()
	RETURN e
$$) AS (i bigint);
SELECT * FROM cypher('type_coercion', $$
	MATCH p=()-[]-()
	RETURN p
$$) AS (i bigint);

--
-- Test typecasting '::' transform and execution logic
--

--
-- Test from an agtype value to agtype int
--
SELECT * FROM cypher('expr', $$
RETURN 0.0::int
$$) AS r(result agtype);
SELECT * FROM cypher('expr', $$
RETURN 0.0::integer
$$) AS r(result agtype);
SELECT * FROM cypher('expr', $$
RETURN '0'::int
$$) AS r(result agtype);
SELECT * FROM cypher('expr', $$
RETURN '0'::integer
$$) AS r(result agtype);
SELECT * FROM cypher('expr', $$
RETURN 0.0::numeric::int
$$) AS r(result agtype);
SELECT * FROM cypher('expr', $$
RETURN 2.71::int
$$) AS r(result agtype);
SELECT * FROM cypher('expr', $$
RETURN 2.71::numeric::int
$$) AS r(result agtype);
SELECT * FROM cypher('expr', $$
RETURN true::int
$$) AS r(result agtype);
SELECT * FROM cypher('expr', $$
RETURN false::int
$$) AS r(result agtype);
SELECT * FROM cypher('expr', $$
RETURN ([0, {one: 1.0, pie: 3.1415927, e: 2::numeric}, 2, null][1].one)::int
$$) AS r(result agtype);
SELECT * FROM cypher('expr', $$
RETURN ([0, {one: 1.0::int, pie: 3.1415927, e: 2.718281::numeric}, 2, null][1].one)
$$) AS r(result agtype);
SELECT * FROM cypher('expr', $$
RETURN ([0, {one: 1::float, pie: 3.1415927, e: 2.718281::numeric}, 2, null][1].one)::int
$$) AS r(result agtype);
SELECT * FROM cypher('expr', $$
RETURN ([0, {one: 1, pie: 3.1415927, e: 2.718281::numeric}, 2, null][3])::int
$$) AS r(result agtype);
-- should return SQL null
SELECT agtype_typecast_int('null'::agtype);
SELECT agtype_typecast_int(null);
SELECT * FROM cypher('expr', $$
RETURN null::int
$$) AS r(result agtype);
-- should return JSON null
SELECT agtype_in('null::int');
-- these should fail
SELECT * FROM cypher('expr', $$
RETURN '0.0'::int
$$) AS r(result agtype);
SELECT * FROM cypher('expr', $$
RETURN '1.5'::int
$$) AS r(result agtype);
SELECT * FROM cypher('graph_name', $$
RETURN "15555555555555555555555555555"::int
$$) AS (string_result agtype);
SELECT * FROM cypher('expr', $$
RETURN 'NaN'::float::int
$$) AS r(result agtype);
SELECT * FROM cypher('expr', $$
RETURN 'infinity'::float::int
$$) AS r(result agtype);
SELECT * FROM cypher('expr', $$
RETURN ''::int
$$) AS r(result agtype);
SELECT * FROM cypher('expr', $$
RETURN 'false_'::int
$$) AS r(result agtype);
--
-- Test from an agtype value to agtype int
--
SELECT * FROM cypher('expr', $$
RETURN 0::bool
$$) AS r(result agtype);

-- these should fail
SELECT * FROM cypher('expr', $$
RETURN 1.23::bool
$$) AS r(result agtype);
SELECT * FROM cypher('expr', $$
RETURN ''::bool
$$) AS r(result agtype);
SELECT * FROM cypher('expr', $$
RETURN 'false_'::bool
$$) AS r(result agtype);
-- Test from an agtype value to an agtype numeric
--
SELECT * FROM cypher('expr', $$
RETURN 0::numeric
$$) AS r(result agtype);
SELECT * FROM cypher('expr', $$
RETURN 2.71::numeric
$$) AS r(result agtype);
SELECT * FROM cypher('expr', $$
RETURN '2.71'::numeric
$$) AS r(result agtype);
SELECT * FROM cypher('expr', $$
RETURN (2.71::numeric)::numeric
$$) AS r(result agtype);
SELECT * FROM cypher('expr', $$
RETURN ('2.71'::numeric)::numeric
$$) AS r(result agtype);
SELECT * FROM cypher('expr', $$
RETURN ('NaN'::numeric)::numeric
$$) AS r(result agtype);
SELECT * FROM cypher('expr', $$
RETURN ((1 + 2.71) * 3)::numeric
$$) AS r(result agtype);
SELECT * FROM cypher('expr', $$
RETURN ([0, {one: 1, pie: 3.1415927, e: 2.718281::numeric}, 2, null][1].pie)::numeric
$$) AS r(result agtype);
SELECT * FROM cypher('expr', $$
RETURN ([0, {one: 1, pie: 3.1415927, e: 2.718281::numeric}, 2, null][1].e)
$$) AS r(result agtype);
SELECT * FROM cypher('expr', $$
RETURN ([0, {one: 1, pie: 3.1415927, e: 2.718281::numeric}, 2, null][1].e)::numeric
$$) AS r(result agtype);
SELECT * FROM cypher('expr', $$
RETURN ([0, {one: 1, pie: 3.1415927, e: 2.718281::numeric}, 2, null][3])::numeric
$$) AS r(result agtype);
SELECT * FROM cypher('expr', $$
RETURN ([0, {one: 1, pie: 3.1415927, e: 2.718281::numeric}, 2::numeric, null])
$$) AS r(result agtype);
-- should return SQL null
SELECT agtype_typecast_numeric('null'::agtype);
SELECT agtype_typecast_numeric(null);
SELECT * FROM cypher('expr', $$
RETURN null::numeric
$$) AS r(result agtype);
-- should return JSON null
SELECT agtype_in('null::numeric');
-- these should fail
SELECT * FROM cypher('expr', $$
RETURN ('2:71'::numeric)::numeric
$$) AS r(result agtype);
SELECT * FROM cypher('expr', $$
RETURN ('inf'::numeric)::numeric
$$) AS r(result agtype);
SELECT * FROM cypher('expr', $$
RETURN ('infinity'::numeric)::numeric
$$) AS r(result agtype);
-- verify that output can be accepted and reproduced correctly via agtype_in
SELECT agtype_in('2.71::numeric');
SELECT agtype_in('[0, {"e": 2.718281::numeric, "one": 1, "pie": 3.1415927}, 2::numeric, null]');
SELECT * FROM cypher('expr', $$
RETURN (['NaN'::numeric, {one: 1, pie: 3.1415927, nan: 'nAn'::numeric}, 2::numeric, null])
$$) AS r(result agtype);
SELECT agtype_in('[NaN::numeric, {"nan": NaN::numeric, "one": 1, "pie": 3.1415927}, 2::numeric, null]');

--
-- Test from an agtype value to agtype float
--
SELECT * FROM cypher('expr', $$
RETURN 0::float
$$) AS r(result agtype);
SELECT * FROM cypher('expr', $$
RETURN '2.71'::float
$$) AS r(result agtype);
SELECT * FROM cypher('expr', $$
RETURN 2.71::float
$$) AS r(result agtype);
SELECT * FROM cypher('expr', $$
RETURN ([0, {one: 1, pie: 3.1415927, e: 2::numeric}, 2, null][1].one)::float
$$) AS r(result agtype);
SELECT * FROM cypher('expr', $$
RETURN ([0, {one: 1::float, pie: 3.1415927, e: 2.718281::numeric}, 2, null][1].one)
$$) AS r(result agtype);
SELECT * FROM cypher('expr', $$
RETURN ([0, {one: 1::float, pie: 3.1415927, e: 2.718281::numeric}, 2, null][1].one)::float
$$) AS r(result agtype);
SELECT * FROM cypher('expr', $$
RETURN ([0, {one: 1, pie: 3.1415927, e: 2.718281::numeric}, 2, null][3])::float
$$) AS r(result agtype);
-- test NaN, infinity, and -infinity
SELECT * FROM cypher('expr', $$
RETURN 'NaN'::float
$$) AS r(result agtype);
SELECT * FROM cypher('expr', $$
RETURN 'inf'::float
$$) AS r(result agtype);
SELECT * FROM cypher('expr', $$
RETURN '-inf'::float
$$) AS r(result agtype);
SELECT * FROM cypher('expr', $$
RETURN 'infinity'::float
$$) AS r(result agtype);
SELECT * FROM cypher('expr', $$
RETURN '-infinity'::float
$$) AS r(result agtype);
-- should return SQL null
SELECT agtype_typecast_float('null'::agtype);
SELECT agtype_typecast_float(null);
SELECT * FROM cypher('expr', $$
RETURN null::float
$$) AS r(result agtype);
-- should return JSON null
SELECT agtype_in('null::float');
-- these should fail
SELECT * FROM cypher('expr', $$
RETURN '2:71'::float
$$) AS r(result agtype);
SELECT * FROM cypher('expr', $$
RETURN 'infi'::float
$$) AS r(result agtype);
-- verify that output can be accepted and reproduced correctly via agtype_in
SELECT * FROM cypher('expr', $$
RETURN ([0, {one: 1::float, pie: 3.1415927, e: 2.718281::numeric}, 2::numeric, null])
$$) AS r(result agtype);
SELECT agtype_in('[0, {"e": 2.718281::numeric, "one": 1.0, "pie": 3.1415927}, 2::numeric, null]');
SELECT * FROM cypher('expr', $$
RETURN (['NaN'::float, {one: 'inf'::float, pie: 3.1415927, e: 2.718281::numeric}, 2::numeric, null])
$$) AS r(result agtype);
SELECT agtype_in('[NaN, {"e": 2.718281::numeric, "one": Infinity, "pie": 3.1415927}, 2::numeric, null]');

--
-- Test typecast ::pg_float8
--
SELECT * FROM cypher('expr', $$
RETURN 0::pg_float8
$$) AS r(result agtype);
SELECT * FROM cypher('expr', $$
RETURN '2.71'::pg_float8
$$) AS r(result agtype);
SELECT * FROM cypher('expr', $$
RETURN 2.71::pg_float8
$$) AS r(result agtype);
SELECT * FROM cypher('expr', $$
RETURN ([0, {one: 1, pie: 3.1415927, e: 2::numeric}, 2, null][1].one)::pg_float8
$$) AS r(result agtype);
SELECT * FROM cypher('expr', $$
RETURN ([0, {one: 1::pg_float8, pie: 3.1415927, e: 2.718281::numeric}, 2, null][1].one)
$$) AS r(result agtype);
SELECT * FROM cypher('expr', $$
RETURN ([0, {one: 1::pg_float8, pie: 3.1415927, e: 2.718281::numeric}, 2, null][1].one)::float
$$) AS r(result agtype);
SELECT * FROM cypher('expr', $$
RETURN ([0, {one: 1, pie: 3.1415927, e: 2.718281::numeric}, 2, null][3])::pg_float8
$$) AS r(result agtype);
SELECT * FROM cypher('expr', $$
RETURN (['NaN'::pg_float8, {one: 'inf'::pg_float8, pie: 3.1415927, e: 2.718281::numeric}, 2::numeric, null])
$$) AS r(result agtype);
SELECT * FROM cypher('expr', $$
RETURN ([0, {one: 1, pie: 3.1415927, e: 2.718281::numeric}, 2, null][1].e)::pg_float8
$$) AS r(result agtype);
-- test NaN, Infinity and -Infinity
SELECT * FROM cypher('expr', $$
RETURN 'NaN'::pg_float8
$$) AS r(result agtype);
SELECT * FROM cypher('expr', $$
RETURN 'inf'::pg_float8
$$) AS r(result agtype);
SELECT * FROM cypher('expr', $$
RETURN '-inf'::pg_float8
$$) AS r(result agtype);
SELECT * FROM cypher('expr', $$
RETURN 'infinity'::pg_float8
$$) AS r(result agtype);
SELECT * FROM cypher('expr', $$
RETURN '-infinity'::pg_float8
$$) AS r(result agtype);
SELECT * FROM cypher('expr', $$
RETURN null::pg_float8
$$) AS r(result agtype);
-- these should fail
SELECT * FROM cypher('expr', $$
RETURN ''::pg_float8
$$) AS r(result agtype);
SELECT * FROM cypher('expr', $$
RETURN '2:71'::pg_float8
$$) AS r(result agtype);
SELECT * FROM cypher('expr', $$
RETURN 'infi'::pg_float8
$$) AS r(result agtype);

--
-- Test typecast :: transform and execution logic for object (vertex & edge)
--
SELECT * FROM cypher('expr', $$
RETURN {id:0, label:"vertex 0", properties:{}}::vertex
$$) AS r(result agtype);
SELECT * FROM cypher('expr', $$
RETURN {vertex_0:{id:0, label:"vertex 0", properties:{}}::vertex}
$$) AS r(result agtype);
SELECT * FROM cypher('expr', $$
RETURN {name:"container 0", vertices:[{vertex_0:{id:0, label:"vertex 0", properties:{}}::vertex}, {vertex_0:{id:0, label:"vertex 0", properties:{}}::vertex}]}
$$) AS r(result agtype);
SELECT * FROM cypher('expr', $$
RETURN {id:3, label:"edge 0", properties:{}, start_id:0, end_id:1}::edge
$$) AS r(result agtype);
SELECT * FROM cypher('expr', $$
RETURN {edge_0:{id:3, label:"edge 0", properties:{}, start_id:0, end_id:1}::edge}
$$) AS r(result agtype);
--invalid edge typecast
SELECT * FROM cypher('expr', $$
RETURN {edge_0:{id:3, label:"edge 0", properties:{}, startid:0, end_id:1}::edge}
$$) AS r(result agtype);
SELECT * FROM cypher('expr', $$
RETURN {edge_0:{id:3, label:"edge 0", properties:{}, start_id:0, endid:1}::edge}
$$) AS r(result agtype);
SELECT * FROM cypher('expr', $$
RETURN {name:"container 1", edges:[{id:3, label:"edge 0", properties:{}, start_id:0, end_id:1}::edge, {id:4, label:"edge 1", properties:{}, start_id:1, end_id:0}::edge]}
$$) AS r(result agtype);
SELECT * FROM cypher('expr', $$
RETURN {name:"path 1", path:[{id:0, label:"vertex 0", properties:{}}::vertex, {id:2, label:"edge 0", properties:{}, start_id:0, end_id:1}::edge, {id:1, label:"vertex 1", properties:{}}::vertex]}
$$) AS r(result agtype);
-- should return null
SELECT * FROM cypher('expr', $$
RETURN NULL::vertex
$$) AS r(result agtype);
SELECT * FROM cypher('expr', $$
RETURN NULL::edge
$$) AS r(result agtype);
SELECT agtype_typecast_vertex('null'::agtype);
SELECT agtype_typecast_vertex(null);
SELECT agtype_typecast_edge('null'::agtype);
SELECT agtype_typecast_edge(null);
-- should return JSON null
SELECT agtype_in('null::vertex');
SELECT agtype_in('null::edge');
-- should all fail
SELECT * FROM cypher('expr', $$
RETURN {id:0, labelz:"vertex 0", properties:{}}::vertex
$$) AS r(result agtype);
SELECT * FROM cypher('expr', $$
RETURN {id:0, label:"vertex 0"}::vertex
$$) AS r(result agtype);
SELECT * FROM cypher('expr', $$
RETURN {id:"0", label:"vertex 0", properties:{}}::vertex
$$) AS r(result agtype);
SELECT * FROM cypher('expr', $$
RETURN {}::vertex
$$) AS r(result agtype);
SELECT * FROM cypher('expr', $$
RETURN {id:3, labelz:"edge 0", properties:{}, start_id:0, end_id:1}::edge
$$) AS r(result agtype);
SELECT * FROM cypher('expr', $$
RETURN {id:3, label:"edge 0", start_id:0, end_id:1}::edge
$$) AS r(result agtype);
SELECT * FROM cypher('expr', $$
RETURN {}::edge
$$) AS r(result agtype);
-- make sure that output can be read back in and reproduce the output
SELECT agtype_in('{"name": "container 0", "vertices": [{"vertex_0": {"id": 0, "label": "vertex 0", "properties": {}}::vertex}, {"vertex_0": {"id": 0, "label": "vertex 0", "properties": {}}::vertex}]}');
SELECT agtype_in('{"name": "container 1", "edges": [{"id": 3, "label": "edge 0", "end_id": 1, "start_id": 0, "properties": {}}::edge, {"id": 4, "label": "edge 1", "end_id": 0, "start_id": 1, "properties": {}}::edge]}');
SELECT agtype_in('{"name": "path 1", "path": [{"id": 0, "label": "vertex 0", "properties": {}}::vertex, {"id": 2, "label": "edge 0", "end_id": 1, "start_id": 0, "properties": {}}::edge, {"id": 1, "label": "vertex 1", "properties": {}}::vertex]}');

-- typecast to path
SELECT agtype_in('[{"id": 0, "label": "vertex 0", "properties": {}}::vertex, {"id": 2, "label": "edge 0", "end_id": 1, "start_id": 0, "properties": {}}::edge, {"id": 1, "label": "vertex 1", "properties": {}}::vertex]::path');
SELECT agtype_in('{"Path" : [{"id": 0, "label": "vertex 0", "properties": {}}::vertex, {"id": 2, "label": "edge 0", "end_id": 1, "start_id": 0, "properties": {}}::edge, {"id": 1, "label": "vertex 1", "properties": {}}::vertex]::path}');
SELECT * FROM cypher('expr', $$ RETURN [{id: 0, label: "vertex 0", properties: {}}::vertex, {id: 2, label: "edge 0", end_id: 1, start_id: 0, properties: {}}::edge, {id: 1, label: "vertex 1", properties: {}}::vertex]::path $$) AS r(result agtype);
SELECT * FROM cypher('expr', $$ RETURN {path : [{id: 0, label: "vertex 0", properties: {}}::vertex, {id: 2, label: "edge 0", end_id: 1, start_id: 0, properties: {}}::edge, {id: 1, label: "vertex 1", properties: {}}::vertex]::path} $$) AS r(result agtype);
-- verify that the output can be input
SELECT agtype_in('[{"id": 0, "label": "vertex 0", "properties": {}}::vertex, {"id": 2, "label": "edge 0", "end_id": 1, "start_id": 0, "properties": {}}::edge, {"id": 1, "label": "vertex 1", "properties": {}}::vertex]::path');
SELECT agtype_in('{"path": [{"id": 0, "label": "vertex 0", "properties": {}}::vertex, {"id": 2, "label": "edge 0", "end_id": 1, "start_id": 0, "properties": {}}::edge, {"id": 1, "label": "vertex 1", "properties": {}}::vertex]::path}');
-- invalid paths should fail
SELECT agtype_in('[{"id": 0, "label": "vertex 0", "properties": {}}::vertex, {"id": 2, "label": "edge 0", "end_id": 1, "start_id": 0, "properties": {}}::edge]::path');
SELECT agtype_in('{"Path" : [{"id": 2, "label": "edge 0", "end_id": 1, "start_id": 0, "properties": {}}::edge, {"id": 0, "label": "vertex 0", "properties": {}}::vertex, {"id": 2, "label": "edge 0", "end_id": 1, "start_id": 0, "properties": {}}::edge, {"id": 1, "label": "vertex 1", "properties": {}}::vertex]::path}');
SELECT * FROM cypher('expr', $$ RETURN [{id: 0, label: "vertex 0", properties: {}}::vertex]::path $$) AS r(result agtype);
SELECT * FROM cypher('expr', $$ RETURN [{id: 2, label: "edge 0", end_id: 1, start_id: 0, properties: {}}::edge]::path $$) AS r(result agtype);
SELECT * FROM cypher('expr', $$ RETURN []::path $$) AS r(result agtype);
-- should be JSON null
SELECT agtype_in('null::path');
-- should be SQL null
SELECT * FROM cypher('expr', $$ RETURN null::path $$) AS r(result agtype);
SELECT agtype_typecast_path(agtype_in('null'));
SELECT agtype_typecast_path(null);

-- test functions
-- create some vertices and edges
SELECT * FROM cypher('expr', $$CREATE (:v)$$) AS (a agtype);
SELECT * FROM cypher('expr', $$CREATE (:v {i: 0})$$) AS (a agtype);
SELECT * FROM cypher('expr', $$CREATE (:v {i: 1})$$) AS (a agtype);
SELECT * FROM cypher('expr', $$
    CREATE (:v1 {id:'initial'})-[:e1]->(:v1 {id:'middle'})-[:e1]->(:v1 {id:'end'})
$$) AS (a agtype);
-- show them
SELECT * FROM cypher('expr', $$ MATCH (v) RETURN v $$) AS (expression agtype);
SELECT * FROM cypher('expr', $$ MATCH ()-[e]-() RETURN e $$) AS (expression agtype);
-- id()
SELECT * FROM cypher('expr', $$
    MATCH ()-[e]-() RETURN id(e)
$$) AS (id agtype);
SELECT * FROM cypher('expr', $$
    MATCH (v) RETURN id(v)
$$) AS (id agtype);
-- should return null
SELECT * FROM cypher('expr', $$
    RETURN id(null)
$$) AS (id agtype);
-- should error
SELECT * FROM cypher('expr', $$
    RETURN id()
$$) AS (id agtype);
-- start_id()
SELECT * FROM cypher('expr', $$
    MATCH ()-[e]-() RETURN start_id(e)
$$) AS (start_id agtype);
-- should return null
SELECT * FROM cypher('expr', $$
    RETURN start_id(null)
$$) AS (start_id agtype);
-- should error
SELECT * FROM cypher('expr', $$
    MATCH (v) RETURN start_id(v)
$$) AS (start_id agtype);
SELECT * FROM cypher('expr', $$
    RETURN start_id()
$$) AS (start_id agtype);
-- end_id()
SELECT * FROM cypher('expr', $$
    MATCH ()-[e]-() RETURN end_id(e)
$$) AS (end_id agtype);
-- should return null
SELECT * FROM cypher('expr', $$
    RETURN end_id(null)
$$) AS (end_id agtype);
-- should error
SELECT * FROM cypher('expr', $$
    MATCH (v) RETURN end_id(v)
$$) AS (end_id agtype);
SELECT * FROM cypher('expr', $$
    RETURN end_id()
$$) AS (end_id agtype);
-- startNode()
SELECT * FROM cypher('expr', $$
    MATCH ()-[e]-() RETURN id(e), start_id(e), startNode(e)
$$) AS (id agtype, start_id agtype, startNode agtype);
-- should return null
SELECT * FROM cypher('expr', $$
    RETURN startNode(null)
$$) AS (startNode agtype);
-- should error
SELECT * FROM cypher('expr', $$
    MATCH (v) RETURN startNode(v)
$$) AS (startNode agtype);
SELECT * FROM cypher('expr', $$
    RETURN startNode()
$$) AS (startNode agtype);
-- endNode()
SELECT * FROM cypher('expr', $$
    MATCH ()-[e]-() RETURN id(e), end_id(e), endNode(e)
$$) AS (id agtype, end_id agtype, endNode agtype);
-- should return null
SELECT * FROM cypher('expr', $$
    RETURN endNode(null)
$$) AS (endNode agtype);
-- should error
SELECT * FROM cypher('expr', $$
    MATCH (v) RETURN endNode(v)
$$) AS (endNode agtype);
SELECT * FROM cypher('expr', $$
    RETURN endNode()
$$) AS (endNode agtype);
-- type()
SELECT * FROM cypher('expr', $$
    MATCH ()-[e]-() RETURN type(e)
$$) AS (type agtype);
-- should return null
SELECT * FROM cypher('expr', $$
    RETURN type(null)
$$) AS (type agtype);
-- should error
SELECT * FROM cypher('expr', $$
    MATCH (v) RETURN type(v)
$$) AS (type agtype);
SELECT * FROM cypher('expr', $$
    RETURN type()
$$) AS (type agtype);
-- label ()
SELECT * FROM cypher('expr', $$
    MATCH (v) RETURN label(v)
$$) AS (label agtype);
SELECT * FROM cypher('expr', $$
    MATCH ()-[e]->() RETURN label(e)
$$) AS (label agtype);
SELECT * FROM cypher('expr', $$
    RETURN label({id: 0, label: 'typecast', properties: {}}::vertex)
$$) AS (label agtype);
-- return NULL
SELECT * FROM cypher('expr', $$
    RETURN label(NULL)
$$) AS (label agtype);
SELECT ag_catalog.age_label(NULL);
-- should error
SELECT * FROM cypher('expr', $$
    MATCH p=()-[]->() RETURN label(p)
$$) AS (label agtype);
SELECT * FROM cypher('expr', $$
    RETURN label(1)
$$) AS (label agtype);
SELECT * FROM cypher('expr', $$
    MATCH (n) RETURN label([n])
$$) AS (label agtype);
SELECT * FROM cypher('expr', $$
    RETURN label({id: 0, label: 'failed', properties: {}})
$$) AS (label agtype);
-- timestamp() can't be done as it will always have a different value
-- size() of a string
SELECT * FROM cypher('expr', $$
    RETURN size('12345')
$$) AS (size agtype);
SELECT * FROM cypher('expr', $$
    RETURN size("1234567890")
$$) AS (size agtype);
-- size() of an array
SELECT * FROM cypher('expr', $$
    RETURN size([1, 2, 3, 4, 5])
$$) AS (size agtype);
SELECT * FROM cypher('expr', $$
    RETURN size([])
$$) AS (size agtype);
-- should return null
SELECT * FROM cypher('expr', $$
    RETURN size(null)
$$) AS (size agtype);
-- should fail
SELECT * FROM cypher('expr', $$
    RETURN size(1234567890)
$$) AS (size agtype);
SELECT * FROM cypher('expr', $$
    RETURN size()
$$) AS (size agtype);
-- head() of an array
SELECT * FROM cypher('expr', $$
    RETURN head([1, 2, 3, 4, 5])
$$) AS (head agtype);
SELECT * FROM cypher('expr', $$
    RETURN head([1])
$$) AS (head agtype);
-- should return null
SELECT * FROM cypher('expr', $$
    RETURN head([])
$$) AS (head agtype);
SELECT * FROM cypher('expr', $$
    RETURN head(null)
$$) AS (head agtype);
-- should fail
SELECT * FROM cypher('expr', $$
    RETURN head(1234567890)
$$) AS (head agtype);
SELECT * FROM cypher('expr', $$
    RETURN head()
$$) AS (head agtype);
-- last()
SELECT * FROM cypher('expr', $$
    RETURN last([1, 2, 3, 4, 5])
$$) AS (last agtype);
SELECT * FROM cypher('expr', $$
    RETURN last([1])
$$) AS (last agtype);
-- should return null
SELECT * FROM cypher('expr', $$
    RETURN last([])
$$) AS (last agtype);
SELECT * FROM cypher('expr', $$
    RETURN last(null)
$$) AS (last agtype);
-- should fail
SELECT * FROM cypher('expr', $$
    RETURN last(1234567890)
$$) AS (last agtype);
SELECT * FROM cypher('expr', $$
    RETURN last()
$$) AS (last agtype);
-- properties()
SELECT * FROM cypher('expr', $$
    MATCH (v) RETURN properties(v)
$$) AS (properties agtype);
SELECT * FROM cypher('expr', $$
    MATCH ()-[e]-() RETURN properties(e)
$$) AS (properties agtype);
-- should return null
SELECT * FROM cypher('expr', $$
    RETURN properties(null)
$$) AS (properties agtype);
-- should fail
SELECT * FROM cypher('expr', $$
    RETURN properties(1234)
$$) AS (properties agtype);
SELECT * FROM cypher('expr', $$
    RETURN properties()
$$) AS (properties agtype);
-- coalesce
SELECT * FROM cypher('expr', $$
    RETURN coalesce(null, 1, null, null)
$$) AS (coalesce agtype);
SELECT * FROM cypher('expr', $$
    RETURN coalesce(null, -3.14, null, null)
$$) AS (coalesce agtype);
SELECT * FROM cypher('expr', $$
    RETURN coalesce(null, "string", null, null)
$$) AS (coalesce agtype);
SELECT * FROM cypher('expr', $$
    RETURN coalesce(null, null, null, [])
$$) AS (coalesce agtype);
SELECT * FROM cypher('expr', $$
    RETURN coalesce(null, null, null, {})
$$) AS (coalesce agtype);
-- should return null
SELECT * FROM cypher('expr', $$
    RETURN coalesce(null, id(null), null)
$$) AS (coalesce agtype);
SELECT * FROM cypher('expr', $$
    RETURN coalesce(null)
$$) AS (coalesce agtype);
-- should fail
SELECT * FROM cypher('expr', $$
    RETURN coalesce()
$$) AS (coalesce agtype);
-- toBoolean()
SELECT * FROM cypher('expr', $$
    RETURN toBoolean(true)
$$) AS (toBoolean agtype);
SELECT * FROM cypher('expr', $$
    RETURN toBoolean(false)
$$) AS (toBoolean agtype);
SELECT * FROM cypher('expr', $$
    RETURN toBoolean("true")
$$) AS (toBoolean agtype);
SELECT * FROM cypher('expr', $$
    RETURN toBoolean("false")
$$) AS (toBoolean agtype);
-- should return null
SELECT * FROM cypher('expr', $$
    RETURN toBoolean("false_")
$$) AS (toBoolean agtype);
SELECT * FROM cypher('expr', $$
    RETURN toBoolean(null)
$$) AS (toBoolean agtype);
-- should fail
SELECT * FROM cypher('expr', $$
    RETURN toBoolean(1)
$$) AS (toBoolean agtype);
SELECT * FROM cypher('expr', $$
    RETURN toBoolean()
$$) AS (toBoolean agtype);

-- toBooleanList()
SELECT * FROM cypher('expr', $$
    RETURN toBooleanList([true, false, true])
$$) AS (toBooleanList agtype);

SELECT * FROM cypher('expr', $$
    RETURN toBooleanList(["true", "false", "true"])
$$) AS (toBooleanList agtype);

SELECT * FROM cypher('expr', $$
    RETURN toBooleanList(["True", "False", "True"])
$$) AS (toBooleanList agtype);

-- should return null
SELECT * FROM cypher('expr', $$
    RETURN toBooleanList([])
$$) AS (toBooleanList agtype);

SELECT * FROM cypher('expr', $$
    RETURN toBooleanList([null, null, null])
$$) AS (toBooleanList agtype);

SELECT * FROM cypher('expr', $$
    RETURN toBooleanList(["Hello", "world!"])
$$) AS (toBooleanList agtype);

SELECT * FROM cypher('expr', $$
    RETURN toBooleanList([["A", "B"], ["C", "D"]])
$$) AS (toBooleanList agtype);

SELECT * FROM cypher('expr', $$
    RETURN toBooleanList([0,1,2,3,4])
$$) AS (toBooleanList agtype);

-- should fail
SELECT * FROM cypher('expr', $$
    RETURN toBooleanList(fail)
$$) AS (toBooleanList agtype);

SELECT * FROM cypher('expr', $$
    RETURN toBooleanList("fail")
$$) AS (toBooleanList agtype);

SELECT * FROM cypher('expr', $$
    RETURN toBooleanList(123)
$$) AS (toBooleanList agtype);

-- toFloat()
SELECT * FROM cypher('expr', $$
    RETURN toFloat(1)
$$) AS (toFloat agtype);
SELECT * FROM cypher('expr', $$
    RETURN toFloat(1.2)
$$) AS (toFloat agtype);
SELECT * FROM cypher('expr', $$
    RETURN toFloat("1")
$$) AS (toFloat agtype);
SELECT * FROM cypher('expr', $$
    RETURN toFloat("1.2")
$$) AS (toFloat agtype);
SELECT * FROM cypher('expr', $$
    RETURN toFloat("1.2"::numeric)
$$) AS (toFloat agtype);
-- should return null
SELECT * FROM cypher('expr', $$
    RETURN toFloat("false_")
$$) AS (toFloat agtype);
SELECT * FROM cypher('expr', $$
    RETURN toFloat(null)
$$) AS (toFloat agtype);
-- should fail
SELECT * FROM cypher('expr', $$
    RETURN toFloat(true)
$$) AS (toFloat agtype);
SELECT * FROM cypher('expr', $$
    RETURN toFloat()
$$) AS (toFloat agtype);
-- toFloatList()
SELECT * FROM cypher('expr', $$
    RETURN toFloatList([1.3])
$$) AS (toFloatList agtype);
SELECT * FROM cypher('expr', $$
    RETURN toFloatList([1.2, '4.654'])
$$) AS (toFloatList agtype);
SELECT * FROM cypher('expr', $$
    RETURN toFloatList(['1.9432', 8.6222, '9.4111212', 344.22])
$$) AS (toFloatList agtype);
SELECT * FROM cypher('expr', $$
    RETURN toFloatList(['999.2'])
$$) AS (toFloatList agtype);
SELECT * FROM cypher('expr', $$
    RETURN toFloatList([1.20002])
$$) AS (toFloatList agtype);
-- should return null
SELECT * FROM cypher('expr', $$
    RETURN toFloatList(['true'])
$$) AS (toFloatList agtype);
SELECT * FROM cypher('expr', $$
    RETURN toFloatList([null])
$$) AS (toFloatList agtype);
-- should fail
SELECT * FROM cypher('expr', $$
    RETURN toFloatList([failed])
$$) AS (toFloatList agtype);
SELECT * FROM cypher('expr', $$
    RETURN toFloatList("failed")
$$) AS (toFloatList agtype);
SELECT * FROM cypher('expr', $$
    RETURN toFloatList(555)
$$) AS (toFloatList agtype);
-- toInteger()
SELECT * FROM cypher('expr', $$
    RETURN toInteger(1)
$$) AS (toInteger agtype);
SELECT * FROM cypher('expr', $$
    RETURN toInteger(1.2)
$$) AS (toInteger agtype);
SELECT * FROM cypher('expr', $$
    RETURN toInteger("1")
$$) AS (toInteger agtype);
SELECT * FROM cypher('expr', $$
    RETURN toInteger("1.2")
$$) AS (toInteger agtype);
SELECT * FROM cypher('expr', $$
    RETURN toInteger("1.2"::numeric)
$$) AS (toInteger agtype);
-- should return null
SELECT * FROM cypher('expr', $$
    RETURN toInteger("false_")
$$) AS (toInteger agtype);
SELECT * FROM cypher('expr', $$
    RETURN toInteger(null)
$$) AS (toInteger agtype);
-- should fail
SELECT * FROM cypher('expr', $$
    RETURN toInteger(true)
$$) AS (toInteger agtype);
SELECT * FROM cypher('expr', $$
    RETURN toInteger()
$$) AS (toInteger agtype);
-- toIntegerList()
SELECT * FROM cypher('expr', $$
    RETURN toIntegerList([1, 7.8, 9.0, '88'])
$$) AS (toIntegerList agtype);
SELECT * FROM cypher('expr', $$
    RETURN toIntegerList([4.2, '123', '8', 8])
$$) AS (toIntegerList agtype);
SELECT * FROM cypher('expr', $$
    RETURN toIntegerList(['41', '12', 2])
$$) AS (toIntegerList agtype);
SELECT * FROM cypher('expr', $$
    RETURN toIntegerList([1, 2, 3, '10.2'])
$$) AS (toIntegerList agtype);
SELECT * FROM cypher('expr', $$
    RETURN toIntegerList([0000])
$$) AS (toIntegerList agtype);
-- should return null
SELECT * FROM cypher('expr', $$
    RETURN toIntegerList(["false_", 'asdsad', '123k1kdk1'])
$$) AS (toIntegerList agtype);
SELECT * FROM cypher('expr', $$
    RETURN toIntegerList([null, '123false', 'one'])
$$) AS (toIntegerList agtype);
-- should fail
SELECT * FROM cypher('expr', $$
    RETURN toIntegerList(123, '123')
$$) AS (toIntegerList agtype);
SELECT * FROM cypher('expr', $$
    RETURN toIntegerList(32[])
$$) AS (toIntegerList agtype);
-- length() of a path
SELECT * FROM cypher('expr', $$
    RETURN length([{id: 0, label: "vertex 0", properties: {}}::vertex, {id: 2, label: "edge 0", end_id: 1, start_id: 0, properties: {}}::edge, {id: 1, label: "vertex 1", properties: {}}::vertex]::path)
$$) AS (length agtype);
SELECT * FROM cypher('expr', $$
    RETURN length([{id: 0, label: "vertex 0", properties: {}}::vertex, {id: 2, label: "edge 0", end_id: 1, start_id: 0, properties: {}}::edge, {id: 1, label: "vertex 1", properties: {}}::vertex, {id: 2, label: "edge 0", end_id: 1, start_id: 0, properties: {}}::edge, {id: 1, label: "vertex 1", properties: {}}::vertex]::path)
$$) AS (length agtype);
-- should return null
SELECT * FROM cypher('expr', $$
    RETURN length(null)
$$) AS (length agtype);
-- should fail
SELECT * FROM cypher('expr', $$
    RETURN length(true)
$$) AS (length agtype);
SELECT * FROM cypher('expr', $$
    RETURN length()
$$) AS (length agtype);

--
-- toString()
--

-- PG types
SELECT * FROM age_toString(3);
SELECT * FROM age_toString(3.14);
SELECT * FROM age_toString(3.14::float);
SELECT * FROM age_toString(3.14::numeric);
SELECT * FROM age_toString(true);
SELECT * FROM age_toString(false);
SELECT * FROM age_toString('a string');
SELECT * FROM age_toString('a cstring'::cstring);
SELECT * FROM age_toString('a text string'::text);
SELECT * FROM age_toString(pg_typeof(3.14));
-- agtypes
SELECT * FROM age_toString(agtype_in('3'));
SELECT * FROM age_toString(agtype_in('3.14'));
SELECT * FROM age_toString(agtype_in('3.14::float'));
SELECT * FROM age_toString(agtype_in('3.14::numeric'));
SELECT * FROM age_toString(agtype_in('true'));
SELECT * FROM age_toString(agtype_in('false'));
SELECT * FROM age_toString(agtype_in('"a string"'));
SELECT * FROM cypher('expr', $$ RETURN toString(3.14::numeric) $$) AS (results agtype);
-- should return null
SELECT * FROM age_toString(NULL);
SELECT * FROM age_toString(agtype_in(null));
-- should fail
SELECT * FROM age_toString();
SELECT * FROM cypher('expr', $$ RETURN toString() $$) AS (results agtype);
-- toStringList() --
SELECT * FROM cypher('expr', $$ 
    RETURN toStringList([5, 10, 7.8, 9, 1.3]) 
$$) AS (toStringList agtype);
SELECT * FROM cypher('expr', $$ 
    RETURN toStringList(['test', 89, 'again', 7.1, 9]) 
$$) AS (toStringList agtype);
SELECT * FROM cypher('expr', $$ 
    RETURN toStringList([null, false, true, 'string']) 
$$) AS (toStringList agtype);
SELECT * FROM cypher('expr', $$ 
    RETURN toStringList([9.123456789, 5.123, 1.12345, 0.123123]) 
$$) AS (toStringList agtype);
-- should return null
SELECT * FROM cypher('expr', $$ 
    RETURN toStringList([null]) 
$$) AS (toStringList agtype);
SELECT * FROM cypher('expr', $$ 
    RETURN toStringList([true, false, true, true]) 
$$) AS (toStringList agtype);
-- should fail
SELECT * FROM cypher('expr', $$ 
    RETURN toStringList([['a', b]]) 
$$) AS (toStringList agtype);
SELECT * FROM cypher('expr', $$ 
    RETURN toStringList([test]) 
$$) AS (toStringList agtype);

--
-- reverse(string)
--
SELECT * FROM cypher('expr', $$
    RETURN reverse("gnirts a si siht")
$$) AS (results agtype);
SELECT * FROM age_reverse('gnirts a si siht');
SELECT * FROM age_reverse('gnirts a si siht'::text);
SELECT * FROM age_reverse('gnirts a si siht'::cstring);
-- should return null
SELECT * FROM cypher('expr', $$
    RETURN reverse(null)
$$) AS (results agtype);
SELECT * FROM age_reverse(null);
-- should return error
SELECT * FROM age_reverse([4923, 'abc', 521, NULL, 487]);
-- Should return the reversed list
SELECT * FROM cypher('expr', $$
    RETURN reverse([4923, 'abc', 521, NULL, 487])
$$) AS (u agtype);
SELECT * FROM cypher('expr', $$
    RETURN reverse([4923])
$$) AS (u agtype);
SELECT * FROM cypher('expr', $$
    RETURN reverse([4923, 257])
$$) as (u agtype);
SELECT * FROM cypher('expr', $$
    RETURN reverse([4923, 257, null])
$$) as (u agtype);
SELECT * FROM cypher('expr', $$
    RETURN reverse([4923, 257, 'tea'])
$$) as (u agtype);
SELECT * FROM cypher('expr', $$
    RETURN reverse([[1, 4, 7], 4923, [1, 2, 3], 'abc', 521, NULL, 487, ['fgt', 7, 10]])
$$) as (u agtype);
SELECT * FROM cypher('expr', $$
    RETURN reverse([4923, 257, {test1: "key"}])
$$) as (u agtype);
SELECT * FROM cypher('expr', $$
    RETURN reverse([4923, 257, {test2: [1, 2, 3]}])
$$) as (u agtype);
SELECT * FROM cypher('expr', $$
    CREATE ({test: [1, 2, 3]})
$$) as (u agtype);
SELECT * FROM cypher('expr', $$
    MATCH (v) WHERE exists(v.test) RETURN reverse(v.test)
$$) as (u agtype);

-- should fail
SELECT * FROM cypher('expr', $$
    RETURN reverse(true)
$$) AS (results agtype);
SELECT * FROM age_reverse(true);
SELECT * FROM cypher('expr', $$
    RETURN reverse(3.14)
$$) AS (results agtype);
SELECT * FROM age_reverse(3.14);
SELECT * FROM cypher('expr', $$
    RETURN reverse()
$$) AS (results agtype);
SELECT * FROM age_reverse();

--
-- toUpper() and toLower()
--
SELECT * FROM cypher('expr', $$
    RETURN toUpper('to uppercase')
$$) AS (toUpper agtype);
SELECT * FROM cypher('expr', $$
    RETURN toLower('TO LOWERCASE')
$$) AS (toLower agtype);
SELECT * FROM age_toupper('text'::text);
SELECT * FROM age_toupper('cstring'::cstring);
SELECT * FROM age_tolower('TEXT'::text);
SELECT * FROM age_tolower('CSTRING'::cstring);
-- should return null
SELECT * FROM cypher('expr', $$
    RETURN toUpper(null)
$$) AS (toUpper agtype);
SELECT * FROM cypher('expr', $$
    RETURN toLower(null)
$$) AS (toLower agtype);
SELECT * FROM age_toupper(null);
SELECT * FROM age_tolower(null);
-- should fail
SELECT * FROM cypher('expr', $$
    RETURN toUpper(true)
$$) AS (toUpper agtype);
SELECT * FROM cypher('expr', $$
    RETURN toUpper()
$$) AS (toUpper agtype);
SELECT * FROM cypher('expr', $$
    RETURN toLower(true)
$$) AS (toLower agtype);
SELECT * FROM cypher('expr', $$
    RETURN toLower()
$$) AS (toLower agtype);
SELECT * FROM age_toupper();
SELECT * FROM age_tolower();

--
-- lTrim(), rTrim(), trim()
--

SELECT * FROM cypher('expr', $$
    RETURN lTrim("  string   ")
$$) AS (results agtype);
SELECT * FROM cypher('expr', $$
    RETURN rTrim("  string   ")
$$) AS (results agtype);
SELECT * FROM cypher('expr', $$
    RETURN trim("  string   ")
$$) AS (results agtype);
SELECT * FROM age_ltrim('  string   ');
SELECT * FROM age_rtrim('  string   ');
SELECT * FROM age_trim('  string   ');
-- should return null
SELECT * FROM cypher('expr', $$
    RETURN lTrim(null)
$$) AS (results agtype);
SELECT * FROM cypher('expr', $$
    RETURN rTrim(null)
$$) AS (results agtype);
SELECT * FROM cypher('expr', $$
    RETURN trim(null)
$$) AS (results agtype);
SELECT * FROM age_ltrim(null);
SELECT * FROM age_rtrim(null);
SELECT * FROM age_trim(null);
-- should fail
SELECT * FROM cypher('expr', $$
    RETURN lTrim(true)
$$) AS (results agtype);
SELECT * FROM cypher('expr', $$
    RETURN rTrim(true)
$$) AS (results agtype);
SELECT * FROM cypher('expr', $$
    RETURN trim(true)
$$) AS (results agtype);
SELECT * FROM cypher('expr', $$
    RETURN lTrim()
$$) AS (results agtype);
SELECT * FROM cypher('expr', $$
    RETURN rTrim()
$$) AS (results agtype);
SELECT * FROM cypher('expr', $$
    RETURN trim()
$$) AS (results agtype);

SELECT * FROM age_ltrim();
SELECT * FROM age_rtrim();
SELECT * FROM age_trim();

--
-- left(), right(), & substring()
-- left()
SELECT * FROM cypher('expr', $$
    RETURN left("123456789", 1)
$$) AS (results agtype);
SELECT * FROM cypher('expr', $$
    RETURN left("123456789", 3)
$$) AS (results agtype);
-- should return null
SELECT * FROM cypher('expr', $$
    RETURN left("123456789", 0)
$$) AS (results agtype);
SELECT * FROM cypher('expr', $$
    RETURN left(null, 1)
$$) AS (results agtype);
SELECT * FROM cypher('expr', $$
    RETURN left(null, null)
$$) AS (results agtype);
SELECT * FROM age_left(null, 1);
SELECT * FROM age_left(null, null);
-- should fail
SELECT * FROM cypher('expr', $$
    RETURN left("123456789", null)
$$) AS (results agtype);
SELECT * FROM cypher('expr', $$
    RETURN left("123456789", -1)
$$) AS (results agtype);
SELECT * FROM cypher('expr', $$
    RETURN left()
$$) AS (results agtype);
SELECT * FROM age_left('123456789', null);
SELECT * FROM age_left('123456789', -1);
SELECT * FROM age_left();
--right()
SELECT * FROM cypher('expr', $$
    RETURN right("123456789", 1)
$$) AS (results agtype);
SELECT * FROM cypher('expr', $$
    RETURN right("123456789", 3)
$$) AS (results agtype);
-- should return null
SELECT * FROM cypher('expr', $$
    RETURN right("123456789", 0)
$$) AS (results agtype);
SELECT * FROM cypher('expr', $$
    RETURN right(null, 1)
$$) AS (results agtype);
SELECT * FROM cypher('expr', $$
    RETURN right(null, null)
$$) AS (results agtype);
SELECT * FROM age_right(null, 1);
SELECT * FROM age_right(null, null);
-- should fail
SELECT * FROM cypher('expr', $$
    RETURN right("123456789", null)
$$) AS (results agtype);
SELECT * FROM cypher('expr', $$
    RETURN right("123456789", -1)
$$) AS (results agtype);
SELECT * FROM cypher('expr', $$
    RETURN right()
$$) AS (results agtype);
SELECT * FROM age_right('123456789', null);
SELECT * FROM age_right('123456789', -1);
SELECT * FROM age_right();
-- substring()
SELECT * FROM cypher('expr', $$
    RETURN substring("0123456789", 0, 1)
$$) AS (results agtype);
SELECT * FROM cypher('expr', $$
    RETURN substring("0123456789", 1, 3)
$$) AS (results agtype);
SELECT * FROM cypher('expr', $$
    RETURN substring("0123456789", 3)
$$) AS (results agtype);
SELECT * FROM cypher('expr', $$
    RETURN substring("0123456789", 0)
$$) AS (results agtype);
SELECT * FROM age_substring('0123456789', 3, 2);
SELECT * FROM age_substring('0123456789', 1);
-- should return null
SELECT * FROM cypher('expr', $$
    RETURN substring(null, null, null)
$$) AS (results agtype);
SELECT * FROM cypher('expr', $$
    RETURN substring(null, null)
$$) AS (results agtype);
SELECT * FROM cypher('expr', $$
    RETURN substring(null, 1)
$$) AS (results agtype);
SELECT * FROM age_substring(null, null, null);
SELECT * FROM age_substring(null, null);
SELECT * FROM age_substring(null, 1);
-- should fail
SELECT * FROM cypher('expr', $$
    RETURN substring("123456789", null)
$$) AS (results agtype);
SELECT * FROM cypher('expr', $$
    RETURN substring("123456789", 0, -1)
$$) AS (results agtype);
SELECT * FROM cypher('expr', $$
    RETURN substring("123456789", -1)
$$) AS (results agtype);
SELECT * FROM cypher('expr', $$
    RETURN substring("123456789")
$$) AS (results agtype);
SELECT * FROM age_substring('123456789', null);
SELECT * FROM age_substring('123456789', 0, -1);
SELECT * FROM age_substring('123456789', -1);
SELECT * FROM age_substring();

--
-- split()
--
SELECT * FROM cypher('expr', $$
    RETURN split("a,b,c,d,e,f", ",")
$$) AS (results agtype);
SELECT * FROM cypher('expr', $$
    RETURN split("a,b,c,d,e,f", "")
$$) AS (results agtype);
SELECT * FROM cypher('expr', $$
    RETURN split("a,b,c,d,e,f", " ")
$$) AS (results agtype);
SELECT * FROM cypher('expr', $$
    RETURN split("a,b,cd  e,f", " ")
$$) AS (results agtype);
SELECT * FROM cypher('expr', $$
    RETURN split("a,b,cd  e,f", "  ")
$$) AS (results agtype);
SELECT * FROM cypher('expr', $$
    RETURN split("a,b,c,d,e,f", "c,")
$$) AS (results agtype);
-- should return null
SELECT * FROM cypher('expr', $$
    RETURN split(null, null)
$$) AS (results agtype);
SELECT * FROM cypher('expr', $$
    RETURN split("a,b,c,d,e,f", null)
$$) AS (results agtype);
SELECT * FROM cypher('expr', $$
    RETURN split(null, ",")
$$) AS (results agtype);
SELECT * FROM age_split(null, null);
SELECT * FROM age_split('a,b,c,d,e,f', null);
SELECT * FROM age_split(null, ',');
-- should fail
SELECT * FROM cypher('expr', $$
    RETURN split(123456789, ",")
$$) AS (results agtype);
SELECT * FROM cypher('expr', $$
    RETURN split("a,b,c,d,e,f", -1)
$$) AS (results agtype);
SELECT * FROM cypher('expr', $$
    RETURN split("a,b,c,d,e,f")
$$) AS (results agtype);
SELECT * FROM cypher('expr', $$
    RETURN split()
$$) AS (results agtype);
SELECT * FROM age_split(123456789, ',');
SELECT * FROM age_split('a,b,c,d,e,f', -1);
SELECT * FROM age_split('a,b,c,d,e,f');
SELECT * FROM age_split();

--
-- replace()
--
SELECT * FROM cypher('expr', $$
    RETURN replace("Hello", "lo", "p")
$$) AS (results agtype);
SELECT * FROM cypher('expr', $$
    RETURN replace("Hello", "hello", "Good bye")
$$) AS (results agtype);
SELECT * FROM cypher('expr', $$
    RETURN replace("abcabcabc", "abc", "a")
$$) AS (results agtype);
SELECT * FROM cypher('expr', $$
    RETURN replace("abcabcabc", "ab", "")
$$) AS (results agtype);
SELECT * FROM cypher('expr', $$
    RETURN replace("ababab", "ab", "ab")
$$) AS (results agtype);
-- should return null
SELECT * FROM cypher('expr', $$
    RETURN replace(null, null, null)
$$) AS (results agtype);
SELECT * FROM cypher('expr', $$
    RETURN replace("Hello", null, null)
$$) AS (results agtype);
SELECT * FROM cypher('expr', $$
    RETURN replace("Hello", "", null)
$$) AS (results agtype);
SELECT * FROM cypher('expr', $$
    RETURN replace("", "", "")
$$) AS (results agtype);
SELECT * FROM cypher('expr', $$
    RETURN replace("Hello", "Hello", "")
$$) AS (results agtype);
SELECT * FROM cypher('expr', $$
    RETURN replace("", "Hello", "Mellow")
$$) AS (results agtype);
SELECT * FROM age_replace(null, null, null);
SELECT * FROM age_replace('Hello', null, null);
SELECT * FROM age_replace('Hello', '', null);
SELECT * FROM age_replace('', '', '');
SELECT * FROM age_replace('Hello', 'Hello', '');
SELECT * FROM age_replace('', 'Hello', 'Mellow');
-- should fail
SELECT * FROM cypher('expr', $$
    RETURN replace()
$$) AS (results agtype);
SELECT * FROM cypher('expr', $$
    RETURN replace("Hello")
$$) AS (results agtype);
SELECT * FROM cypher('expr', $$
    RETURN replace("Hello", null)
$$) AS (results agtype);
SELECT * FROM cypher('expr', $$
    RETURN replace("Hello", "e", 1)
$$) AS (results agtype);
SELECT * FROM cypher('expr', $$
    RETURN replace("Hello", 1, "e")
$$) AS (results agtype);
SELECT * FROM age_replace();
SELECT * FROM age_replace(null);
SELECT * FROM age_replace(null, null);
SELECT * FROM age_replace('Hello', 'e', 1);
SELECT * FROM age_replace('Hello', 1, 'E');

--
-- sin, cos, tan, cot
--
SELECT sin = results FROM cypher('expr', $$
    RETURN sin(3.1415)
$$) AS (results agtype), sin(3.1415);
SELECT cos = results FROM cypher('expr', $$
    RETURN cos(3.1415)
$$) AS (results agtype), cos(3.1415);
SELECT tan = results FROM cypher('expr', $$
    RETURN tan(3.1415)
$$) AS (results agtype), tan(3.1415);
SELECT cot = results FROM cypher('expr', $$
    RETURN cot(3.1415)
$$) AS (results agtype), cot(3.1415);
SELECT sin = age_sin FROM sin(3.1415), age_sin(3.1415);
SELECT cos = age_cos FROM cos(3.1415), age_cos(3.1415);
SELECT tan = age_tan FROM tan(3.1415), age_tan(3.1415);
SELECT cot = age_cot FROM cot(3.1415), age_cot(3.1415);
-- should return null
SELECT * FROM cypher('expr', $$
    RETURN sin(null)
$$) AS (results agtype);
SELECT * FROM cypher('expr', $$
    RETURN cos(null)
$$) AS (results agtype);
SELECT * FROM cypher('expr', $$
    RETURN tan(null)
$$) AS (results agtype);
SELECT * FROM cypher('expr', $$
    RETURN cot(null)
$$) AS (results agtype);
SELECT * FROM age_sin(null);
SELECT * FROM age_cos(null);
SELECT * FROM age_tan(null);
SELECT * FROM age_cot(null);
-- should fail
SELECT * FROM cypher('expr', $$
    RETURN sin("0")
$$) AS (results agtype);
SELECT * FROM cypher('expr', $$
    RETURN cos("0")
$$) AS (results agtype);
SELECT * FROM cypher('expr', $$
    RETURN tan("0")
$$) AS (results agtype);
SELECT * FROM cypher('expr', $$
    RETURN cot("0")
$$) AS (results agtype);
SELECT * FROM cypher('expr', $$
    RETURN sin()
$$) AS (results agtype);
SELECT * FROM cypher('expr', $$
    RETURN cos()
$$) AS (results agtype);
SELECT * FROM cypher('expr', $$
    RETURN tan()
$$) AS (results agtype);
SELECT * FROM cypher('expr', $$
    RETURN cot()
$$) AS (results agtype);
SELECT * FROM age_sin('0');
SELECT * FROM age_cos('0');
SELECT * FROM age_tan('0');
SELECT * FROM age_cot('0');
SELECT * FROM age_sin();
SELECT * FROM age_cos();
SELECT * FROM age_tan();
SELECT * FROM age_cot();

--
-- Arc functions: asin, acos, atan, & atan2
--
SELECT * FROM cypher('expr', $$
    RETURN asin(1)*2
$$) AS (results agtype);
SELECT * FROM cypher('expr', $$
    RETURN acos(0)*2
$$) AS (results agtype);
SELECT * FROM cypher('expr', $$
    RETURN atan(1)*4
$$) AS (results agtype);
SELECT * FROM cypher('expr', $$
    RETURN atan2(1, 1)*4
$$) AS (results agtype);
SELECT * FROM asin(1), age_asin(1);
SELECT * FROM acos(0), age_acos(0);
SELECT * FROM atan(1), age_atan(1);
SELECT * FROM atan2(1, 1), age_atan2(1, 1);
-- should return null
SELECT * FROM cypher('expr', $$
    RETURN asin(1.1)
$$) AS (results agtype);
SELECT * FROM cypher('expr', $$
    RETURN acos(1.1)
$$) AS (results agtype);
SELECT * FROM cypher('expr', $$
    RETURN asin(-1.1)
$$) AS (results agtype);
SELECT * FROM cypher('expr', $$
    RETURN acos(-1.1)
$$) AS (results agtype);
SELECT * FROM cypher('expr', $$
    RETURN asin(null)
$$) AS (results agtype);
SELECT * FROM cypher('expr', $$
    RETURN acos(null)
$$) AS (results agtype);
SELECT * FROM cypher('expr', $$
    RETURN atan(null)
$$) AS (results agtype);
SELECT * FROM cypher('expr', $$
    RETURN atan2(null, null)
$$) AS (results agtype);
SELECT * FROM cypher('expr', $$
    RETURN atan2(null, 1)
$$) AS (results agtype);
SELECT * FROM cypher('expr', $$
    RETURN atan2(1, null)
$$) AS (results agtype);
SELECT * FROM age_asin(null);
SELECT * FROM age_acos(null);
SELECT * FROM age_atan(null);
SELECT * FROM age_atan2(null, null);
SELECT * FROM age_atan2(1, null);
SELECT * FROM age_atan2(null, 1);
-- should fail
SELECT * FROM cypher('expr', $$
    RETURN asin("0")
$$) AS (results agtype);
SELECT * FROM cypher('expr', $$
    RETURN acos("0")
$$) AS (results agtype);
SELECT * FROM cypher('expr', $$
    RETURN atan("0")
$$) AS (results agtype);
SELECT * FROM cypher('expr', $$
    RETURN atan2("0", 1)
$$) AS (results agtype);
SELECT * FROM cypher('expr', $$
    RETURN atan2(0, "1")
$$) AS (results agtype);
SELECT * FROM cypher('expr', $$
    RETURN asin()
$$) AS (results agtype);
SELECT * FROM cypher('expr', $$
    RETURN acos()
$$) AS (results agtype);
SELECT * FROM cypher('expr', $$
    RETURN atan()
$$) AS (results agtype);
SELECT * FROM cypher('expr', $$
    RETURN atan2()
$$) AS (results agtype);
SELECT * FROM cypher('expr', $$
    RETURN atan2(null)
$$) AS (results agtype);
SELECT * FROM age_asin('0');
SELECT * FROM age_acos('0');
SELECT * FROM age_atan('0');
SELECT * FROM age_atan2('0', 1);
SELECT * FROM age_atan2(1, '0');
SELECT * FROM age_asin();
SELECT * FROM age_acos();
SELECT * FROM age_atan();
SELECT * FROM age_atan2();
SELECT * FROM age_atan2(1);

--
-- pi
--
SELECT * FROM cypher('expr', $$
    RETURN pi()
$$) AS (results agtype);
SELECT * FROM cypher('expr', $$
    RETURN sin(pi())
$$) AS (results agtype);
SELECT * FROM cypher('expr', $$
    RETURN sin(pi()/4)
$$) AS (results agtype);
SELECT * FROM cypher('expr', $$
    RETURN cos(pi())
$$) AS (results agtype);
SELECT * FROM cypher('expr', $$
    RETURN cos(pi()/2)
$$) AS (results agtype);
SELECT * FROM cypher('expr', $$
    RETURN sin(pi()/2)
$$) AS (results agtype);
-- should fail
SELECT * FROM cypher('expr', $$
    RETURN pi(null)
$$) AS (results agtype);
SELECT * FROM cypher('expr', $$
    RETURN pi(1)
$$) AS (results agtype);

--
-- radians() & degrees()
--
SELECT * FROM cypher('expr', $$
    RETURN radians(0)
$$) AS (results agtype);
SELECT * FROM cypher('expr', $$
    RETURN degrees(0)
$$) AS (results agtype);
SELECT * FROM cypher('expr', $$
    RETURN radians(360), 2*pi()
$$) AS (results agtype, Two_PI agtype);
SELECT * FROM cypher('expr', $$
    RETURN degrees(2*pi())
$$) AS (results agtype);
SELECT * FROM cypher('expr', $$
    RETURN radians(180), pi()
$$) AS (results agtype, PI agtype);
SELECT * FROM cypher('expr', $$
    RETURN degrees(pi())
$$) AS (results agtype);
SELECT * FROM cypher('expr', $$
    RETURN radians(90), pi()/2
$$) AS (results agtype, Half_PI agtype);
SELECT * FROM cypher('expr', $$
    RETURN degrees(pi()/2)
$$) AS (results agtype);
-- should return null
SELECT * FROM cypher('expr', $$
    RETURN radians(null)
$$) AS (results agtype);
SELECT * FROM cypher('expr', $$
    RETURN degrees(null)
$$) AS (results agtype);
-- should fail
SELECT * FROM cypher('expr', $$
    RETURN radians()
$$) AS (results agtype);
SELECT * FROM cypher('expr', $$
    RETURN degrees()
$$) AS (results agtype);
SELECT * FROM cypher('expr', $$
    RETURN radians("1")
$$) AS (results agtype);
SELECT * FROM cypher('expr', $$
    RETURN degrees("1")
$$) AS (results agtype);

--
-- abs(), ceil(), floor(), & round()
--
SELECT * FROM cypher('expr', $$
    RETURN abs(0)
$$) AS (results agtype);
SELECT * FROM cypher('expr', $$
    RETURN abs(10)
$$) AS (results agtype);
SELECT * FROM cypher('expr', $$
    RETURN abs(-10)
$$) AS (results agtype);
SELECT * FROM cypher('expr', $$
    RETURN ceil(0)
$$) AS (results agtype);
SELECT * FROM cypher('expr', $$
    RETURN ceil(1)
$$) AS (results agtype);
SELECT * FROM cypher('expr', $$
    RETURN ceil(-1)
$$) AS (results agtype);
SELECT * FROM cypher('expr', $$
    RETURN ceil(1.01)
$$) AS (results agtype);
SELECT * FROM cypher('expr', $$
    RETURN ceil(-1.01)
$$) AS (results agtype);
SELECT * FROM cypher('expr', $$
    RETURN floor(0)
$$) AS (results agtype);
SELECT * FROM cypher('expr', $$
    RETURN floor(1)
$$) AS (results agtype);
SELECT * FROM cypher('expr', $$
    RETURN floor(-1)
$$) AS (results agtype);
SELECT * FROM cypher('expr', $$
    RETURN floor(1.01)
$$) AS (results agtype);
SELECT * FROM cypher('expr', $$
    RETURN floor(-1.01)
$$) AS (results agtype);
SELECT * FROM cypher('expr', $$
    RETURN round(0)
$$) AS (results agtype);
SELECT * FROM cypher('expr', $$
    RETURN round(4.49999999)
$$) AS (results agtype);
SELECT * FROM cypher('expr', $$
    RETURN round(4.5)
$$) AS (results agtype);
SELECT * FROM cypher('expr', $$
    RETURN round(-4.49999999)
$$) AS (results agtype);
SELECT * FROM cypher('expr', $$
    RETURN round(-4.5)
$$) AS (results agtype);
SELECT * FROM cypher('expr', $$
    RETURN round(7.4163, 3)
$$) AS (results agtype);
SELECT * FROM cypher('expr', $$
    RETURN round(7.416343479, 8)
$$) AS (results agtype);
SELECT * FROM cypher('expr', $$
    RETURN round(7.416343479, NULL)
$$) AS (results agtype);
SELECT * FROM cypher('expr', $$
    RETURN round(NULL, 7)
$$) AS (results agtype);
SELECT * FROM cypher('expr', $$
    RETURN round(7, 2)
$$) AS (results agtype);
SELECT * FROM cypher('expr', $$
    RETURN round(7.4342, 2.1123)
$$) AS (results agtype);
SELECT * FROM cypher('expr', $$
    RETURN round(NULL, NULL)
$$) AS (results agtype);
SELECT * FROM cypher('expr', $$
    RETURN sign(10)
$$) AS (results agtype);
SELECT * FROM cypher('expr', $$
    RETURN sign(-10)
$$) AS (results agtype);
SELECT * FROM cypher('expr', $$
    RETURN sign(0)
$$) AS (results agtype);
-- should return null
SELECT * FROM cypher('expr', $$
    RETURN abs(null)
$$) AS (results agtype);
SELECT * FROM cypher('expr', $$
    RETURN ceil(null)
$$) AS (results agtype);
SELECT * FROM cypher('expr', $$
    RETURN floor(null)
$$) AS (results agtype);
SELECT * FROM cypher('expr', $$
    RETURN round(null)
$$) AS (results agtype);
SELECT * FROM cypher('expr', $$
    RETURN sign(null)
$$) AS (results agtype);
-- should fail
SELECT * FROM cypher('expr', $$
    RETURN abs()
$$) AS (results agtype);
SELECT * FROM cypher('expr', $$
    RETURN ceil()
$$) AS (results agtype);
SELECT * FROM cypher('expr', $$
    RETURN floor()
$$) AS (results agtype);
SELECT * FROM cypher('expr', $$
    RETURN round()
$$) AS (results agtype);
SELECT * FROM cypher('expr', $$
    RETURN sign()
$$) AS (results agtype);
SELECT * FROM cypher('expr', $$
    RETURN abs("1")
$$) AS (results agtype);
SELECT * FROM cypher('expr', $$
    RETURN ceil("1")
$$) AS (results agtype);
SELECT * FROM cypher('expr', $$
    RETURN floor("1")
$$) AS (results agtype);
SELECT * FROM cypher('expr', $$
    RETURN round("1")
$$) AS (results agtype);
SELECT * FROM cypher('expr', $$
    RETURN sign("1")
$$) AS (results agtype);

--
-- rand()
--
-- should select 0 rows as rand() is in [0,1)
SELECT * FROM cypher('expr', $$
    RETURN rand()
$$) AS (result agtype)
WHERE result >= 1 or result < 0;
-- should select 0 rows as rand() should not return the same value
SELECT * FROM cypher('expr', $$
    RETURN rand()
$$) AS cypher_1(result agtype),
    cypher('expr', $$
    RETURN rand()
$$) AS cypher_2(result agtype)
WHERE cypher_1.result = cypher_2.result;

--
-- log (ln) and log10
--
SELECT * from cypher('expr', $$
    RETURN log(2.718281828459045)
$$) as (result agtype);
SELECT * from cypher('expr', $$
    RETURN log10(10)
$$) as (result agtype);
-- should return null
SELECT * from cypher('expr', $$
    RETURN log(null)
$$) as (result agtype);
SELECT * from cypher('expr', $$
    RETURN log10(null)
$$) as (result agtype);
SELECT * from cypher('expr', $$
    RETURN log(0)
$$) as (result agtype);
SELECT * from cypher('expr', $$
    RETURN log10(0)
$$) as (result agtype);
SELECT * from cypher('expr', $$
    RETURN log(-1)
$$) as (result agtype);
SELECT * from cypher('expr', $$
    RETURN log10(-1)
$$) as (result agtype);
-- should fail
SELECT * from cypher('expr', $$
    RETURN log()
$$) as (result agtype);
SELECT * from cypher('expr', $$
    RETURN log10()
$$) as (result agtype);

--
-- e()
--
SELECT * from cypher('expr', $$
    RETURN e()
$$) as (result agtype);
SELECT * from cypher('expr', $$
    RETURN log(e())
$$) as (result agtype);

--
-- exp() aka e^x
--
SELECT * from cypher('expr', $$
    RETURN exp(1)
$$) as (result agtype);
SELECT * from cypher('expr', $$
    RETURN exp(0)
$$) as (result agtype);
-- should return null
SELECT * from cypher('expr', $$
    RETURN exp(null)
$$) as (result agtype);
-- should fail
SELECT * from cypher('expr', $$
    RETURN exp()
$$) as (result agtype);
SELECT * from cypher('expr', $$
    RETURN exp("1")
$$) as (result agtype);

--
-- sqrt()
--
SELECT * from cypher('expr', $$
    RETURN sqrt(25)
$$) as (result agtype);
SELECT * from cypher('expr', $$
    RETURN sqrt(1)
$$) as (result agtype);
SELECT * from cypher('expr', $$
    RETURN sqrt(0)
$$) as (result agtype);
-- should return null
SELECT * from cypher('expr', $$
    RETURN sqrt(-1)
$$) as (result agtype);
SELECT * from cypher('expr', $$
    RETURN sqrt(null)
$$) as (result agtype);
-- should fail
SELECT * from cypher('expr', $$
    RETURN sqrt()
$$) as (result agtype);
SELECT * from cypher('expr', $$
    RETURN sqrt("1")
$$) as (result agtype);

--
-- user defined function expressions - using pg functions for these tests
--
SELECT * from cypher('expr', $$
    RETURN pg_catalog.sqrt(25::pg_float8)
$$) as (result agtype);
SELECT * from cypher('expr', $$
    RETURN pg_catalog.sqrt("25"::pg_float8)
$$) as (result agtype);
SELECT * from cypher('expr', $$
    RETURN ag_catalog.age_sqrt(25)
$$) as (result agtype);
-- should return null
SELECT * from cypher('expr', $$
    RETURN pg_catalog.sqrt(null::pg_float8)
$$) as (result agtype);
-- should fail
SELECT * from cypher('expr', $$
    RETURN pg_catalog.sqrt()
$$) as (result agtype);
SELECT * from cypher('expr', $$
    RETURN pg_catalog.sqrt(-1::pg_float8)
$$) as (result agtype);
SELECT * from cypher('expr', $$
    RETURN something.pg_catalog.sqrt("1"::pg_float8)
$$) as (result agtype);
-- should fail do to schema but using a reserved_keyword
SELECT * from cypher('expr', $$
    RETURN distinct.age_sqrt(25)
$$) as (result agtype);
SELECT * from cypher('expr', $$
    RETURN contains.age_sqrt(25)
$$) as (result agtype);

--
-- aggregate functions avg(), sum(), count(), & count(*)
--
SELECT create_graph('UCSC');
SELECT * FROM cypher('UCSC', $$CREATE (:students {name: "Jack", gpa: 3.0, age: 21, zip: 94110})$$) AS (a agtype);
SELECT * FROM cypher('UCSC', $$CREATE (:students {name: "Jill", gpa: 3.5, age: 27, zip: 95060})$$) AS (a agtype);
SELECT * FROM cypher('UCSC', $$CREATE (:students {name: "Jim", gpa: 3.75, age: 32, zip: 96062})$$) AS (a agtype);
SELECT * FROM cypher('UCSC', $$CREATE (:students {name: "Rick", gpa: 2.5, age: 24, zip: "95060"})$$) AS (a agtype);
SELECT * FROM cypher('UCSC', $$CREATE (:students {name: "Ann", gpa: 3.8::numeric, age: 23})$$) AS (a agtype);
SELECT * FROM cypher('UCSC', $$CREATE (:students {name: "Derek", gpa: 4.0, age: 19, zip: 90210})$$) AS (a agtype);
SELECT * FROM cypher('UCSC', $$CREATE (:students {name: "Jessica", gpa: 3.9::numeric, age: 20})$$) AS (a agtype);
SELECT * FROM cypher('UCSC', $$ MATCH (u) RETURN (u) $$) AS (vertex agtype);
SELECT * FROM cypher('UCSC', $$ MATCH (u) RETURN avg(u.gpa), sum(u.gpa), sum(u.gpa)/count(u.gpa), count(u.gpa), count(*) $$) 
AS (avg agtype, sum agtype, sum_divided_by_count agtype, count agtype, count_star agtype);
-- add in 2 null gpa records
SELECT * FROM cypher('UCSC', $$CREATE (:students {name: "Dave", age: 24})$$) AS (a agtype);
SELECT * FROM cypher('UCSC', $$CREATE (:students {name: "Mike", age: 18})$$) AS (a agtype);
SELECT * FROM cypher('UCSC', $$ MATCH (u) RETURN (u) $$) AS (vertex agtype);
SELECT * FROM cypher('UCSC', $$ MATCH (u) RETURN avg(u.gpa), sum(u.gpa), sum(u.gpa)/count(u.gpa), count(u.gpa), count(*) $$) 
AS (avg agtype, sum agtype, sum_divided_by_count agtype, count agtype, count_star agtype);
-- should return null
SELECT * FROM cypher('UCSC', $$ RETURN avg(NULL) $$) AS (avg agtype);
SELECT * FROM cypher('UCSC', $$ RETURN sum(NULL) $$) AS (sum agtype);
-- should return 0
SELECT * FROM cypher('UCSC', $$ RETURN count(NULL) $$) AS (count agtype);
-- should fail
SELECT * FROM cypher('UCSC', $$ RETURN avg() $$) AS (avg agtype);
SELECT * FROM cypher('UCSC', $$ RETURN sum() $$) AS (sum agtype);
SELECT * FROM cypher('UCSC', $$ RETURN count() $$) AS (count agtype);

--
-- aggregate functions min() & max()
--
SELECT * FROM cypher('UCSC', $$ MATCH (u) RETURN min(u.gpa), max(u.gpa), count(u.gpa), count(*) $$)
AS (min agtype, max agtype, count agtype, count_star agtype);
SELECT * FROM cypher('UCSC', $$ MATCH (u) RETURN min(u.gpa), max(u.gpa), count(u.gpa), count(*) $$)
AS (min agtype, max agtype, count agtype, count_star agtype);
SELECT * FROM cypher('UCSC', $$ MATCH (u) RETURN min(u.name), max(u.name), count(u.name), count(*) $$)
AS (min agtype, max agtype, count agtype, count_star agtype);
-- check that min() & max() can work against mixed types
SELECT * FROM cypher('UCSC', $$ MATCH (u) RETURN min(u.zip), max(u.zip), count(u.zip), count(*) $$)
AS (min agtype, max agtype, count agtype, count_star agtype);
CREATE TABLE min_max_tbl (oid oid);
insert into min_max_tbl VALUES (16), (17188), (1000), (869);

SELECT age_min(oid::int), age_max(oid::int) FROM min_max_tbl;
SELECT age_min(oid::int::float), age_max(oid::int::float) FROM min_max_tbl;
SELECT age_min(oid::int::float::numeric), age_max(oid::int::float::numeric) FROM min_max_tbl;
SELECT age_min(oid::text), age_max(oid::text) FROM min_max_tbl;

DROP TABLE min_max_tbl;
-- should return null
SELECT * FROM cypher('UCSC', $$ RETURN min(NULL) $$) AS (min agtype);
SELECT * FROM cypher('UCSC', $$ RETURN max(NULL) $$) AS (max agtype);
SELECT age_min(NULL);
SELECT age_min(agtype_in('null'));
SELECT age_max(NULL);
SELECT age_max(agtype_in('null'));
-- should fail
SELECT * FROM cypher('UCSC', $$ RETURN min() $$) AS (min agtype);
SELECT * FROM cypher('UCSC', $$ RETURN max() $$) AS (max agtype);
SELECT age_min();
SELECT age_min();
--
-- aggregate functions stDev() & stDevP()
--
SELECT * FROM cypher('UCSC', $$ MATCH (u) RETURN stDev(u.gpa), stDevP(u.gpa) $$)
AS (stDev agtype, stDevP agtype);
-- should return 0
SELECT * FROM cypher('UCSC', $$ RETURN stDev(NULL) $$) AS (stDev agtype);
SELECT * FROM cypher('UCSC', $$ RETURN stDevP(NULL) $$) AS (stDevP agtype);
-- should fail
SELECT * FROM cypher('UCSC', $$ RETURN stDev() $$) AS (stDev agtype);
SELECT * FROM cypher('UCSC', $$ RETURN stDevP() $$) AS (stDevP agtype);

--
-- aggregate functions percentileCont() & percentileDisc()
--
SELECT * FROM cypher('UCSC', $$ MATCH (u) RETURN percentileCont(u.gpa, .55), percentileDisc(u.gpa, .55), percentileCont(u.gpa, .9), percentileDisc(u.gpa, .9) $$)
AS (percentileCont1 agtype, percentileDisc1 agtype, percentileCont2 agtype, percentileDisc2 agtype);
SELECT * FROM cypher('UCSC', $$ MATCH (u) RETURN percentileCont(u.gpa, .55) $$)
AS (percentileCont agtype);
SELECT * FROM cypher('UCSC', $$ MATCH (u) RETURN percentileDisc(u.gpa, .55) $$)
AS (percentileDisc agtype);
-- should return null
SELECT * FROM cypher('UCSC', $$ RETURN percentileCont(NULL, .5) $$) AS (percentileCont agtype);
SELECT * FROM cypher('UCSC', $$ RETURN percentileDisc(NULL, .5) $$) AS (percentileDisc agtype);
-- should fail
SELECT * FROM cypher('UCSC', $$ RETURN percentileCont(.5, NULL) $$) AS (percentileCont agtype);
SELECT * FROM cypher('UCSC', $$ RETURN percentileDisc(.5, NULL) $$) AS (percentileDisc agtype);

--
-- aggregate function collect()
--
SELECT * FROM cypher('UCSC', $$ MATCH (u) RETURN collect(u.name), collect(u.age), collect(u.gpa), collect(u.zip) $$)
AS (name agtype, age agtype, gqa agtype, zip agtype);
SELECT * FROM cypher('UCSC', $$ MATCH (u) RETURN collect(u.gpa), collect(u.gpa) $$)
AS (gpa1 agtype, gpa2 agtype);
SELECT * FROM cypher('UCSC', $$ MATCH (u) RETURN collect(u.zip), collect(u.zip) $$)
AS (zip1 agtype, zip2 agtype);
SELECT * FROM cypher('UCSC', $$ RETURN collect(5) $$) AS (result agtype);
-- should return an empty array
SELECT * FROM cypher('UCSC', $$ RETURN collect(NULL) $$) AS (empty agtype);
SELECT * FROM cypher('UCSC', $$ MATCH (u) WHERE u.name =~ "doesn't exist" RETURN collect(u.name) $$) AS (name agtype);

-- should fail
SELECT * FROM cypher('UCSC', $$ RETURN collect() $$) AS (collect agtype);

-- test DISTINCT inside aggregate functions
SELECT * FROM cypher('UCSC', $$CREATE (:students {name: "Sven", gpa: 3.2, age: 27, zip: 94110})$$)
AS (a agtype);
SELECT * FROM cypher('UCSC', $$ MATCH (u) RETURN (u) $$) AS (vertex agtype);
SELECT * FROM cypher('UCSC', $$ MATCH (u) RETURN count(u.zip), count(DISTINCT u.zip) $$)
AS (zip agtype, distinct_zip agtype);
SELECT * FROM cypher('UCSC', $$ MATCH (u) RETURN count(u.age), count(DISTINCT u.age) $$)
AS (age agtype, distinct_age agtype);

-- test AUTO GROUP BY for aggregate functions
SELECT create_graph('group_by');
SELECT * FROM cypher('group_by', $$CREATE (:row {i: 1, j: 2, k:3})$$) AS (result agtype);
SELECT * FROM cypher('group_by', $$CREATE (:row {i: 1, j: 2, k:4})$$) AS (result agtype);
SELECT * FROM cypher('group_by', $$CREATE (:row {i: 1, j: 3, k:5})$$) AS (result agtype);
SELECT * FROM cypher('group_by', $$CREATE (:row {i: 2, j: 3, k:6})$$) AS (result agtype);
SELECT * FROM cypher('group_by', $$MATCH (u:row) RETURN u.i, u.j, u.k$$) AS (i agtype, j agtype, k agtype);
SELECT * FROM cypher('group_by', $$MATCH (u:row) RETURN u.i, u.j, sum(u.k)$$) AS (i agtype, j agtype, sumk agtype);
SELECT * FROM cypher('group_by', $$CREATE (:L {a: 1, b: 2, c:3})$$) AS (result agtype);
SELECT * FROM cypher('group_by', $$CREATE (:L {a: 2, b: 3, c:1})$$) AS (result agtype);
SELECT * FROM cypher('group_by', $$CREATE (:L {a: 3, b: 1, c:2})$$) AS (result agtype);
SELECT * FROM cypher('group_by', $$MATCH (x:L) RETURN x.a, x.b, x.c, x.a + count(*) + x.b + count(*) + x.c$$)
AS (a agtype, b agtype, c agtype, result agtype);
SELECT * FROM cypher('group_by', $$MATCH (x:L) RETURN x.a + x.b + x.c, x.a + x.b + x.c + count(*) + count(*) $$)
AS (a_b_c agtype,  result agtype);
-- with WITH clause
SELECT * FROM cypher('group_by', $$MATCH(x:L) WITH x, count(x) AS c RETURN x.a + x.b + x.c + c$$)
AS (result agtype);
SELECT * FROM cypher('group_by', $$MATCH(x:L) WITH x, count(x) AS c RETURN x.a + x.b + x.c + c + c$$)
AS (result agtype);
SELECT * FROM cypher('group_by', $$MATCH(x:L) WITH x.a + x.b + x.c AS v, count(x) as c RETURN v + c + c $$)
AS (result agtype);
-- should fail
SELECT * FROM cypher('group_by', $$MATCH (x:L) RETURN x.a, x.a + count(*) + x.b + count(*) + x.c$$)
AS (a agtype, result agtype);
SELECT * FROM cypher('group_by', $$MATCH (x:L) RETURN x.a + count(*) + x.b + count(*) + x.c$$)
AS (result agtype);

--ORDER BY
SELECT create_graph('order_by');
SELECT * FROM cypher('order_by', $$CREATE ()$$) AS (result agtype);
SELECT * FROM cypher('order_by', $$CREATE ({i: '1'})$$) AS (result agtype);
SELECT * FROM cypher('order_by', $$CREATE ({i: 1})$$) AS (result agtype);
SELECT * FROM cypher('order_by', $$CREATE ({i: 1.0})$$) AS (result agtype);
SELECT * FROM cypher('order_by', $$CREATE ({i: 1::numeric})$$) AS (result agtype);
SELECT * FROM cypher('order_by', $$CREATE ({i: true})$$) AS (result agtype);
SELECT * FROM cypher('order_by', $$CREATE ({i: false})$$) AS (result agtype);
SELECT * FROM cypher('order_by', $$CREATE ({i: {key: 'value'}})$$) AS (result agtype);
SELECT * FROM cypher('order_by', $$CREATE ({i: [1]})$$) AS (result agtype);

SELECT * FROM cypher('order_by', $$
	MATCH (u)
	RETURN u.i
	ORDER BY u.i
$$) AS (i agtype);

SELECT * FROM cypher('order_by', $$
	MATCH (u)
	RETURN u.i
	ORDER BY u.i DESC
$$) AS (i agtype);

--CASE
SELECT create_graph('case_statement');
SELECT * FROM cypher('case_statement', $$CREATE ({id: 1, i: 1, j: null})-[:connected_to {id: 1, k:0}]->({id: 2, i: 'a', j: 'b'})$$) AS (result agtype);
SELECT * FROM cypher('case_statement', $$CREATE ({id: 3, i: 0, j: 1})-[:connected_to {id: 2, k:1}]->({id: 4, i: true, j: false})$$) AS (result agtype);
SELECT * FROM cypher('case_statement', $$CREATE ({id: 5, i: [], j: [0,1,2]})$$) AS (result agtype);
SELECT * FROM cypher('case_statement', $$CREATE ({id: 6, i: {}, j: {i:1}})$$) AS (result agtype);

--standalone case & edge cases
--base case
SELECT * FROM cypher('case_statement', $$ RETURN (CASE WHEN true THEN true END) $$) as (a agtype);
--should return 1 empty row
SELECT * FROM cypher('case_statement', $$ RETURN (CASE WHEN false THEN true END) $$) as (a agtype);
--should return 'false'
SELECT * FROM cypher('case_statement', $$ RETURN (CASE WHEN true THEN false END) $$) as (a agtype);
--invalid case (WHEN should be boolean)
SELECT * FROM cypher('case_statement', $$ RETURN (CASE WHEN 1 THEN 'fail' END) $$) as (a agtype);

-- booleans + logic gates
SELECT * FROM cypher('case_statement', $$ RETURN (CASE WHEN true THEN (true AND true) END) $$) as (a agtype);
-- invalid mixed logic gate
SELECT * FROM cypher('case_statement', $$ RETURN (CASE WHEN true THEN (true AND 1) END) $$) as (a agtype);



--CASE WHEN condition THEN result END
SELECT * FROM cypher('case_statement', $$
	MATCH (n)
	RETURN n.i, n.j, CASE
    WHEN null THEN 'should not return me'
		WHEN n.i = 1 THEN 'i is 1'
		WHEN n.j = 'b' THEN 'j is b'
    WHEN n.i = 0 AND n.j = 1 THEN '0 AND 1'
    WHEN n.i = true OR n.j = true THEN 'i or j true'
		ELSE 'default'
	END
$$ ) AS (i agtype, j agtype, case_statement agtype);

--CASE expression WHEN value THEN result END
SELECT * FROM cypher('case_statement', $$
	MATCH (n)
	RETURN n.j, CASE n.j
    WHEN null THEN 'should not return me'
    WHEN 'b' THEN 'b'
    WHEN 1 THEN 1
    WHEN false THEN false
    WHEN [0,1,2] THEN [0,1,2]
    WHEN {i:1} THEN {i:1}
		ELSE 'not a or b'
	END
$$ ) AS (j agtype, case_statement agtype);

--CASE agtype_vertex WHEN value THEN result END
SELECT * FROM cypher('case_statement', $$
  MATCH (n)
  RETURN CASE n
    WHEN null THEN 'should not return me'
    WHEN 'agtype_string' THEN 'wrong'
    WHEN n THEN n
    ELSE 'no n'
  END
$$ ) AS (case_statement agtype);

--CASE with match and edges
SELECT * FROM cypher('case_statement', $$
  MATCH (n)-[e]->(m)
  RETURN CASE
    WHEN null THEN 'should not return me'
    WHEN n.i = 1 THEN n
    WHEN n.i = 0 THEN m
    ELSE 'none'
  END
$$ ) AS (case_statement agtype);

SELECT * FROM cypher('case_statement', $$
  MATCH (n)-[e]->(m)
  RETURN CASE
    WHEN null THEN 'should not return me'
    WHEN e.k = 1 THEN e
    WHEN e.k = 0 THEN e
    ELSE 'none'
  END
$$ ) AS (case_statement agtype);


-- RETURN * and (u)--(v) optional forms
SELECT create_graph('opt_forms');
SELECT * FROM cypher('opt_forms', $$CREATE ({i:1})-[:KNOWS]->({i:2})<-[:KNOWS]-({i:3})$$)AS (result agtype);
SELECT * FROM cypher('opt_forms', $$MATCH (u) RETURN u$$) AS (result agtype);
SELECT * FROM cypher('opt_forms', $$MATCH (u) RETURN *$$) AS (result agtype);
SELECT * FROM cypher('opt_forms', $$MATCH (u)--(v) RETURN u.i, v.i$$) AS (u agtype, v agtype);
SELECT * FROM cypher('opt_forms', $$MATCH (u)-->(v) RETURN u.i, v.i$$) AS (u agtype, v agtype);
SELECT * FROM cypher('opt_forms', $$MATCH (u)<--(v) RETURN u.i, v.i$$) AS (u agtype, v agtype);
SELECT * FROM cypher('opt_forms', $$MATCH (u)-->()<--(v) RETURN u.i, v.i$$) AS (u agtype, v agtype);
SELECT * FROM cypher('opt_forms', $$MATCH (u) CREATE (u)-[:edge]->() RETURN *$$) AS (results agtype);
SELECT * FROM cypher('opt_forms', $$MATCH (u)-->()<--(v) RETURN *$$) AS (col1 agtype, col2 agtype);

-- Added typecasts ::pg_bigint and ::pg_float8
SELECT * FROM cypher('expr', $$
RETURN true::pg_bigint
$$) AS (result agtype);
SELECT * FROM cypher('expr', $$
RETURN "1.0"::pg_float8
$$) AS (result agtype);
SELECT * from cypher('expr', $$
RETURN pg_catalog.sqrt(pg_catalog.sqrt(pg_catalog.sqrt(256::pg_bigint)))
$$) as (result agtype);
SELECT * from cypher('expr', $$
RETURN pg_catalog.sqrt(pg_catalog.sqrt(pg_catalog.sqrt(256::pg_float8)))
$$) as (result agtype);

-- VLE
SELECT create_graph('VLE');
-- should return 0 rows
SELECT * FROM cypher('VLE', $$MATCH (u)-[*]-(v) RETURN u, v$$) AS (u agtype, v agtype);
SELECT * FROM cypher('VLE', $$MATCH (u)-[*0..1]-(v) RETURN u, v$$) AS (u agtype, v agtype);
SELECT * FROM cypher('VLE', $$MATCH (u)-[*..1]-(v) RETURN u, v$$) AS (u agtype, v agtype);
SELECT * FROM cypher('VLE', $$MATCH (u)-[*..5]-(v) RETURN u, v$$) AS (u agtype, v agtype);

-- Create a graph to test
SELECT * FROM cypher('VLE', $$CREATE (b:begin)-[:edge {name: 'main edge', number: 1, dangerous: {type: "all", level: "all"}}]->(u1:middle)-[:edge {name: 'main edge', number: 2, dangerous: {type: "all", level: "all"}, packages: [2,4,6]}]->(u2:middle)-[:edge {name: 'main edge', number: 3, dangerous: {type: "all", level: "all"}}]->(u3:middle)-[:edge {name: 'main edge', number: 4, dangerous: {type: "all", level: "all"}}]->(e:end), (u1)-[:self_loop {name: 'self loop', number: 1, dangerous: {type: "all", level: "all"}}]->(u1), (e)-[:self_loop {name: 'self loop', number: 2, dangerous: {type: "all", level: "all"}}]->(e), (b)-[:alternate_edge {name: 'alternate edge', number: 1, packages: [2,4,6], dangerous: {type: "poisons", level: "all"}}]->(u1), (u2)-[:alternate_edge {name: 'alternate edge', number: 2, packages: [2,4,6], dangerous: {type: "poisons", level: "all"}}]->(u3), (u3)-[:alternate_edge {name: 'alternate edge', number: 3, packages: [2,4,6], dangerous: {type: "poisons", level: "all"}}]->(e), (u2)-[:bypass_edge {name: 'bypass edge', number: 1, packages: [1,3,5,7]}]->(e), (e)-[:alternate_edge {name: 'backup edge', number: 1, packages: [1,3,5,7]}]->(u3), (u3)-[:alternate_edge {name: 'backup edge', number: 2, packages: [1,3,5,7]}]->(u2), (u2)-[:bypass_edge {name: 'bypass edge', number: 2, packages: [1,3,5,7], dangerous: {type: "poisons", level: "all"}}]->(b) RETURN b, e $$) AS (b agtype, e agtype);

-- test vertex_stats command
SELECT * FROM cypher('VLE', $$ MATCH (u) RETURN vertex_stats(u) $$) AS (result agtype);

-- test indirection operator for a function
SELECT * FROM cypher('VLE', $$ MATCH (u) WHERE vertex_stats(u).self_loops <> 0 RETURN vertex_stats(u) $$) AS (result agtype);
SELECT * FROM cypher('VLE', $$ MATCH (u) WHERE vertex_stats(u).in_degree < vertex_stats(u).out_degree RETURN vertex_stats(u) $$) AS (result agtype);
SELECT * FROM cypher('VLE', $$ MATCH (u) WHERE vertex_stats(u).out_degree < vertex_stats(u).in_degree RETURN vertex_stats(u) $$) AS (result agtype);


-- list functions relationships(), range(), keys()
SELECT create_graph('keys');
-- keys()
SELECT * FROM cypher('keys', $$CREATE ({name: 'hikaru utada', age: 38, job: 'singer'})-[:collaborated_with {song:"face my fears"}]->( {name: 'sonny moore', age: 33, stage_name: 'skrillex', job: 'producer'})$$) AS (result agtype);
SELECT * FROM cypher('keys', $$CREATE ({name: 'alexander guy cook', age: 31, stage_name:"a. g. cook", job: 'producer'})$$) AS (result agtype);
SELECT * FROM cypher('keys', $$CREATE ({name: 'keiko fuji', age: 62, job: 'singer'})$$) AS (result agtype);
SELECT * FROM cypher('keys', $$MATCH (a),(b) WHERE a.name = 'hikaru utada' AND b.name = 'alexander guy cook' CREATE (a)-[:collaborated_with {song:"one last kiss"}]->(b)$$) AS (result agtype);
SELECT * FROM cypher('keys', $$MATCH (a),(b) WHERE a.name = 'hikaru utada' AND b.name = 'keiko fuji' CREATE (a)-[:knows]->(b)$$) AS (result agtype);
SELECT * FROM cypher('keys', $$MATCH (v) RETURN keys(v)$$) AS (vertex_keys agtype);
SELECT * FROM cypher('keys', $$MATCH ()-[e]-() RETURN keys(e)$$) AS (edge_keys agtype);
SELECT * FROM cypher('keys', $$RETURN keys({a:1,b:'two',c:[1,2,3]})$$) AS (keys agtype);

--should return empty list
SELECT * FROM cypher('keys', $$RETURN keys({})$$) AS (keys agtype);
--should return sql null
SELECT * FROM cypher('keys', $$RETURN keys(null)$$) AS (keys agtype);
--should return error
SELECT * from cypher('keys', $$RETURN keys([1,2,3])$$) as (keys agtype);
SELECT * from cypher('keys', $$RETURN keys("string")$$) as (keys agtype);
SELECT * from cypher('keys', $$MATCH u=()-[]-() RETURN keys(u)$$) as (keys agtype);

SELECT create_graph('list');
SELECT * from cypher('list', $$CREATE p=({name:"rick"})-[:knows]->({name:"morty"}) RETURN p$$) as (path agtype);
SELECT * from cypher('list', $$CREATE p=({name:'rachael'})-[:knows]->({name:'monica'})-[:knows]->({name:'phoebe'}) RETURN p$$) as (path agtype);
-- nodes()
SELECT * from cypher('list', $$MATCH p=()-[]->() RETURN nodes(p)$$) as (nodes agtype);
SELECT * from cypher('list', $$MATCH p=()-[]->()-[]->() RETURN nodes(p)$$) as (nodes agtype);
-- should return nothing
SELECT * from cypher('list', $$MATCH p=()-[]->()-[]->()-[]->() RETURN nodes(p)$$) as (nodes agtype);
-- should return SQL NULL
SELECT * from cypher('list', $$RETURN nodes(NULL)$$) as (nodes agtype);
-- should return an error
SELECT * from cypher('list', $$MATCH (u) RETURN nodes([1,2,3])$$) as (nodes agtype);
SELECT * from cypher('list', $$MATCH (u) RETURN nodes("string")$$) as (nodes agtype);
SELECT * from cypher('list', $$MATCH (u) RETURN nodes(u)$$) as (nodes agtype);
SELECT * from cypher('list', $$MATCH (u)-[]->() RETURN nodes(u)$$) as (nodes agtype);
-- relationships()
SELECT * from cypher('list', $$MATCH p=()-[]->() RETURN relationships(p)$$) as (relationships agtype);
SELECT * from cypher('list', $$MATCH p=()-[]->()-[]->() RETURN relationships(p)$$) as (relationships agtype);
-- should return nothing
SELECT * from cypher('list', $$MATCH p=()-[]->()-[]->()-[]->() RETURN relationships(p)$$) as (relationships agtype);
-- should return SQL NULL
SELECT * from cypher('list', $$RETURN relationships(NULL)$$) as (relationships agtype);
-- should return an error
SELECT * from cypher('list', $$MATCH (u) RETURN relationships([1,2,3])$$) as (relationships agtype);
SELECT * from cypher('list', $$MATCH (u) RETURN relationships("string")$$) as (relationships agtype);
SELECT * from cypher('list', $$MATCH (u) RETURN relationships(u)$$) as (relationships agtype);
SELECT * from cypher('list', $$MATCH ()-[e]->() RETURN relationships(e)$$) as (relationships agtype);
-- range()
SELECT * from cypher('list', $$RETURN range(0, 10)$$) as (range agtype);
SELECT * from cypher('list', $$RETURN range(0, 10, null)$$) as (range agtype);
SELECT * from cypher('list', $$RETURN range(0, 10, 1)$$) as (range agtype);
SELECT * from cypher('list', $$RETURN range(0, 10, 3)$$) as (range agtype);
SELECT * from cypher('list', $$RETURN range(0, -10, -1)$$) as (range agtype);
SELECT * from cypher('list', $$RETURN range(0, -10, -3)$$) as (range agtype);
SELECT * from cypher('list', $$RETURN range(0, 10, 11)$$) as (range agtype);
SELECT * from cypher('list', $$RETURN range(-20, 10, 5)$$) as (range agtype);
-- should return an empty list []
SELECT * from cypher('list', $$RETURN range(0, -10)$$) as (range agtype);
SELECT * from cypher('list', $$RETURN range(0, 10, -1)$$) as (range agtype);
SELECT * from cypher('list', $$RETURN range(-10, 10, -1)$$) as (range agtype);
-- should return an error
SELECT * from cypher('list', $$RETURN range(null, -10, -3)$$) as (range agtype);
SELECT * from cypher('list', $$RETURN range(0, null, -3)$$) as (range agtype);
SELECT * from cypher('list', $$RETURN range(0, -10.0, -3.0)$$) as (range agtype);
-- tail()
-- should return the last elements of the list
SELECT * FROM cypher('list', $$ RETURN tail([1,2,3,4,5]) $$) AS (tail agtype);
SELECT * FROM cypher('list', $$ RETURN tail(["a","b","c","d","e"]) $$) AS (tail agtype);
-- should return null
SELECT * FROM cypher('list', $$ RETURN tail([1]) $$) AS (tail agtype);
SELECT * FROM cypher('list', $$ RETURN tail([]) $$) AS (tail agtype);
-- should throw errors
SELECT * FROM cypher('list', $$ RETURN tail(123) $$) AS (tail agtype);
SELECT * FROM cypher('list', $$ RETURN tail(abc) $$) AS (tail agtype);
SELECT * FROM cypher('list', $$ RETURN tail() $$) AS (tail agtype);
-- labels()
SELECT * from cypher('list', $$CREATE (u:People {name: "John"}) RETURN u$$) as (Vertices agtype);
SELECT * from cypher('list', $$CREATE (u:People {name: "Larry"}) RETURN u$$) as (Vertices agtype);
SELECT * from cypher('list', $$CREATE (u:Cars {name: "G35"}) RETURN u$$) as (Vertices agtype);
SELECT * from cypher('list', $$CREATE (u:Cars {name: "MR2"}) RETURN u$$) as (Vertices agtype);
SELECT * from cypher('list', $$MATCH (u) RETURN labels(u), u$$) as (Labels agtype, Vertices agtype);
-- should return SQL NULL
SELECT * from cypher('list', $$RETURN labels(NULL)$$) as (Labels agtype);
-- should return an error
SELECT * from cypher('list', $$RETURN labels("string")$$) as (Labels agtype);

-- Issue 989: Impossible to create an object with an array field of more than
--            100 elements.
SELECT * FROM cypher('list', $$ CREATE (any_vertex: test_label { `largeArray`: [] }) RETURN any_vertex $$) AS (u agtype);
SELECT * FROM cypher('list', $$ CREATE (any_vertex: test_label { `largeArray`: [0] }) RETURN any_vertex $$) AS (u agtype);
SELECT * FROM cypher('list', $$ CREATE (any_vertex: test_label { `largeArray`: [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63, 64, 65, 66, 67, 68, 69, 70, 71, 72, 73, 74, 75, 76, 77, 78, 79, 80, 81, 82, 83, 84, 85, 86, 87, 88, 89, 90, 91, 92, 93, 94, 95, 96, 97, 98, 99] }) RETURN any_vertex $$) AS (u agtype);
SELECT * FROM cypher('list', $$ CREATE (any_vertex: test_label { `largeArray`: [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63, 64, 65, 66, 67, 68, 69, 70, 71, 72, 73, 74, 75, 76, 77, 78, 79, 80, 81, 82, 83, 84, 85, 86, 87, 88, 89, 90, 91, 92, 93, 94, 95, 96, 97, 98, 99, 100] }) RETURN any_vertex $$) AS (u agtype);
SELECT * FROM cypher('list', $$ CREATE (any_vertex: test_label { `largeArray`: [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63, 64, 65, 66, 67, 68, 69, 70, 71, 72, 73, 74, 75, 76, 77, 78, 79, 80, 81, 82, 83, 84, 85, 86, 87, 88, 89, 90, 91, 92, 93, 94, 95, 96, 97, 98, 99, 100, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63, 64, 65, 66, 67, 68, 69, 70, 71, 72, 73, 74, 75, 76, 77, 78, 79, 80, 81, 82, 83, 84, 85, 86, 87, 88, 89, 90, 91, 92, 93, 94, 95, 96, 97, 98, 99] }) RETURN any_vertex $$) AS (u agtype);
SELECT * FROM cypher('list', $$ CREATE (any_vertex: test_label { `largeArray`: [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63, 64, 65, 66, 67, 68, 69, 70, 71, 72, 73, 74, 75, 76, 77, 78, 79, 80, 81, 82, 83, 84, 85, 86, 87, 88, 89, 90, 91, 92, 93, 94, 95, 96, 97, 98, 99, 100, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63, 64, 65, 66, 67, 68, 69, 70, 71, 72, 73, 74, 75, 76, 77, 78, 79, 80, 81, 82, 83, 84, 85, 86, 87, 88, 89, 90, 91, 92, 93, 94, 95, 96, 97, 98, 99, 100, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63, 64, 65, 66, 67, 68, 69, 70, 71, 72, 73, 74, 75, 76, 77, 78, 79, 80, 81, 82, 83, 84, 85, 86, 87, 88, 89, 90, 91, 92, 93, 94, 95, 96, 97, 98, 99, 100, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63, 64, 65, 66, 67, 68, 69, 70, 71, 72, 73, 74, 75, 76, 77, 78, 79, 80, 81, 82, 83, 84, 85, 86, 87, 88, 89, 90, 91, 92, 93, 94, 95, 96, 97, 98, 99] }) RETURN any_vertex $$) AS (u agtype);
SELECT * FROM cypher('list', $$ CREATE (any_vertex: test_label { `largeArray`: [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63, 64, 65, 66, 67, 68, 69, 70, 71, 72, 73, 74, 75, 76, 77, 78, 79, 80, 81, 82, 83, 84, 85, 86, 87, 88, 89, 90, 91, 92, 93, 94, 95, 96, 97, 98, 99, 100, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63, 64, 65, 66, 67, 68, 69, 70, 71, 72, 73, 74, 75, 76, 77, 78, 79, 80, 81, 82, 83, 84, 85, 86, 87, 88, 89, 90, 91, 92, 93, 94, 95, 96, 97, 98, 99, 100, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63, 64, 65, 66, 67, 68, 69, 70, 71, 72, 73, 74, 75, 76, 77, 78, 79, 80, 81, 82, 83, 84, 85, 86, 87, 88, 89, 90, 91, 92, 93, 94, 95, 96, 97, 98, 99, 100, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63, 64, 65, 66, 67, 68, 69, 70, 71, 72, 73, 74, 75, 76, 77, 78, 79, 80, 81, 82, 83, 84, 85, 86, 87, 88, 89, 90, 91, 92, 93, 94, 95, 96, 97, 98, 99, 100, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63, 64, 65, 66, 67, 68, 69, 70, 71, 72, 73, 74, 75, 76, 77, 78, 79, 80, 81, 82, 83, 84, 85, 86, 87, 88, 89, 90, 91, 92, 93, 94, 95, 96, 97, 98, 99, 100, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63, 64, 65, 66, 67, 68, 69, 70, 71, 72, 73, 74, 75, 76, 77, 78, 79, 80, 81, 82, 83, 84, 85, 86, 87, 88, 89, 90, 91, 92, 93, 94, 95, 96, 97, 98, 99, 100, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63, 64, 65, 66, 67, 68, 69, 70, 71, 72, 73, 74, 75, 76, 77, 78, 79, 80, 81, 82, 83, 84, 85, 86, 87, 88, 89, 90, 91, 92, 93, 94, 95, 96, 97, 98, 99, 100, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63, 64, 65, 66, 67, 68, 69, 70, 71, 72, 73, 74, 75, 76, 77, 78, 79, 80, 81, 82, 83, 84, 85, 86, 87, 88, 89, 90, 91, 92, 93, 94, 95, 96, 97, 98, 99] }) RETURN any_vertex $$) AS (u agtype);
-- should return 7 rows with counts: 0, 1, 100, 101, 200, 400, 800
SELECT * FROM cypher('list', $$ MATCH (u:test_label) RETURN size(u.largeArray) $$) AS (u agtype);
-- nested cases
SELECT * FROM cypher('list',$$ CREATE (n:xyz {array:[0, 1, 2, 3, 4, 5, 6, 7, 8, 9,
                                                10, 11, 12, 13, 14, 15, 16, 17, 18, 19,
                                                20,21, 22, 23, 24, 25, 26, 27, 28, 29,
                                                30, 31, 32, 33, 34, 35, 36, 37, 38, 39,
                                                40, 41, 42, 43, 44, 45, 46, 47, 48, 49,
                                                50, 51, 52, 53, 54, 55, 56, 57, 58, 59,
                                                60, 61, 62, 63, 64, 65, 66, 67, 68, 69,
                                                70, 71, 72, 73, 74, 75, 76, 77, 78, 79,
                                                80, 81, 82, 83, 84, 85, 86, 87, 88, 89,
                                                90, 91, 92, 93, 94, 95, 96, 97, 98, 99,
                                                [0, 1, 2, 3, 4, 5, 6, 7, 8, 9,
                                                10, 11, 12, 13, 14, 15, 16, 17, 18, 19,
                                                20,21, 22, 23, 24, 25, 26, 27, 28, 29,
                                                30, 31, 32, 33, 34, 35, 36, 37, 38, 39,
                                                40, 41, 42, 43, 44, 45, 46, 47, 48, 49,
                                                50, 51, 52, 53, 54, 55, 56, 57, 58, 59,
                                                60, 61, 62, 63, 64, 65, 66, 67, 68, 69,
                                                70, 71, 72, 73, 74, 75, 76, 77, 78, 79,
                                                80, 81, 82, 83, 84, 85, 86, 87, 88, 89,
                                                90, 91, 92, 93, 94, 95, 96, 97, 98, 99,
                                                100], 100]}) return n $$) as (a agtype);
SELECT * FROM cypher('list',$$ MATCH (n:xyz) CREATE (m:xyz {array:[0,1,2,3,n.array,5,6,7,8,9,
                                                           10, 11, 12, 13, 14, 15, 16, 17, 18, 19,
                                                           20,21, 22, 23, 24, 25, 26, 27, 28, 29,
                                                           30, 31, 32, 33, 34, 35, 36, 37, 38, 39,
                                                           40, 41, 42, 43, 44, 45, 46, 47, 48, 49,
                                                           50, 51, 52, 53, 54, 55, 56, 57, 58, 59,
                                                           60, 61, 62, 63, 64, 65, 66, 67, 68, 69,
                                                           70, 71, 72, 73, 74, 75, 76, 77, 78, 79,
                                                           80, 81, 82, 83, 84, 85, 86, 87, 88, 89,
                                                           90, 91, 92, 93, 94, 95, 96, 97, 98, 99,
                                                           100]}) return m $$) as (a agtype);
SELECT * FROM cypher('list',$$ MATCH (n:xyz) CREATE (m:xyz {array:[n.array,[n.array,[n.array]]]}) return m $$) as (a agtype);
-- SET
SELECT * FROM cypher('list',$$ CREATE (n:xyz)-[e:KNOWS {array:[0, 1, 2, 3, 4, 5, 6, 7, 8, 9,
                                                           10, 11, 12, 13, 14, 15, 16, 17, 18, 19,
                                                           20,21, 22, 23, 24, 25, 26, 27, 28, 29,
                                                           30, 31, 32, 33, 34, 35, 36, 37, 38, 39,
                                                           40, 41, 42, 43, 44, 45, 46, 47, 48, 49,
                                                           50, 51, 52, 53, 54, 55, 56, 57, 58, 59,
                                                           60, 61, 62, 63, 64, 65, 66, 67, 68, 69,
                                                           70, 71, 72, 73, 74, 75, 76, 77, 78, 79,
                                                           80, 81, 82, 83, 84, 85, 86, 87, 88, 89,
                                                           90, 91, 92, 93, 94, 95, 96, 97, 98, 99,
                                                           100]}]->(m:xyz) $$) as (a agtype);
SELECT * FROM cypher('list',$$ MATCH p=(n:xyz)-[e]->() SET n.array=[0, 1, 2, 3, 4, 5, 6, 7, 8, 9,
                                                           10, 11, 12, 13, 14, 15, 16, 17, 18, 19,
                                                           20,21, 22, 23, 24, 25, 26, 27, 28, 29,
                                                           30, 31, 32, 33, 34, 35, 36, 37, 38, 39,
                                                           40, 41, 42, 43, 44, 45, 46, 47, 48, 49,
                                                           50, 51, 52, 53, 54, 55, 56, 57, 58, 59,
                                                           60, 61, 62, 63, 64, 65, 66, 67, 68, 69,
                                                           70, 71, 72, 73, 74, 75, 76, 77, 78, 79,
                                                           80, 81, 82, 83, 84, 85, 86, 87, 88, 89,
                                                           90, 91, 92, 93, 94, 95, 96, 97, 98, 99,
                                                           100] return n,e $$) as (a agtype, b agtype);
SELECT * FROM cypher('list',$$ MATCH p=(n:xyz)-[e]->() SET n.array=[0, 1, 2, 3, 4, 5, 6, 7, 8, 9,
                                                           10, 11, 12, 13, 14, 15, 16, 17, 18, 19,
                                                           20,21, 22, 23, 24, 25, 26, 27, 28, 29,
                                                           30, 31, 32, 33, 34, 35, 36, 37, 38, 39,
                                                           40, 41, 42, 43, 44, 45, 46, 47, 48, 49,
                                                           50, 51, 52, 53, 54, 55, 56, 57, 58, 59,
                                                           60, 61, 62, 63, 64, 65, 66, 67, 68, 69,
                                                           70, 71, 72, 73, 74, 75, 76, 77, 78, 79,
                                                           80, 81, 82, 83, 84, 85, 86, 87, 88, 89,
                                                           90, 91, 92, 93, 94, 95, 96, 97, 98, 99,
                                                           e.array, 100] return n,e $$) as (a agtype, b agtype);

-- pg_typeof
SELECT * FROM cypher('expr', $$MATCH (u) RETURN toString(pg_catalog.pg_typeof(u.id)) $$) AS (u agtype);

-- issue: 395 aggregate function collect() incorrect container for operation
SELECT create_graph('graph_395');
SELECT * FROM cypher('graph_395', $$ CREATE (n:Project {name: 'Project A'}),
                                            (m:Project {name: 'Project B'}),
                                            (a:Task {name: 'Task A', size: 10}),
                                            (b:Task {name: 'Task B', size: 5}),
                                            (c:Task {name: 'Task C', size: 7}),
                                            (x:Person {name: 'John', age: 55}),
                                            (y:Person {name: 'Bob', age: 43}),
                                            (z:Person {name: 'Alice', age: 33}),
                                            (n)-[:Has]->(a),
                                            (n)-[:Has]->(b),
                                            (m)-[:Has]->(c),
                                            (a)-[:AssignedTo]->(x),
                                            (b)-[:AssignedTo]->(y),
                                            (c)-[:AssignedTo]->(y) $$) as (n agtype);

SELECT * FROM cypher('graph_395', $$ MATCH (p:Project)-[:Has]->(t:Task)-[:AssignedTo]->(u:Person)
                                     WITH p, t, collect(u) AS users
                                     WITH p, {tn: t.name, users: users} AS task
                                     RETURN task $$) AS (p agtype);

SELECT * FROM cypher('graph_395', $$ MATCH (p:Project)-[:Has]->(t:Task)-[:AssignedTo]->(u:Person)
                                     WITH p, t, collect(u) AS users
                                     WITH p, {tn: t.name, users: users} AS task
                                     WITH p, collect(task) AS tasks
                                     RETURN tasks $$) AS (p agtype);

SELECT * FROM cypher('graph_395', $$ MATCH (p:Project)-[:Has]->(t:Task)-[:AssignedTo]->(u:Person)
                                     WITH p, t, collect(u) AS users
                                     WITH p, {tn: t.name, users: users} AS task
                                     WITH p, collect(task) AS tasks
                                     WITH {pn: p.name, tasks:tasks} AS project
                                     RETURN project $$) AS (p agtype);

---
--- Fix: Segmentation fault when using specific names for tables #1124
---
--- The following are just a few commands to test SQLValueFunction types
---

SELECT count(*) FROM CURRENT_ROLE;
SELECT count(*) FROM CURRENT_USER;
SELECT count(*) FROM USER;
SELECT count(*) FROM SESSION_USER;
SELECT * FROM CURRENT_CATALOG;
SELECT * FROM CURRENT_SCHEMA;
SELECT * FROM create_graph('issue_1124');
SELECT results, pg_typeof(user) FROM cypher('issue_1124', $$ CREATE (u) RETURN u $$) AS (results agtype), user;
SELECT results, pg_typeof(user) FROM cypher('issue_1124', $$ MATCH (u) RETURN u $$) AS (results agtype), user;

--
-- issue 1303: segmentation fault on queries like SELECT * FROM agtype(null);
--

-- Test Const and CoerceViaIO expression node types
SELECT * FROM agtype(null);
SELECT * FROM agtype('1');
SELECT * FROM agtype('[1, 2, 3]');
SELECT * FROM agtype('{"a": 1}');
SELECT * FROM agtype('{"id": 844424930131971, "label": "v", "properties": {"i": 1}}::vertex');
SELECT * FROM agtype('{"id": 1407374883553282, "label": "e1", "end_id": 1125899906842626, "start_id": 1125899906842625, "properties": {}}::edge');
SELECT * FROM agtype('[{"id": 1125899906842625, "label": "v1", "properties": {"id": "initial"}}::vertex, {"id": 1407374883553282, "label": "e1", "end_id": 1125899906842626, "start_id": 1125899906842625, "properties": {}}::edge, {"id": 1125899906842626, "label": "v1", "properties": {"id": "middle"}}::vertex, {"id": 1407374883553281, "label": "e1", "end_id": 1125899906842627, "start_id": 1125899906842626, "properties": {}}::edge, {"id": 1125899906842627, "label": "v1", "properties": {"id": "end"}}::vertex]::path');

SELECT * FROM text(1);
SELECT * FROM text('1');
SELECT * FROM int4(1);
SELECT * FROM json('1');
SELECT * FROM jsonb('1');
SELECT * FROM bytea('1');

-- Test Var expression node types
SELECT create_graph('issue_1303');
SELECT result, agtype('[1, 2, 3]')  FROM cypher('issue_1303', $$ CREATE (u) RETURN u $$) AS (result agtype);
SELECT result, result2, pg_typeof(result2) FROM cypher('issue_1303', $$ MATCH (u) RETURN u $$) AS (result agtype), agtype('[1, 2, 3]') AS result2;
SELECT result, result2, pg_typeof(result2) FROM cypher('issue_1303', $$ MATCH (u) RETURN u $$) AS (result agtype), text(1) AS result2;
SELECT result, result2, pg_typeof(result2), result3, pg_typeof(result3) FROM cypher('issue_1303', $$ MATCH (u) RETURN u $$) AS (result agtype), text(1) AS result2, agtype(result) AS result3;
SELECT result, result2, pg_typeof(result2), result3, pg_typeof(result3) FROM cypher('issue_1303', $$ MATCH (u) RETURN u $$) AS (result agtype), text(1) AS result2, agtype(result2) AS result3;

-- Text OpExpr expression node types
SELECT * FROM agtype('[1, 2, 3]'::agtype || '[5, 6, 7]');
SELECT * FROM agtype('[1, 2, 3]'::agtype -> 2);
SELECT * FROM agtype('{"a": 1, "b": 2}'::agtype -> 'a'::text);

-- Text BoolExpr expression node types
SELECT * FROM bool(true AND false);

--
-- Cleanup
--
SELECT * FROM drop_graph('issue_1124', true);
SELECT * FROM drop_graph('issue_1303', true);
SELECT * FROM drop_graph('graph_395', true);
SELECT * FROM drop_graph('chained', true);
SELECT * FROM drop_graph('VLE', true);
SELECT * FROM drop_graph('case_statement', true);
SELECT * FROM drop_graph('opt_forms', true);
SELECT * FROM drop_graph('type_coercion', true);
SELECT * FROM drop_graph('order_by', true);
SELECT * FROM drop_graph('group_by', true);
SELECT * FROM drop_graph('UCSC', true);
SELECT * FROM drop_graph('expr', true);
SELECT * FROM drop_graph('regex', true);
SELECT * FROM drop_graph('keys', true);
SELECT * FROM drop_graph('list', true);

--
-- End of tests
--
