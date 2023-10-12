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

SELECT create_graph('cypher_delete');

--Test 1: Delete Vertices
SELECT * FROM cypher('cypher_delete', $$CREATE (:v)$$) AS (a agtype);
SELECT * FROM cypher('cypher_delete', $$CREATE (:v {i: 0, j: 5, a: 0})$$) AS (a agtype);
SELECT * FROM cypher('cypher_delete', $$CREATE (:v {i: 1})$$) AS (a agtype);

SELECT * FROM cypher('cypher_delete', $$MATCH(n) DELETE n RETURN n$$) AS (a agtype);
SELECT * FROM cypher('cypher_delete', $$MATCH(n) RETURN n$$) AS (a agtype);

--Test 2: Delete Edges
SELECT * FROM cypher('cypher_delete', $$CREATE (:v)-[:e]->(:v)$$) AS (a agtype);

--Should Fail
SELECT * FROM cypher('cypher_delete', $$MATCH(n1)-[e]->(n2) DELETE n1 RETURN n1$$) AS (a agtype);
SELECT * FROM cypher('cypher_delete', $$MATCH(n1)-[e]->(n2) DELETE n2 RETURN n2$$) AS (a agtype);

SELECT * FROM cypher('cypher_delete', $$MATCH(n1)-[e]->(n2) DELETE e RETURN e$$) AS (a agtype);
SELECT * FROM cypher('cypher_delete', $$MATCH()-[e]->() DELETE e RETURN e$$) AS (a agtype);

--Cleanup
SELECT * FROM cypher('cypher_delete', $$MATCH(n) DELETE n RETURN n$$) AS (a agtype);

--Test 4: DETACH DELETE a vertex
SELECT * FROM cypher('cypher_delete', $$CREATE (:v)-[:e]->(:v)$$) AS (a agtype);
SELECT * FROM cypher('cypher_delete', $$MATCH(n1)-[e]->(n2) DETACH DELETE n1 RETURN e$$) AS (a agtype);

--Cleanup
SELECT * FROM cypher('cypher_delete', $$MATCH(n) RETURN n$$) AS (a agtype);

--Test 4: DETACH DELETE two vertices tied to the same edge
SELECT * FROM cypher('cypher_delete', $$CREATE (:v)-[:e]->(:v)$$) AS (a agtype);
SELECT * FROM cypher('cypher_delete', $$MATCH(n1)-[e]->(n2) DETACH DELETE n1, n2 RETURN e$$) AS (a agtype);

--Test 4: DETACH DELETE a vertex
SELECT * FROM cypher('cypher_delete', $$CREATE (:v)-[:e]->(:v)$$) AS (a agtype);
SELECT * FROM cypher('cypher_delete', $$MATCH(n1)-[e]->(n2) DETACH DELETE n1, n2 RETURN e$$) AS (a agtype);

--Test 5: DETACH DELETE a vertex that points to itself
SELECT * FROM cypher('cypher_delete', $$CREATE (n:v)-[:e]->(n)$$) AS (a agtype);
SELECT * FROM cypher('cypher_delete', $$MATCH(n1)-[e]->() DETACH DELETE n1 RETURN e$$) AS (a agtype);

--Test 6: DETACH Delete a vertex twice
SELECT * FROM cypher('cypher_delete', $$CREATE (n:v)-[:e]->(n)$$) AS (a agtype);
SELECT * FROM cypher('cypher_delete', $$MATCH(n1)-[e]->(n2) DETACH DELETE n1, n2 RETURN e$$) AS (a agtype);

--Test 7: Test the SET Clause on DELETED node
SELECT * FROM cypher('cypher_delete', $$CREATE (:v)$$) AS (a agtype);
SELECT * FROM cypher('cypher_delete', $$CREATE (:v {i: 0, j: 5, a: 0})$$) AS (a agtype);
SELECT * FROM cypher('cypher_delete', $$CREATE (:v {i: 1})$$) AS (a agtype);

SELECT * FROM cypher('cypher_delete', $$MATCH(n) DELETE n SET n.lol = 'ftw' RETURN n$$) AS (a agtype);
SELECT * FROM cypher('cypher_delete', $$MATCH(n) RETURN n$$) AS (a agtype);

--Test 8:
SELECT * FROM cypher('cypher_delete', $$CREATE (n:v)-[:e]->(:v) CREATE (n)-[:e]->(:v)$$) AS (a agtype);
SELECT * FROM cypher('cypher_delete', $$MATCH(n1)-[]->() DELETE n1 RETURN n1$$) AS (a agtype);

