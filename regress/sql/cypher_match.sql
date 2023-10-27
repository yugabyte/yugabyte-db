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

SELECT create_graph('cypher_match');

SELECT * FROM cypher('cypher_match', $$CREATE (:v)$$) AS (a agtype);
SELECT * FROM cypher('cypher_match', $$CREATE (:v {i: 0})$$) AS (a agtype);
SELECT * FROM cypher('cypher_match', $$CREATE (:v {i: 1})$$) AS (a agtype);

SELECT * FROM cypher('cypher_match', $$MATCH (n:v) RETURN n$$) AS (n agtype);
SELECT * FROM cypher('cypher_match', $$MATCH (n:v) RETURN n.i$$) AS (i agtype);

SELECT * FROM cypher('cypher_match', $$
MATCH (n:v) WHERE n.i > 0
RETURN n.i
$$) AS (i agtype);

--Directed Paths
SELECT * FROM cypher('cypher_match', $$
	CREATE (:v1 {id:'initial'})-[:e1]->(:v1 {id:'middle'})-[:e1]->(:v1 {id:'end'})
$$) AS (a agtype);

--Undirected Path Tests
SELECT * FROM cypher('cypher_match', $$
	MATCH p=(:v1)-[:e1]-(:v1)-[:e1]-(:v1) RETURN p
$$) AS (a agtype);

SELECT * FROM cypher('cypher_match', $$
	MATCH p=(a:v1)-[]-()-[]-() RETURN a
$$) AS (a agtype);

SELECT * FROM cypher('cypher_match', $$
	MATCH ()-[]-()-[]-(a:v1) RETURN a
$$) AS (a agtype);

SELECT * FROM cypher('cypher_match', $$
	MATCH ()-[]-(a:v1)-[]-() RETURN a
$$) AS (a agtype);

SELECT * FROM cypher('cypher_match', $$
	MATCH ()-[b:e1]-()-[]-() RETURN b
$$) AS (a agtype);

SELECT * FROM cypher('cypher_match', $$
	MATCH (a:v1)-[]->(), ()-[]->(a) RETURN a
$$) AS (a agtype);

SELECT * FROM cypher('cypher_match', $$
    MATCH p=()-[e]-() RETURN e
$$) AS (a agtype);

SELECT * FROM cypher('cypher_match', $$
    MATCH (:v1)-[e]-() RETURN e
$$) AS (a agtype);

SELECT * FROM cypher('cypher_match', $$
    MATCH (:v1)-[e]-(:v1) RETURN e
$$) AS (a agtype);

SELECT * FROM cypher('cypher_match', $$
    MATCH ()-[]-()-[e]-(:v1) RETURN e
$$) AS (a agtype);

SELECT * FROM cypher('cypher_match', $$
    MATCH (a)-[]-()-[]-(:v1) RETURN a
$$) AS (a agtype);

-- Right Path Test
SELECT * FROM cypher('cypher_match', $$
	MATCH (a:v1)-[:e1]->(b:v1)-[:e1]->(c:v1) RETURN a, b, c
$$) AS (a agtype, b agtype, c agtype);

SELECT * FROM cypher('cypher_match', $$
	MATCH p=(a:v1)-[]-()-[]->() RETURN a
$$) AS (a agtype);

SELECT * FROM cypher('cypher_match', $$
	MATCH p=(a:v1)-[]->()-[]-() RETURN a
$$) AS (a agtype);

SELECT * FROM cypher('cypher_match', $$
	MATCH ()-[]-()-[]->(a:v1) RETURN a
$$) AS (a agtype);

SELECT * FROM cypher('cypher_match', $$
	MATCH ()-[]-(a:v1)-[]->() RETURN a
$$) AS (a agtype);

SELECT * FROM cypher('cypher_match', $$
	MATCH ()-[b:e1]-()-[]->() RETURN b
$$) AS (a agtype);

SELECT * FROM cypher('cypher_match', $$
    MATCH (:v1)-[e]->() RETURN e
$$) AS (a agtype);

SELECT * FROM cypher('cypher_match', $$
    MATCH ()-[e]->(:v1) RETURN e
$$) AS (a agtype);

SELECT * FROM cypher('cypher_match', $$
    MATCH (:v1)-[e]->(:v1) RETURN e
$$) AS (a agtype);

--Left Path Test
SELECT * FROM cypher('cypher_match', $$
	MATCH (a:v1)<-[:e1]-(b:v1)<-[:e1]-(c:v1) RETURN a, b, c
$$) AS (a agtype, b agtype, c agtype);

SELECT * FROM cypher('cypher_match', $$
	MATCH p=(a:v1)<-[]-()-[]-() RETURN a
$$) AS (a agtype);

SELECT * FROM cypher('cypher_match', $$
	MATCH p=(a:v1)-[]-()<-[]-() RETURN a
$$) AS (a agtype);

SELECT * FROM cypher('cypher_match', $$
	MATCH ()<-[]-()-[]-(a:v1) RETURN a
$$) AS (a agtype);

SELECT * FROM cypher('cypher_match', $$
	MATCH ()<-[]-(a:v1)-[]-() RETURN a
$$) AS (a agtype);

SELECT * FROM cypher('cypher_match', $$
	MATCH ()<-[b:e1]-()-[]-() RETURN b
$$) AS (a agtype);

SELECT * FROM cypher('cypher_match', $$
    MATCH (:v1)<-[e]-(:v1) RETURN e
$$) AS (a agtype);

--Divergent Path Tests
SELECT * FROM cypher('cypher_match', $$
	CREATE (:v2 {id:'initial'})<-[:e2]-(:v2 {id:'middle'})-[:e2]->(:v2 {id:'end'})
$$) AS (a agtype);

SELECT * FROM cypher('cypher_match', $$
	MATCH ()<-[]-(n:v2)-[]->()
	MATCH p=()-[]->(n)
	RETURN p
$$) AS (i agtype);

SELECT * FROM cypher('cypher_match', $$
	MATCH ()<-[]-(n:v2)-[]->()
	MATCH p=(n)-[]->()
	RETURN p
$$) AS (i agtype);

SELECT * FROM cypher('cypher_match', $$
	MATCH ()-[]-(n:v2)
	RETURN n
$$) AS (i agtype);

SELECT * FROM cypher('cypher_match', $$
    MATCH (:v2)<-[]-(:v2)-[]->(:v2)
    MATCH p=()-[]->()
    RETURN p
$$) AS (i agtype);

SELECT * FROM cypher('cypher_match', $$
    MATCH ()<-[]-(:v2)-[]->()
	MATCH p=()-[]->()
    RETURN p
$$) AS (i agtype);

--Convergent Path Tests
SELECT * FROM cypher('cypher_match', $$
	CREATE (:v3 {id:'initial'})-[:e3]->(:v3 {id:'middle'})<-[:e3]-(:v3 {id:'end'})
$$) AS (a agtype);

SELECT * FROM cypher('cypher_match', $$
	MATCH ()-[b:e1]->()
	RETURN b
$$) AS (i agtype);

SELECT * FROM cypher('cypher_match', $$
    MATCH (:v3)-[b:e3]->()
    RETURN b
$$) AS (i agtype);

