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

SELECT create_graph('map_proj');

SELECT * FROM cypher('map_proj',
$$
    CREATE
        (tom:Actor {name:'Tom Hanks', age:60}),
        (bale:Actor {name:'Christian Bale', age:50}),
        (tom)-[:ACTED_IN {during: 1990}]->(:Movie {title:'Forrest Gump'}),
        (tom)-[:ACTED_IN {during: 1995}]->(:Movie {title:'Finch'}),
        (tom)-[:ACTED_IN {during: 1999}]->(:Movie {title:'The Circle'}),
        (bale)-[:ACTED_IN {during: 2002}]->(:Movie {title:'The Prestige'}),
        (bale)-[:ACTED_IN {during: 2008}]->(:Movie {title:'The Dark Knight'})
$$) as (a agtype);

-- all property selection
SELECT * FROM cypher('map_proj', $$ WITH {name:'Bob', age:50} AS map RETURN map { .* } $$) as (a agtype);
-- property selector
SELECT * FROM cypher('map_proj', $$ WITH {name:'Bob', age:50} AS map RETURN map { .name } $$) as (a agtype);
-- literal entry
SELECT * FROM cypher('map_proj', $$ WITH {name:'Bob', age:50} AS map RETURN map { name:'Tom' } $$) as (a agtype);
-- variable selector
SELECT * FROM cypher('map_proj', $$ WITH {name:'Bob', age:50} AS map, 'Tom' as name RETURN map { name } $$) as (a agtype);
-- duplicate all property selector
SELECT * FROM cypher('map_proj', $$ WITH {name:'Bob', age:50} AS map RETURN map { .*, .* } $$) as (a agtype);
-- name being selected twice
SELECT * FROM cypher('map_proj', $$ WITH {name:'Bob', age:50} AS map RETURN map { .name, .* } $$) as (a agtype);
SELECT * FROM cypher('map_proj', $$ WITH {name:'Bob', age:50} AS map RETURN map { .name, .name } $$) as (a agtype);
-- name being selected twice with different value
SELECT * FROM cypher('map_proj', $$ WITH {name:'Bob', age:50} AS map RETURN map { name:'Tom', .* } $$) as (a agtype);
SELECT * FROM cypher('map_proj', $$ WITH {name:'Bob', age:50} AS map, 'Tom' as name RETURN map { name, .* } $$) as (a agtype);
-- new entry added
SELECT * FROM cypher('map_proj', $$ WITH {name:'Bob', age:50} AS map RETURN map { .name, .age, height:180 } $$) as (a agtype);
-- NULL as a map
SELECT * FROM cypher('map_proj', $$ WITH NULL AS map RETURN map { .name } $$) as (a agtype);
-- vertex as a map
SELECT * FROM cypher('map_proj', $$ MATCH (n:Actor) RETURN n { .name } $$) as (a agtype);
-- edge as a map
SELECT * FROM cypher('map_proj', $$ MATCH ()-[e:ACTED_IN]->() RETURN e { .during } $$) as (a agtype);
-- syntax error
SELECT * FROM cypher('map_proj', $$ WITH 12 AS map RETURN map { .name } $$) as (a agtype);
SELECT * FROM cypher('map_proj', $$ WITH [] AS map RETURN map { .name } $$) as (a agtype);
SELECT * FROM cypher('map_proj', $$ WITH {name:'Bob'} AS map RETURN map { 'name' } $$) as (a agtype);
-- advanced
SELECT * FROM cypher('map_proj',
$$
    MATCH (a:Actor)-[:ACTED_IN]->(m:Movie)
    WITH a, collect(m { .title }) AS movies
    RETURN collect(a { .name, movies })
$$) as (a agtype);

-- drop
SELECT drop_graph('map_proj', true);
