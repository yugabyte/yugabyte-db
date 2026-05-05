CREATE EXTENSION mage;
SELECT extname, extversion FROM pg_extension WHERE extname = 'mage';
SET search_path TO mag_catalog;
SELECT create_graph('basic');
SELECT name, namespace FROM ag_graph WHERE name = 'basic';
SELECT * FROM cypher('basic', $$ CREATE (:person {name: 'Alice'}) $$) AS (a agtype);
SELECT * FROM cypher('basic', $$ CREATE (:person {name: 'Bob'}) $$) AS (a agtype);
SELECT * FROM cypher('basic', $$ MATCH (n:person) RETURN count(n) $$) AS (cnt agtype);
-- YB: create_complete_graph and age_create_barbell_graph reach
-- yb_insert_*_simple, which lacks tenant column support yet (#31338).
-- Confirm they raise feature_not_supported rather than violating the
-- meko_* NOT NULL constraints.
SELECT create_complete_graph('basic', 3, 'gen_edge', 'gen_vertex');
SELECT age_create_barbell_graph('basic', 3, 0, 'bb_vertex', NULL, 'bb_edge', NULL);
RESET search_path;
-- DROP EXTENSION must trigger ag_ProcessUtility_hook -> drop_age_extension,
-- which cleans up the per-graph schema. The two count(*) queries below catch
-- regressions in is_age_drop()'s extension-name match.
DROP EXTENSION mage;
SELECT count(*) FROM pg_extension WHERE extname = 'mage';
SELECT count(*) FROM pg_namespace WHERE nspname = 'basic';