--Cleanup
SELECT * FROM cypher('cypher_delete', $$MATCH(n) DETACH DELETE n RETURN n$$) AS (a agtype);
SELECT * FROM cypher('cypher_delete', $$MATCH(n) RETURN n$$) AS (a agtype);

--Test 9:
SELECT * FROM cypher('cypher_delete', $$CREATE (n:v)-[:e]->(:v)$$) AS (a agtype);
SELECT * FROM cypher('cypher_delete', $$MATCH(n1)-[e]->() DELETE e, n1 RETURN n1$$) AS (a agtype);

--Cleanup
SELECT * FROM cypher('cypher_delete', $$MATCH(n) DETACH DELETE n RETURN n$$) AS (a agtype);
SELECT * FROM cypher('cypher_delete', $$MATCH(n) RETURN n$$) AS (a agtype);

--Test 10:
SELECT * FROM cypher('cypher_delete', $$CREATE (n:v)-[:e]->(:v)$$) AS (a agtype);
SELECT * FROM cypher('cypher_delete', $$MATCH(n1)-[e]->() DELETE n1, e RETURN n1$$) AS (a agtype);

--Cleanup
SELECT * FROM cypher('cypher_delete', $$MATCH(n) DETACH DELETE n RETURN n$$) AS (a agtype);
SELECT * FROM cypher('cypher_delete', $$MATCH(n) RETURN n$$) AS (a agtype);

--Test 11: Delete a vertex twice
SELECT * FROM cypher('cypher_delete', $$CREATE (n:v)-[:e]->(:v), (n)-[:e]->(:v) RETURN n$$) AS (a agtype);
SELECT * FROM cypher('cypher_delete', $$MATCH(n1)-[e]->() DETACH DELETE n1 RETURN n1, e$$) AS (a agtype, b agtype);

--Cleanup
SELECT * FROM cypher('cypher_delete', $$MATCH(n) DELETE n RETURN n$$) AS (a agtype);
SELECT * FROM cypher('cypher_delete', $$MATCH(n) RETURN n$$) AS (a agtype);

--Test 12:
SELECT * FROM cypher('cypher_delete', $$CREATE (:v)-[:e]->(:v)$$) AS (a agtype);
SELECT * FROM cypher('cypher_delete', $$MATCH(n)-[e]->() DETACH DELETE n CREATE (n)-[:e2]->(:v) RETURN e$$) AS (a agtype);

--Cleanup
SELECT * FROM cypher('cypher_delete', $$MATCH(n) DETACH DELETE n RETURN n$$) AS (a agtype);
SELECT * FROM cypher('cypher_delete', $$MATCH(n) RETURN n$$) AS (a agtype);

--Test 13:
SELECT * FROM cypher('cypher_delete', $$CREATE (n:v)-[:e]->(n)$$) AS (a agtype);
SELECT * FROM cypher('cypher_delete', $$MATCH(n)-[e]->(m) DETACH DELETE n CREATE (m)-[:e2]->(:v) RETURN e$$) AS (a agtype);

--Cleanup
SELECT * FROM cypher('cypher_delete', $$MATCH(n) DETACH DELETE n RETURN n$$) AS (a agtype);
SELECT * FROM cypher('cypher_delete', $$MATCH(n) RETURN n$$) AS (a agtype);

--Test 14:
SELECT * FROM cypher('cypher_delete', $$CREATE (n:v)-[:e]->(n)$$) AS (a agtype);
SELECT * FROM cypher('cypher_delete', $$CREATE (:v)-[:e]->(:v)$$) AS (a agtype);
SELECT * FROM cypher('cypher_delete', $$MATCH(n)-[e]->(m) DETACH DELETE n CREATE (m)-[:e2]->(:v) RETURN e$$) AS (a agtype);

--Cleanup
SELECT * FROM cypher('cypher_delete', $$MATCH(n) DETACH DELETE n RETURN n$$) AS (a agtype);
SELECT * FROM cypher('cypher_delete', $$MATCH(n) RETURN n$$) AS (a agtype);

--Test 15:
SELECT * FROM cypher('cypher_delete', $$CREATE (:v)$$) AS (a agtype);
SELECT * FROM cypher('cypher_delete', $$MATCH(n) SET n.i = 0 DELETE n RETURN n$$) AS (a agtype);

