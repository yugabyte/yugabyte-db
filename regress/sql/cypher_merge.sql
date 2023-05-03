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
 * test 5: Prev clause has results, path does not exist (differnt property name)
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
SELECT count(*) FROM cypher('cypher_merge', $$MATCH p=()-[e:e]->() RETURN p$$) AS (p agtype)

-- Returns 1, the path created in MERGE
SELECT count(*) FROM cypher('cypher_merge', $$MATCH p=()-[:e]->()-[]->() RETURN p$$) AS (p agtype);

-- 5 vertices total should have been created
SELECT count(*) FROM cypher('cypher_merge', $$MATCH (n) RETURN n$$) AS (n agtype);

--clean up
SELECT * FROM cypher('cypher_merge', $$MATCH (n) DETACH DELETE n $$) AS (a agtype);

/*
 * test 13: edge doesn't exists (differnt label), using MATCH
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
 * XXX: Incorrect Output. To FIX
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

--clean up
SELECT * FROM cypher('cypher_merge', $$MATCH (n) DETACH DELETE n $$) AS (a agtype);

/*
 * Clean up graph
 */
SELECT drop_graph('cypher_merge', true);

