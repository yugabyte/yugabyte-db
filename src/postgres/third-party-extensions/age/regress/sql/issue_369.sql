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

SELECT create_graph('cypher');

SELECT * from cypher('cypher', $$ CREATE (a {x:1})-[:foo]->(b {x:2}),(c {x:3}) $$) as (v0 agtype);

SELECT * from cypher('cypher', $$ MATCH ()-[a:foo]->(), (b {x:2}),(c {x:3}) MERGE (b)-[d:bar]->(c) RETURN d $$) as (v0 agtype);

SELECT * from cypher('cypher', $$ MATCH (x)-[a:foo]->(), (b {x:2}),(c {x:3}) MERGE (b)-[d:bar]->(c) RETURN d $$) as (v0 agtype);

SELECT * from cypher('cypher', $$ MATCH ()-[a:foo]->(y), (b {x:2}),(c {x:3}) MERGE (b)-[d:bar]->(c) RETURN d $$) as (v0 agtype);
