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
SET search_path=ag_catalog;

SELECT create_graph('graph');

-- Should error out
SELECT * FROM cypher('graph', $$ RETURN show_trgm("hello") $$) AS (n agtype);

-- Create the extension in the public schema
CREATE EXTENSION pg_trgm SCHEMA public;

-- Should error out
SELECT * FROM cypher('graph', $$ RETURN show_trgm("hello") $$) AS (n agtype);

-- Should work
SET search_path=ag_catalog, public;
SELECT * FROM cypher('graph', $$ CREATE (:Person {name: "Jane"}),
                                        (:Person {name: "John"}),
                                        (:Person {name: "Jone"}),
                                        (:Person {name: "Jack"}),
                                        (:Person {name: "Jax"}),
                                        (:Person {name: "Jake"}),
                                        (:Person {name: "Julie"}),
                                        (:Person {name: "Julius"}),
                                        (:Person {name: "Jill"}),
                                        (:Person {name: "Jillie"}),
                                        (:Person {name: "Julian"})
$$) AS (n agtype);
SELECT * FROM cypher('graph', $$ MATCH (p) return show_trgm(p.name) $$) AS (n text[]);
SELECT * FROM cypher('graph', $$ MATCH (p) with p, similarity(p.name, "Jon") as sim return p.name, sim ORDER BY sim DESC $$) AS (n agtype, s real);
SELECT * FROM cypher('graph', $$ MATCH (p) with p, word_similarity(p.name, "Jon") as sim return p.name, sim ORDER BY sim DESC $$) AS (n agtype, s real);

-- Clean up
SELECT drop_graph('graph', true);
DROP EXTENSION pg_trgm CASCADE;