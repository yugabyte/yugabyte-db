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

SELECT create_graph('cypher_merge');


/*
 * Section 1: MERGE with single vertex
 */
/*
 * test 1: Single MERGE Clause, path doesn't exist
 */
--test query
SELECT * FROM cypher('cypher_merge', $$MERGE (n {i: "Hello Merge", j: (null IS NULL), k: (null IS NOT NULL)})$$) AS (a agtype);

--validate
SELECT * FROM cypher('cypher_merge', $$MATCH (n) RETURN n$$) AS (n agtype);

--clean up
SELECT * FROM cypher('cypher_merge', $$MATCH (n) DETACH DELETE n $$) AS (a agtype);

/*
 * test 2: Single MERGE Clause, path exists
 */
--data setup
SELECT * FROM cypher('cypher_merge', $$CREATE ({i: "Hello Merge", j: (null IS NULL)}) $$) AS (a agtype);

--test_query
SELECT * FROM cypher('cypher_merge', $$MERGE ({i: "Hello Merge"})$$) AS (a agtype);
SELECT * FROM cypher('cypher_merge', $$MERGE ({j: (null IS NULL)})$$) AS (a agtype);

--validate
SELECT * FROM cypher('cypher_merge', $$MATCH (n) RETURN n$$) AS (n agtype);

--clean up
SELECT * FROM cypher('cypher_merge', $$MATCH (n) DETACH DELETE n $$) AS (a agtype);

/*
 * test 3: Prev clause returns no results, no data created
 */
--test query
SELECT * FROM cypher('cypher_merge', $$MATCH (n) MERGE ({i: n.i})$$) AS (a agtype);

--validate
SELECT * FROM cypher('cypher_merge', $$MATCH (n) RETURN n$$) AS (n agtype);

--clean up
SELECT * FROM cypher('cypher_merge', $$MATCH (n) DETACH DELETE n $$) AS (a agtype);

/*
 * test 4: Prev clause has results, path exists
 */
--test query
SELECT * FROM cypher('cypher_merge', $$CREATE ({i: "Hello Merge"}) $$) AS (a agtype);
SELECT * FROM cypher('cypher_merge', $$MATCH (n) MERGE ({i: n.i})$$) AS (a agtype);

--validate
SELECT * FROM cypher('cypher_merge', $$MATCH (n) RETURN n$$) AS (n agtype);

--clean up
SELECT * FROM cypher('cypher_merge', $$MATCH (n) DETACH DELETE n $$) AS (a agtype);

/*
 * test 5: Prev clause has results, path does not exist (different property name)
 */
--data setup
SELECT * FROM cypher('cypher_merge', $$CREATE ({i: "Hello Merge"}) $$) AS (a agtype);

--test query
SELECT * FROM cypher('cypher_merge', $$MATCH (n) MERGE ({j: n.i})$$) AS (a agtype);

--validate
SELECT * FROM cypher('cypher_merge', $$MATCH (n) RETURN n$$) AS (n agtype);

--clean up
SELECT * FROM cypher('cypher_merge', $$MATCH (n) DETACH DELETE n $$) AS (a agtype);

/*
 * test 6: MERGE with no prev clause, filters correctly, data created
 */
-- setup
SELECT * FROM cypher('cypher_merge', $$CREATE ({i: 2}) $$) AS (a agtype);

--test query
SELECT * FROM cypher('cypher_merge', $$MERGE (n {i: 1}) RETURN n$$) AS (a agtype);

--validate
SELECT * FROM cypher('cypher_merge', $$MATCH (n) RETURN n$$) AS (n agtype);

--clean up
SELECT * FROM cypher('cypher_merge', $$MATCH (n) DETACH DELETE n $$) AS (a agtype);

/*
 * test 7: MERGE with no prev clause, filters correctly, no data created
 */
-- setup
SELECT * FROM cypher('cypher_merge', $$CREATE ({i: 1}) $$) AS (a agtype);
SELECT * FROM cypher('cypher_merge', $$CREATE ({i: 1}) $$) AS (a agtype);
SELECT * FROM cypher('cypher_merge', $$CREATE ({i: 2}) $$) AS (a agtype);
SELECT * FROM cypher('cypher_merge', $$CREATE () $$) AS (a agtype);

--test query
SELECT * FROM cypher('cypher_merge', $$MERGE (n {i: 1}) RETURN n$$) AS (a agtype);

--validate
SELECT * FROM cypher('cypher_merge', $$MATCH (n) RETURN n$$) AS (n agtype);

--clean up
SELECT * FROM cypher('cypher_merge', $$MATCH (n) DETACH DELETE n $$) AS (a agtype);


/*
 * Section 2: MERGE with edges
 */

/*
 * test 8: MERGE creates edge
 */
-- setup
SELECT * FROM cypher('cypher_merge', $$CREATE () $$) AS (a agtype);

--test query
SELECT * FROM cypher('cypher_merge', $$MATCH (n) MERGE (n)-[:e]->(:v)$$) AS (a agtype);

--validate
SELECT * FROM cypher('cypher_merge', $$MATCH (n)-[e:e]->(m:v) RETURN n, e, m$$) AS (n agtype, e agtype, m agtype);

