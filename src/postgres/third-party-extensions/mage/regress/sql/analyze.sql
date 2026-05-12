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
-- Start of tests
--

SELECT * FROM create_graph('analyze');
SELECT * FROM cypher('analyze', $$ CREATE (u) RETURN u $$) AS (result agtype);

-- should error due to invalid input to cypher function
SELECT * FROM cypher(NULL, NULL) AS (result agtype);
SELECT * FROM cypher('analyze', NULL) AS (result agtype);
SELECT * FROM cypher(NULL, '') AS (result agtype);
SELECT * FROM cypher('', '') AS (result agtype);
SELECT * FROM cypher('analyze', '') AS (result agtype);
-- should error due to bad cypher statement
SELECT * FROM cypher('analyze', $$ $$) AS (result agtype);

-- should return false due to invalid input to age_prepare_function
SELECT * FROM age_prepare_cypher(NULL, NULL);
SELECT * FROM age_prepare_cypher('analyze', NULL);
SELECT * FROM age_prepare_cypher(NULL, '');
-- should return true but cypher should fail
SELECT * FROM age_prepare_cypher('analyze', '');
SELECT * FROM cypher(NULL, NULL) AS (result agtype);
-- should return true and execute cypher command
SELECT * FROM age_prepare_cypher('analyze', 'MATCH (u) RETURN (u)');
SELECT * FROM cypher(NULL, NULL) AS (result agtype);
-- should error due to invalid input to cypher function
SELECT * FROM cypher(NULL, NULL) AS (result agtype);
-- should return true but cypher should fail
SELECT * FROM age_prepare_cypher('analyze', '$$ $$');
SELECT * FROM cypher(NULL, NULL) AS (result agtype);

-- should return errors
SELECT * FROM cypher() AS (result agtype);
SELECT * FROM cypher(NULL) AS (result agtype);

-- drop graphs
SELECT * FROM drop_graph('analyze', true);

--
-- End of tests
--
