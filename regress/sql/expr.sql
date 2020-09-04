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
-- should return SQL null, nothing
SELECT * FROM cypher('expr',
$$RETURN null IN ['str', 1, 1.0, true, null]$$) AS r(c boolean);
SELECT * FROM cypher('expr',
$$RETURN null IN ['str', 1, 1.0, true]$$) AS r(c boolean);
SELECT * FROM cypher('expr',
$$RETURN 'str' IN null $$) AS r(c boolean);
-- should all return false
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
-- should error - ERROR:  object of IN must be a list
SELECT * FROM cypher('expr',
$$RETURN null IN 'str' $$) AS r(c boolean);
SELECT * FROM cypher('expr',
$$RETURN 'str' IN 'str' $$) AS r(c boolean);

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

--
-- Test typecasting '::' transform and execution logic
--

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
    RETURN toBoolean("falze")
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
    RETURN toFloat("falze")
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
    RETURN toInteger("falze")
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
SELECT * FROM toString(3);
SELECT * FROM toString(3.14);
SELECT * FROM toString(3.14::float);
SELECT * FROM toString(3.14::numeric);
SELECT * FROM toString(true);
SELECT * FROM toString(false);
SELECT * FROM toString('a string');
SELECT * FROM toString('a cstring'::cstring);
SELECT * FROM toString('a text string'::text);
-- agtypes
SELECT * FROM toString(agtype_in('3'));
SELECT * FROM toString(agtype_in('3.14'));
SELECT * FROM toString(agtype_in('3.14::float'));
SELECT * FROM toString(agtype_in('3.14::numeric'));
SELECT * FROM toString(agtype_in('true'));
SELECT * FROM toString(agtype_in('false'));
SELECT * FROM toString(agtype_in('"a string"'));
SELECT * FROM cypher('expr', $$ RETURN toString(3.14::numeric) $$) AS (results agtype);
-- should return null
SELECT * FROM toString(NULL);
SELECT * FROM toString(agtype_in(null));
-- should fail
SELECT * FROM toString();
SELECT * FROM cypher('expr', $$ RETURN toString() $$) AS (results agtype);

--
-- reverse(string)
--
SELECT * FROM cypher('expr', $$
    RETURN reverse("gnirts a si siht")
$$) AS (results agtype);
SELECT * FROM reverse('gnirts a si siht');
SELECT * FROM reverse('gnirts a si siht'::text);
SELECT * FROM reverse('gnirts a si siht'::cstring);
-- should return null
SELECT * FROM cypher('expr', $$
    RETURN reverse(null)
$$) AS (results agtype);
SELECT * FROM reverse(null);
-- should fail
SELECT * FROM cypher('expr', $$
    RETURN reverse(true)
$$) AS (results agtype);
SELECT * FROM reverse(true);
SELECT * FROM cypher('expr', $$
    RETURN reverse(3.14)
$$) AS (results agtype);
SELECT * FROM reverse(3.14);
SELECT * FROM cypher('expr', $$
    RETURN reverse()
$$) AS (results agtype);
SELECT * FROM reverse();

--
-- toUpper() and toLower()
--
SELECT * FROM cypher('expr', $$
    RETURN toUpper('to uppercase')
$$) AS (toUpper agtype);
SELECT * FROM cypher('expr', $$
    RETURN toLower('TO LOWERCASE')
$$) AS (toLower agtype);
SELECT * FROM touppercase('text'::text);
SELECT * FROM touppercase('cstring'::cstring);
SELECT * FROM tolowercase('TEXT'::text);
SELECT * FROM tolowercase('CSTRING'::cstring);
-- should return null
SELECT * FROM cypher('expr', $$
    RETURN toUpper(null)
$$) AS (toUpper agtype);
SELECT * FROM cypher('expr', $$
    RETURN toLower(null)
$$) AS (toLower agtype);
SELECT * FROM touppercase(null);
SELECT * FROM tolowercase(null);
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
SELECT * FROM touppercase();
SELECT * FROM tolowercase();

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
SELECT * FROM l_trim('  string   ');
SELECT * FROM r_trim('  string   ');
SELECT * FROM b_trim('  string   ');
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
SELECT * FROM l_trim(null);
SELECT * FROM r_trim(null);
SELECT * FROM b_trim(null);
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

