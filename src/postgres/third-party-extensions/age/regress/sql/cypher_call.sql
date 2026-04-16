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



SELECT create_graph('cypher_call');

SELECT * FROM cypher('cypher_call', $$CREATE ({n: 'a'})$$) as (a agtype);
SELECT * FROM cypher('cypher_call', $$CREATE ({n: 'b'})$$) as (a agtype);

CREATE SCHEMA call_stmt_test;

CREATE FUNCTION call_stmt_test.add_agtype(agtype, agtype) RETURNS agtype
    AS 'select $1 + $2;'
    LANGUAGE SQL
    IMMUTABLE
    RETURNS NULL ON NULL INPUT;

/*
 * CALL (solo)
 */
SELECT * FROM cypher('cypher_call', $$CALL sqrt(64)$$) as (sqrt agtype);
/*  CALL RETURN, should fail */
SELECT * FROM cypher('cypher_call', $$CALL sqrt(64) RETURN sqrt$$) as (sqrt agtype); 
/* CALL YIELD */
SELECT * FROM cypher('cypher_call', $$CALL sqrt(64) YIELD sqrt$$) as (sqrt agtype);
/* incorrect variable should fail */
SELECT * FROM cypher('cypher_call', $$CALL sqrt(64) YIELD squirt$$) as (sqrt agtype);

/* qualified name */
SELECT * FROM cypher('cypher_call', $$CALL call_stmt_test.add_agtype(1,2)$$) as (sqrt agtype);
/* nonexistent schema should fail */
SELECT * FROM cypher('cypher_call', $$CALL ag_catalog.add_agtype(1,2)$$) as (sqrt agtype);

/* CALL YIELD WHERE, should fail */
SELECT * FROM cypher('cypher_call', $$CALL sqrt(64) YIELD sqrt WHERE sqrt > 1$$) as (sqrt agtype);

/*
 * subquery
 */

 /* CALL YIELD UPDATE/RETURN */
SELECT * FROM cypher('cypher_call', $$ CALL sqrt(64) YIELD sqrt RETURN sqrt $$) as (sqrt agtype);
/* Unrecognized YIELD, correct RETURN, should fail*/
SELECT * FROM cypher('cypher_call', $$ CALL sqrt(64) YIELD squirt RETURN sqrt $$) as (sqrt agtype);

/* CALL YIELD WHERE RETURN */
SELECT * FROM cypher('cypher_call', $$CALL sqrt(64) YIELD sqrt WHERE sqrt > 1 RETURN sqrt $$) as (a agtype);
SELECT * FROM cypher('cypher_call', $$CALL sqrt(64) YIELD sqrt WHERE sqrt = 1 RETURN sqrt $$) as (a agtype);
SELECT * FROM cypher('cypher_call', $$CALL sqrt(64) YIELD sqrt WHERE sqrt = 8 RETURN sqrt $$) as (a agtype);
/* should fail */
SELECT * FROM cypher('cypher_call', $$CALL sqrt(64) YIELD sqrt WHERE a = 8 RETURN sqrt $$) as (a agtype);

/* MATCH CALL RETURN, should fail */
SELECT * FROM cypher('cypher_call', $$ MATCH (a) CALL sqrt(64) RETURN sqrt $$) as (sqrt agtype);

/* MATCH CALL YIELD RETURN */
SELECT * FROM cypher('cypher_call', $$ MATCH (a) CALL sqrt(64) YIELD sqrt RETURN a, sqrt $$) as (a agtype, sqrt agtype);

