LOAD 'agensgraph';
SET search_path TO ag_catalog;

SELECT * FROM create_graph('cypher_with_graph');

SELECT * FROM cypher('cypher_with_graph', $$
WITH true AS b
RETURN b
$$) AS (b bool);

-- Expression item must be aliased.
SELECT * FROM cypher('cypher_with_graph', $$
WITH 1 + 1
RETURN i
$$) AS (i int);

SELECT * FROM drop_graph('cypher_with_graph');
