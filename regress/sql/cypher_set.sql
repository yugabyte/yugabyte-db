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

LOAD 'age';
SET search_path TO ag_catalog;

SELECT create_graph('cypher_set');

SELECT * FROM cypher('cypher_set', $$CREATE (:v)$$) AS (a agtype);
SELECT * FROM cypher('cypher_set', $$CREATE (:v {i: 0, j: 5, a: 0})$$) AS (a agtype);
SELECT * FROM cypher('cypher_set', $$CREATE (:v {i: 1})$$) AS (a agtype);

--Simple SET test case
SELECT * FROM cypher('cypher_set', $$MATCH (n) SET n.i = 3$$) AS (a agtype);

SELECT * FROM cypher('cypher_set', $$MATCH (n) WHERE n.j = 5 SET n.i = NULL RETURN n$$) AS (a agtype);
SELECT * FROM cypher('cypher_set', $$MATCH (n) RETURN n$$) AS (a agtype);

SELECT * FROM cypher('cypher_set', $$MATCH (n) SET n.i = NULL RETURN n$$) AS (a agtype);
SELECT * FROM cypher('cypher_set', $$MATCH (n) RETURN n$$) AS (a agtype);

SELECT * FROM cypher('cypher_set', $$MATCH (n) SET n.i = 3 RETURN n$$) AS (a agtype);
SELECT * FROM cypher('cypher_set', $$MATCH (n) RETURN n$$) AS (a agtype);

--Handle Inheritance
SELECT * FROM cypher('cypher_set', $$CREATE ()$$) AS (a agtype);
SELECT * FROM cypher('cypher_set', $$MATCH (n) SET n.i = 3 RETURN n$$) AS (a agtype);
SELECT * FROM cypher('cypher_set', $$MATCH (n) RETURN n$$) AS (a agtype);

--Validate Paths are updated
SELECT * FROM cypher('cypher_set', $$MATCH (n) CREATE (n)-[:e {j:20}]->(:other_v {k:10}) RETURN n$$) AS (a agtype);
SELECT * FROM cypher('cypher_set', $$MATCH p=(n)-[]->() SET n.i = 50 RETURN p$$) AS (a agtype);

--Edges
SELECT * FROM cypher('cypher_set', $$MATCH ()-[n]-(:other_v) SET n.i = 3 RETURN n$$) AS (a agtype);
SELECT * FROM cypher('cypher_set', $$MATCH ()-[n]->(:other_v) RETURN n$$) AS (a agtype);

SELECT * FROM cypher('cypher_set', $$
        MATCH (n {j: 5})
        SET n.y = 50
        SET n.z = 99
        RETURN n
$$) AS (a agtype);

SELECT * FROM cypher('cypher_set', $$
        MATCH (n {j: 5})
        RETURN n
$$) AS (a agtype);

--Create a loop and see that set can work after create
SELECT * FROM cypher('cypher_set', $$
	MATCH (n {j: 5})
	CREATE p=(n)-[e:e {j:34}]->(n)
	SET n.y = 99
	RETURN n, p
$$) AS (a agtype, b agtype);

--Create a loop and see that set can work after create
SELECT * FROM cypher('cypher_set', $$
	CREATE ()-[e:e {j:34}]->()
	SET e.y = 99
	RETURN e
$$) AS (a agtype);

SELECT * FROM cypher('cypher_set', $$
        MATCH (n)
        MATCH (n)-[e:e {j:34}]->()
        SET n.y = 1
        RETURN n
$$) AS (a agtype);

SELECT * FROM cypher('cypher_set', $$
        MATCH (n)
        MATCH ()-[e:e {j:34}]->(n)
        SET n.y = 2
        RETURN n
$$) AS (a agtype);

SELECT * FROM cypher('cypher_set', $$MATCH (n)-[]->(n) SET n.y = 99 RETURN n$$) AS (a agtype);

SELECT * FROM cypher('cypher_set', $$MATCH (n) MATCH (n)-[]->(m) SET n.t = 150 RETURN n$$) AS (a agtype);

-- prepared statements
PREPARE p_1 AS SELECT * FROM cypher('cypher_set', $$MATCH (n) SET n.i = 3 RETURN n $$) AS (a agtype);
EXECUTE p_1;

EXECUTE p_1;

PREPARE p_2 AS SELECT * FROM cypher('cypher_set', $$MATCH (n) SET n.i = $var_name RETURN n $$, $1) AS (a agtype);
EXECUTE p_2('{"var_name": 4}');

EXECUTE p_2('{"var_name": 6}');

CREATE FUNCTION set_test()
RETURNS TABLE(vertex agtype)
LANGUAGE plpgsql
VOLATILE
AS $BODY$
BEGIN
	RETURN QUERY SELECT * FROM cypher('cypher_set', $$MATCH (n) SET n.i = 7 RETURN n $$) AS (a agtype);
END
$BODY$;

SELECT set_test();

SELECT set_test();

--
-- Updating multiple fieds
--
SELECT * FROM cypher('cypher_set', $$MATCH (n) SET n.i = 3, n.j = 5 RETURN n $$) AS (a agtype);

SELECT * FROM cypher('cypher_set', $$MATCH (n)-[m]->(n) SET m.y = n.y RETURN n, m$$) AS (a agtype, b agtype);

--Errors
SELECT * FROM cypher('cypher_set', $$SET n.i = NULL$$) AS (a agtype);

