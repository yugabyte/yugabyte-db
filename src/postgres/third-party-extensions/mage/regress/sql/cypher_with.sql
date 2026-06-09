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

--
-- Load data
--
SELECT create_graph('cypher_with');

SELECT * FROM cypher('cypher_with', $$
    CREATE (andres {name : 'Andres', age : 36}),
    (caesar {name : 'Caesar', age : 25}),
    (bossman {name : 'Bossman', age : 55}),
    (david {name : 'David', age : 35}),
    (george {name : 'George', age : 37}),
    (andres)-[:BLOCKS]->(caesar),
    (andres)-[:KNOWS]->(bossman),
    (caesar)-[:KNOWS]->(george),
    (bossman)-[:BLOCKS]->(david),
    (bossman)-[:KNOWS]->(george),
    (david)-[:KNOWS]->(andres)
$$) as (a agtype);

--
-- Test WITH clause
--

SELECT * FROM cypher('cypher_with', $$
    MATCH (n)-[e]->(m) 
    WITH n,e,m
    RETURN n,e,m
$$) AS (N1 agtype, edge agtype, N2 agtype);

-- WITH/AS

SELECT * FROM cypher('cypher_with', $$
    MATCH (n)-[e]->(m)
    WITH n.name AS n1, e as edge, m.name as n2
    RETURN n1,label(edge),n2
$$) AS (start_node agtype,edge agtype, end_node agtype);

SELECT * FROM cypher('cypher_with',$$
    MATCH (person)-[r]->(otherPerson)
    WITH *, type(r) AS connectionType
    RETURN person.name, connectionType, otherPerson.name
$$) AS (start_node agtype, connection agtype, end_node agtype);

SELECT * FROM cypher('cypher_with', $$
    WITH true AS b
    RETURN b
$$) AS (b bool);

-- WITH/WHERE

SELECT * FROM cypher('cypher_with', $$
MATCH (george {name: 'George'})<-[]-(otherPerson)
    WITH otherPerson, toUpper(otherPerson.name) AS upperCaseName
    WHERE upperCaseName STARTS WITH 'C'
    RETURN otherPerson.name
$$) as (name agtype);

SELECT * FROM cypher('cypher_with', $$
	MATCH (david {name: 'David'})-[]-(otherPerson)-[]->()
	WITH otherPerson, count(*) AS foaf
	WHERE foaf > 1
	RETURN otherPerson.name
$$) as (name agtype);

SELECT * FROM cypher('cypher_with', $$
    MATCH p = (m)-[*1..2]->(b) 
    WITH p, length(p) AS path_length 
    WHERE path_length > 1 
    RETURN p
$$) AS (pattern agtype);

-- MATCH/WHERE with WITH/WHERE

SELECT * FROM cypher('cypher_with', $$
    MATCH (m)-[e]->(b)
    WHERE label(e) = 'KNOWS'
    WITH *
    WHERE m.name = 'Andres'
    RETURN m.name,label(e),b.name
$$) AS (N1 agtype, edge agtype, N2 agtype);

-- WITH/ORDER BY

SELECT * FROM cypher('cypher_with', $$
    MATCH (n)
    WITH n
    ORDER BY id(n)
    RETURN n
$$) as (name agtype);

-- WITH/ORDER BY/DESC

SELECT * FROM cypher('cypher_with', $$
    MATCH (n)
    WITH n
    ORDER BY n.name DESC LIMIT 3
    RETURN collect(n.name)
$$) as (names agtype);

SELECT * FROM cypher('cypher_with', $$
    MATCH (n {name: 'Andres'})-[]-(m)
    WITH m
    ORDER BY m.name DESC LIMIT 1
    MATCH (m)-[]-(o)
    RETURN o.name ORDER BY o.name
$$) as (name agtype);

-- multiple WITH clauses

SELECT * FROM cypher('cypher_with', $$
    MATCH (n)-[e]->(m)
    WITH n, e, m
    WHERE label(e) = 'KNOWS'
    WITH n.name as n1, label(e) as edge, m.name as n2
    WHERE n1 = 'Andres'
    RETURN n1,edge,n2
$$) AS (N1 agtype, edge agtype, N2 agtype);

SELECT * FROM cypher('cypher_with', $$
    UNWIND [1, 2, 3, 4, 5, 6] AS x
    WITH x
    WHERE x > 2
    WITH x
    LIMIT 5
    RETURN x
$$) as (name agtype);

SELECT * FROM cypher('cypher_with', $$
    MATCH (m)-[]->(b)
    WITH m,b
    ORDER BY id(m) DESC LIMIT 5
    WITH m as start_node, b as end_node
    WHERE end_node.name = 'George'
    RETURN id(start_node),start_node.name,id(end_node),end_node.name
$$) AS (id1 agtype, name1 agtype, id2 agtype, name2 agtype);

-- Expression item must be aliased.

SELECT * FROM cypher('cypher_with', $$
    WITH 1 + 1
    RETURN i
$$) AS (i int);

SELECT * FROM cypher('cypher_with', $$
    MATCH (m)-[]->(b)
    WITH id(m)
    RETURN m
$$) AS (id agtype);

-- Reference undefined variable in WITH clause (should error out)

SELECT count(*) FROM cypher('cypher_with', $$
    MATCH (m)-[]->(b)
    WITH m  
    RETURN m,b
$$) AS (a agtype, b agtype);

SELECT * FROM cypher('cypher_with', $$
    MATCH (m)-[]->(b)
    WITH m AS start_node,b AS end_node
    WHERE start_node.name = 'Andres'
    WITH start_node
    WHERE start_node.name = 'George'
    RETURN id(start_node),end_node.name
$$) AS (id agtype, node agtype);

-- Clean up

SELECT drop_graph('cypher_with', true);

-- Issue 329 (should error out)

SELECT create_graph('graph');

SELECT * FROM cypher('graph', $$
    CREATE (a:A)-[:incs]->(:C), (a)-[:incs]->(:C)
    RETURN a
$$) AS (n agtype);

SELECT * FROM cypher('graph', $$
    MATCH (a:A) 
    WHERE ID(a)=0 
    WITH a 
    OPTIONAL MATCH (a)-[:incs]->(c)-[d:incs]-() 
    WITH a,c,COUNT(d) AS deps 
    WHERE deps<=1 
    RETURN c,d
$$) AS (n agtype, d agtype);

-- Issue 396 (should error out)

SELECT * FROM cypher('graph',$$
    CREATE (v),(u),(w),
        (v)-[:hasFriend]->(u),
        (u)-[:hasFriend]->(w)
$$) as (a agtype);

SELECT * FROM cypher('graph',$$
    MATCH p=(v)-[*1..2]->(u) 
    WITH p,length(p) AS path_length 
    RETURN v,path_length
$$) as (a agtype,b agtype);

-- Clean up

SELECT drop_graph('graph', true);

--
-- End of test
--
