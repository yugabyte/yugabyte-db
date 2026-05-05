CREATE EXTENSION mage;
SELECT extname, extversion FROM pg_extension WHERE extname = 'mage';
SET search_path TO mag_catalog;
SELECT create_graph('basic');
SELECT name, namespace FROM ag_graph WHERE name = 'basic';
SELECT * FROM cypher('basic', $$ CREATE (:person {name: 'Alice'}) $$) AS (a agtype);
SELECT * FROM cypher('basic', $$ CREATE (:person {name: 'Bob'}) $$) AS (a agtype);
SELECT * FROM cypher('basic', $$ MATCH (n:person) RETURN count(n) $$) AS (cnt agtype);
RESET search_path;
-- DROP EXTENSION must trigger ag_ProcessUtility_hook -> drop_age_extension,
-- which cleans up the per-graph schema. The two count(*) queries below catch
-- regressions in is_age_drop()'s extension-name match.
DROP EXTENSION mage;
SELECT count(*) FROM pg_extension WHERE extname = 'mage';
SELECT count(*) FROM pg_namespace WHERE nspname = 'basic';