SELECT * FROM cypher('cypher_match', $$
	MATCH ()-[]->(n:v1)<-[]-()
	MATCH p=(n)<-[]-()
	RETURN p
$$) AS (i agtype);

SELECT * FROM cypher('cypher_match', $$
	MATCH ()-[]->(n:v1)<-[]-()
	MATCH p=()-[]->(n)
	RETURN p
$$) AS (i agtype);

SELECT * FROM cypher('cypher_match', $$
	MATCH ()-[]->(n:v1)<-[]-()
	MATCH p=(n)-[]->()
	RETURN p
$$) AS (i agtype);

SELECT * FROM cypher('cypher_match', $$
	MATCH con_path=(a)-[]->()<-[]-()
	where a.id = 'initial'
	RETURN con_path
$$) AS (con_path agtype);

SELECT * FROM cypher('cypher_match', $$
	MATCH div_path=(b)<-[]-()-[]->()
	where b.id = 'initial'
	RETURN div_path
$$) AS (div_path agtype);

SELECT * FROM cypher('cypher_match', $$
	MATCH (a)-[]->(:v3)<-[]-(b)
	where a.id = 'initial'
	RETURN b       
$$) AS (con_path agtype);

--Patterns
SELECT * FROM cypher('cypher_match', $$
	MATCH (a:v1), p=(a)-[]-()-[]-()
	where a.id = 'initial'
	RETURN p
$$) AS (p agtype);

SELECT * FROM cypher('cypher_match', $$
	MATCH con_path=(a)-[]->()<-[]-(), div_path=(b)<-[]-()-[]->()
	where a.id = 'initial'
	and b.id = 'initial'
	RETURN con_path, div_path
$$) AS (con_path agtype, div_path agtype);

SELECT * FROM cypher('cypher_match', $$
	MATCH (a:v), p=()-[]->()-[]->()
	RETURN a.i, p
$$) AS (i agtype, p agtype);

--Multiple Match Clauses
SELECT * FROM cypher('cypher_match', $$
	MATCH (a:v1)
	where a.id = 'initial'
	MATCH p=(a)-[]-()-[]-()
	RETURN p
$$) AS (p agtype);

SELECT * FROM cypher('cypher_match', $$
	MATCH (a:v)
	MATCH p=()-[]->()-[]->()
	RETURN a.i, p
$$) AS (i agtype, p agtype);

SELECT * FROM cypher('cypher_match', $$
	MATCH (a:v)
	MATCH (b:v1)-[]-(c)
	RETURN a.i, b.id, c.id
$$) AS (i agtype, b agtype, c agtype);

SELECT * FROM cypher('cypher_match', $$
	MATCH (a:v)
	MATCH (:v1)-[]-(c)
	RETURN a.i, c.id
$$) AS (i agtype,  c agtype);

--
-- Property constraints
--
SELECT * FROM cypher('cypher_match',
 $$CREATE ({string_key: "test", int_key: 1, float_key: 3.14, map_key: {key: "value"}, list_key: [1, 2, 3]}) $$)
AS (p agtype);

SELECT * FROM cypher('cypher_match',
 $$CREATE ({lst: [1, NULL, 3.14, "string", {key: "value"}, []]}) $$)
AS (p agtype);

SELECT * FROM cypher('cypher_match',
 $$MATCH (n  {string_key: NULL}) RETURN n $$)
AS (n agtype);

SELECT * FROM cypher('cypher_match',
 $$MATCH (n  {string_key: "wrong value"}) RETURN n $$)
AS (n agtype);


SELECT * FROM cypher('cypher_match', $$
    MATCH (n {string_key: "test", int_key: 1, float_key: 3.14, map_key: {key: "value"}, list_key: [1, 2, 3]})
    RETURN n $$)
AS (p agtype);

SELECT * FROM cypher('cypher_match',
 $$MATCH (n {string_key: "test"}) RETURN n $$)
AS (p agtype);

SELECT * FROM cypher('cypher_match',
 $$MATCH (n {lst: [1, NULL, 3.14, "string", {key: "value"}, []]})  RETURN n $$)
AS (p agtype);

SELECT * FROM cypher('cypher_match',
 $$MATCH (n {lst: [1, NULL, 3.14, "string", {key: "value"}, [], "extra value"]})  RETURN n $$)
AS (p agtype);


--
-- Prepared Statement Property Constraint
--
PREPARE property_ps(agtype) AS SELECT * FROM cypher('cypher_match',
 $$MATCH (n $props) RETURN n $$, $1)
AS (p agtype);

EXECUTE property_ps(agtype_build_map('props',
                                     agtype_build_map('string_key', 'test')));

-- need a following RETURN clause (should fail)
SELECT * FROM cypher('cypher_match', $$MATCH (n:v)$$) AS (a agtype);

--invalid variable reuse, these should fail
SELECT * FROM cypher('cypher_match', $$
	MATCH (a)-[]-()-[]-(a:v1) RETURN a
$$) AS (a agtype);
SELECT * FROM cypher('cypher_match', $$
	MATCH (a)-[]-(a:v2)-[]-(a) RETURN a
$$) AS (a agtype);
SELECT * FROM cypher('cypher_match', $$
	MATCH (a)-[]-(a:v1) RETURN a
$$) AS (a agtype);
SELECT * FROM cypher('cypher_match', $$
	MATCH (a)-[]-(a)-[]-(a:v1) RETURN a
$$) AS (a agtype);
SELECT * FROM cypher('cypher_match', $$
	MATCH (a)-[]-(a)-[]-(a:invalid_label) RETURN a
$$) AS (a agtype);
SELECT * FROM cypher('cypher_match', $$
	MATCH (a) MATCH (a:v1) RETURN a
$$) AS (a agtype);
SELECT * FROM cypher('cypher_match', $$
	MATCH (a) MATCH (a:invalid_label) RETURN a
$$) AS (a agtype);
SELECT * FROM cypher('cypher_match', $$
	MATCH (a:v1)-[]-()-[a]-() RETURN a
$$) AS (a agtype);

-- valid variable reuse for edge labels across clauses
SELECT * FROM cypher('cypher_match', $$
        MATCH ()-[r0]->() MATCH ()-[r0]->() RETURN r0
$$) AS (r0 agtype);
SELECT * FROM cypher('cypher_match', $$
        MATCH ()-[r0:e1]->() MATCH ()-[r0:e1]->() RETURN r0
$$) AS (r0 agtype);
SELECT * FROM cypher('cypher_match', $$
        MATCH ()-[r0:e2]->() MATCH ()-[r0:e2]->() RETURN r0
$$) AS (r0 agtype);
SELECT * FROM cypher('cypher_match', $$
        MATCH ()-[r0:e1]->()-[r1]->() RETURN r0,r1
$$) AS (r0 agtype, r1 agtype);
SELECT * FROM cypher('cypher_match', $$
        MATCH p0=()-[:e1]->() MATCH p1=()-[:e2]->() RETURN p1
$$) AS (p1 agtype);
SELECT * FROM cypher('cypher_match', $$
        MATCH ()-[r0:e1]->()-[r1]->() MATCH ()-[r0:e1]->()-[r1]->() RETURN r0,r1
$$) AS (r0 agtype, r1 agtype);
SELECT * FROM cypher('cypher_match', $$
        MATCH ()-[]->() MATCH ()-[r1:e2]->() RETURN r1
$$) AS (r1 agtype);
SELECT * FROM cypher('cypher_match', $$
        MATCH ()-[r0:e1]->() MATCH ()-[r1:e2]->() RETURN r0,r1
$$) AS (r0 agtype, r1 agtype);

