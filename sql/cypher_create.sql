LOAD 'agensgraph';
SET search_path TO ag_catalog;

SELECT * FROM cypher($$CREATE ()$$) AS (a agtype);

-- Column definition list for CREATE clause must contain a single agtype
-- attribute.

SELECT * FROM cypher($$CREATE ()$$) AS (a int);
SELECT * FROM cypher($$CREATE ()$$) AS (a agtype, b int);