--clean up
SELECT * FROM cypher('cypher_merge', $$MATCH (n) DETACH DELETE n $$) AS (a agtype);


/*
 * test 9: edge already exists
 */
-- setup
SELECT * FROM cypher('cypher_merge', $$CREATE ()-[:e]->() $$) AS (a agtype);

--test query
SELECT * FROM cypher('cypher_merge', $$MERGE (n)-[:e]->(:v)$$) AS (a agtype);

--validate
SELECT * FROM cypher('cypher_merge', $$MATCH (n)-[e:e]->(m:v) RETURN n, e, m$$) AS (n agtype, e agtype, m agtype);

--clean up
SELECT * FROM cypher('cypher_merge', $$MATCH (n) DETACH DELETE n $$) AS (a agtype);

/*
 * test 10: edge doesn't exist, using MATCH
 */
-- setup
SELECT * FROM cypher('cypher_merge', $$CREATE () $$) AS (a agtype);

--test query
SELECT * FROM cypher('cypher_merge', $$MATCH (n) MERGE (n)-[:e]->(:v)$$) AS (a agtype);

--validate created correctly
SELECT * FROM cypher('cypher_merge', $$MATCH (n)-[e:e]->(m:v) RETURN n, e, m$$) AS (n agtype, e agtype, m agtype);

--clean up
SELECT * FROM cypher('cypher_merge', $$MATCH (n) DETACH DELETE n $$) AS (a agtype);

/*
 * test 11: edge already exists, using MATCH
 */
-- setup
SELECT * FROM cypher('cypher_merge', $$CREATE ()-[:e]->() $$) AS (a agtype);

--test query
SELECT * FROM cypher('cypher_merge', $$MATCH (n) MERGE (n)-[:e]->(:v)$$) AS (a agtype);

--validate created correctly
SELECT * FROM cypher('cypher_merge', $$MATCH (n)-[e:e]->(m:v) RETURN n, e, m$$) AS (n agtype, e agtype, m agtype);

--clean up
SELECT * FROM cypher('cypher_merge', $$MATCH (n) DETACH DELETE n $$) AS (a agtype);

/*
 * test 12: Partial Path Exists, creates whole path
 */
-- setup
SELECT * FROM cypher('cypher_merge', $$CREATE ()-[:e]->() $$) AS (a agtype);

--test query
SELECT * FROM cypher('cypher_merge', $$MERGE ()-[:e]->()-[:e]->()$$) AS (a agtype);

--validate created correctly
--Returns 3. One for the data setup and 2 for the longer path in MERGE
SELECT count(*) FROM cypher('cypher_merge', $$MATCH p=()-[e:e]->() RETURN p$$) AS (p agtype);

-- Returns 1, the path created in MERGE
SELECT count(*) FROM cypher('cypher_merge', $$MATCH p=()-[:e]->()-[]->() RETURN p$$) AS (p agtype);

-- 5 vertices total should have been created
SELECT count(*) FROM cypher('cypher_merge', $$MATCH (n) RETURN n$$) AS (n agtype);

--clean up
SELECT * FROM cypher('cypher_merge', $$MATCH (n) DETACH DELETE n $$) AS (a agtype);

/*
 * test 13: edge doesn't exists (different label), using MATCH
 */
-- setup
SELECT * FROM cypher('cypher_merge', $$CREATE ()-[:e]->() $$) AS (a agtype);

--test query
SELECT * FROM cypher('cypher_merge', $$MATCH (n) MERGE (n)-[:e_new]->(:v)$$) AS (a agtype);

--validate created correctly
SELECT * FROM cypher('cypher_merge', $$MATCH (n)-[e]->(m:v) RETURN n, e, m$$) AS (n agtype, e agtype, m agtype);

--clean up
SELECT * FROM cypher('cypher_merge', $$MATCH (n) DETACH DELETE n $$) AS (a agtype);

/*
 * test 14: edge doesn't exists (different label), without MATCH
 */
-- setup
SELECT * FROM cypher('cypher_merge', $$CREATE ()-[:e]->() $$) AS (a agtype);

--test query
SELECT * FROM cypher('cypher_merge', $$MERGE (n)-[:e_new]->(:v)$$) AS (a agtype);

--validate created correctly
SELECT * FROM cypher('cypher_merge', $$MATCH (n)-[e]->(m:v) RETURN n, e, m$$) AS (n agtype, e agtype, m agtype);

--clean up
SELECT * FROM cypher('cypher_merge', $$MATCH (n) DETACH DELETE n $$) AS (a agtype);

/*
 * Section 3: MERGE with writing clauses
 */

/*
 * test 15:
 */

--test query
SELECT * FROM cypher('cypher_merge', $$CREATE () MERGE (n)$$) AS (a agtype);

--validate created correctly
SELECT * FROM cypher('cypher_merge', $$MATCH (n) RETURN n$$) AS (n agtype);

--clean up
SELECT * FROM cypher('cypher_merge', $$MATCH (n) DETACH DELETE n $$) AS (a agtype);

/*
 * test 16:
 */

