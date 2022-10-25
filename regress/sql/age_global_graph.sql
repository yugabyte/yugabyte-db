LOAD 'age';
SET search_path TO ag_catalog;

--
-- test delete_specific_GRAPH_global_contexts function 
--

-- create 3 graphs 
SELECT * FROM create_graph('age_global_graph_1');
SELECT * FROM cypher('age_global_graph_1', $$ CREATE (v:vertex_from_graph_1) RETURN v  $$) AS (v agtype);

SELECT * FROM create_graph('age_global_graph_2');
SELECT * FROM cypher('age_global_graph_2', $$ CREATE (v:vertex_from_graph_2) RETURN v  $$) AS (v agtype);

SELECT * FROM create_graph('age_global_graph_3');
SELECT * FROM cypher('age_global_graph_3', $$ CREATE (v:vertex_from_graph_3) RETURN v  $$) AS (v agtype);

-- load contexts using the vertex_stats command 
SELECT * FROM cypher('age_global_graph_3', $$ MATCH (u) RETURN vertex_stats(u) $$) AS (result agtype);
SELECT * FROM cypher('age_global_graph_2', $$ MATCH (u) RETURN vertex_stats(u) $$) AS (result agtype);
SELECT * FROM cypher('age_global_graph_1', $$ MATCH (u) RETURN vertex_stats(u) $$) AS (result agtype);

-- delete age_global_graph_2's context
-- should return true (succeeded)
SELECT * FROM cypher('age_global_graph_2', $$ RETURN delete_global_graphs('age_global_graph_2') $$) AS (result agtype);

-- delete age_global_graph_1's context
-- should return true (succeed) because previous command should not delete the 1st graph's context
SELECT * FROM cypher('age_global_graph_1', $$ RETURN delete_global_graphs('age_global_graph_1') $$) AS (result agtype);

-- delete age_global_graph_3's context
-- should return true (succeed) because previous commands should not delete the 3rd graph's context
SELECT * FROM cypher('age_global_graph_3', $$ RETURN delete_global_graphs('age_global_graph_3') $$) AS (result agtype);

-- delete all graphs' context again
-- should return false (did not succeed) for all of them because already removed
SELECT * FROM cypher('age_global_graph_2', $$ RETURN delete_global_graphs('age_global_graph_2') $$) AS (result agtype);
SELECT * FROM cypher('age_global_graph_1', $$ RETURN delete_global_graphs('age_global_graph_1') $$) AS (result agtype);
SELECT * FROM cypher('age_global_graph_3', $$ RETURN delete_global_graphs('age_global_graph_3') $$) AS (result agtype);

    
--
-- test delete_GRAPH_global_contexts function 
--

-- load contexts again 
SELECT * FROM cypher('age_global_graph_3', $$ MATCH (u) RETURN vertex_stats(u) $$) AS (result agtype);
SELECT * FROM cypher('age_global_graph_2', $$ MATCH (u) RETURN vertex_stats(u) $$) AS (result agtype);
SELECT * FROM cypher('age_global_graph_1', $$ MATCH (u) RETURN vertex_stats(u) $$) AS (result agtype);

-- delete all graph contexts
-- should return true
SELECT * FROM cypher('age_global_graph_1', $$ RETURN delete_global_graphs(NULL) $$) AS (result agtype);

-- delete all graphs' context individually
-- should return false for all of them because already removed
SELECT * FROM cypher('age_global_graph_1', $$ RETURN delete_global_graphs('age_global_graph_1') $$) AS (result agtype);
SELECT * FROM cypher('age_global_graph_2', $$ RETURN delete_global_graphs('age_global_graph_2') $$) AS (result agtype);
SELECT * FROM cypher('age_global_graph_3', $$ RETURN delete_global_graphs('age_global_graph_3') $$) AS (result agtype);

-- drop graphs
SELECT * FROM drop_graph('age_global_graph_1', true);
SELECT * FROM drop_graph('age_global_graph_2', true);
SELECT * FROM drop_graph('age_global_graph_3', true);

--
-- End of tests
--
