/*
 * Copyright 2020 Bitnine Co., Ltd.
 * 
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 * 
 *     http://www.apache.org/licenses/LICENSE-2.0
 * 
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

--
-- Initial setup
--

LOAD 'agensgraph';
SET search_path TO ag_catalog;

--
-- create_graph() and drop_graph() tests.
--

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

--
-- alter_graph() RENAME function test.
--

-- Create 2 graphs for test.
SELECT create_graph('GraphA');
SELECT create_graph('GraphB');

-- Show GraphA's construction to verify case is preserved.
SELECT * FROM ag_graph WHERE name = 'GraphA';
SELECT nspname FROM pg_namespace WHERE nspname = 'GraphA';

-- Rename GraphA to GraphX.
SELECT alter_graph('GraphA', 'RENAME', 'GraphX');

-- Show GraphX's construction to verify case is preserved.
SELECT * FROM ag_graph WHERE name = 'GraphX';
SELECT nspname FROM pg_namespace WHERE nspname = 'GraphX';

-- Verify there isn't a graph GraphA anymore.
SELECT * FROM ag_graph WHERE name = 'GraphA';
SELECT * FROM pg_namespace WHERE nspname = 'GraphA';

-- Sanity check that graphx does not exist - should return 0.
SELECT count(*) FROM ag_graph where name = 'graphx';

-- Verify case sensitivity (graphx does not exist, but GraphX does) - should fail.
SELECT alter_graph('graphx', 'RENAME', 'GRAPHX');

-- Checks for collisions (GraphB already exists) - should fail.
SELECT alter_graph('GraphX', 'RENAME', 'GraphB');

-- Remove graphs.
SELECT drop_graph('GraphX');
SELECT drop_graph('GraphB');

-- Verify that renaming a graph that does not exist fails.
SELECT alter_graph('GraphB', 'RENAME', 'GraphA');

-- Verify NULL input checks.
SELECT alter_graph(NULL, 'RENAME', 'GraphA');
SELECT alter_graph('GraphB', NULL, 'GraphA');
SELECT alter_graph('GraphB', 'RENAME', NULL);

-- Verify invalid input check for operation parameter.
SELECT alter_graph('GraphB', 'DUMMY', 'GraphA');

--
-- End tests
--