--test query
SELECT * FROM cypher('cypher_merge', $$CREATE (n) WITH n as a MERGE (a)-[:e]->() $$) AS (a agtype);

--validate created correctly
SELECT * FROM cypher('cypher_merge', $$MATCH p=()-[:e]->() RETURN p$$) AS (p agtype);

--clean up
SELECT * FROM cypher('cypher_merge', $$MATCH (n) DETACH DELETE n $$) AS (a agtype);


/*
 * test 17:
 */

--test query
SELECT * FROM cypher('cypher_merge', $$CREATE (n) MERGE (n)-[:e]->() $$) AS (a agtype);

--validate created correctly
SELECT * FROM cypher('cypher_merge', $$MATCH p=()-[:e]->() RETURN p$$) AS (p agtype);

--clean up
SELECT * FROM cypher('cypher_merge', $$MATCH (n) DETACH DELETE n $$) AS (a agtype);


/*
 * test 18:
 */

--test query
SELECT * FROM cypher('cypher_merge', $$CREATE (n {i : 1}) SET n.i = 2 MERGE ({i: 2}) $$) AS (a agtype);

--validate created correctly
SELECT * FROM cypher('cypher_merge', $$MATCH (a) RETURN a$$) AS (a agtype);

--clean up
SELECT * FROM cypher('cypher_merge', $$MATCH (n) DETACH DELETE n $$) AS (a agtype);

/*
 * test 19:
 */
--test query
SELECT * FROM cypher('cypher_merge', $$CREATE (n {i : 1}) SET n.i = 2 WITH n as a MERGE ({i: 2}) $$) AS (a agtype);

--validate created correctly
SELECT * FROM cypher('cypher_merge', $$MATCH (a) RETURN a$$) AS (a agtype);

--clean up
SELECT * FROM cypher('cypher_merge', $$MATCH (n) DETACH DELETE n $$) AS (a agtype);

/*
 * test 20:
 */
--data setup
SELECT * FROM cypher('cypher_merge', $$CREATE (n {i : 1})$$) AS (a agtype);


--test query
SELECT * FROM cypher('cypher_merge', $$MATCH (n {i : 1}) SET n.i = 2 WITH n as a MERGE ({i: 2}) $$) AS (a agtype);

--validate created correctly
SELECT * FROM cypher('cypher_merge', $$MATCH (a) RETURN a$$) AS (a agtype);

--clean up
SELECT * FROM cypher('cypher_merge', $$MATCH (n) DETACH DELETE n $$) AS (a agtype);

/*
 * test 21:
 */
--data setup
SELECT * FROM cypher('cypher_merge', $$CREATE (n {i : 1})$$) AS (a agtype);


--test query
SELECT * FROM cypher('cypher_merge', $$MATCH (n {i : 1}) DELETE n  MERGE (n)-[:e]->() $$) AS (a agtype);

--validate, transaction was rolled back because of the error message
SELECT * FROM cypher('cypher_merge', $$MATCH (a) RETURN a$$) AS (a agtype);

--clean up
SELECT * FROM cypher('cypher_merge', $$MATCH (n) DETACH DELETE n $$) AS (a agtype);

/*
 * test 22:
 * MERGE after MERGE
 */
SELECT * FROM cypher('cypher_merge', $$
    CREATE (n:Person {name : "Rob Reiner", bornIn: "New York"})
$$) AS (a agtype);

SELECT * FROM cypher('cypher_merge', $$
    CREATE (n:Person {name : "Michael Douglas", bornIn: "New Jersey"})
$$) AS (a agtype);

SELECT * FROM cypher('cypher_merge', $$
    CREATE (n:Person {name : "Martin Sheen", bornIn: "Ohio"})
$$) AS (a agtype);

--test query
SELECT * FROM cypher('cypher_merge', $$
    MATCH (person:Person)
    MERGE (city:City {name: person.bornIn})
    MERGE (person)-[r:BORN_IN]->(city)
    RETURN person.name, person.bornIn, city
$$) AS (name agtype, bornIn agtype, city agtype);

--validate
SELECT * FROM cypher('cypher_merge', $$MATCH (a) RETURN a$$) AS (a agtype);

--clean up
SELECT * FROM cypher('cypher_merge', $$MATCH (n) DETACH DELETE n $$) AS (a agtype);

/*
 * test 23:
 */
SELECT * FROM cypher('cypher_merge', $$MERGE ()-[:e]-()$$) AS (a agtype);

--validate
SELECT * FROM cypher('cypher_merge', $$MATCH p=()-[]->() RETURN p$$) AS (a agtype);

--clean up
SELECT * FROM cypher('cypher_merge', $$MATCH (n) DETACH DELETE n $$) AS (a agtype);

/*
 * test 24:
 */
SELECT * FROM cypher('cypher_merge', $$MERGE (a) RETURN a$$) AS (a agtype);

--validate
SELECT * FROM cypher('cypher_merge', $$MATCH (a) RETURN a$$) AS (a agtype);

--clean up
SELECT * FROM cypher('cypher_merge', $$MATCH (n) DETACH DELETE n $$) AS (a agtype);

/*
 * test 25:
 */
SELECT * FROM cypher('cypher_merge', $$MERGE p=()-[:e]-() RETURN p$$) AS (a agtype);

