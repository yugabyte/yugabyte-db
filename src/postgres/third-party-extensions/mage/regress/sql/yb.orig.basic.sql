CREATE EXTENSION mage;
SELECT extname, extversion FROM pg_extension WHERE extname = 'mage';
SET search_path TO mag_catalog;
SELECT create_graph('basic');
SELECT name, namespace FROM ag_graph WHERE name = 'basic';
-- meko_datapack_id, meko_user_id and meko_agent_id are NOT NULL on
-- vertex/edge tables, so the cypher CREATE must supply them as properties.
SELECT * FROM cypher('basic', $$ CREATE (:person {name: 'Alice',
    meko_datapack_id: '00000000-0000-0000-0000-000000000001',
    meko_user_id: '00000000-0000-0000-0000-000000000002',
    meko_agent_id: 'agent-1'}) $$) AS (a agtype);
SELECT * FROM cypher('basic', $$ CREATE (:person {name: 'Bob',
    meko_datapack_id: '00000000-0000-0000-0000-000000000001',
    meko_user_id: '00000000-0000-0000-0000-000000000002',
    meko_agent_id: 'agent-1'}) $$) AS (a agtype);
SELECT * FROM cypher('basic', $$ MATCH (n:person) RETURN count(n) $$) AS (cnt agtype);
-- cypher CREATE without the required meko_* tenant properties errors out.
SELECT * FROM cypher('basic', $$ CREATE (:person {name: 'NoTenant'}) $$) AS (a agtype);
-- SET cannot mutate meko_* tenant properties.
SELECT * FROM cypher('basic', $$ MATCH (n:person {name: 'Alice'})
    SET n.meko_user_id = '00000000-0000-0000-0000-000000000099' $$) AS (a agtype);
-- create_complete_graph and age_create_barbell_graph reach
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