-- valid variable reuse for vertex labels across clauses
SELECT * FROM cypher('cypher_match', $$
         MATCH (r1:invalid), (r1:invalid) return r1
$$) AS (r1 agtype);
SELECT * FROM cypher('cypher_match', $$
         MATCH (r1), (r1) return r1
$$) AS (r1 agtype);
SELECT * FROM cypher('cypher_match', $$
         MATCH (r1:invalid), (r1) return r1
$$) AS (r1 agtype);
SELECT * FROM cypher('cypher_match', $$
         MATCH (r1:invalid), (r1), (r1), (r1:invalid) return r1
$$) AS (r1 agtype);
SELECT * FROM cypher('cypher_match', $$
         MATCH (r1:invalid)-[]->(r1)-[]->(r1:invalid)-[]->(r1) return r1
$$) AS (r1 agtype);
SELECT * FROM cypher('cypher_match', $$
         MATCH (r1:invalid)-[]->()-[]->()-[]->(r1:invalid) return r1
$$) AS (r1 agtype);
SELECT * FROM cypher('cypher_match', $$
        MATCH ()-[r0:e1]->()-[r1]->() MATCH ()-[r0:e1]->()-[r0]->() RETURN r0
$$) AS (r0 agtype);
SELECT * FROM cypher('cypher_match', $$
        MATCH ()-[r0:e1]->() MATCH ()-[r0]->() RETURN r0
$$) AS (r0 agtype);

-- invalid variable reuse for vertex
SELECT * FROM cypher('cypher_match', $$
         MATCH (r1:invalid)-[]->(r1)-[]->(r1)-[]->(r1:invalids) return r1
$$) AS (r1 agtype);
SELECT * FROM cypher('cypher_match', $$
         MATCH (r1:invalid)-[]->(r1)-[]->(r1)-[]->(r1)-[r1]->() return r1
$$) AS (r1 agtype);

-- invalid variable reuse for labels across clauses
SELECT * FROM cypher('cypher_match', $$
         MATCH (r1:e1), (r1:e2) return r1
$$) AS (r1 agtype);
SELECT * FROM cypher('cypher_match', $$
         MATCH (r1:invalid), (r1:e2) return r1
$$) AS (r1 agtype);
SELECT * FROM cypher('cypher_match', $$
         MATCH (r1:e1), (r1:invalid) return r1
$$) AS (r1 agtype);
SELECT * FROM cypher('cypher_match', $$
         MATCH (r1:e1), (r1), (r1:invalid) return r1
$$) AS (r1 agtype);
SELECT * FROM cypher('cypher_match', $$
        MATCH (r0)-[r0]->() MATCH ()-[]->() RETURN r0
$$) AS (r0 agtype);
SELECT * FROM cypher('cypher_match', $$
        MATCH (r0)-[]->() MATCH ()-[r0]->() RETURN r0
$$) AS (r0 agtype);
SELECT * FROM cypher('cypher_match', $$
        MATCH ()-[r0]->() MATCH ()-[]->(r0) RETURN r0
$$) AS (r0 agtype);
SELECT * FROM cypher('cypher_match', $$
        MATCH ()-[r0:e1]->() MATCH ()-[r0:e2]->() RETURN r0
$$) AS (r0 agtype);
SELECT * FROM cypher('cypher_match', $$
        MATCH ()-[r0]->() MATCH ()-[r0:e2]->() RETURN r0
$$) AS (r0 agtype);
SELECT * FROM cypher('cypher_match', $$
        MATCH ()-[r0:e1]->() MATCH ()-[r0:e2]->() RETURN r0
$$) AS (r0 agtype);
SELECT * FROM cypher('cypher_match', $$
        MATCH ()-[r0:e1]->()-[r0]->() MATCH ()-[r0:e2]->() RETURN r0
$$) AS (r0 agtype);
SELECT * FROM cypher('cypher_match', $$
        MATCH ()-[r0:e1]->()-[r1]->() MATCH ()-[r1:e1]->()-[r0]->() RETURN r0
$$) AS (r0 agtype);

-- Labels that don't exist but do match
SELECT * FROM cypher('cypher_match', $$
        MATCH (r0)-[r1:related]->() MATCH ()-[r1:related]->() RETURN r0
$$) AS (r0 agtype);

-- Labels that don't exist and don't match
SELECT * FROM cypher('cypher_match', $$
        MATCH (r0)-[r1]->() MATCH ()-[r1:related]->() RETURN r0
$$) AS (r0 agtype);
SELECT * FROM cypher('cypher_match', $$
        MATCH (r0)-[r1:related]->() MATCH ()-[r1:relateds]->() RETURN r0
$$) AS (r0 agtype);

--Valid variable reuse, although why would you want to do it this way?
SELECT * FROM cypher('cypher_match', $$
	MATCH (a:v1)-[]-()-[]-(a {id:'will_not_fail'}) RETURN a
$$) AS (a agtype);

--Incorrect Labels
SELECT * FROM cypher('cypher_match', $$MATCH (n)-[:v]-() RETURN n$$) AS (n agtype);

SELECT * FROM cypher('cypher_match', $$MATCH (n)-[:emissing]-() RETURN n$$) AS (n agtype);

SELECT * FROM cypher('cypher_match', $$MATCH (n:e1)-[]-() RETURN n$$) AS (n agtype);

SELECT * FROM cypher('cypher_match', $$MATCH (n:vmissing)-[]-() RETURN n$$) AS (n agtype);

SELECT * FROM cypher('cypher_match', $$MATCH (:e1)-[r]-() RETURN r$$) AS (r agtype);

SELECT * FROM cypher('cypher_match', $$MATCH (:vmissing)-[r]-() RETURN r$$) AS (r agtype);

SELECT * FROM cypher('cypher_match', $$MATCH (n),(:e1) RETURN n$$) AS (n agtype);

SELECT * FROM cypher('cypher_match', $$MATCH (n),()-[:v]-() RETURN n$$) AS (n agtype);

--
-- Path of one vertex. This should select 14
--
SELECT * FROM cypher('cypher_match', $$
       MATCH p=() RETURN p
$$) AS (p agtype);

--
-- MATCH with WHERE EXISTS(pattern)
--
SELECT * FROM cypher('cypher_match',
 $$MATCH (u)-[e]->(v) RETURN u, e, v $$) AS (u agtype, e agtype, v agtype);

SELECT * FROM cypher('cypher_match',
 $$MATCH (u)-[e]->(v) WHERE EXISTS((u)-[e]->(v)) RETURN u, e, v $$)
AS (u agtype, e agtype, v agtype);


-- Property Constraint in EXISTS
SELECT * FROM cypher('cypher_match',
 $$MATCH (u) WHERE EXISTS((u)-[]->({id: "middle"})) RETURN u $$)
AS (u agtype);

SELECT * FROM cypher('cypher_match',
 $$MATCH (u) WHERE EXISTS((u)-[]->({id: "not a valid id"})) RETURN u $$)
AS (u agtype);

