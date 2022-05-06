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
 * Section 2: Graphid Indices to Improve Join Performance
 */
SELECT * FROM cypher('cypher_index', $$
    CREATE (us:Country {name: "United States"}),
        (ca:Country {name: "Canada"}),
        (mx:Country {name: "Mexico"}),
        (us)<-[:has_city]-(:City {name:"New York", country_code:"US"}),
        (us)<-[:has_city]-(:City {name:"San Fransisco", country_code:"US"}),
        (us)<-[:has_city]-(:City {name:"Los Angeles", country_code:"US"}),
        (us)<-[:has_city]-(:City {name:"Seattle", country_code:"US"}),
        (ca)<-[:has_city]-(:City {name:"Vancouver", country_code:"CA"}),
        (ca)<-[:has_city]-(:City {name:"Toroto", country_code:"CA"}),
        (ca)<-[:has_city]-(:City {name:"Montreal", country_code:"CA"}),
        (mx)<-[:has_city]-(:City {name:"Mexico City", country_code:"MX"}),
        (mx)<-[:has_city]-(:City {name:"Monterrey", country_code:"MX"}),
        (mx)<-[:has_city]-(:City {name:"Tijuana", country_code:"MX"})
$$) as (n agtype);

ALTER TABLE cypher_index."Country" ADD PRIMARY KEY (id);

CREATE UNIQUE INDEX CONCURRENTLY cntry_id_idx ON cypher_index."Country" (id);
ALTER TABLE cypher_index."Country"  CLUSTER ON cntry_id_idx;

ALTER TABLE cypher_index."City" ADD PRIMARY KEY (id);

CREATE UNIQUE INDEX city_id_idx ON cypher_index."City" (id);

ALTER TABLE cypher_index."City" CLUSTER ON city_id_idx;

ALTER TABLE cypher_index.has_city
ADD CONSTRAINT has_city_end_fk FOREIGN KEY (end_id)
REFERENCES cypher_index."Country"(id) MATCH FULL;

CREATE INDEX load_has_city_eid_idx ON cypher_index.has_city (end_id);

CREATE INDEX load_has_city_sid_idx ON cypher_index.has_city (start_id);

ALTER TABLE cypher_index."has_city" CLUSTER ON load_has_city_eid_idx;

SET enable_mergejoin = ON;
SET enable_hashjoin = OFF;
SET enable_nestloop = OFF;

SELECT COUNT(*) FROM cypher('cypher_index', $$
    MATCH (a:Country)<-[e:has_city]-()
    RETURN e
$$) as (n agtype);

SET enable_mergejoin = OFF;
SET enable_hashjoin = ON;
SET enable_nestloop = OFF;

SELECT COUNT(*) FROM cypher('cypher_index', $$
    MATCH (a:Country)<-[e:has_city]-()
    RETURN e
$$) as (n agtype);

SET enable_mergejoin = OFF;
SET enable_hashjoin = OFF;
SET enable_nestloop = ON;

SELECT COUNT(*) FROM cypher('cypher_index', $$
    MATCH (a:Country)<-[e:has_city]-()
    RETURN e
$$) as (n agtype);

SET enable_mergejoin = ON;
SET enable_hashjoin = ON;
SET enable_nestloop = ON;

--
-- Section 3: Agtype GIN Indices to Improve WHERE clause Performance
--
CREATE INDEX load_city_gid_idx
ON cypher_index."City" USING gin (properties);

SELECT COUNT(*) FROM cypher('cypher_index', $$
    MATCH (c:City {country_code: "AD"})
    RETURN c
$$) as (n agtype);

DROP INDEX load_city_gid_idx;

--
-- Section 4: Index use with WHERE clause
--
SELECT COUNT(*) FROM cypher('cypher_index', $$
    MATCH (a:City)
    WHERE a.country_code = 'RS'
    RETURN a
$$) as (n agtype);

CREATE INDEX CONCURRENTLY cntry_ode_idx ON cypher_index."City"
(ag_catalog.agtype_access_operator(properties, '"country_code"'::agtype));

SELECT COUNT(*) FROM cypher('agload_test_graph', $$
    MATCH (a:City)
    WHERE a.country_code = 'RS'
    RETURN a
$$) as (n agtype);

--
-- General Cleanup
--
SELECT drop_graph('cypher_index', true);
SELECT drop_graph('agload_test_graph', true);