--validate
SELECT * FROM cypher('cypher_merge', $$MATCH p=()-[]->() RETURN p$$) AS (a agtype);

--clean up
SELECT * FROM cypher('cypher_merge', $$MATCH (n) DETACH DELETE n $$) AS (a agtype);

/*
 * test 26:
 */
SELECT * FROM cypher('cypher_merge', $$MERGE (a)-[:e]-(b) RETURN a$$) AS (a agtype);

--validate
SELECT * FROM cypher('cypher_merge', $$MATCH p=()-[]->() RETURN p$$) AS (a agtype);

--clean up
SELECT * FROM cypher('cypher_merge', $$MATCH (n) DETACH DELETE n $$) AS (a agtype);

/*
 * test 27:
 */
SELECT  * FROM cypher('cypher_merge', $$CREATE p=()-[:e]->() RETURN p$$) AS (a agtype);

SELECT * FROM cypher('cypher_merge', $$MERGE p=()-[:e]-() RETURN p$$) AS (a agtype);


--clean up
SELECT * FROM cypher('cypher_merge', $$MATCH (n) DETACH DELETE n $$) AS (a agtype);

/*
 * Section 4: Error Messages
 */
/*
 * test 28:
 * Only single paths allowed
 */
SELECT * FROM cypher('cypher_merge', $$MERGE (n), (m) RETURN n, m$$) AS (a agtype, b agtype);

/*
 * test 29:
 * Edges cannot reference existing variables
 */
SELECT * FROM cypher('cypher_merge', $$MATCH ()-[e]-() MERGE ()-[e]->()$$) AS (a agtype);

/*
 * test 30:
 * NULL vertex given to MERGE
 */
--data setup
SELECT * FROM cypher('cypher_merge', $$CREATE (n)$$) AS (a agtype);

--test query
SELECT * FROM cypher('cypher_merge', $$MATCH (n) OPTIONAL MATCH (n)-[:e]->(m) MERGE (m)$$) AS (a agtype);

-- validate only 1 vertex exits
SELECT * FROM cypher('cypher_merge', $$MATCH (n) RETURN n$$) AS (a agtype);

