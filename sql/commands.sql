LOAD 'agensgraph';
SET search_path TO ag_catalog;

SELECT create_graph('g');
SELECT count(*) FROM ag_graph WHERE name = 'g';
SELECT count(*) FROM pg_namespace WHERE nspname = 'g';

-- Create a temporary table to test drop_graph().
CREATE TABLE g.tmp (i int);

SELECT drop_graph('g');
SELECT drop_graph('g', true);
SELECT count(*) FROM pg_namespace WHERE nspname = 'g';
SELECT count(*) FROM ag_graph WHERE name = 'g';

SELECT create_graph(NULL);
SELECT drop_graph(NULL);