SELECT * FROM cypher('cypher_match',
 $$MATCH (u) WHERE EXISTS((u)-[]->({id: NULL})) RETURN u $$)
AS (u agtype);

-- Exists checks for a loop. There shouldn't be any.
SELECT * FROM cypher('cypher_match',
 $$MATCH (u)-[e]->(v) WHERE EXISTS((u)-[e]->(u)) RETURN u, e, v $$)
AS (u agtype, e agtype, v agtype);

-- Create a loop
SELECT * FROM cypher('cypher_match', $$
        CREATE (u:loop {id:'initial'})-[:self]->(u)
$$) AS (a agtype);

-- dump paths
SELECT * FROM cypher('cypher_match',
 $$MATCH (u)-[e]->(v) WHERE EXISTS((u)-[e]->(v)) RETURN u, e, v $$)
AS (u agtype, e agtype, v agtype);

-- Exists checks for a loop. There should be one.
SELECT * FROM cypher('cypher_match',
 $$MATCH (u)-[e]->(v) WHERE EXISTS((u)-[e]->(u)) RETURN u, e, v $$)
AS (u agtype, e agtype, v agtype);

-- Exists checks for a loop. There should be one.
SELECT * FROM cypher('cypher_match',
 $$MATCH (u)-[e]->(v) WHERE EXISTS((v)-[e]->(v)) RETURN u, e, v $$)
AS (u agtype, e agtype, v agtype);

-- Multiple exists
SELECT * FROM cypher('cypher_match',
 $$MATCH (u)-[e]->(v) WHERE EXISTS((u)) AND EXISTS((v)) RETURN u, e, v $$)
AS (u agtype, e agtype, v agtype);

SELECT * FROM cypher('cypher_match',
 $$MATCH (u)-[e]->(v) WHERE EXISTS((u)-[e]->(u)) AND EXISTS((v)-[e]->(v)) RETURN u, e, v $$)
AS (u agtype, e agtype, v agtype);

-- Return exists(pattern)

SELECT * FROM cypher('cypher_match',
 $$MATCH (u) RETURN EXISTS((u)-[]->()) $$)
AS (exists agtype);

SELECT * FROM cypher('cypher_match',
 $$MATCH p=(u)-[e]->(v) RETURN EXISTS((p)) $$)
AS (exists agtype);

SELECT * FROM cypher('cypher_match',
 $$MATCH (u)-[e]->(v) RETURN EXISTS((u)-[e]->(v)-[e]->(u))$$)
AS (exists agtype);

-- These should error
-- Bad pattern
SELECT * FROM cypher('cypher_match',
 $$MATCH (u)-[e]->(v) WHERE EXISTS((u)) AND EXISTS([e]) AND EXISTS((v)) RETURN u, e, v $$)
AS (u agtype, e agtype, v agtype);

-- variable creation error
SELECT * FROM cypher('cypher_match',
 $$MATCH (u)-[e]->(v) WHERE EXISTS((u)-[e]->(x)) RETURN u, e, v $$)
AS (u agtype, e agtype, v agtype);

--
-- Tests for EXISTS(property)
--

-- dump all vertices
SELECT * FROM cypher('cypher_match', $$MATCH (u) RETURN u $$) AS (u agtype);

-- select vertices with id as a property
SELECT * FROM cypher('cypher_match',
 $$MATCH (u) WHERE EXISTS(u.id) RETURN u $$)
AS (u agtype);

-- select vertices without id as a property
SELECT * FROM cypher('cypher_match',
 $$MATCH (u) WHERE NOT EXISTS(u.id) RETURN u $$)
AS (u agtype);

-- select vertices without id as a property but with a property i
SELECT * FROM cypher('cypher_match',
 $$MATCH (u) WHERE NOT EXISTS(u.id) AND EXISTS(u.i) RETURN u $$)
AS (u agtype);

-- select vertices with id as a property and have a self loop
SELECT * FROM cypher('cypher_match',
 $$MATCH (u) WHERE EXISTS(u.id) AND EXISTS((u)-[]->(u)) RETURN u$$)
AS (u agtype);

-- Return exists(property)
SELECT * FROM cypher('cypher_match',
 $$MATCH (u) RETURN EXISTS(u.id), properties(u) $$)
AS (exists agtype, properties agtype);

SELECT * FROM cypher('cypher_match',
 $$MATCH (u) RETURN EXISTS(u.name), properties(u) $$)
AS (exists agtype, properties agtype);

-- should give an error
SELECT * FROM cypher('cypher_match',
 $$MATCH (u) WHERE EXISTS(u) RETURN u$$)
AS (u agtype);

--
-- MATCH with WHERE isEmpty(property)
--

SELECT create_graph('for_isEmpty');

-- Create vertices

SELECT * FROM cypher('for_isEmpty',
 $$CREATE (u:for_pred {id:1, type: "empty", list: [], map: {}, string: ""}),
		  (v:for_pred {id:2, type: "filled", list: [1], map: {a:1}, string: "a"}),
		  (w:for_pred)$$)
AS (a agtype);

-- Match vertices with empty properties

SELECT * FROM cypher('for_isEmpty',
 $$MATCH (u:for_pred) WHERE isEmpty(u.list) RETURN properties(u) $$)
AS (u agtype);

SELECT * FROM cypher('for_isEmpty',
 $$MATCH (u:for_pred) WHERE isEmpty(u.map) RETURN properties(u) $$)
AS (u agtype);

SELECT * FROM cypher('for_isEmpty',
 $$MATCH (u:for_pred) WHERE isEmpty(u.string) RETURN properties(u) $$)
AS (u agtype);

-- Match vertices with non-empty properties

SELECT * FROM cypher('for_isEmpty',
 $$MATCH (u:for_pred) WHERE NOT isEmpty(u.list) RETURN properties(u) $$)
AS (u agtype);

SELECT * FROM cypher('for_isEmpty',
 $$MATCH (u:for_pred) WHERE NOT isEmpty(u.map) RETURN properties(u) $$)
AS (u agtype);

SELECT * FROM cypher('for_isEmpty',
 $$MATCH (u:for_pred) WHERE NOT isEmpty(u.string) RETURN properties(u) $$)
AS (u agtype);

-- Match vertices with no properties

SELECT * FROM cypher('for_isEmpty',
 $$MATCH (u:for_pred) WHERE isEmpty(properties(u)) RETURN properties(u) $$)
AS (u agtype);

-- Match vertices with properties

SELECT * FROM cypher('for_isEmpty',
 $$MATCH (u:for_pred) WHERE NOT isEmpty(properties(u)) RETURN properties(u) $$)
AS (u agtype);

-- Match vertices with null property (should return nothing since WHERE null)

SELECT * FROM cypher('for_isEmpty',
 $$MATCH (u:for_pred) WHERE isEmpty(u.tree) RETURN properties(u) $$)
AS (u agtype);

-- Match and Return bool

SELECT * FROM cypher('for_isEmpty',
 $$MATCH (u:for_pred) WHERE isEmpty(u.list) RETURN isEmpty(u.list), u.type $$)
AS (b agtype, type agtype);

SELECT * FROM cypher('for_isEmpty',
 $$MATCH (u:for_pred) WHERE NOT isEmpty(u.list) RETURN isEmpty(u.list), u.type $$)
