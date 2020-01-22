LOAD 'agensgraph';
SET search_path TO ag_catalog;

SELECT * FROM create_graph('cypher_create_graph');

SELECT * FROM cypher('cypher_create_graph', $$CREATE ()$$) AS (a agtype);
SELECT * FROM cypher('cypher_create_graph', $$CREATE (:label1)$$) as q(a agtype);

SELECT * FROM cypher('cypher_create_graph', $$CREATE ()-[]-()$$) as q(a agtype);

SELECT * FROM cypher_create_graph.label1;

-- Column definition list for CREATE clause must contain a single agtype
-- attribute.

SELECT * FROM cypher('cypher_create_graph', $$CREATE ()$$) AS (a int);
SELECT * FROM cypher('cypher_create_graph', $$CREATE ()$$) AS (a agtype, b int);

SELECT * FROM drop_graph('cypher_create_graph', true);
