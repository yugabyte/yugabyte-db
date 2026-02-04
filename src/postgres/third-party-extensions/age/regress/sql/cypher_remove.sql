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

SELECT create_graph('cypher_remove');


--test 1
SELECT * FROM cypher('cypher_remove', $$CREATE (:test_1)$$) AS (a agtype);
SELECT * FROM cypher('cypher_remove', $$CREATE (:test_1 {i: 0, j: 5, a: 0})$$) AS (a agtype);
SELECT * FROM cypher('cypher_remove', $$CREATE (:test_1 {i: 1})$$) AS (a agtype);

SELECT * FROM cypher('cypher_remove', $$MATCH (n:test_1) REMOVE n.i $$) AS (a agtype);
SELECT * FROM cypher('cypher_remove', $$MATCH (n:test_1) RETURN n$$) AS (a agtype);

--test 2
SELECT * FROM cypher('cypher_remove', $$CREATE (:test_2)$$) AS (a agtype);
SELECT * FROM cypher('cypher_remove', $$CREATE (:test_2 {i: 0, j: 5, a: 0})$$) AS (a agtype);
SELECT * FROM cypher('cypher_remove', $$CREATE (:test_2 {i: 1})$$) AS (a agtype);

SELECT * FROM cypher('cypher_remove', $$MATCH (n:test_2) REMOVE n.j RETURN n$$) AS (a agtype);
SELECT * FROM cypher('cypher_remove', $$MATCH (n:test_2) RETURN n$$) AS (a agtype);

--test 3 Validate Paths are updated
SELECT * FROM cypher('cypher_remove', $$CREATE (:test_3 { i : 20 } )-[:test_3_edge {j:20}]->(:test_3 {i:10})$$) AS (a agtype);

SELECT * FROM cypher('cypher_remove', $$MATCH p=(n)-[:test_3_edge]->() REMOVE n.i RETURN p$$) AS (a agtype);

--test 4 Edges
SELECT * FROM cypher('cypher_remove', $$CREATE (:test_4 { i : 20 } )-[:test_4_edge {j:20}]->(:test_4 {i:10})$$) AS (a agtype);

SELECT * FROM cypher('cypher_remove', $$MATCH ()-[n]->(:test_4) REMOVE n.i RETURN n$$) AS (a agtype);
SELECT * FROM cypher('cypher_remove', $$MATCH ()-[n]->(:test_4) RETURN n$$) AS (a agtype);

--test 5 two REMOVE clauses
SELECT * FROM cypher('cypher_remove', $$CREATE (:test_5 {i: 1, j : 2, k : 3}) $$) AS (a agtype);
SELECT * FROM cypher('cypher_remove', $$
        MATCH (n:test_5)
        REMOVE n.i
        REMOVE n.j
        RETURN n
$$) AS (a agtype);


SELECT * FROM cypher('cypher_remove', $$
        MATCH (n:test_5)
        RETURN n
$$) AS (a agtype);

--test 6 Create a loop and see that set can work after create
SELECT * FROM cypher('cypher_remove', $$CREATE (:test_6 {j: 5, y: 99})$$) AS (a agtype);
SELECT * FROM cypher('cypher_remove', $$
	MATCH (n {j: 5})
	CREATE p=(n)-[e:e {j:34}]->(n)
	REMOVE n.y
	RETURN n, p
$$) AS (a agtype, b agtype);
SELECT * FROM cypher('cypher_remove', $$MATCH (n:test_6) RETURN n$$) AS (a agtype);


--test 7 Create a loop and see that set can work after create
SELECT * FROM cypher('cypher_remove', $$
	CREATE (:test_7)-[e:e {j:34}]->()
	REMOVE e.y
	RETURN e
$$) AS (a agtype);
SELECT * FROM cypher('cypher_remove', $$MATCH (n:test_7) RETURN n$$) AS (a agtype);



--test 8
SELECT * FROM cypher('cypher_remove', $$
        MATCH (n:test_7)
        MATCH (n)-[e:e {j:34}]->()
        REMOVE n.y
        RETURN n
$$) AS (a agtype);

--Handle Inheritance
SELECT * FROM cypher('cypher_remove', $$CREATE ( {i : 1 })$$) AS (a agtype);
SELECT * FROM cypher('cypher_remove', $$MATCH (n) REMOVE n.i RETURN n$$) AS (a agtype);
SELECT * FROM cypher('cypher_remove', $$MATCH (n) RETURN n$$) AS (a agtype);

-- prepared statements
SELECT * FROM cypher('cypher_remove', $$CREATE ( {i : 1 })$$) AS (a agtype);
PREPARE p_1 AS SELECT * FROM cypher('cypher_remove', $$MATCH (n) REMOVE n.i RETURN n $$) AS (a agtype);
EXECUTE p_1;

SELECT * FROM cypher('cypher_remove', $$CREATE ( {i : 1 })$$) AS (a agtype);
EXECUTE p_1;
-- pl/pgsql
SELECT * FROM cypher('cypher_remove', $$CREATE ( {i : 1 })$$) AS (a agtype);

CREATE FUNCTION remove_test()
RETURNS TABLE(vertex agtype)
LANGUAGE plpgsql
VOLATILE
AS $BODY$
BEGIN
	RETURN QUERY SELECT * FROM cypher('cypher_remove', $$MATCH (n) REMOVE n.i RETURN n$$) AS (a agtype);
END
$BODY$;

SELECT remove_test();

SELECT * FROM cypher('cypher_remove', $$CREATE ( {i : 1 })$$) AS (a agtype);
SELECT remove_test();


--
-- Updating Multiple Fields
--
SELECT * FROM cypher('cypher_remove', $$MATCH (n) RETURN n$$) AS (a agtype);
SELECT * FROM cypher('cypher_remove', $$MATCH (n) REMOVE n.i, n.j, n.k RETURN n$$) AS (a agtype);
SELECT * FROM cypher('cypher_remove', $$MATCH (n) RETURN n$$) AS (a agtype);

SELECT * FROM cypher('cypher_remove', $$CREATE ()-[:edge_multi_property { i: 5, j: 20}]->()$$) AS (a agtype);
SELECT * FROM cypher('cypher_remove', $$MATCH ()-[e:edge_multi_property]-() RETURN e$$) AS (a agtype);
SELECT * FROM cypher('cypher_remove', $$MATCH ()-[e:edge_multi_property]-() REMOVE e.i, e.j RETURN e$$) AS (a agtype);

--Errors
SELECT * FROM cypher('cypher_remove', $$REMOVE n.i$$) AS (a agtype);

SELECT * FROM cypher('cypher_remove', $$MATCH (n) REMOVE n.i = NULL$$) AS (a agtype);

SELECT * FROM cypher('cypher_remove', $$MATCH (n) REMOVE wrong_var.i$$) AS (a agtype);

--
-- Clean up
--
DROP FUNCTION remove_test;
SELECT drop_graph('cypher_remove', true);

--
-- End
--