AS (b agtype, type agtype);

-- Return null on null

SELECT * FROM cypher('for_isEmpty',
 $$MATCH (u:for_pred) RETURN isEmpty(u.tree) $$)
AS (b agtype);

-- Should give an error

SELECT * FROM cypher('for_isEmpty',
 $$MATCH (u:for_pred) WHERE isEmpty(u) RETURN properties(u) $$)
AS (u agtype);

SELECT * FROM cypher('for_isEmpty',
 $$MATCH (u:for_pred) WHERE isEmpty(1) RETURN properties(u) $$)
AS (u agtype);

SELECT * FROM cypher('for_isEmpty',
 $$MATCH (u:for_pred) WHERE isEmpty(1,2,3) RETURN properties(u) $$)
AS (u agtype);

SELECT * FROM cypher('for_isEmpty',
 $$MATCH (u:for_pred) WHERE isEmpty() RETURN properties(u) $$)
AS (u agtype);

-- clean up
SELECT drop_graph('for_isEmpty', true);

--
--Distinct
--
SELECT * FROM cypher('cypher_match', $$
	MATCH (u)
	RETURN DISTINCT u.id
$$) AS (i agtype);

SELECT * FROM cypher('cypher_match', $$
	CREATE (u:duplicate)-[:dup_edge {id:1 }]->(:other_v)
$$) AS (a agtype);

SELECT * FROM cypher('cypher_match', $$
	MATCH (u:duplicate)
	CREATE (u)-[:dup_edge {id:2 }]->(:other_v)
$$) AS (a agtype);

SELECT * FROM cypher('cypher_match', $$
	MATCH (u:duplicate)-[]-(:other_v)
	RETURN DISTINCT u
$$) AS (i agtype);

SELECT * FROM cypher('cypher_match', $$
	MATCH p=(:duplicate)-[]-(:other_v)
	RETURN DISTINCT p
$$) AS (i agtype);

--
-- Limit
--
SELECT * FROM cypher('cypher_match', $$
	MATCH (u)
	RETURN u
$$) AS (i agtype);

SELECT * FROM cypher('cypher_match', $$
	MATCH (u)
	RETURN u LIMIT 3
$$) AS (i agtype);

--
-- Skip
--
SELECT * FROM cypher('cypher_match', $$
	MATCH (u)
	RETURN u SKIP 7
$$) AS (i agtype);

SELECT * FROM cypher('cypher_match', $$
	MATCH (u)
	RETURN u SKIP 7 LIMIT 3
$$) AS (i agtype);


--
-- Optional Match
--
SELECT * FROM cypher('cypher_match', $$
    CREATE (:opt_match_v {name: 'someone'})-[:opt_match_e]->(:opt_match_v {name: 'somebody'}),
           (:opt_match_v {name: 'anybody'})-[:opt_match_e]->(:opt_match_v {name: 'nobody'})
$$) AS (u agtype);

SELECT * FROM cypher('cypher_match', $$
    MATCH (u:opt_match_v)
    OPTIONAL MATCH (u)-[m]-(l)
    RETURN u.name as u, type(m), l.name as l
    ORDER BY u, m, l
$$) AS (u agtype, m agtype, l agtype);

SELECT * FROM cypher('cypher_match', $$
    OPTIONAL MATCH (n:opt_match_v)-[r]->(p), (m:opt_match_v)-[s]->(q)
    WHERE id(n) <> id(m)
    RETURN n.name as n, type(r) AS r, p.name as p,
           m.name AS m, type(s) AS s, q.name AS q
    ORDER BY n, p, m, q
$$) AS (n agtype, r agtype, p agtype, m agtype, s agtype, q agtype);

SELECT * FROM cypher('cypher_match', $$
    MATCH (n:opt_match_v), (m:opt_match_v)
    WHERE id(n) <> id(m)
    OPTIONAL MATCH (n)-[r]->(p), (m)-[s]->(q)
    RETURN n.name AS n, type(r) AS r, p.name AS p,
           m.name AS m, type(s) AS s, q.name AS q
    ORDER BY n, p, m, q
 $$) AS (n agtype, r agtype, p agtype, m agtype, s agtype, q agtype);

-- Tests to catch match following optional match logic
-- this syntax is invalid in cypher
SELECT * FROM cypher('cypher_match', $$
	OPTIONAL MATCH (n)
    MATCH (m)
    RETURN n,m
 $$) AS (n agtype, m agtype);

SELECT * FROM cypher('cypher_match', $$
	MATCH (n)
	OPTIONAL MATCH (m)
    MATCH (o)
    RETURN n,m
 $$) AS (n agtype, m agtype);

--
-- Tests retrieving Var from some parent's cpstate during transformation
--
SELECT create_graph('test_retrieve_var');
SELECT * FROM cypher('test_retrieve_var', $$ CREATE (:A)-[:incs]->(:C) $$) as (a agtype);

-- Tests with node Var
-- both queries should return the same result
-- first query does not retrieve any variable from any parent's cpstate
-- second query retrieves variable 'a', inside WHERE, from parent's parent's cpstate
SELECT * FROM cypher('test_retrieve_var', $$
    MATCH (a:A) WITH a
    OPTIONAL MATCH (a)-[:incs]->(c)
    WHERE EXISTS((c)<-[:incs]-())
    RETURN a, c
$$) AS (a agtype, c agtype);

SELECT * FROM cypher('test_retrieve_var', $$
    MATCH (a:A) WITH a
    OPTIONAL MATCH (a)-[:incs]->(c)
    WHERE EXISTS((c)<-[:incs]-(a))
    RETURN a, c
$$) AS (a agtype, c agtype);

-- Tests with edge Var
-- both queries should return the same result
-- first query does not retrieve any variable from any parent's cpstate
-- second query retrieves variable 'r', inside WHERE, from parent's parent's cpstate
SELECT * FROM cypher('test_retrieve_var', $$
    MATCH (a:A)-[r:incs]->() WITH a, r
    OPTIONAL MATCH (a)-[r]->(c)
    WHERE EXISTS(()<-[]-(c))
    RETURN a, r
$$) AS (a agtype, r agtype);

SELECT * FROM cypher('test_retrieve_var', $$
    MATCH (a:A)-[r:incs]->() WITH a, r
    OPTIONAL MATCH (a)-[r]->(c)
    WHERE EXISTS((:A)<-[]-(c))
    RETURN a, r
$$) AS (a agtype, r agtype);

SELECT * FROM cypher('test_retrieve_var', $$
    MATCH (a:A)-[r:incs]->() WITH a, r
    OPTIONAL MATCH (a)-[r]->(c)
    WHERE EXISTS((c)<-[]-(:A))
    RETURN a, r
$$) AS (a agtype, r agtype);

SELECT * FROM cypher('test_retrieve_var', $$
    MATCH (a:A)-[r:incs]->() WITH a, r
    OPTIONAL MATCH (a)-[r]->(c)
    WHERE EXISTS((:C)<-[]-(:A))
    RETURN a, r
$$) AS (a agtype, r agtype);

SELECT * FROM cypher('test_retrieve_var', $$
    MATCH (a:A)-[r:incs]->() WITH a, r
    OPTIONAL MATCH (a)-[r]->(c)
    WHERE EXISTS(()<-[r]-(c))
    RETURN a, r
$$) AS (a agtype, r agtype);

