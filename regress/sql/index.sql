\! cp -r regress/age_load/data regress/instance/data/age_load

LOAD 'age';
SET search_path TO ag_catalog;

SET enable_mergejoin = ON;
SET enable_hashjoin = ON;
SET enable_nestloop = ON;

SELECT create_graph('cypher_index');

/*
 * Section 1: Unique Index on Properties
 */
--Section 1 Setup
SELECT create_vlabel('cypher_index', 'idx');
CREATE UNIQUE INDEX cypher_index_idx_props_uq ON cypher_index.idx(properties);

--Test 1
SELECT * FROM cypher('cypher_index', $$ CREATE (:idx {i: 1}) $$) AS (a agtype);
SELECT * FROM cypher('cypher_index', $$ CREATE (:idx {i: 1}) $$) AS (a agtype);

--Clean Up
SELECT * FROM cypher('cypher_index', $$ MATCH(n) DETACH DELETE n $$) AS (a agtype);

--Test 2
SELECT * FROM cypher('cypher_index', $$ CREATE (:idx {i: 1}), (:idx {i: 1}) $$) AS (a agtype);

--Clean Up
SELECT * FROM cypher('cypher_index', $$ MATCH(n) DETACH DELETE n $$) AS (a agtype);

--Test 3
--Data Setup
SELECT * FROM cypher('cypher_index', $$ CREATE (:idx {i: 1}) $$) AS (a agtype);
SELECT * FROM cypher('cypher_index', $$ CREATE (:idx) $$) AS (a agtype);

--Query
SELECT * FROM cypher('cypher_index', $$ MATCH(n) SET n.i = 1$$) AS (a agtype);

--Clean Up
SELECT * FROM cypher('cypher_index', $$ MATCH(n) DETACH DELETE n $$) AS (a agtype);

--Test 4
--create a vertex with i = 1
SELECT * FROM cypher('cypher_index', $$ CREATE (:idx {i: 1}) $$) AS (a agtype);

--delete the vertex
SELECT * FROM cypher('cypher_index', $$ MATCH(n) DETACH DELETE n $$) AS (a agtype);

--we should be able to create a new vertex with the same value
SELECT * FROM cypher('cypher_index', $$ CREATE (:idx {i: 1}) $$) AS (a agtype);

--data cleanup
SELECT * FROM cypher('cypher_index', $$ MATCH(n) DETACH DELETE n $$) AS (a agtype);

/*
 * Test 5
 *
 * Same queries as Test 4, only in 1 transaction
 */
BEGIN TRANSACTION;
--create a vertex with i = 1
SELECT * FROM cypher('cypher_index', $$ CREATE (:idx {i: 1}) $$) AS (a agtype);

--delete the vertex
SELECT * FROM cypher('cypher_index', $$ MATCH(n) DETACH DELETE n $$) AS (a agtype);

--we should be able to create a new vertex with the same value
SELECT * FROM cypher('cypher_index', $$ CREATE (:idx {i: 1}) $$) AS (a agtype);

COMMIT;

--data cleanup
SELECT * FROM cypher('cypher_index', $$ MATCH(n) DETACH DELETE n $$) AS (a agtype);


--Test 6
--create a vertex with i = 1
SELECT * FROM cypher('cypher_index', $$ CREATE (:idx {i: 1}) $$) AS (a agtype);

-- change the value
SELECT * FROM cypher('cypher_index', $$ MATCH(n) SET n.i = 2 $$) AS (a agtype);

--we should be able to create a new vertex with the same value
SELECT * FROM cypher('cypher_index', $$ CREATE (:idx {i: 1}) $$) AS (a agtype);

--validate the data
SELECT * FROM cypher('cypher_index', $$ MATCH(n) RETURN n $$) AS (a agtype);

--data cleanup
SELECT * FROM cypher('cypher_index', $$ MATCH(n) DETACH DELETE n $$) AS (a agtype);

/*
 * Test 7
 *
 * Same queries as Test 6, only in 1 transaction
 */
BEGIN TRANSACTION;
--create a vertex with i = 1
SELECT * FROM cypher('cypher_index', $$ CREATE (:idx {i: 1}) $$) AS (a agtype);

