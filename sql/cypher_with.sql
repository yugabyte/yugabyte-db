LOAD 'agensgraph';
SET search_path TO ag_catalog;

SELECT create_graph('cypher_with');

SELECT * FROM cypher('cypher_with', $$
WITH true AS b
RETURN b
$$) AS (b bool);

-- Expression item must be aliased.
SELECT * FROM cypher('cypher_with', $$
WITH 1 + 1
RETURN i
$$) AS (i int);

SELECT drop_graph('cypher_with');