--Cleanup
SELECT * FROM cypher('cypher_delete', $$MATCH(n) RETURN n$$) AS (a agtype);

--Test 16:
SELECT * FROM cypher('cypher_delete', $$CREATE (:v)$$) AS (a agtype);
SELECT * FROM cypher('cypher_delete', $$MATCH(n) DELETE n SET n.i = 0 RETURN n$$) AS (a agtype);

--Cleanup
SELECT * FROM cypher('cypher_delete', $$MATCH(n) RETURN n$$) AS (a agtype);

--Test 17:
SELECT * FROM cypher('cypher_delete', $$CREATE (n:v)-[:e]->(:v)$$) AS (a agtype);
SELECT * FROM cypher('cypher_delete', $$MATCH(n)-[e]->(m) DETACH DELETE n SET e.i = 1 RETURN e$$) AS (a agtype);

--Cleanup
--Note: Expect 1 vertex
SELECT * FROM cypher('cypher_delete', $$MATCH(n) DELETE n RETURN n$$) AS (a agtype);

--Test 18:
SELECT * FROM cypher('cypher_delete', $$CREATE (n:v)-[:e]->(:v)$$) AS (a agtype);
SELECT * FROM cypher('cypher_delete', $$MATCH(n)-[e]->(m) SET e.i = 1 DETACH DELETE n RETURN e$$) AS (a agtype);

--Cleanup
SELECT * FROM cypher('cypher_delete', $$MATCH(n) DELETE n RETURN n$$) AS (a agtype);

--Test 19:
SELECT * FROM cypher('cypher_delete', $$CREATE (n:v)$$) AS (a agtype);
SELECT * FROM cypher('cypher_delete', $$MATCH (n) DELETE n CREATE (n)-[:e]->(:v) RETURN n$$) AS (a agtype);

--Cleanup
SELECT * FROM cypher('cypher_delete', $$MATCH(n) DELETE n RETURN n$$) AS (a agtype);

--Test 20 Undefined Reference:
SELECT * FROM cypher('cypher_delete', $$MATCH (n) DELETE m RETURN n$$) AS (a agtype);

--Test 21 Prepared Statements
SELECT * FROM cypher('cypher_delete', $$CREATE (v:v)$$) AS (a agtype);

PREPARE d AS SELECT * FROM cypher('cypher_delete', $$MATCH (v) DELETE (v) RETURN v$$) AS (a agtype);
EXECUTE d;

SELECT * FROM cypher('cypher_delete', $$CREATE (v:v)$$) AS (a agtype);
EXECUTE d;

--Test 22 pl/pgsql Functions
SELECT * FROM cypher('cypher_delete', $$CREATE (v:v)$$) AS (a agtype);

CREATE FUNCTION delete_test()
RETURNS TABLE(vertex agtype)
LANGUAGE plpgsql
VOLATILE
AS $BODY$
BEGIN
	RETURN QUERY SELECT * FROM cypher('cypher_delete', $$MATCH (v) DELETE (v) RETURN v$$) AS (a agtype);
END
$BODY$;

SELECT delete_test();

SELECT * FROM cypher('cypher_delete', $$CREATE (v:v)$$) AS (a agtype);
SELECT delete_test();

-- Clean Up
SELECT * FROM cypher('cypher_delete', $$MATCH(n) DETACH DELETE n RETURN n$$) AS (a agtype);

--Test 23
SELECT * FROM cypher('cypher_delete', $$CREATE (n:v)-[:e]->()$$) AS (a agtype);
SELECT * FROM cypher('cypher_delete', $$CREATE (n:v)-[:e2]->()$$) AS (a agtype);

SELECT * FROM cypher('cypher_delete', $$MATCH p=()-[]->() RETURN p$$) AS (a agtype);
SELECT * FROM cypher('cypher_delete', $$MATCH(n)-[e]->(m)  DELETE e$$) AS (a agtype);
SELECT * FROM cypher('cypher_delete', $$MATCH p=()-[]->() RETURN p$$) AS (a agtype);

-- Clean Up
SELECT * FROM cypher('cypher_delete', $$MATCH(n)  DELETE n RETURN n$$) AS (a agtype);

--Test 24
SELECT * FROM cypher('cypher_delete', $$CREATE (n:v)-[:e]->()$$) AS (a agtype);
SELECT * FROM cypher('cypher_delete', $$CREATE (n:v)-[:e2]->()$$) AS (a agtype);

