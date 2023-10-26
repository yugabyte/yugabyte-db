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

SELECT create_graph('cypher_unwind');

-- Create nodes and relations

SELECT * FROM cypher('cypher_unwind', $$
    CREATE (n {name: 'node1', a: [1, 2, 3]}),
           (m {name: 'node2', a: [4, 5, 6]}),
           (o {name: 'node3', a: [7, 8, 9]}),
           (n)-[:KNOWS]->(m),
           (m)-[:KNOWS]->(o)
$$) as (i agtype);

SELECT * FROM cypher('cypher_unwind', $$
    MATCH (n)
    RETURN n
$$) as (i agtype);

--
-- Test UNWIND clause
--

SELECT * FROM cypher('cypher_unwind', $$
    UNWIND [1, 2, 3] AS i
    RETURN i
$$) as (i agtype);

SELECT * FROM cypher('cypher_unwind', $$
    MATCH (n)
    WITH n.a AS a
    UNWIND a AS i
    RETURN *
$$) as (i agtype, j agtype);

SELECT * FROM cypher('cypher_unwind', $$
    WITH [[1, 2], [3, 4], 5] AS nested
    UNWIND nested AS x
    UNWIND x AS y
    RETURN y
$$) as (i agtype);

-- UNWIND vertices

SELECT * FROM cypher('cypher_unwind', $$
    MATCH p=(n)-[:KNOWS]->(m)
    UNWIND nodes(p) as node
    RETURN node
$$) as (i agtype);

SELECT * FROM cypher('cypher_unwind', $$
    MATCH p=(n)-[:KNOWS]->(m)
    UNWIND nodes(p) as node
    RETURN node.name
$$) as (i agtype);

-- UNWIND edges

SELECT * FROM cypher('cypher_unwind', $$
    MATCH p=(n)-[:KNOWS]->(m)
    UNWIND relationships(p) as relation
    RETURN relation
$$) as (i agtype);

SELECT * FROM cypher('cypher_unwind', $$
    MATCH p=(n)-[:KNOWS]->(m)
    UNWIND relationships(p) as relation
    RETURN type(relation)
$$) as (i agtype);

-- UNWIND paths (vle)

SELECT * FROM cypher('cypher_unwind', $$
    MATCH p=({name:'node1'})-[e:KNOWS*]->({name:'node3'})
    UNWIND [p] as path
    RETURN path
$$) as (i agtype);

SELECT * FROM cypher('cypher_unwind', $$
    MATCH p=({name:'node1'})-[e:KNOWS*]->({name:'node3'})
    UNWIND [p] as path
    RETURN relationships(path)
$$) as (i agtype);

SELECT * FROM cypher('cypher_unwind', $$
    MATCH p=({name:'node1'})-[e:KNOWS*]->({name:'node3'})
    UNWIND [p] as path
    UNWIND relationships(path) as edge
    RETURN edge
$$) as (i agtype);

-- Unwind with SET clause

SELECT * FROM cypher('cypher_unwind', $$
    MATCH p=(n)-[:KNOWS]->(m)
    UNWIND nodes(p) as node
    SET node.type = 'vertex'
$$) as (i agtype);

SELECT * FROM cypher('cypher_unwind', $$
    MATCH (n)
    RETURN n
$$) as (i agtype);

--
-- Issue 1302
--

SELECT create_graph('issue_1302');

SELECT * FROM cypher('cypher_unwind', $$
    CREATE (agtype {name: 'node1', a: [1, 2, 3]}),
           (m {name: 'node2', a: [4, 5, 6]}),
           (o {name: 'node3', a: [7, 8, 9]}),
           (n)-[:KNOWS]->(m),
           (m)-[:KNOWS]->(o)
$$) as (i agtype);

SELECT * FROM cypher('cypher_unwind', $$
    MATCH (n)
    WITH n.a AS a
    UNWIND a AS i
    RETURN *
$$) as (i agtype, j agtype);

SELECT * FROM cypher('cypher_unwind', $$
    UNWIND NULL as i
    RETURN i
$$) as (i agtype);

--
-- Clean up
--

SELECT drop_graph('cypher_unwind', true);
SELECT drop_graph('issue_1302', true);