--
-- JIRA: AGE2-544
--

-- Clean up
SELECT DISTINCT * FROM cypher('cypher_match', $$
    MATCH (u) DETACH DELETE (u)
$$) AS (i agtype);

-- Prepare
SELECT * FROM cypher('cypher_match', $$
    CREATE (u {name: "orphan"})
    CREATE (u1 {name: "F"})-[u2:e1]->(u3 {name: "T"})
    RETURN u1, u2, u3
$$) as (u1 agtype, u2 agtype, u3 agtype);

-- Querying NOT EXISTS syntax
SELECT * FROM cypher('cypher_match', $$
     MATCH (f),(t)
     WHERE NOT EXISTS((f)-[]->(t))
     RETURN f.name, t.name
 $$) as (f agtype, t agtype);

-- Querying EXISTS syntax
SELECT * FROM cypher('cypher_match', $$
    MATCH (f),(t)
    WHERE EXISTS((f)-[]->(t))
    RETURN f.name, t.name
 $$) as (f agtype, t agtype);

-- Querying ALL
SELECT * FROM cypher('cypher_match', $$
    MATCH (f),(t)
    WHERE NOT EXISTS((f)-[]->(t)) or true
    RETURN f.name, t.name
$$) as (f agtype, t agtype);

-- Querying ALL
SELECT * FROM cypher('cypher_match', $$
    MATCH (f),(t)
    RETURN f.name, t.name
$$) as (f agtype, t agtype);

--
-- Constraints and WHERE clause together
--
SELECT * FROM cypher('cypher_match', $$
    CREATE ({i: 1, j: 2, k: 3}), ({i: 1, j: 3}), ({i:2, k: 3})
$$) as (a agtype);

SELECT * FROM cypher('cypher_match', $$
    MATCH (n {j: 2})
    WHERE n.i = 1
    RETURN n
$$) as (n agtype);

--
-- Regression tests to check previous clause variable refs
--
-- set up initial state and show what we're working with
SELECT * FROM cypher('cypher_match', $$
    CREATE (a {age: 4}) RETURN a $$) as (a agtype);
SELECT * FROM cypher('cypher_match', $$
	CREATE (b {age: 6}) RETURN b $$) as (b agtype);

SELECT * FROM cypher('cypher_match', $$
    MATCH (a) RETURN a $$) as (a agtype);
SELECT * FROM cypher('cypher_match', $$
    MATCH (a) WHERE exists(a.name) RETURN a $$) as (a agtype);
SELECT * FROM cypher('cypher_match', $$
    MATCH (a) WHERE exists(a.name) SET a.age = 4 RETURN a $$) as (a agtype);

SELECT * FROM cypher('cypher_match', $$
	MATCH (a),(b) WHERE a.age = 4 AND a.name = "T" AND b.age = 6
	RETURN a,b $$) as (a agtype, b agtype);
SELECT * FROM cypher('cypher_match', $$
	MATCH (a),(b) WHERE a.age = 4 AND a.name = "T" AND b.age = 6 CREATE
	(a)-[:knows {relationship: "friends", years: 3}]->(b) $$) as (r agtype);
SELECT * FROM cypher('cypher_match', $$
	MATCH (a),(b) WHERE a.age = 4 AND a.name = "orphan" AND b.age = 6 CREATE
	(a)-[:knows {relationship: "enemies", years: 4}]->(b) $$) as (r agtype);
SELECT * FROM cypher('cypher_match', $$
	MATCH (a)-[r]-(b) RETURN r $$) as (r agtype);

-- check reuse of 'a' clause-to-clause - vertices
SELECT * FROM cypher('cypher_match', $$
    MATCH (a {age:4}) RETURN a $$) as (a agtype);
SELECT * FROM cypher('cypher_match', $$
    MATCH (a) MATCH (a {age:4}) RETURN a $$) as (a agtype);

SELECT * FROM cypher('cypher_match', $$
    MATCH (a {age:4, name: "orphan"}) RETURN a $$) as (a agtype);
SELECT * FROM cypher('cypher_match', $$
    MATCH (a) MATCH (a {age:4}) MATCH (a {name: "orphan"}) RETURN a $$) as (a agtype);
SELECT * FROM cypher('cypher_match', $$
    MATCH (a {age:4}) MATCH (a {name: "orphan"}) RETURN a $$) as (a agtype);

SELECT * FROM cypher('cypher_match', $$
    MATCH (a) MATCH (a {age:4}) MATCH (a {name: "orphan"}) SET a.age = 3 RETURN a $$) as (a agtype);

SELECT * FROM cypher('cypher_match', $$
    MATCH (a) MATCH (a {age:3}) MATCH (a {name: "orphan"}) RETURN a $$) as (a agtype);
SELECT * FROM cypher('cypher_match', $$
    MATCH (a {name: "orphan"}) MATCH (a {age:3}) RETURN a $$) as (a agtype);

SELECT * FROM cypher('cypher_match', $$
    MATCH (a) WHERE exists(a.age) AND exists(a.name) RETURN a $$) as (a agtype);

SELECT * FROM cypher('cypher_match', $$
    MATCH (a) WHERE exists(a.age) AND NOT exists(a.name) RETURN a $$) as (a agtype);

-- check reuse of 'r' clause-to-clause - edges
SELECT * FROM cypher('cypher_match', $$
	MATCH ()-[r]-() RETURN r $$) as (r agtype);
SELECT * FROM cypher('cypher_match', $$
	MATCH ()-[r]-() MATCH ()-[r {relationship: "friends"}]-() RETURN r $$) as (r agtype);
SELECT * FROM cypher('cypher_match', $$
	MATCH ()-[r {years:3, relationship: "friends"}]-() RETURN r $$) as (r agtype);
SELECT * FROM cypher('cypher_match', $$
	MATCH ()-[r {years:3}]-() MATCH ()-[r {relationship: "friends"}]-() RETURN r $$) as (r agtype);
--mismatch year #, should return nothing
SELECT * FROM cypher('cypher_match', $$
	MATCH ()-[r {years:2}]-() MATCH ()-[r {relationship: "friends"}]-() RETURN r $$) as (r agtype);
SELECT * FROM cypher('cypher_match', $$
	MATCH ()-[r {relationship:"enemies"}]-() MATCH ()-[r {years:4}]-() RETURN r $$) as (r agtype);
SELECT * FROM cypher('cypher_match', $$
	MATCH ()-[r {relationship:"enemies"}]-() MATCH ()-[r {relationship:"friends"}]-() RETURN r $$) as (r agtype);

