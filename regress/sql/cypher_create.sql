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

LOAD 'agensgraph';
SET search_path TO ag_catalog;

SELECT create_graph('cypher_create');

SELECT * FROM cypher('cypher_create', $$CREATE ()$$) AS (a agtype);
SELECT * FROM cypher('cypher_create', $$CREATE (:label1)$$) as q(a agtype);

SELECT * FROM cypher('cypher_create', $$CREATE ()-[]-()$$) as q(a agtype);

SELECT * FROM cypher_create.label1;

-- Column definition list for CREATE clause must contain a single agtype
-- attribute.

SELECT * FROM cypher('cypher_create', $$CREATE ()$$) AS (a int);
SELECT * FROM cypher('cypher_create', $$CREATE ()$$) AS (a agtype, b int);

SELECT drop_graph('cypher_create', true);
