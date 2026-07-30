LOAD 'age';
SET search_path TO ag_catalog;

--
-- delete_specific_GRAPH_global_contexts
--

--
SELECT * FROM create_graph('ag_graph_1');
SELECT * FROM cypher('ag_graph_1', $$ CREATE (v:vertex1) RETURN v  $$) AS (v agtype);

SELECT * FROM create_graph('ag_graph_2');
SELECT * FROM cypher('ag_graph_2', $$ CREATE (v:vertex2) RETURN v  $$) AS (v agtype);

SELECT * FROM create_graph('ag_graph_3');
SELECT * FROM cypher('ag_graph_3', $$ CREATE (v:vertex3) RETURN v  $$) AS (v agtype);

-- load contexts using the vertex_stats command
SELECT * FROM cypher('ag_graph_3', $$ MATCH (u) RETURN vertex_stats(u) $$) AS (result agtype);
SELECT * FROM cypher('ag_graph_2', $$ MATCH (u) RETURN vertex_stats(u) $$) AS (result agtype);
SELECT * FROM cypher('ag_graph_1', $$ MATCH (u) RETURN vertex_stats(u) $$) AS (result agtype);

--- loading undefined contexts
--- should throw exception - graph "ag_graph_4" does not exist
SELECT * FROM cypher('ag_graph_4', $$ MATCH (u) RETURN vertex_stats(u) $$) AS (result agtype);

--- delete with invalid parameter
---should return false
SELECT * FROM cypher('ag_graph_2', $$ RETURN delete_global_graphs('E1337') $$) AS (result agtype);

-- delete ag_graph_2's context
-- should return true
SELECT * FROM cypher('ag_graph_2', $$ RETURN delete_global_graphs('ag_graph_2') $$) AS (result agtype);

-- delete ag_graph_1's context
-- should return true(succeed) because the previous command should not delete the 1st graph's context
SELECT * FROM cypher('ag_graph_1', $$ RETURN delete_global_graphs('ag_graph_1') $$) AS (result agtype);

-- delete ag_graph_3's context
-- should return true(succeed) because the previous commands should not delete the 3rd graph's context
SELECT * FROM cypher('ag_graph_3', $$ RETURN delete_global_graphs('ag_graph_3') $$) AS (result agtype);

-- delete all graphs' context again
-- should return false (did not succeed) for all of them because they are already removed
SELECT * FROM cypher('ag_graph_2', $$ RETURN delete_global_graphs('ag_graph_2') $$) AS (result agtype);
SELECT * FROM cypher('ag_graph_1', $$ RETURN delete_global_graphs('ag_graph_1') $$) AS (result agtype);
SELECT * FROM cypher('ag_graph_3', $$ RETURN delete_global_graphs('ag_graph_3') $$) AS (result agtype);

--- delete uninitialized graph context
--- should throw exception graph "ag_graph_4" does not exist
SELECT * FROM cypher('ag_graph_4', $$ RETURN delete_global_graphs('ag_graph_4') $$) AS (result agtype);

--
-- delete_GRAPH_global_contexts
--

-- load contexts again
SELECT * FROM cypher('ag_graph_3', $$ MATCH (u) RETURN vertex_stats(u) $$) AS (result agtype);
SELECT * FROM cypher('ag_graph_2', $$ MATCH (u) RETURN vertex_stats(u) $$) AS (result agtype);
SELECT * FROM cypher('ag_graph_1', $$ MATCH (u) RETURN vertex_stats(u) $$) AS (result agtype);

-- delete all graph contexts
-- should return true
SELECT * FROM cypher('ag_graph_1', $$ RETURN delete_global_graphs(NULL) $$) AS (result agtype);

-- delete all graphs' context individually
-- should return false for all of them because already removed
SELECT * FROM cypher('ag_graph_1', $$ RETURN delete_global_graphs('ag_graph_1') $$) AS (result agtype);
SELECT * FROM cypher('ag_graph_2', $$ RETURN delete_global_graphs('ag_graph_2') $$) AS (result agtype);
SELECT * FROM cypher('ag_graph_3', $$ RETURN delete_global_graphs('ag_graph_3') $$) AS (result agtype);

