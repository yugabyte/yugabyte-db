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

 \! cp -r regress/age_load/data regress/instance/data/age_load

LOAD 'age';
SET search_path TO ag_catalog;

--
-- Load data
--
SELECT create_graph('cypher_with');

SELECT create_vlabel('cypher_with','Country');
SELECT load_labels_from_file('cypher_with', 'Country',
    'age_load/countries.csv');

SELECT create_vlabel('cypher_with','City');
SELECT load_labels_from_file('cypher_with', 'City',
    'age_load/cities.csv');

SELECT create_elabel('cypher_with','has_city');
SELECT load_edges_from_file('cypher_with', 'has_city',
     'age_load/edges.csv');

--
-- Test WITH clause
--

SELECT count(*) FROM cypher('cypher_with', $$
    MATCH (m:City)-[:has_city]->(b:Country {iso2 : 'AT'}) 
    WITH m,b
    RETURN m,b
$$) AS (City agtype, Country agtype);

-- WITH/AS

SELECT * FROM cypher('cypher_with', $$
    MATCH (b:Country {iso2 : 'AT'}) 
    WITH b.name AS name, id(b) AS id 
    RETURN name,id
$$) AS (Country agtype, Country_id agtype);

SELECT * FROM cypher('cypher_with', $$
    MATCH (m:City {name: 'Zell'})-[]-(b:Country {iso2 : 'AT'})
    WITH b as country, count(*) AS foaf
    WHERE foaf > 1      
    RETURN country.name, foaf
$$) as (name agtype, foaf agtype);

SELECT * FROM cypher('cypher_with', $$
    WITH true AS b
    RETURN b
$$) AS (b bool);

-- WITH/WHERE

SELECT * FROM cypher('cypher_with', $$
    MATCH (m:City)-[:has_city]->(b:Country{iso2:'BE'})
    WITH b,m 
    WHERE m.name='x'
    RETURN m.name,b.iso2
$$) AS ( "m.name" agtype, "b" agtype);

SELECT * FROM cypher('cypher_with', $$
    MATCH (b:Country {iso2 : 'AT'}) 
    WITH b.name AS name, id(b) AS id
    WHERE name = 'Austria'
    RETURN name,id
$$) AS (Country agtype, Country_id agtype);

SELECT * FROM cypher('cypher_with', $$
    MATCH (b:Country {iso2 : 'AT'}) 
    WITH b.name AS name, id(b) AS id
    WHERE name = 'Austria' OR name = 'Kosovo'
    RETURN name,id                             
$$) AS (Country agtype, Country_id agtype);

SELECT * FROM cypher('cypher_with', $$
    MATCH p = (m:City)-[:has_city*1..2]->(b:Country {iso2 : 'AT'}) 
    WITH p, length(p) AS path_length 
    WHERE path_length > 1 
    RETURN p
$$) AS (pattern agtype);

-- MATCH/WHERE with WITH/WHERE

SELECT * FROM cypher('cypher_with', $$
    MATCH (m:City)-[:has_city]->(b:Country {iso2 : 'AT'})
    WHERE b.name = 'Austria'
    WITH m.name AS city,b.name AS country
    WHERE city = 'Vienna'
    RETURN city,country
$$) AS (City agtype, Country agtype);

-- WITH/ORDER BY

SELECT * FROM cypher('cypher_with', $$
    MATCH (m:City)-[:has_city]->(b:Country {iso2 : 'AT'})
    WITH m AS city,b AS country
    ORDER BY id(m) DESC LIMIT 10
    RETURN id(city),city
$$) AS (id agtype, city agtype);

SELECT * FROM cypher('cypher_with', $$
    MATCH (m:City) 
    WITH m AS city
    ORDER BY id(m) ASC LIMIT 10
    RETURN id(city),city.name
$$) AS (id agtype, names agtype);

-- WITH/ORDER BY/DESC/WHERE

SELECT * FROM cypher('cypher_with', $$
    MATCH (m:City)-[:has_city]->(b:Country {iso2 : 'AT'})
    WITH m AS city,b AS country
    ORDER BY id(m) DESC LIMIT 10
    WHERE city.name = 'Zell' OR city.name = 'Umberg'
    RETURN id(city),city.name,country.name
$$) AS (id agtype, city agtype, country agtype);

-- multiple WITH clauses

SELECT * FROM cypher('cypher_with', $$
    MATCH (m:City)-[:has_city]->(b:Country {iso2 : 'AT'})
    WITH m AS city,b AS country     
    WITH city LIMIT 10
    RETURN city.name
$$) AS (city agtype);

SELECT * FROM cypher('cypher_with', $$
    MATCH (m:City)-[:has_city]->(b:Country {iso2 : 'AT'})
    WITH m AS city,b AS country
    ORDER BY id(m) DESC LIMIT 10
    WITH city
    WHERE city.name = 'Zell'
    RETURN id(city),city.name
$$) AS (id agtype, city agtype);

SELECT * FROM cypher('cypher_with', $$
    MATCH (m:City)-[:has_city]->(b:Country {iso2 : 'AT'})
    WITH m AS city,b AS country
    WHERE country.name = 'Austria'
    WITH city
    ORDER BY id(city) DESC
    WHERE city.name = 'Zell'
    RETURN id(city),city.name
$$) AS (id agtype, city agtype);

-- Expression item must be aliased.

SELECT * FROM cypher('cypher_with', $$
    WITH 1 + 1
    RETURN i
$$) AS (i int);

SELECT * FROM cypher('cypher_with', $$
    MATCH (m:City)-[:has_city]->(b:Country {iso2 : 'AT'})
    WITH id(m)
    RETURN m
$$) AS (id agtype, city agtype);

-- Reference undefined variable in WITH clause (should error out)

SELECT count(*) FROM cypher('cypher_with', $$
    MATCH (m:City)-[:has_city]->(b:Country {iso2 : 'AT'}) 
    WITH m  
    RETURN m,b
$$) AS (City agtype, Country agtype);

SELECT * FROM cypher('cypher_with', $$
    MATCH (m:City)-[:has_city]->(b:Country {iso2 : 'AT'})
    WITH m AS city,b AS country
    WHERE country.name = 'Austria'
    WITH city
    WHERE city.name = 'Zell'
    RETURN id(city),country.name
$$) AS (id agtype, country agtype);

-- Clean up

SELECT drop_graph('cypher_with', true);

-- Issue 329 (should error out)

SELECT create_graph('graph');

SELECT * FROM cypher('graph', $$
    CREATE (a:A)-[:incs]->(:C), (a)-[:incs]->(:C)
    RETURN a
$$) AS (n agtype);

SELECT * FROM cypher('graph', $$
    MATCH (a:A) 
    WHERE ID(a)=0 
    WITH a 
    OPTIONAL MATCH (a)-[:incs]->(c)-[d:incs]-() 
    WITH a,c,COUNT(d) AS deps 
    WHERE deps<=1 
    RETURN c,d
$$) AS (n agtype, d agtype);

-- Issue 396 (should error out)

SELECT * FROM cypher('graph',$$
     CREATE (v),(u),(w),
            (v)-[:hasFriend]->(u),
            (u)-[:hasFriend]->(w)
$$) as (a agtype);

SELECT * FROM cypher('graph',$$
      MATCH p=(v)-[*1..2]->(u) 
      WITH p,length(p) AS path_length 
      RETURN v,path_length
$$) as (a agtype,b agtype);

-- Clean up

SELECT drop_graph('graph', true);

--
-- End of test
--
