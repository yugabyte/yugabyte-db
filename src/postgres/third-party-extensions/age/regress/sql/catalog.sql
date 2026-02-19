/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

LOAD 'age';
SET search_path TO ag_catalog;

--
-- create_graph(), drop_label(), and drop_graph() tests
--

SELECT create_graph('graph');
SELECT name, namespace FROM ag_graph WHERE name = 'graph';

-- create a label to test drop_label()
SELECT * FROM cypher('graph', $$CREATE (:l)$$) AS r(a agtype);

-- test drop_label()
SELECT drop_label('graph', 'l');

-- create a label to test drop_graph()
SELECT * FROM cypher('graph', $$CREATE (:v)$$) AS r(a agtype);

-- DROP SCHEMA ... CASCADE should fail
DROP SCHEMA graph CASCADE;

-- DROP TABLE ... should fail
DROP TABLE graph.v;

-- should fail (cascade = false)
SELECT drop_graph('graph');

SELECT drop_graph('graph', true);
SELECT count(*) FROM ag_graph WHERE name = 'graph';
SELECT count(*) FROM pg_namespace WHERE nspname = 'graph';

-- invalid cases
SELECT create_graph(NULL);
SELECT drop_graph(NULL);
SELECT create_graph('');
--
-- alter_graph() RENAME function tests
--

-- create 2 graphs for test.
SELECT create_graph('GraphA');
SELECT create_graph('GraphB');

-- Show GraphA's construction to verify case is preserved.
SELECT name, namespace FROM ag_graph WHERE name = 'GraphA';
SELECT nspname FROM pg_namespace WHERE nspname = 'GraphA';

-- Rename GraphA to GraphX.
SELECT alter_graph('GraphA', 'RENAME', 'GraphX');

-- Show GraphX's construction to verify case is preserved.
SELECT name, namespace FROM ag_graph WHERE name = 'GraphX';
SELECT nspname FROM pg_namespace WHERE nspname = 'GraphX';

-- Verify there isn't a graph GraphA anymore.
SELECT name, namespace FROM ag_graph WHERE name = 'GraphA';
SELECT * FROM pg_namespace WHERE nspname = 'GraphA';

-- Sanity check that graphx does not exist - should return 0.
SELECT count(*) FROM ag_graph where name = 'graphx';

-- Verify case sensitivity (graphx does not exist, but GraphX does) - should fail.
SELECT alter_graph('graphx', 'RENAME', 'GRAPHX');

-- Checks for collisions (GraphB already exists) - should fail.
SELECT alter_graph('GraphX', 'RENAME', 'GraphB');

-- Remove graphs.
SELECT drop_graph('GraphX', true);
SELECT drop_graph('GraphB', true);

-- Verify that renaming a graph that does not exist fails.
SELECT alter_graph('GraphB', 'RENAME', 'GraphA');

-- Verify NULL input checks.
SELECT alter_graph(NULL, 'RENAME', 'GraphA');
SELECT alter_graph('GraphB', NULL, 'GraphA');
SELECT alter_graph('GraphB', 'RENAME', NULL);

-- Verify invalid input check for operation parameter.
SELECT alter_graph('GraphB', 'DUMMY', 'GraphA');

--
-- label id test
--

SELECT create_graph('graph');

-- label id starts from 1
SELECT * FROM cypher('graph', $$CREATE (:v1)$$) AS r(a agtype);
SELECT name, id, kind, relation FROM ag_label;

-- skip label id 2 to test the logic that gets an unused label id after cycle
SELECT nextval('graph._label_id_seq');

-- label id is now 3
SELECT * FROM cypher('graph', $$CREATE (:v3)$$) as r(a agtype);
SELECT name, id, kind, relation FROM ag_label;

-- to use 65535 as the next label id, set label id to 65534
SELECT setval('graph._label_id_seq', 65534);

-- label id is now 65535
SELECT * FROM cypher('graph', $$CREATE (:v65535)$$) as r(a agtype);
SELECT name, id, kind, relation FROM ag_label;

-- after cycle, label id is now 2
SELECT * FROM cypher('graph', $$CREATE (:v2)$$) as r(a agtype);
SELECT name, id, kind, relation FROM ag_label;

SELECT drop_graph('graph', true);


-- create labels
SELECT create_graph('graph');
SELECT create_vlabel('graph', 'n');
SELECT create_elabel('graph', 'r');

-- check if labels have been created or not
SELECT name, id, kind, relation FROM ag_label;

-- try to create duplicate labels
SELECT create_vlabel('graph', 'n');
SELECT create_elabel('graph', 'r');

-- remove the labels that have been created
SELECT drop_label('graph', 'n', false);
SELECT drop_label('graph', 'r', false);

-- check if labels have been deleted or not
SELECT name, id, kind, relation FROM ag_label;

-- try to remove labels that is not there
SELECT drop_label('graph', 'n');
SELECT drop_label('graph', 'r');

-- Trying to call the functions with label null
SELECT create_vlabel('graph', NULL);
SELECT create_elabel('graph', NULL);

-- Trying to call the functions with graph null
SELECT create_vlabel(NULL, 'n');
SELECT create_elabel(NULL, 'r');

-- Trying to call the functions with both null
SELECT create_vlabel(NULL, NULL);
SELECT create_elabel(NULL, NULL);

-- age_graph_exists()
CREATE FUNCTION raise_notice(graph_name TEXT)
RETURNS void AS $$
DECLARE
    res BOOLEAN;
BEGIN
    -- this tests whether graph_exists works with IF-ELSE.
    SELECT graph_exists('graph1') INTO res;
    IF res THEN
        RAISE NOTICE 'graph exists';
    ELSE
        RAISE NOTICE 'graph does not exist';
    END IF;
END  $$ LANGUAGE plpgsql;

SELECT graph_exists('graph1');

SELECT create_graph('graph1');
SELECT graph_exists('graph1');
SELECT raise_notice('graph1');

SELECT drop_graph('graph1', true);
SELECT graph_exists('graph1');
SELECT raise_notice('graph1');

DROP FUNCTION raise_notice(TEXT);

-- dropping the graph
SELECT drop_graph('graph', true);