-- change the value
SELECT * FROM cypher('cypher_index', $$ MATCH(n) SET n.i = 2 $$) AS (a agtype);

--we should be able to create a new vertex with the same value
SELECT * FROM cypher('cypher_index', $$ CREATE (:idx {i: 1}) $$) AS (a agtype);

--validate the data
SELECT * FROM cypher('cypher_index', $$ MATCH(n) RETURN n $$) AS (a agtype);

COMMIT;

--validate the data again out of the transaction, just in case
SELECT * FROM cypher('cypher_index', $$ MATCH(n) RETURN n $$) AS (a agtype);

--data cleanup
SELECT * FROM cypher('cypher_index', $$ MATCH(n) DETACH DELETE n $$) AS (a agtype);


--Test 8
--create a vertex with i = 1
SELECT * FROM cypher('cypher_index', $$ CREATE (:idx {i: 1}) $$) AS (a agtype);

-- Use Merge and force an index error
SELECT * FROM cypher('cypher_index', $$ MATCH(n) MERGE (n)-[:e]->(:idx {i: n.i}) $$) AS (a agtype);

--data cleanup
SELECT * FROM cypher('cypher_index', $$ MATCH(n) DETACH DELETE n $$) AS (a agtype);


/*
 * Section 2: Indices to Improve Query Runtime
 */
SELECT create_graph('agload_test_graph');

ALTER TABLE agload_test_graph._ag_label_vertex
CLUSTER ON _ag_label_vertex_pkey;

SELECT create_vlabel('agload_test_graph','Country');
SELECT load_labels_from_file('agload_test_graph', 'Country',
    'age_load/countries.csv');

ALTER TABLE agload_test_graph."Country" ADD PRIMARY KEY (id);
CREATE UNIQUE INDEX CONCURRENTLY cntry_id_idx ON agload_test_graph."Country" (id);
ALTER TABLE agload_test_graph."Country"  CLUSTER ON cntry_id_idx;


SELECT create_vlabel('agload_test_graph','City');
SELECT load_labels_from_file('agload_test_graph', 'City',
    'age_load/cities.csv');

ALTER TABLE agload_test_graph."City"
ADD PRIMARY KEY (id);

CREATE UNIQUE INDEX city_id_idx
ON agload_test_graph."City" (id);

ALTER TABLE agload_test_graph."City"
CLUSTER ON city_id_idx;


SELECT create_elabel('agload_test_graph','has_city');
SELECT load_edges_from_file('agload_test_graph', 'has_city',
     'age_load/edges.csv');

ALTER TABLE agload_test_graph.has_city
ADD CONSTRAINT has_city_end_fk FOREIGN KEY (end_id)
REFERENCES agload_test_graph."Country"(id) MATCH FULL;

CREATE INDEX load_has_city_eid_idx
ON agload_test_graph.has_city (end_id);

CREATE INDEX load_has_city_sid_idx
ON agload_test_graph.has_city (start_id);

ALTER TABLE agload_test_graph."has_city"
CLUSTER ON load_has_city_eid_idx;

SET enable_mergejoin = ON;
SET enable_hashjoin = OFF;
SET enable_nestloop = OFF;

SELECT COUNT(*) FROM cypher('agload_test_graph', $$
    MATCH (a:Country)<-[e:has_city]-()
    RETURN e
$$) as (n agtype);

SET enable_mergejoin = OFF;
SET enable_hashjoin = ON;
SET enable_nestloop = OFF;

SELECT COUNT(*) FROM cypher('agload_test_graph', $$
    MATCH (a:Country)<-[e:has_city]-()
    RETURN e
$$) as (n agtype);

SET enable_mergejoin = OFF;
SET enable_hashjoin = OFF;
SET enable_nestloop = ON;

SELECT COUNT(*) FROM cypher('agload_test_graph', $$
    MATCH (a:Country)<-[e:has_city]-()
    RETURN e
$$) as (n agtype);

SELECT drop_graph('cypher_index', true);
SELECT drop_graph('agload_test_graph', true);
