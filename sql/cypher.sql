CREATE EXTENSION agensgraph;

SET search_path TO ag_catalog;

SELECT * FROM cypher(NULL) AS r(c text);
SELECT * FROM cypher($$RETURN 0$$) AS r(c text);
