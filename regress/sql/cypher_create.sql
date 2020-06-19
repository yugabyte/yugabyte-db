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

SELECT create_graph('cypher_create');

SELECT * FROM cypher('cypher_create', $$CREATE ()$$) AS (a agtype);

-- vertex graphid
SELECT * FROM cypher('cypher_create', $$CREATE (:v)$$) AS (a agtype);

SELECT * FROM cypher('cypher_create', $$CREATE (:v {})$$) AS (a agtype);

SELECT * FROM cypher('cypher_create', $$CREATE (:v {key: 'value'})$$) AS (a agtype);

SELECT * FROM cypher('cypher_create', $$MATCH (n:v) RETURN n$$) AS (n agtype);

-- Left relationship
SELECT * FROM cypher('cypher_create', $$
    CREATE (:v {id:"right rel, initial node"})-[:e {id:"right rel"}]->(:v {id:"right rel, end node"})
$$) AS (a agtype);

-- Right relationship
SELECT * FROM cypher('cypher_create', $$
    CREATE (:v {id:"left rel, initial node"})<-[:e {id:"left rel"}]-(:v {id:"left rel, end node"})
$$) AS (a agtype);

-- Pattern creates a path from the initial node to the last node
SELECT * FROM cypher('cypher_create', $$
    CREATE (:v {id: "path, initial node"})-[:e {id: "path, edge one"}]->(:v {id:"path, middle node"})-[:e {id:"path, edge two"}]->(:v {id:"path, last node"})
$$) AS (a agtype);

-- middle vertex points to the initial and last vertex
SELECT * FROM cypher('cypher_create', $$
    CREATE (:v {id: "divergent, initial node"})<-[:e {id: "divergent, edge one"}]-(:v {id: "divergent middle node"})-[:e {id: "divergent, edge two"}]->(:v {id: "divergent, end node"})
$$) AS (a agtype);

-- initial and last vertex point to the middle vertex
SELECT * FROM cypher('cypher_create', $$
    CREATE (:v {id: "convergent, initial node"})-[:e {id: "convergent, edge one"}]->(:v {id: "convergent middle node"})<-[:e {id: "convergent, edge two"}]-(:v {id: "convergent, end node"})
$$) AS (a agtype);

-- Validate Paths work correctly
SELECT * FROM cypher('cypher_create', $$
    CREATE (:v {id: "paths, vertex one"})-[:e {id: "paths, edge one"}]->(:v {id: "paths, vertex two"}),
           (:v {id: "paths, vertex three"})-[:e {id: "paths, edge two"}]->(:v {id: "paths, vertex four"})
$$) AS (a agtype);

--edge with double relationship will throw an error
SELECT * FROM cypher('cypher_create', $$CREATE (:v)<-[:e]->()$$) AS (a agtype);

--edge with no relationship will throw an error
SELECT * FROM cypher('cypher_create', $$CREATE (:v)-[:e]-()$$) AS (a agtype);

--edges without labels are not supported
SELECT * FROM cypher('cypher_create', $$CREATE (:v)-[]->(:v)$$) AS (a agtype);

SELECT * FROM cypher_create.e;

SELECT * FROM cypher_create.v;

SELECT * FROM cypher('cypher_create', $$
	CREATE (:n_var {name: 'Node A'})
$$) as (a agtype);

SELECT * FROM cypher('cypher_create', $$
	CREATE (:n_var {name: 'Node B'})
$$) as (a agtype);

SELECT * FROM cypher('cypher_create', $$
	CREATE (:n_var {name: 'Node C'})
$$) as (a agtype);

SELECT * FROM cypher('cypher_create', $$
	MATCH (a:n_var), (b:n_var)
	WHERE a.name <> b.name
	CREATE (a)-[:e_var {name: a.name + ' -> ' + b.name}]->(b)
$$) as (a agtype);

SELECT * FROM cypher('cypher_create', $$
	MATCH (a:n_var)
	CREATE (a)-[:e_var {name: a.name + ' -> ' + a.name}]->(a)
$$) as (a agtype);

SELECT * FROM cypher('cypher_create', $$
	MATCH (a:n_var)
	CREATE (a)-[:e_var {name: a.name + ' -> new node'}]->(:n_other_node)
$$) as (a agtype);

SELECT * FROM cypher('cypher_create', $$
	MATCH (a:n_var)
	WHERE a.name = 'Node Z'
	CREATE (a)-[:e_var {name: a.name + ' -> doesn''t exist'}]->(:n_other_node)
$$) as (a agtype);

SELECT * FROM cypher_create.n_var;
SELECT * FROM cypher_create.e_var;

--Check every label has been created
SELECT * FROM ag_label;

--Validate every vertex has the correct label
SELECT * FROM cypher('cypher_create', $$MATCH (n) RETURN n$$) AS (n agtype);

--
-- Errors
--
-- Var 'a' cannot have properties in the create clause
SELECT * FROM cypher('cypher_create', $$
	MATCH (a:n_var)
	WHERE a.name = 'Node A'
	CREATE (a {test:1})-[:e_var]->()
$$) as (a agtype);

-- Var 'a' cannot change labels
SELECT * FROM cypher('cypher_create', $$
	MATCH (a:n_var)
	WHERE a.name = 'Node A'
	CREATE (a:new_label)-[:e_var]->()
$$) as (a agtype);

SELECT * FROM cypher('cypher_create', $$
	MATCH (a:n_var)
	WHERE a.name = 'Node A'
	CREATE (a)-[b:e_var]->()
$$) as (a agtype);


-- column definition list for CREATE clause must contain a single agtype
-- attribute
SELECT * FROM cypher('cypher_create', $$CREATE ()$$) AS (a int);
SELECT * FROM cypher('cypher_create', $$CREATE ()$$) AS (a agtype, b int);

-- intial and last vertex point to the middle vertex
SELECT drop_graph('cypher_create', true);