--
-- MERGE/SET test
-- Node does exist, then set (github issue #235)
SELECT * FROM cypher('cypher_merge', $$ CREATE (n:node {name: 'Jason'}) RETURN n $$) AS (n agtype);
SELECT * FROM cypher('cypher_merge', $$ MATCH (n:node) RETURN n $$) AS (n agtype);
SELECT * FROM cypher('cypher_merge', $$ MERGE (n:node {name: 'Jason'}) SET n.name = 'Lisa' RETURN n $$) AS (n agtype);
SELECT * FROM cypher('cypher_merge', $$ MATCH (n:node) RETURN n $$) AS (n agtype);

-- Node doesn't exist, is created, then set
SELECT * FROM cypher('cypher_merge', $$ MATCH (n:node) DELETE n $$) AS (n agtype);
SELECT * FROM cypher('cypher_merge', $$ MATCH (n:node) RETURN n $$) AS (n agtype);
SELECT * FROM cypher('cypher_merge', $$ MERGE (n:node {name: 'Jason'}) SET n.name = 'Lisa' RETURN n $$) AS (n agtype);
SELECT * FROM cypher('cypher_merge', $$ MATCH (n:node) RETURN n $$) AS (n agtype);
-- Multiple SETs
SELECT * FROM cypher('cypher_merge', $$ MERGE (n:node {name: 'Lisa'}) SET n.age = 23, n.gender = "Female" RETURN n $$) AS (n agtype);
SELECT * FROM cypher('cypher_merge', $$ MATCH (n:node) RETURN n $$) AS (n agtype);
SELECT * FROM cypher('cypher_merge', $$ MERGE (n:node {name: 'Jason'}) SET n.name = 'Lisa', n.age = 23, n.gender = 'Female' RETURN n $$) AS (n agtype);
SELECT * FROM cypher('cypher_merge', $$ MATCH (n:node) RETURN n $$) AS (n agtype);

--
-- Complex MERGE w/wo RETURN values
--
-- The first one should create a path, the others should just return parts of it.
SELECT * FROM cypher('cypher_merge', $$ MERGE ()-[:B]->(x:C)-[:E]->(x:C)<-[f:F]-(y:I) $$) AS (x agtype);
SELECT * FROM cypher('cypher_merge', $$ MERGE ()-[:B]->(x:C)-[:E]->(x:C)<-[f:F]-(y:I) RETURN x $$) AS (x agtype);
SELECT * FROM cypher('cypher_merge', $$ MERGE p=()-[:B]->(x:C)-[:E]->(x:C)<-[f:F]-(y:I) $$) AS (p agtype);
SELECT * FROM cypher('cypher_merge', $$ MERGE p=()-[:B]->(x:C)-[:E]->(x:C)<-[f:F]-(y:I) RETURN p $$) AS (p agtype);
SELECT * FROM cypher('cypher_merge', $$ MERGE p=()-[:B]->(x:C)-[:E]->(x:C)<-[f:F]-(y:I) RETURN p $$) AS (p agtype);

-- This should only return 1 row, as the path should already exist.
SELECT * FROM cypher('cypher_merge', $$ MATCH p=()-[:B]->(:C)-[:E]->(:C)<-[:F]-(:I) RETURN p $$) AS (p agtype);

-- test variable reuse in MERGE - the first MERGE of each group should create,
-- the second MERGE shouldn't.
SELECT * FROM cypher('cypher_merge', $$ MATCH p=(x:P)-[:E]->(x:P) RETURN p, x $$) AS (p agtype, x agtype);
SELECT * FROM cypher('cypher_merge', $$ MERGE (x:P)-[:E]->(x:P) $$) AS (x agtype);
SELECT * FROM cypher('cypher_merge', $$ MERGE (x:P)-[:E]->(x) $$) AS (x agtype);
SELECT * FROM cypher('cypher_merge', $$ MATCH p=(x:P)-[:E]->(x) RETURN p, x $$) AS (p agtype, x agtype);
SELECT * FROM cypher('cypher_merge', $$ MATCH p=(x:Q)-[:E]->(x:Q) RETURN p, x $$) AS (p agtype, x agtype);
SELECT * FROM cypher('cypher_merge', $$ MERGE (x:Q)-[:E]->(x) $$) AS (x agtype);
SELECT * FROM cypher('cypher_merge', $$ MERGE (x:Q)-[:E]->(x:Q) $$) AS (x agtype);
SELECT * FROM cypher('cypher_merge', $$ MATCH p=(x:Q)-[:E]->(x) RETURN p, x $$) AS (p agtype, x agtype);
SELECT * FROM cypher('cypher_merge', $$ MATCH p=(x:R)-[:E]->(x) RETURN p, x $$) AS (p agtype, x agtype);
SELECT * FROM cypher('cypher_merge', $$ MERGE p=(x:R)-[:E]->(x) RETURN p, x $$) AS (p agtype, x agtype);
SELECT * FROM cypher('cypher_merge', $$ MERGE p=(x:R)-[:E]->(x) RETURN p, x $$) AS (p agtype, x agtype);
SELECT * FROM cypher('cypher_merge', $$ MATCH p=(x:R)-[:E]->(x) RETURN p, x $$) AS (p agtype, x agtype);
-- should return 4 rows
SELECT * FROM cypher('cypher_merge', $$ MERGE p=(x)-[:E]->(x) RETURN p, x $$) AS (p agtype, x agtype);
-- should create 1 row
SELECT * FROM cypher('cypher_merge', $$ MERGE p=(x)-[:E1]->(x) RETURN p, x $$) AS (p agtype, x agtype);
SELECT * FROM cypher('cypher_merge', $$ MATCH p=(x)-[:E1]->(x) RETURN p, x $$) AS (p agtype, x agtype);
-- the following should fail due to multiple labels
SELECT * FROM cypher('cypher_merge', $$ MERGE p=(x)-[:E]->(x:R) RETURN p, x $$) AS (p agtype, x agtype);
SELECT * FROM cypher('cypher_merge', $$ MERGE p=(x:r)-[:E]->(x:R) RETURN p, x $$) AS (p agtype, x agtype);
SELECT * FROM cypher('cypher_merge', $$ MERGE (x)-[:E]->(x:R) $$) AS (x agtype);
SELECT * FROM cypher('cypher_merge', $$ MERGE (x:r)-[:E]->(x:R) $$) AS (x agtype);
-- the following should fail due to reuse issues
SELECT * FROM cypher('cypher_merge', $$ MERGE (x:r)-[y:E]->(x)-[y]->(x) $$) AS (x agtype);
SELECT * FROM cypher('cypher_merge', $$ MERGE (x:r)-[y:E]->(x)-[x]->(y) $$) AS (x agtype);
SELECT * FROM cypher('cypher_merge', $$ MERGE (x:r)-[y:E]->(x)-[z:E]->(y) $$) AS (x agtype);
SELECT * FROM cypher('cypher_merge', $$ MERGE p=(x:r)-[y:E]->(x)-[p]->(x) $$) AS (x agtype);
SELECT * FROM cypher('cypher_merge', $$ MERGE p=(x:r)-[y:E]->(x)-[p:E]->(x) $$) AS (x agtype);
SELECT * FROM cypher('cypher_merge', $$ MERGE p=(x:r)-[y:E]->(p)-[x]->(y) $$) AS (x agtype);

-- issue 1219
SELECT * FROM create_graph('issue_1219');
SELECT * FROM cypher('issue_1219', $$ CREATE (x:Label1{arr:[1,2,3,4]}) RETURN x $$) as (a agtype);
SELECT * FROM cypher('issue_1219', $$
    MATCH (x:Label1{arr:[1,2,3,4]}) MERGE (y:Label2{key1:2, key2:x.arr, key3:3}) RETURN y
$$) as (result agtype);
SELECT * FROM cypher('issue_1219', $$
    MATCH (x:Label1{arr:[1,2,3,4]}) MERGE (y:Label2{key2:id(x)}) RETURN y
$$) as (result agtype);
SELECT * FROM cypher('issue_1219', $$ MATCH (x) RETURN x $$) as (a agtype);
-- these shouldn't create more
SELECT * FROM cypher('issue_1219', $$
    MATCH (x:Label1{arr:[1,2,3,4]}) MERGE (y:Label2{key1:2, key2:x.arr, key3:3}) RETURN y
$$) as (result agtype);
SELECT * FROM cypher('issue_1219', $$
    MATCH (x:Label1{arr:[1,2,3,4]}) MERGE (y:Label2{key2:id(x)}) RETURN y
$$) as (result agtype);
SELECT * FROM cypher('issue_1219', $$ MATCH (x) RETURN x $$) as (a agtype);
-- create a path
SELECT * FROM cypher('issue_1219', $$
    CREATE (u:Label1{name: "John"})-[e:knows]->(v:Label1{name: "Jane"})
$$) as (result agtype);
SELECT * FROM cypher('issue_1219', $$
    MATCH (u:Label1{name:"John"})-[e:knows]->(v:Label1{name: "Jane"})
    MERGE (y:Label2{start_id:id(u), edge_id:id(e), end_id:id(v)}) RETURN y
$$) as (result agtype);
SELECT * FROM cypher('issue_1219', $$ MATCH (x) RETURN x $$) as (result agtype);
SELECT * FROM cypher('issue_1219', $$ MATCH ()-[e]->() RETURN e $$) as (result agtype);
SELECT drop_graph('issue_1219', true);

--
-- the following tests should fail due to invalid variable reuse (issue#1513)
--
SELECT * FROM cypher('cypher_merge', $$
  MERGE (n) MERGE (n) RETURN n
$$) as (a agtype);

SELECT * FROM cypher('cypher_merge', $$
  MATCH (n) MERGE (n) RETURN n
$$) as (a agtype);

SELECT * FROM cypher('cypher_merge', $$
  CREATE (n) MERGE (n) RETURN n
$$) as (a agtype);

SELECT * FROM cypher('cypher_merge', $$
  MATCH (n) WITH n AS r MERGE (r) RETURN r
$$) as (a agtype);

SELECT * FROM cypher('cypher_merge', $$
  WITH {id: 281474976710657, label: "", properties: {}}::vertex AS n MERGE (n) RETURN n
$$) as (a agtype);

SELECT * FROM cypher('cypher_merge', $$
	MERGE (n)-[e:edge]->(n)-[e1:edge]->(n) MERGE(n) RETURN e
$$) as (a agtype);

SELECT * FROM cypher('cypher_merge', $$
  MERGE (n)-[e:edge]->(m) MERGE (e)-[:edge]->() RETURN e
$$) as (a agtype);

SELECT * FROM cypher('cypher_merge', $$
	WITH {id: 1407374883553281, label: "edge", end_id: 281474976710658, start_id: 281474976710657, properties: {}}::edge AS e MERGE ()-[e:edge]->() RETURN e
$$) as (a agtype);

SELECT * FROM cypher('cypher_merge', $$
  MATCH (n) WITH n AS r MERGE (r) RETURN r
$$) as (a agtype);

-- valid variable reuse
SELECT * FROM cypher('cypher_merge', $$
  MERGE (n)-[e1:edge]->(m) MERGE (n)-[e2:edge]->()
$$) as (n agtype);

SELECT * FROM cypher('cypher_merge', $$
  MERGE (n) WITH n AS r MERGE (r)-[e:edge]->()
$$) as (a agtype);

SELECT * FROM cypher('cypher_merge', $$
  CREATE (n), (m) WITH n AS r MERGE (m)
$$) as (a agtype);

--
-- Issue 1630 - MERGE using array not working in some cases
--
SELECT * FROM create_graph('issue_1630');
SELECT * FROM cypher('issue_1630', $$ MATCH (u) RETURN (u) $$) AS (u agtype);
-- will it create it?
SELECT * FROM cypher('issue_1630',
                     $$ WITH ['jon', 'snow'] AS cols
                        MERGE (v:PERSION {first: cols[0], last: cols[1]})
                        RETURN v $$) AS (v agtype);
-- will it retrieve it, if it exists?
SELECT * FROM cypher('issue_1630',
                     $$ WITH ['jon', 'snow'] AS cols
                        MERGE (v:PERSION {first: cols[0], last: cols[1]})
                        RETURN v $$) AS (v agtype);
SELECT * FROM cypher('issue_1630', $$ MATCH (u) RETURN (u) $$) AS (u agtype);

-- a whole array
SELECT * FROM cypher('issue_1630',
                     $$ WITH ['jon', 'snow'] AS cols
                        MERGE (v:PERSION {namea: cols})
                        RETURN v $$) AS (v agtype);

-- a whole object
SELECT * FROM cypher('issue_1630',
                     $$ WITH {first: 'jon', last: 'snow'} AS cols
                        MERGE (v:PERSION {nameo: cols})
                        RETURN v $$) AS (v agtype);

-- delete them to start over
SELECT * FROM cypher('issue_1630', $$ MATCH (u) DELETE(u) $$) AS (u agtype);
SELECT * FROM cypher('issue_1630',
                     $$ WITH {first: 'jon', last: 'snow'} AS cols
                        MERGE (v:PERSION {first: cols.first, last: cols.last})
                        RETURN v $$) AS (v agtype);

-- delete them to start over
-- check pushing through a few clauses
SELECT * FROM cypher('issue_1630', $$ MATCH (u) DELETE(u) $$) AS (u agtype);
SELECT * FROM cypher('issue_1630',
                     $$ WITH {first: 'jon', last: 'snow'} AS jonsnow
                        WITH jonsnow AS name
                        WITH name AS cols
                        MERGE (v:PERSION {first: cols.first, last: cols.last})
                        RETURN v $$) AS (v agtype);

-- will it retrieve the one created?
SELECT * FROM cypher('issue_1630',
                     $$ WITH {first: 'jon', last: 'snow'} AS jonsnow
                        WITH jonsnow AS name
                        WITH name AS cols
                        MERGE (v:PERSION {first: cols.first, last: cols.last})
                        RETURN v $$) AS (v agtype);

-- delete them to start over
-- check pushing through a few clauses and returning both vars
SELECT * FROM cypher('issue_1630', $$ MATCH (u) DELETE(u) $$) AS (u agtype);
SELECT * FROM cypher('issue_1630',
                     $$ WITH {first: 'jon', last: 'snow'} AS jonsnow
                        WITH jonsnow AS name
                        WITH name AS cols
                        MERGE (v:PERSION {first: cols.first, last: cols.last})
                        RETURN v, cols $$) AS (v agtype, cols agtype);

--
-- Issue 1691 - MERGE incorrectly creates multiple vertices
--
SELECT * FROM create_graph('issue_1691');
SELECT * FROM cypher('issue_1691', $$ MATCH (u) RETURN (u) $$) AS (u agtype);

-- should only create 2 distinct rows but return 4, the extra 2 being duplicates
SELECT * FROM cypher('issue_1691', $$ UNWIND ["foo", "bar", "foo", "foo"] as n
                                      MERGE (u {name: n})-[e:knows]->(v)
                                      RETURN u, e, v  $$) AS (u agtype, e agtype, v agtype);
-- should only return the same above 4 rows
SELECT * FROM cypher('issue_1691', $$ UNWIND ["foo", "bar", "foo", "foo"] as n
                                      MERGE (u {name: n})-[e:knows]->(v)
                                      RETURN u, e, v  $$) AS (u agtype, e agtype, v agtype);
-- should only return 2 distinct rows from above
SELECT * FROM cypher('issue_1691', $$ MATCH (u)-[e]->(v)
                                      RETURN u, e, v  $$) AS (u agtype, e agtype, v agtype);

SELECT * FROM cypher('issue_1691', $$MATCH ()-[e]->() DELETE e $$) AS (a agtype);
SELECT * FROM cypher('issue_1691', $$MATCH (u) DELETE u $$) AS (a agtype);

-- should only create 1 record but return 2, one a dup of the other
SELECT * FROM cypher('issue_1691', $$ UNWIND ["foo", "foo"] AS each
                                      MERGE (v:TEST {name: each})
                                      RETURN v $$) AS (v agtype);

SELECT * FROM cypher('issue_1691', $$ MATCH (u) RETURN (u) $$) AS (u agtype);

-- should just return 5 foo records that are all the same one
SELECT * FROM cypher('issue_1691', $$ UNWIND ["foo", "foo", "bar", "foo", "bar"] AS each
                                      MERGE (v:TEST {name: "foo"})
                                      RETURN v $$) AS (v agtype);

SELECT * FROM cypher('issue_1691', $$ MATCH (u) RETURN (u) $$) AS (u agtype);

-- should just return 5 bar records that are all the same one
SELECT * FROM cypher('issue_1691', $$ UNWIND ["foo", "foo", "bar", "foo", "bar"] AS each
                                      MERGE (v:TEST {name: "bar"})
                                      RETURN v $$) AS (v agtype);

SELECT * FROM cypher('issue_1691', $$ MATCH (u) RETURN (u) $$) AS (u agtype);
SELECT * FROM cypher('issue_1691', $$MATCH (u) DELETE u $$) AS (a agtype);

-- should create 2 rows foo->bar and bar->bar and the other 3 are just returning dups
SELECT * FROM cypher('issue_1691', $$ UNWIND ["foo", "bar", "foo", "foo", "bar"] as n
                                      MERGE (u {name: n})-[e1:knows]->(v {name: "bar"})-[e2:knows]->(w)
                                      RETURN u, e1, v, e2, w  $$) AS (u agtype, e1 agtype, v agtype, e2 agtype, w agtype);

-- clean up
SELECT * FROM cypher('issue_1691', $$MATCH ()-[e]->() DELETE e $$) AS (a agtype);
SELECT * FROM cypher('issue_1691', $$MATCH (u) DELETE u $$) AS (a agtype);

--
-- Issue 1709 - MERGE creates incomplete vertices after the first one
--              This is actually an issue with MERGE not using the correct command id
--
SELECT * FROM create_graph('issue_1709');
SELECT * FROM cypher('issue_1709', $$ MATCH (u) RETURN u $$) AS (u agtype);

SELECT * FROM cypher('issue_1709', $$ UNWIND [{first: 'jon', last: 'snow'}, {first: 'ned', last: 'stark'}] AS map
                                      MERGE (v:PERSON {first: map.first})
                                      SET v=map
                                      RETURN v $$) AS (v agtype);
SELECT * FROM cypher('issue_1709', $$ MATCH (u) RETURN u $$) AS (u agtype);
SELECT * FROM cypher('issue_1709', $$ MATCH (u) DELETE u $$) AS (a agtype);

SELECT * FROM cypher('issue_1709', $$ UNWIND [{first: 'jon', last: 'snow'}, {first: 'ned', last: 'stark', middle: 'jim'}, {first: 'jane', last: 'doe'}, {first: 'ned', last: 'flanders'}, {first: 'wanda', last: 'cosmo'}] AS map
                                      MERGE (v:PERSON {first: map.first})
                                      SET v=map
                                      RETURN v $$) AS (v agtype);
SELECT * FROM cypher('issue_1709', $$ MATCH (u) RETURN u $$) AS (u agtype);
SELECT * FROM cypher('issue_1709', $$ MATCH (u) DELETE u $$) AS (a agtype);

SELECT * FROM cypher('issue_1709', $$ UNWIND [{first: 'jon', last: 'snow'}, {first: 'ned', last: 'stark'}] AS map
                                      MERGE (u: PERSON {last: map.last})-[e:KNOWS]->(v:PERSON {first: map.first})
                                      SET v=map
                                      RETURN u,e,v $$) AS (u agtype, e agtype, v agtype);
SELECT * FROM cypher('issue_1709', $$ MATCH (u)-[e]->(v) RETURN u,e,v $$) AS (u agtype, e agtype, v agtype);
SELECT * FROM cypher('issue_1709', $$ MATCH ()-[e]->() DELETE e $$) AS (a agtype);
SELECT * FROM cypher('issue_1709', $$ MATCH (u) DELETE u $$) AS (a agtype);

SELECT * FROM cypher('issue_1709', $$ UNWIND [{first: 'jon', last: 'snow'}, {first: 'ned', last: 'stark'}] AS map
                                      MERGE (u: PERSON {last: map.last})-[e:KNOWS]->(v:PERSON {first: map.first})
                                      SET u=map SET v=map
                                      RETURN u,e,v $$) AS (u agtype, e agtype, v agtype);
SELECT * FROM cypher('issue_1709', $$ MATCH (u)-[e]->(v) RETURN u,e,v $$) AS (u agtype, e agtype, v agtype);
SELECT * FROM cypher('issue_1709', $$ MATCH ()-[e]->() DELETE e $$) AS (a agtype);
-- clean up
SELECT * FROM cypher('issue_1709', $$ MATCH (u) DELETE u $$) AS (a agtype);

--
-- Fix issue 1907: SET on MERGE not storing edge properties
--
-- setup
SELECT * FROM create_graph('issue_1907');
SELECT * from cypher('issue_1907', $$ CREATE (n:Testnode {name: 'Test Node A'})
                                      RETURN n $$) as (n agtype);
SELECT * from cypher('issue_1907', $$ CREATE (n:Testnode {name: 'Test Node B'})
                                      RETURN n $$) as (n agtype);
SELECT * FROM cypher('issue_1907', $$ MATCH ()-[r]->() RETURN r $$) AS (r agtype);
-- should return properties added
SELECT * FROM cypher('issue_1907', $$ MERGE (a {name: 'Test Node A'})-[r:RELATED_TO]->(b {name: 'Test Node B'})
                                      SET r = {property1: 'something', property2: 'else'}
                                      RETURN r $$) AS (r agtype);
-- should return properties added
SELECT * FROM cypher('issue_1907', $$ MATCH ()-[r]->() RETURN r $$) AS (r agtype);
-- cleanup
SELECT * FROM cypher('issue_1907', $$ MATCH ()-[r]->() DELETE r $$) AS (r agtype);
-- do it again, but a different way
SELECT * FROM cypher('issue_1907', $$ MERGE (a {name: 'Test Node A'})-[r:RELATED_TO]->(b {name: 'Test Node B'})
                                      SET r.property1 = 'something', r.property2 = 'else'
                                      RETURN r $$) AS (r agtype);
-- should return properties added
SELECT * FROM cypher('issue_1907', $$ MATCH ()-[r]->() RETURN r $$) AS (r agtype);

--
-- clean up graphs
--
SELECT * FROM cypher('cypher_merge', $$ MATCH (n) DETACH DELETE n $$) AS (a agtype);
SELECT * FROM cypher('issue_1630', $$ MATCH (n) DETACH DELETE n $$) AS (a agtype);
SELECT * FROM cypher('issue_1709', $$ MATCH (n) DETACH DELETE n $$) AS (a agtype);

--
-- delete graphs
--
SELECT drop_graph('issue_1907', true);
SELECT drop_graph('cypher_merge', true);
SELECT drop_graph('issue_1630', true);
SELECT drop_graph('issue_1691', true);
SELECT drop_graph('issue_1709', true);

--
-- End
--