SELECT * FROM cypher('cypher_delete', $$MATCH p=()-[]->() RETURN p$$) AS (a agtype);
SELECT * FROM cypher('cypher_delete', $$MATCH(n)-[]->() DETACH DELETE n$$) AS (a agtype);
SELECT * FROM cypher('cypher_delete', $$MATCH p=()-[]->() RETURN p$$) AS (a agtype);

-- Clean Up
SELECT * FROM cypher('cypher_delete', $$MATCH(n) DELETE n RETURN n$$) AS (a agtype);

-- test DELETE in transaction block
SELECT * FROM cypher('cypher_delete', $$CREATE (u:vertices) RETURN u $$) AS (result agtype);

BEGIN;

SELECT * FROM cypher('cypher_delete', $$MATCH (u:vertices) RETURN u $$) AS (result agtype);

SELECT * FROM cypher('cypher_delete', $$MATCH (u:vertices) DELETE u RETURN u $$) AS (result agtype);
SELECT * FROM cypher('cypher_delete', $$MATCH (u:vertices) RETURN u $$) AS (result agtype);

SELECT * FROM cypher('cypher_delete', $$CREATE (u:vertices) RETURN u $$) AS (result agtype);
SELECT * FROM cypher('cypher_delete', $$MATCH (u:vertices) RETURN u $$) AS (result agtype);
SELECT * FROM cypher('cypher_delete', $$MATCH (u:vertices) DELETE u RETURN u $$) AS (result agtype);
SELECT * FROM cypher('cypher_delete', $$MATCH (u:vertices) RETURN u $$) AS (result agtype);

SELECT * FROM cypher('cypher_delete', $$CREATE (u:vertices) RETURN u $$) AS (result agtype);
SELECT * FROM cypher('cypher_delete', $$MATCH (u:vertices) RETURN u $$) AS (result agtype);
SELECT * FROM cypher('cypher_delete', $$MATCH (u:vertices) DELETE u SET u.i = 1 RETURN u $$) AS (result agtype);
SELECT * FROM cypher('cypher_delete', $$MATCH (u:vertices) RETURN u $$) AS (result agtype);

END;

SELECT * FROM cypher('cypher_delete', $$MATCH (u:vertices) RETURN u $$) AS (result agtype);

--
-- Detach Delete
--

SELECT create_graph('detach_delete');
SELECT * FROM cypher('detach_delete',
$$
    CREATE (x:Label3{name:'x', delete: true}),
           (y:Label3{name:'y', delete: true}),
           (a:Label1{name:'a', delete: true}),
           (b:Label5{name:'b'}),
           (c:Label5{name:'c'}),
           (d:Label5{name:'d'}),
           (m:Label7{name:'m', delete: true}),
           (n:Label2{name:'n'}),
           (p:Label2{name:'p'}),
           (q:Label2{name:'q'}),
           (a)-[:rel1{name:'ab'}]->(b),
           (c)-[:rel2{name:'cd'}]->(d),
           (n)-[:rel3{name:'nm'}]->(m),
           (a)-[:rel4{name:'am'}]->(m),
           (p)-[:rel5{name:'pq'}]->(q)
$$
) as (a agtype);

-- no vertices or edges are deleted (error is expected)
SELECT * FROM cypher('detach_delete', $$ MATCH (x:Label1), (y:Label3), (z:Label7) DELETE x, y, z RETURN 1 $$) as (a agtype);
SELECT * FROM cypher('detach_delete', $$ MATCH (v) RETURN v.name $$) as (vname agtype);
SELECT * FROM cypher('detach_delete', $$ MATCH ()-[e]->() RETURN e.name $$) as (ename agtype);

-- x, y, a, m, ab, nm, am are deleted
SELECT * FROM cypher('detach_delete', $$ MATCH (x:Label1), (y:Label3), (z:Label7) DETACH DELETE x, y, z RETURN 1 $$) as (a agtype);
SELECT * FROM cypher('detach_delete', $$ MATCH (v) RETURN v.name $$) as (vname agtype);
SELECT * FROM cypher('detach_delete', $$ MATCH ()-[e]->() RETURN e.name $$) as (ename agtype);

SELECT drop_graph('detach_delete', true);

--
-- Clean up
--
DROP FUNCTION delete_test;
SELECT drop_graph('cypher_delete', true);

--
-- End
--
