LOAD 'agensgraph';
SET search_path TO ag_catalog;

SELECT * FROM cypher($$
WITH true AS b
RETURN b
$$) AS (b bool);

-- Expression item must be aliased.
SELECT * FROM cypher($$
WITH 1 + 1
RETURN i
$$) AS (i int);
