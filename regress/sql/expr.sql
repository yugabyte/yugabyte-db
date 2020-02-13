/*
 * Copyright 2020 Bitnine Co., Ltd.
 * 
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 * 
 *     http://www.apache.org/licenses/LICENSE-2.0
 * 
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

LOAD 'agensgraph';
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
-- Test transform logic for AND, OR, and NOT
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

-- these should fail
SELECT * FROM cypher('expr', $$
RETURN "abcdefghijklmnopqrstuvwxyz" STARTS WITH "bcde"
$$) AS r(result agtype);
SELECT * FROM cypher('expr', $$
RETURN "abcdefghijklmnopqrstuvwxyz" ENDS WITH "vwxy"
$$) AS r(result agtype);
SELECT * FROM cypher('expr', $$
RETURN "abcdefghijklmnopqrstuvwxyz" CONTAINS "klmo"
$$) AS r(result agtype);

SELECT * FROM drop_graph('expr', true);

--
-- End of tests
--