-- check reuse within clause - vertices
SELECT * FROM cypher('cypher_match', $$ CREATE (u {name: "Dave"})-[:knows]->({name: "John"})-[:knows]->(u) RETURN u $$) as (u agtype);
SELECT * FROM cypher('cypher_match', $$ MATCH p=(u)-[]-()-[]-(u) RETURN p $$)as (p agtype);
SELECT * FROM cypher('cypher_match', $$ MATCH p=(u)-[]->()-[]->(u) RETURN p $$)as (p agtype);
SELECT * FROM cypher('cypher_match', $$ MATCH p=(a)-[]->()-[]->(a {name: "Dave"}) RETURN p $$)as (p agtype);
SELECT * FROM cypher('cypher_match', $$ MATCH p=(a)-[]->()-[]->(a {name: "John"}) RETURN p $$)as (p agtype);
SELECT * FROM cypher('cypher_match', $$ MATCH p=(a {name: "Dave"})-[]->()-[]->(a {name: "Dave"}) RETURN p $$)as (p agtype);
SELECT * FROM cypher('cypher_match', $$ MATCH p=(a {name: "John"})-[]->()-[]->(a {name: "John"}) RETURN p $$)as (p agtype);
SELECT * FROM cypher('cypher_match', $$ MATCH p=(a {name: "Dave"})-[]->()-[]->(a) RETURN p $$)as (p agtype);
SELECT * FROM cypher('cypher_match', $$ MATCH p=(a {name: "John"})-[]->()-[]->(a) RETURN p $$)as (p agtype);

-- these are illegal and should fail
SELECT * FROM cypher('cypher_match', $$ MATCH p=(a)-[b]->()-[b:knows]->(a) RETURN p $$)as (p agtype);
SELECT * FROM cypher('cypher_match', $$ MATCH p=(a)-[b]->()-[b]->(a) RETURN p $$)as (p agtype);
SELECT * FROM cypher('cypher_match', $$ MATCH p=(a)-[b:knows]->()-[b:knows]->(a) RETURN p $$)as (p agtype);
SELECT * FROM cypher('cypher_match', $$ MATCH p=(a)-[b:knows]->()-[b]->(a) RETURN p $$)as (p agtype);
SELECT * FROM cypher('cypher_match', $$ MATCH p=(p) RETURN p $$)as (p agtype);
SELECT * FROM cypher('cypher_match', $$ MATCH p=(p)-[]->() RETURN p $$)as (p agtype);
SELECT * FROM cypher('cypher_match', $$ MATCH p=()-[p]->() RETURN p $$)as (p agtype);
SELECT * FROM cypher('cypher_match', $$ MATCH p=() MATCH (p) RETURN p $$)as (p agtype);
SELECT * FROM cypher('cypher_match', $$ MATCH p=() MATCH (p)-[]->() RETURN p $$)as (p agtype);
SELECT * FROM cypher('cypher_match', $$ MATCH p=() MATCH ()-[p]->() RETURN p $$)as (p agtype);
SELECT * FROM cypher('cypher_match', $$ MATCH (p) MATCH p=() RETURN p $$)as (p agtype);
SELECT * FROM cypher('cypher_match', $$ MATCH ()-[p]->() MATCH p=() RETURN p $$)as (p agtype);
SELECT * FROM cypher('cypher_match', $$ MATCH ()-[p *]-()-[p]-() RETURN 0 $$)as (p agtype);
SELECT * FROM cypher('cypher_match', $$ CREATE (p) WITH p MATCH p=() RETURN p $$)as (p agtype);
SELECT * FROM cypher('cypher_match', $$ CREATE p=() WITH p MATCH (p) RETURN p $$)as (p agtype);
SELECT * FROM cypher('cypher_match', $$ CREATE ()-[p:knows]->() WITH p MATCH p=() RETURN p $$)as (p agtype);
SELECT * FROM cypher('cypher_match', $$ CREATE p=() WITH p MATCH ()-[p]->() RETURN p $$)as (p agtype);



