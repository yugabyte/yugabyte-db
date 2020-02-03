LOAD 'agensgraph';
SET search_path TO ag_catalog;

SELECT create_graph('cypher_create');

SELECT * FROM cypher('cypher_create', $$CREATE ()$$) AS (a agtype);
SELECT * FROM cypher('cypher_create', $$CREATE (:label1)$$) as q(a agtype);

SELECT * FROM cypher('cypher_create', $$CREATE ()-[]-()$$) as q(a agtype);

SELECT * FROM cypher_create.label1;

-- Column definition list for CREATE clause must contain a single agtype
-- attribute.

SELECT * FROM cypher('cypher_create', $$CREATE ()$$) AS (a int);
SELECT * FROM cypher('cypher_create', $$CREATE ()$$) AS (a agtype, b int);

SELECT drop_graph('cypher_create', true);