SELECT * FROM l_trim();
SELECT * FROM r_trim();
SELECT * FROM b_trim();

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
SELECT * FROM l_substr(null, 1);
SELECT * FROM l_substr(null, null);
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
SELECT * FROM l_substr('123456789', null);
SELECT * FROM l_substr('123456789', -1);
SELECT * FROM l_substr();
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
SELECT * FROM r_substr(null, 1);
SELECT * FROM r_substr(null, null);
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
SELECT * FROM r_substr('123456789', null);
SELECT * FROM r_substr('123456789', -1);
SELECT * FROM r_substr();
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
SELECT * FROM b_substr('0123456789', 3, 2);
SELECT * FROM b_substr('0123456789', 1);
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
SELECT * FROM b_substr(null, null, null);
SELECT * FROM b_substr(null, null);
SELECT * FROM b_substr(null, 1);
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
SELECT * FROM b_substr('123456789', null);
SELECT * FROM b_substr('123456789', 0, -1);
SELECT * FROM b_substr('123456789', -1);
SELECT * FROM b_substr();

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
SELECT * FROM split(null, null);
SELECT * FROM split('a,b,c,d,e,f', null);
SELECT * FROM split(null, ',');
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
SELECT * FROM split(123456789, ',');
SELECT * FROM split('a,b,c,d,e,f', -1);
SELECT * FROM split('a,b,c,d,e,f');
SELECT * FROM split();

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
SELECT * FROM replace(null, null, null);
SELECT * FROM replace('Hello', null, null);
SELECT * FROM replace('Hello', '', null);
SELECT * FROM replace('', '', '');
SELECT * FROM replace('Hello', 'Hello', '');
SELECT * FROM replace('', 'Hello', 'Mellow');
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
SELECT * FROM replace();
SELECT * FROM replace(null);
SELECT * FROM replace(null, null);
SELECT * FROM replace('Hello', 'e', 1);
SELECT * FROM replace('Hello', 1, 'E');

--
-- sin, cos, tan, cot
--
SELECT * FROM cypher('expr', $$
    RETURN sin(3.1415)
$$) AS (results agtype), sin(3.1415);
SELECT * FROM cypher('expr', $$
    RETURN cos(3.1415)
$$) AS (results agtype), cos(3.1415);
SELECT * FROM cypher('expr', $$
    RETURN tan(3.1415)
$$) AS (results agtype), tan(3.1415);
SELECT * FROM cypher('expr', $$
    RETURN cot(3.1415)
$$) AS (results agtype), cot(3.1415);
SELECT * FROM sin(3.1415), r_sin(3.1415);
SELECT * FROM cos(3.1415), r_cos(3.1415);
SELECT * FROM tan(3.1415), r_tan(3.1415);
SELECT * FROM cot(3.1415), r_cot(3.1415);
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
SELECT * FROM r_sin(null);
SELECT * FROM r_cos(null);
SELECT * FROM r_tan(null);
SELECT * FROM r_cot(null);
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
SELECT * FROM r_sin('0');
SELECT * FROM r_cos('0');
SELECT * FROM r_tan('0');
SELECT * FROM r_cot('0');
SELECT * FROM r_sin();
SELECT * FROM r_cos();
SELECT * FROM r_tan();
SELECT * FROM r_cot();

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
SELECT * FROM asin(1), r_asin(1);
SELECT * FROM acos(0), r_acos(0);
SELECT * FROM atan(1), r_atan(1);
SELECT * FROM atan2(1, 1), r_atan2(1, 1);
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
SELECT * FROM r_asin(null);
SELECT * FROM r_acos(null);
SELECT * FROM r_atan(null);
SELECT * FROM r_atan2(null, null);
SELECT * FROM r_atan2(1, null);
SELECT * FROM r_atan2(null, 1);
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
SELECT * FROM r_asin('0');
SELECT * FROM r_acos('0');
SELECT * FROM r_atan('0');
SELECT * FROM r_atan2('0', 1);
SELECT * FROM r_atan2(1, '0');
SELECT * FROM r_asin();
SELECT * FROM r_acos();
SELECT * FROM r_atan();
SELECT * FROM r_atan2();
SELECT * FROM r_atan2(1);

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
-- Cleanup
--
SELECT * FROM drop_graph('expr', true);

--
-- End of tests
--