--
-- Default alias check (issue #883)
--
SELECT * FROM cypher('cypher_match', $$ MATCH (_) RETURN _ $$) as (a agtype);
SELECT * FROM cypher('cypher_match', $$ MATCH () MATCH (_{name: "Dave"}) RETURN 0 $$) as (a agtype);
SELECT * FROM cypher('cypher_match', $$ MATCH () MATCH (_{name: "Dave"}) RETURN _ $$) as (a agtype);
SELECT * FROM cypher('cypher_match', $$ MATCH (my_age_default_{name: "Dave"}) RETURN my_age_default_$$) as (a agtype);
SELECT * FROM cypher('cypher_match', $$ MATCH () MATCH (my_age_default_{name: "Dave"}) RETURN my_age_default_$$) as (a agtype);

-- these should fail as they are prefixed with _age_default_ which is only for internal use
SELECT * FROM cypher('cypher_match', $$ MATCH (_age_default_) RETURN _age_default_ $$) as (a agtype);
SELECT * FROM cypher('cypher_match', $$ MATCH (_age_default_a) RETURN _age_default_a $$) as (a agtype);
SELECT * FROM cypher('cypher_match', $$ MATCH (_age_default_whatever) RETURN 0 $$) as (a agtype);

-- issue 876
SELECT * FROM cypher('cypher_match', $$ MATCH ({name: "Dave"}) MATCH ({name: "Dave"}) MATCH ({name: "Dave"}) RETURN 0 $$) as (a agtype);
SELECT * FROM cypher('cypher_match', $$MATCH ({n0:0}) MATCH ()-[]->() MATCH ({n1:0})-[]-() RETURN 0 AS n2$$) as (a agtype);

--
-- self referencing property constraints (issue #898)
--
SELECT * FROM cypher('cypher_match', $$ MATCH (a {name:a.name}) RETURN a $$) as (a agtype);
SELECT * FROM cypher('cypher_match', $$ MATCH (a {name:a.name, age:a.age}) RETURN a $$) as (a agtype);
SELECT * FROM cypher('cypher_match', $$ MATCH (a {name:a.name}) MATCH (a {age:a.age}) RETURN a $$) as (a agtype);

SELECT * FROM cypher('cypher_match', $$ MATCH p=(a)-[u {relationship: u.relationship}]->(b) RETURN p $$) as (a agtype);
SELECT * FROM cypher('cypher_match', $$ MATCH p=(a)-[u {relationship: u.relationship, years: u.years}]->(b) RETURN p $$) as (a agtype);
SELECT * FROM cypher('cypher_match', $$ MATCH p=(a {name:a.name})-[u {relationship: u.relationship}]->(b {age:b.age}) RETURN p $$) as (a agtype);

SELECT * FROM cypher('cypher_match', $$ CREATE () WITH * MATCH (x{n0:x.n1}) RETURN 0 $$) as (a agtype);

-- these should fail due to multiple labels for a variable
SELECT * FROM cypher('cypher_match', $$ MATCH p=(x)-[]->(x:R) RETURN p, x $$) AS (p agtype, x agtype);
SELECT * FROM cypher('cypher_match', $$ MATCH p=(x:r)-[]->(x:R) RETURN p, x $$) AS (p agtype, x agtype);

--
-- Test age.enable_containment configuration parameter
--
-- Test queries are run before and after switching off this parameter.
-- When  on, the containment operator should be used to filter properties.
-- When off, the access operator should be used.
--

SELECT create_graph('test_enable_containment');
SELECT * FROM cypher('test_enable_containment',
$$
    CREATE (x:Customer {
        name: 'Bob',
        school: {
            name: 'XYZ College',
            program: {
                major: 'Psyc',
                degree: 'BSc'
            }
        },
        phone: [ 123456789, 987654321, 456987123 ],
        addr: [
            {city: 'Vancouver', street: 30},
            {city: 'Toronto', street: 40}
        ]
    })
    RETURN x
$$) as (a agtype);

-- With enable_containment on
SET age.enable_containment = on;
SELECT count(*) FROM cypher('test_enable_containment', $$ MATCH (x:Customer {addr:[{city:'Toronto'}]}) RETURN x $$) as (a agtype);
SELECT count(*) FROM cypher('test_enable_containment', $$ MATCH (x:Customer {addr:[{city:'Toronto'}, {city: 'Vancouver'}]}) RETURN x $$) as (a agtype);
SELECT count(*) FROM cypher('test_enable_containment', $$ MATCH (x:Customer {addr:[{city:'Alberta'}]}) RETURN x $$) as (a agtype);
SELECT count(*) FROM cypher('test_enable_containment', $$ MATCH (x:Customer {school:{program:{major:'Psyc'}}}) RETURN x $$) as (a agtype);
SELECT count(*) FROM cypher('test_enable_containment', $$ MATCH (x:Customer {name:'Bob',school:{program:{degree:'BSc'}}}) RETURN x $$) as (a agtype);
SELECT count(*) FROM cypher('test_enable_containment', $$ MATCH (x:Customer {school:{program:{major:'Cs'}}}) RETURN x $$) as (a agtype);
SELECT count(*) FROM cypher('test_enable_containment', $$ MATCH (x:Customer {name:'Bob',school:{program:{degree:'PHd'}}}) RETURN x $$) as (a agtype);
SELECT count(*) FROM cypher('test_enable_containment', $$ MATCH (x:Customer {phone:[987654321]}) RETURN x $$) as (a agtype);
SELECT count(*) FROM cypher('test_enable_containment', $$ MATCH (x:Customer {phone:[654765876]}) RETURN x $$) as (a agtype);
SELECT * FROM cypher('test_enable_containment', $$ EXPLAIN (COSTS OFF) MATCH (x:Customer {school:{name:'XYZ',program:{degree:'BSc'}},phone:[987654321],parents:{}}) RETURN x $$) as (a agtype);

-- Previous set of queries, with enable_containment off
SET age.enable_containment = off;
SELECT count(*) FROM cypher('test_enable_containment', $$ MATCH (x:Customer {addr:[{city:'Toronto'}]}) RETURN x $$) as (a agtype);
SELECT count(*) FROM cypher('test_enable_containment', $$ MATCH (x:Customer {addr:[{city:'Toronto'}, {city: 'Vancouver'}]}) RETURN x $$) as (a agtype);
SELECT count(*) FROM cypher('test_enable_containment', $$ MATCH (x:Customer {addr:[{city:'Alberta'}]}) RETURN x $$) as (a agtype);
SELECT count(*) FROM cypher('test_enable_containment', $$ MATCH (x:Customer {school:{program:{major:'Psyc'}}}) RETURN x $$) as (a agtype);
SELECT count(*) FROM cypher('test_enable_containment', $$ MATCH (x:Customer {name:'Bob',school:{program:{degree:'BSc'}}}) RETURN x $$) as (a agtype);
SELECT count(*) FROM cypher('test_enable_containment', $$ MATCH (x:Customer {school:{program:{major:'Cs'}}}) RETURN x $$) as (a agtype);
SELECT count(*) FROM cypher('test_enable_containment', $$ MATCH (x:Customer {name:'Bob',school:{program:{degree:'PHd'}}}) RETURN x $$) as (a agtype);
SELECT count(*) FROM cypher('test_enable_containment', $$ MATCH (x:Customer {phone:[987654321]}) RETURN x $$) as (a agtype);
SELECT count(*) FROM cypher('test_enable_containment', $$ MATCH (x:Customer {phone:[654765876]}) RETURN x $$) as (a agtype);
SELECT * FROM cypher('test_enable_containment', $$ EXPLAIN (COSTS OFF) MATCH (x:Customer {school:{name:'XYZ',program:{degree:'BSc'}},phone:[987654321],parents:{}}) RETURN x $$) as (a agtype);

--
-- Issue 945
--
SELECT create_graph('issue_945');
SELECT * FROM cypher('issue_945', $$
    CREATE (a:Part {part_num: '123'}),
           (b:Part {part_num: '345'}),
           (c:Part {part_num: '456'}),
           (d:Part {part_num: '789'})
    $$) as (result agtype);

-- should match 4
SELECT * FROM cypher('issue_945', $$
    MATCH (a:Part) RETURN a
    $$) as (result agtype);

-- each should return 4
SELECT * FROM cypher('issue_945', $$ MATCH (:Part) RETURN count(*) $$) as (result agtype);
SELECT * FROM cypher('issue_945', $$ MATCH (a:Part) RETURN count(*) $$) as (result agtype);

-- each should return 4 rows of 0
SELECT * FROM cypher('issue_945', $$ MATCH (:Part) RETURN 0 $$) as (result agtype);
SELECT * FROM cypher('issue_945', $$ MATCH (a:Part) RETURN 0 $$) as (result agtype);

-- each should return 16 rows of 0
SELECT * FROM cypher('issue_945', $$ MATCH (:Part) MATCH (:Part) RETURN 0 $$) as (result agtype);
SELECT * FROM cypher('issue_945', $$ MATCH (a:Part) MATCH (:Part) RETURN 0 $$) as (result agtype);
SELECT * FROM cypher('issue_945', $$ MATCH (:Part) MATCH (b:Part) RETURN 0 $$) as (result agtype);
SELECT * FROM cypher('issue_945', $$ MATCH (a:Part) MATCH (b:Part) RETURN 0 $$) as (result agtype);

-- each should return a count of 16
SELECT * FROM cypher('issue_945', $$ MATCH (:Part) MATCH (:Part) RETURN count(*) $$) as (result agtype);
SELECT * FROM cypher('issue_945', $$ MATCH (a:Part) MATCH (:Part) RETURN count(*) $$) as (result agtype);
SELECT * FROM cypher('issue_945', $$ MATCH (:Part) MATCH (b:Part) RETURN count(*) $$) as (result agtype);
SELECT * FROM cypher('issue_945', $$ MATCH (a:Part) MATCH (b:Part) RETURN count(*) $$) as (result agtype);


--
-- Issue 1045
--
SELECT * FROM cypher('cypher_match', $$ MATCH p=()-[*]->() RETURN length(p) $$) as (length agtype);
SELECT * FROM cypher('cypher_match', $$ MATCH p=()-[*]->() WHERE length(p) > 1 RETURN length(p) $$) as (length agtype);
SELECT * FROM cypher('cypher_match', $$ MATCH p=()-[*]->() WHERE size(nodes(p)) = 3 RETURN nodes(p)[0] $$) as (nodes agtype);
SELECT * FROM cypher('cypher_match', $$ MATCH (n {name:'Dave'}) MATCH p=()-[*]->() WHERE nodes(p)[0] = n RETURN length(p) $$) as (length agtype);
SELECT * FROM cypher('cypher_match', $$ MATCH p1=(n {name:'Dave'})-[]->() MATCH p2=()-[*]->() WHERE p2=p1 RETURN p2=p1 $$) as (path agtype);

--
-- Clean up
--
SELECT drop_graph('cypher_match', true);
SELECT drop_graph('test_retrieve_var', true);
SELECT drop_graph('test_enable_containment', true);
SELECT drop_graph('issue_945', true);

--
-- End
--
