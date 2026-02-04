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

--
-- AGTYPE data type regression tests
--
SET search_path TO ag_catalog;

-- Agtype Hash Comparison Function
-- Result value varies depending on architecture endianness.
-- Little endian output is in agtype_hash_cmp.out; big endian in agtype_hash_cmp_1.out.
SELECT agtype_hash_cmp(NULL);
SELECT agtype_hash_cmp('1'::agtype);
SELECT agtype_hash_cmp('1.0'::agtype);
SELECT agtype_hash_cmp('"1"'::agtype);
SELECT agtype_hash_cmp('[1]'::agtype);
SELECT agtype_hash_cmp('[1, 1]'::agtype);
SELECT agtype_hash_cmp('[1, 1, 1]'::agtype);
SELECT agtype_hash_cmp('[1, 1, 1, 1]'::agtype);
SELECT agtype_hash_cmp('[1, 1, 1, 1, 1]'::agtype);
SELECT agtype_hash_cmp('[[1]]'::agtype);
SELECT agtype_hash_cmp('[[1, 1]]'::agtype);
SELECT agtype_hash_cmp('[[1], 1]'::agtype);
SELECT agtype_hash_cmp('[1543872]'::agtype);
SELECT agtype_hash_cmp('[1, "abcde", 2.0]'::agtype);
SELECT agtype_hash_cmp(agtype_in('null'));
SELECT agtype_hash_cmp(agtype_in('[null]'));
SELECT agtype_hash_cmp(agtype_in('[null, null]'));
SELECT agtype_hash_cmp(agtype_in('[null, null, null]'));
SELECT agtype_hash_cmp(agtype_in('[null, null, null, null]'));
SELECT agtype_hash_cmp(agtype_in('[null, null, null, null, null]'));
SELECT agtype_hash_cmp('{"id":1, "label":"test", "properties":{"id":100}}'::agtype);
SELECT agtype_hash_cmp('{"id":1, "label":"test", "properties":{"id":100}}::vertex'::agtype);

SELECT agtype_hash_cmp('{"id":2, "start_id":1, "end_id": 3, "label":"elabel", "properties":{}}'::agtype);
SELECT agtype_hash_cmp('{"id":2, "start_id":1, "end_id": 3, "label":"elabel", "properties":{}}::edge'::agtype);

SELECT agtype_hash_cmp('
	[{"id":1, "label":"test", "properties":{"id":100}}::vertex,
	 {"id":2, "start_id":1, "end_id": 3, "label":"elabel", "properties":{}}::edge,
	 {"id":5, "label":"vlabel", "properties":{}}::vertex]'::agtype);

SELECT agtype_hash_cmp('
	[{"id":1, "label":"test", "properties":{"id":100}}::vertex,
	 {"id":2, "start_id":1, "end_id": 3, "label":"elabel", "properties":{}}::edge,
	 {"id":5, "label":"vlabel", "properties":{}}::vertex]::path'::agtype);