--
-- age_vertex_stats
--

--checking validity of vertex_stats
--adding unlabelled vertices to ag_graph_1
SELECT * FROM cypher('ag_graph_1', $$ CREATE (n), (m) $$) as (v agtype);

--adding labelled vertice to graph 2
SELECT * FROM cypher('ag_graph_2', $$ CREATE (:Person) $$) as (v agtype);

---adding edges between nodes
SELECT * FROM cypher('ag_graph_2', $$ MATCH (a:Person), (b:Person) WHERE a.name = 'A' AND b.name = 'B' CREATE (a)-[e:RELTYPE]->(b) RETURN e $$) as (e agtype);

--checking if vertex stats have been updated along with the new label
--should return 3 vertices
SELECT * FROM cypher('ag_graph_1', $$ MATCH (n) RETURN vertex_stats(n) $$) AS (result agtype);

--should return 1 vertice and 1 label
SELECT * FROM cypher('ag_graph_2', $$ MATCH (a) RETURN vertex_stats(a) $$) AS (result agtype);

--
-- graph_stats command
--
-- what's in the current graphs?
SELECT * FROM cypher('ag_graph_1', $$ RETURN graph_stats('ag_graph_1') $$) AS (result agtype);
SELECT * FROM cypher('ag_graph_1', $$ RETURN graph_stats('ag_graph_2') $$) AS (result agtype);
SELECT * FROM cypher('ag_graph_1', $$ RETURN graph_stats('ag_graph_3') $$) AS (result agtype);
-- add some edges
SELECT * FROM cypher('ag_graph_1', $$ CREATE ()-[:knows]->() $$) AS (results agtype);
SELECT * FROM cypher('ag_graph_1', $$ CREATE ()-[:knows]->() $$) AS (results agtype);
SELECT * FROM cypher('ag_graph_1', $$ CREATE ()-[:knows]->() $$) AS (results agtype);
SELECT * FROM cypher('ag_graph_1', $$ CREATE ()-[:knows]->() $$) AS (results agtype);
-- what is there now?
SELECT * FROM cypher('ag_graph_1', $$ RETURN graph_stats('ag_graph_1') $$) AS (result agtype);
-- add some more
SELECT * FROM cypher('ag_graph_1', $$ MATCH (u)-[]->(v) SET u.id = id(u)
                                                        SET v.id = id(v)
                                                        SET u.name = 'u'
                                                        SET v.name = 'v'
                                      RETURN u,v $$) AS (u agtype, v agtype);
SELECT * FROM cypher('ag_graph_1', $$ MATCH (u)-[]->(v) MERGE (v)-[:stalks]->(u) $$) AS (result agtype);
SELECT * FROM cypher('ag_graph_1', $$ MATCH (u)-[e]->(v) RETURN u, e, v $$) AS (u agtype, e agtype, v agtype);
-- what is there now?
SELECT * FROM cypher('ag_graph_1', $$ RETURN graph_stats('ag_graph_1') $$) AS (result agtype);
-- remove some vertices
SELECT * FROM ag_graph_1._ag_label_vertex;
DELETE FROM ag_graph_1._ag_label_vertex WHERE id::text = '281474976710661';
DELETE FROM ag_graph_1._ag_label_vertex WHERE id::text = '281474976710662';
DELETE FROM ag_graph_1._ag_label_vertex WHERE id::text = '281474976710664';
SELECT * FROM ag_graph_1._ag_label_vertex;
SELECT * FROM ag_graph_1._ag_label_edge;
-- there should be warning messages
SELECT * FROM cypher('ag_graph_1', $$ RETURN graph_stats('ag_graph_1') $$) AS (result agtype);

--drop graphs

SELECT * FROM drop_graph('ag_graph_1', true);
SELECT * FROM drop_graph('ag_graph_2', true);
SELECT * FROM drop_graph('ag_graph_3', true);
-----------------------------------------------------------------------------------------------------------------------------
--
-- End of tests
--