/* MATCH CALL YIELD WHERE UPDATE/RETURN */
SELECT * FROM cypher('cypher_call', $$ CALL sqrt(64) YIELD sqrt WHERE sqrt > 1 CREATE ({n:'c'}) $$) as (a agtype);
SELECT * FROM cypher('cypher_call', $$ MATCH (a) CALL sqrt(64) YIELD sqrt WHERE a.n = 'c' DELETE (a) $$) as (a agtype);
SELECT * FROM cypher('cypher_call', $$ MATCH (a) CALL sqrt(64) YIELD sqrt WHERE a.n = 'c' RETURN a $$) as (a agtype);
SELECT * FROM cypher('cypher_call', $$ MATCH (a) CALL sqrt(64) YIELD sqrt WHERE sqrt = 1 RETURN a, sqrt $$) as (a agtype, sqrt agtype);
SELECT * FROM cypher('cypher_call', $$ MATCH (a) CALL sqrt(64) YIELD sqrt WHERE sqrt = 8 RETURN a, sqrt $$) as (a agtype, sqrt agtype);
SELECT * FROM cypher('cypher_call', $$ MATCH (a) CALL sqrt(64) YIELD sqrt WHERE b = 8 RETURN a, sqrt $$) as (a agtype, sqrt agtype);

/* CALL MATCH YIELD WHERE UPDATE/RETURN */
SELECT * FROM cypher('cypher_call', $$ CALL sqrt(64) YIELD sqrt WHERE sqrt > 1 CREATE ({n:'c'}) $$) as (a agtype);
SELECT * FROM cypher('cypher_call', $$ CALL sqrt(64) YIELD sqrt MATCH (a) WHERE a.n = 'c' DELETE (a) $$) as (a agtype);
SELECT * FROM cypher('cypher_call', $$ CALL sqrt(64) YIELD sqrt MATCH (a) WHERE a.n = 'c' RETURN a $$) as (a agtype);
SELECT * FROM cypher('cypher_call', $$ CALL sqrt(64) YIELD sqrt MATCH (a) WHERE sqrt = 1 RETURN a, sqrt $$) as (a agtype, sqrt agtype);
SELECT * FROM cypher('cypher_call', $$ CALL sqrt(64) YIELD sqrt MATCH (a) WHERE sqrt = 8 RETURN a, sqrt $$) as (a agtype, sqrt agtype);
SELECT * FROM cypher('cypher_call', $$ CALL sqrt(64) YIELD sqrt WHERE sqrt = 8 MATCH (a) RETURN a, sqrt $$) as (a agtype, sqrt agtype);

/* Multiple Calls: CALL YIELD CALL YIELD... RETURN */
SELECT * FROM cypher('cypher_call', $$ CALL sqrt(64) YIELD sqrt CALL agtype_sum(2,2) YIELD agtype_sum RETURN sqrt, agtype_sum $$) as (sqrt agtype, agtype_sum agtype);
/* should fail */
SELECT * FROM cypher('cypher_call', $$ CALL sqrt(64) YIELD sqrt CALL sqrt(81) YIELD sqrt RETURN sqrt, sqrt $$) as (a agtype, b agtype);

/* Aliasing: CALL YIELD AS CALL YIELD AS ... UPDATE/RETURN */
SELECT * FROM cypher('cypher_call', $$ CALL sqrt(64) YIELD sqrt CALL sqrt(81) YIELD sqrt AS sqrt1 RETURN sqrt, sqrt1 $$) as (sqrt agtype, sqrt1 agtype);
SELECT * FROM cypher('cypher_call', $$ CALL sqrt(64) YIELD sqrt AS sqrt1 CALL sqrt(81) YIELD sqrt RETURN sqrt, sqrt1 $$) as (sqrt agtype, sqrt1 agtype);
/* duplicated alias should fail */
SELECT * FROM cypher('cypher_call', $$ CALL sqrt(64) YIELD sqrt AS sqrt1 CALL sqrt(81) YIELD sqrt AS sqrt1 RETURN sqrt1, sqrt1 $$) as (a agtype, b agtype);
SELECT * FROM cypher('cypher_call', $$ CALL sqrt(64) YIELD sqrt CALL agtype_sum(2,2) YIELD agtype_sum AS sqrt RETURN sqrt, sqrt $$) as (a agtype, b agtype);

DROP SCHEMA call_stmt_test CASCADE;
SELECT drop_graph('cypher_call', true);