SELECT * FROM cypher('cypher_set', $$MATCH (n) SET wrong_var.i = 3$$) AS (a agtype);

SELECT * FROM cypher('cypher_set', $$MATCH (n) SET i = 3$$) AS (a agtype);

--
-- SET refactor regression tests
--

-- INSERT INTO
CREATE TABLE tbl (result agtype);

SELECT * FROM cypher('cypher_set', $$CREATE (u:vertices) $$) AS (result agtype);
SELECT * FROM cypher('cypher_set', $$CREATE (u:begin)-[:edge]->(v:end) $$) AS (result agtype);
SELECT * FROM cypher('cypher_set', $$MATCH (u:vertices) return u $$) AS (result agtype);
SELECT * FROM cypher('cypher_set', $$MATCH (u:begin)-[:edge]->(v:end) return u, v $$) AS (u agtype, v agtype);

INSERT INTO tbl (SELECT * FROM cypher('cypher_set', $$MATCH (u:vertices) SET u.i = 7 return u $$) AS (result agtype));
INSERT INTO tbl (SELECT * FROM cypher('cypher_set', $$MATCH (u:vertices) SET u.i = 13 return u $$) AS (result agtype));

SELECT * FROM tbl;

SELECT * FROM cypher('cypher_set', $$MATCH (u:vertices) return u $$) AS (result agtype);

BEGIN;
SELECT * FROM cypher('cypher_set', $$MATCH (u:vertices) SET u.i = 1, u.j = 3, u.k = 5 return u $$) AS (result agtype);
SELECT * FROM cypher('cypher_set', $$MATCH (u:vertices) return u $$) AS (result agtype);

SELECT * FROM cypher('cypher_set', $$MATCH (u:vertices) SET u.i = 2, u.j = 4, u.k = 6 return u $$) AS (result agtype);
SELECT * FROM cypher('cypher_set', $$MATCH (u:vertices) return u $$) AS (result agtype);

SELECT * FROM cypher('cypher_set', $$MATCH (u:vertices) SET u.i = 3, u.j = 6, u.k = 9 return u $$) AS (result agtype);
SELECT * FROM cypher('cypher_set', $$MATCH (u:vertices) return u $$) AS (result agtype);

SELECT * FROM cypher('cypher_set', $$MATCH (u:begin)-[:edge]->(v:end) SET u.i = 1, v.i = 2, u.j = 3, v.j = 4 return u, v $$) AS (u agtype, v agtype);
SELECT * FROM cypher('cypher_set', $$MATCH (u:begin)-[:edge]->(v:end) return u, v $$) AS (u agtype, v agtype);

SELECT * FROM cypher('cypher_set', $$MATCH (u:begin)-[:edge]->(v:end) SET u.i = 2, v.i = 1, u.j = 4, v.j = 3 return u, v $$) AS (u agtype, v agtype);
SELECT * FROM cypher('cypher_set', $$MATCH (u:begin)-[:edge]->(v:end) return u, v $$) AS (u agtype, v agtype);
END;

SELECT * FROM cypher('cypher_set', $$MATCH (u:vertices) return u $$) AS (result agtype);
SELECT * FROM cypher('cypher_set', $$MATCH (u:begin)-[:edge]->(v:end) return u, v $$) AS (u agtype, v agtype);

-- test lists
SELECT * FROM cypher('cypher_set', $$MATCH (n) SET n.i = [3, 'test', [1, 2, 3], {id: 1}, 1.0, 1.0::numeric] RETURN n$$) AS (a agtype);

SELECT * FROM cypher('cypher_set', $$MATCH (n) RETURN n$$) AS (a agtype);

-- test that lists get updated in paths
SELECT * FROM cypher('cypher_set', $$MATCH p=(u:begin)-[:edge]->(v:end) SET u.i = [1, 2, 3] return u, p $$) AS (u agtype, p agtype);

SELECT * FROM cypher('cypher_set', $$MATCH p=(u:begin)-[:edge]->(v:end) return u, p $$) AS (u agtype, p agtype);

-- test empty lists
SELECT * FROM cypher('cypher_set', $$MATCH (n) SET n.i = [] RETURN n$$) AS (a agtype);

SELECT * FROM cypher('cypher_set', $$MATCH (n) RETURN n$$) AS (a agtype);

-- test maps
SELECT * FROM cypher('cypher_set', $$MATCH (n) SET n.i = {prop1: 3, prop2:'test', prop3: [1, 2, 3], prop4: {id: 1}, prop5: 1.0, prop6:1.0::numeric} RETURN n$$) AS (a agtype);

SELECT * FROM cypher('cypher_set', $$MATCH (n) RETURN n$$) AS (a agtype);

-- test maps in paths
SELECT * FROM cypher('cypher_set', $$MATCH p=(u:begin)-[:edge]->(v:end) SET u.i = {prop1: 1, prop2: 2, prop3: 3} return u, p $$) AS (u agtype, p agtype);

SELECT * FROM cypher('cypher_set', $$MATCH p=(u:begin)-[:edge]->(v:end) return u, p $$) AS (u agtype, p agtype);

-- test empty maps
SELECT * FROM cypher('cypher_set', $$MATCH (n) SET n.i = {} RETURN n$$) AS (a agtype);

SELECT * FROM cypher('cypher_set', $$MATCH (n) RETURN n$$) AS (a agtype);

--
-- Clean up
--
DROP TABLE tbl;
DROP FUNCTION set_test;
SELECT drop_graph('cypher_set', true);

--

