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
-- jsonb operators in AGE (?, ?&, ?|, ->, ->>, #>, #>>, ||)
--

--
-- Agtype exists operator
--

-- exists (?)

-- should return 't'
SELECT '{"n":null,"a":1,"b":[1,2],"c":{"1":2},"d":{"1":[2,3]}}'::agtype ? '"n"';
SELECT '{"n":null,"a":1,"b":[1,2],"c":{"1":2},"d":{"1":[2,3]}}'::agtype ? '"a"';
SELECT '{"n":null,"a":1,"b":[1,2],"c":{"1":2},"d":{"1":[2,3]}}'::agtype ? '"b"';
SELECT '{"n":null,"a":1,"b":[1,2],"c":{"1":2},"d":{"1":[2,3]}}'::agtype ? '"d"';
SELECT '{"id": 281474976710658, "label": "", "properties": {"n": 100}}'::agtype ? '"label"';
SELECT '{"id": 281474976710658, "label": "", "properties": {"n": 100}}::vertex'::agtype ? '"n"';
SELECT '["1","2"]'::agtype ? '"1"';
SELECT '["hello", "world"]'::agtype ? '"hello"';
SELECT agtype_exists('{"id": 1}','id'::text);
SELECT '{"id": 1}'::agtype ? 'id'::text;

-- should return 'f'
SELECT '{"n":null,"a":1,"b":[1,2],"c":{"1":2},"d":{"1":[2,3]}}'::agtype ? '"e"';
SELECT '{"n":null,"a":1,"b":[1,2],"c":{"1":2},"d":{"1":[2,3]}}'::agtype ? '"e1"';
SELECT '{"n":null,"a":1,"b":[1,2],"c":{"1":2},"d":{"1":[2,3]}}'::agtype ? '"1"';
SELECT '{"n":null,"a":1,"b":[1,2],"c":{"1":2},"d":{"1":[2,3]}}'::agtype ? '1';
SELECT '[{"id": 281474976710658, "label": "", "properties": {"n": 100}}]'::agtype ? '"id"';
SELECT '[{"id": 281474976710658, "label": "", "properties": {"n": 100}}]'::agtype ? 'null';
SELECT '{"id": 281474976710658, "label": "", "properties": {"n": 100}}'::agtype ? 'null';
SELECT '{"id": 281474976710658, "label": "", "properties": {"n": 100}}::vertex'::agtype ? 'null';
SELECT '["hello", "world"]'::agtype ? '"hell"';
SELECT agtype_exists('{"id": 1}','not_id'::text);
SELECT '{"id": 1}'::agtype ? 'not_id'::text;
SELECT '{"n":null,"a":1,"b":[1,2],"c":{"1":2},"d":{"1":[2,3]}}'::agtype ? '["e1", "n"]';
SELECT '{"n":null,"a":1,"b":[1,2],"c":{"1":2},"d":{"1":[2,3]}}'::agtype ? '["n"]';
SELECT '{"n":null,"a":1,"b":[1,2],"c":{"1":2},"d":{"1":[2,3]}}'::agtype ? '["n", "a", "e"]';
SELECT '{"n":null,"a":1,"b":[1,2],"c":{"1":2},"d":{"1":[2,3]}}'::agtype ? '{"n": null}';
SELECT '{"n":null,"a":1,"b":[1,2],"c":{"1":2},"d":{"1":[2,3]}}'::agtype ? '{"n": null, "b": true}';
SELECT '{"n":null,"a":1,"b":[1,2],"c":{"1":2},"d":{"1":[2,3]}}'::agtype ? '["e1"]';

-- errors out
SELECT '{"n":null,"a":1,"b":[1,2],"c":{"1":2},"d":{"1":[2,3]}}'::agtype ? 'e1';
SELECT '{"n":null,"a":1,"b":[1,2],"c":{"1":2},"d":{"1":[2,3]}}'::agtype ? 'e';

-- Exists any (?|)

-- should return 't'
SELECT '{"a":null, "b":"qq"}'::agtype ?| '["a","b"]';
SELECT '{"a":null, "b":"qq"}'::agtype ?| '["b","a"]';
SELECT '{"a":null, "b":"qq"}'::agtype ?| '["c","a"]';
SELECT '{"1":null, "b":"qq"}'::agtype ?| '["c","1"]';
SELECT '{"a":null, "b":"qq"}'::agtype ?| '["a","a", "b", "b", "b"]'::agtype;
SELECT '[1,2,3]'::agtype ?| '[1,2,3,4]';
SELECT '{"id": 281474976710658, "label": "", "properties": {"n": 100}}'::agtype ?| '[null,"id"]'::agtype;
SELECT '{"id": 281474976710658, "label": "", "properties": {"n": 100}}'::agtype ?| '["id",null]'::agtype;
SELECT '{"id": 281474976710658, "label": "", "properties": {"n": 100}}'::agtype ?| '[true,"id"]'::agtype;
SELECT '{"id": 281474976710658, "label": "", "properties": {"n": 100}}'::agtype ?| '[1,"id"]'::agtype;
SELECT '{"id": 1688849860263937, "label": "EDGE", "end_id": 1970324836974593, "start_id": 1407374883553281, "properties": {"n": 100}}::edge'::agtype ?| '[null,null,"n"]'::agtype;
SELECT '{"id": 1688849860263937, "label": "EDGE", "end_id": 1970324836974593, "start_id": 1407374883553281, "properties": {"n": 100}}::edge'::agtype ?| '["n",null]'::agtype;
SELECT agtype_exists_any('{"id": 1}', array['id']);
SELECT '{"id": 1}'::agtype ?| array['id'];

-- should return 'f'
SELECT '{"a":null, "b":"qq"}'::agtype ?| '["c","d"]';
SELECT '{"a":null, "b":"qq"}'::agtype ?| '["1","2"]';
SELECT '{"a":null, "b":"qq"}'::agtype ?| '["c","1"]';
SELECT '{"a":null, "b":"qq"}'::agtype ?| '[]';
SELECT '{"a":null, "b":"qq"}'::agtype ?| '["c","d"]'::agtype;
SELECT '{"id": 281474976710658, "label": "", "properties": {"n": 100}}'::agtype ?| '[null]';
SELECT '{"id": 281474976710658, "label": "", "properties": {"n": 100}}'::agtype ?| '[]';
SELECT '{"id": 281474976710658, "label": "", "properties": {"n": 100}}'::agtype ?| '[null,null]'::agtype;
SELECT '{"id": 281474976710658, "label": "", "properties": {"n": 100}}'::agtype ?| '[null, "idk"]';
SELECT '{"id": 281474976710658, "label": "", "properties": {"n": 100}}'::agtype ?| '[""]';
SELECT '{"id": 281474976710658, "label": "", "properties": {"n": 100}}::vertex' ?| '[null,"idk"]'::agtype;
SELECT '{"id": 281474976710658, "label": "", "properties": {"n": 100}}::vertex'::agtype ?| '[null,null,"idk"]'::agtype;
SELECT '{"id": 281474976710658, "label": "", "properties": {"n": 100}}::vertex'::agtype ?| '["idk",null]'::agtype;
SELECT '{"id": 1688849860263937, "label": "EDGE", "end_id": 1970324836974593, "start_id": 1407374883553281, "properties": {"n": 100}}::edge'::agtype ?| '[null]';
SELECT '{"id": 1688849860263937, "label": "EDGE", "end_id": 1970324836974593, "start_id": 1407374883553281, "properties": {"n": 100}}::edge'::agtype ?| '[]';
SELECT '{"id": 1688849860263937, "label": "EDGE", "end_id": 1970324836974593, "start_id": 1407374883553281, "properties": {"n": 100}}::edge'::agtype ?| '[null,null]'::agtype;
SELECT '{"id": 1688849860263937, "label": "EDGE", "end_id": 1970324836974593, "start_id": 1407374883553281, "properties": {"n": 100}}::edge'::agtype ?| '[[""]]';
SELECT '{"id": 1688849860263937, "label": "EDGE", "end_id": 1970324836974593, "start_id": 1407374883553281, "properties": {"n": 100}}::edge'::agtype ?| '[null,null, "idk"]'::agtype;
SELECT '{"id": 1688849860263937, "label": "EDGE", "end_id": 1970324836974593, "start_id": 1407374883553281, "properties": {"n": 100}}::edge'::agtype ?| '[null,null,"idk"]'::agtype;
SELECT '{"id": 1688849860263937, "label": "EDGE", "end_id": 1970324836974593, "start_id": 1407374883553281, "properties": {"n": 100}}::edge'::agtype ?| '["start_idk",null]'::agtype;
SELECT '{"a":null, "b":"qq"}'::agtype ?| '[["a"], ["b"]]';
SELECT '{"a":null, "b":"qq"}'::agtype ?| '[["a"], ["b"], ["c"]]';
SELECT '[null]'::agtype ?| '[null]'::agtype;
SELECT agtype_exists_any('{"id": 1}', array['not_id']);
SELECT '{"id": 1}'::agtype ?| array['not_id'];

-- errors out
SELECT '{"n":null,"a":1,"b":[1,2],"c":{"1":2},"d":{"1":[2,3]}}'::agtype ?| '"b"';
SELECT '{"n":null,"a":1,"b":[1,2],"c":{"1":2},"d":{"1":[2,3]}}'::agtype ?| '"d"';
SELECT '{"a":null, "b":"qq"}'::agtype ?| '{"a", "b"}';
SELECT '{"a":null, "b":"qq"}'::agtype ?| '';
SELECT '{"a":null, "b":"qq"}'::agtype ?| '""';
SELECT '{"a":null, "b":"qq"}'::agtype ?| '{""}';
SELECT '{"a":null, "b":"qq"}'::agtype ?| '{}';
SELECT '{"a":null, "b":"qq"}'::agtype ?| '0'::agtype;
SELECT '{"a":null, "b":"qq"}'::agtype ?| '0';

-- Exists all (?&)

-- should return 't'
SELECT '{"a":null, "b":"qq"}'::agtype ?& '["a","b"]';
SELECT '{"a":null, "b":"qq"}'::agtype ?& '["b","a"]';
SELECT '{"a":null, "b":"qq"}'::agtype ?& '["a","a", "b", "b", "b"]'::agtype;
SELECT '{"id": 281474976710658, "label": "", "properties": {"n": 100}}'::agtype ?& '[null]';
SELECT '{"id": 281474976710658, "label": "", "properties": {"n": 100}}'::agtype ?& '[]';
SELECT '{"id": 281474976710658, "label": "", "properties": {"n": 100}}'::agtype ?& '[null,null]'::agtype;
SELECT '{"id": 281474976710658, "label": "", "properties": {"n": 100}}'::agtype ?& '[null,null,"id"]'::agtype;
SELECT '{"id": 281474976710658, "label": "", "properties": {"n": 100}}'::agtype ?& '["id",null]'::agtype;
SELECT '{"id": 281474976710658, "label": "", "properties": {"n": 100}}::vertex'::agtype ?& '[null]'::agtype;
SELECT '{"id": 281474976710658, "label": "", "properties": {"n": 100}}::vertex'::agtype ?& '[]';
SELECT '{"id": 281474976710658, "label": "", "properties": {"n": 100}}::vertex'::agtype ?& '[null,null]'::agtype;
SELECT '{"id": 281474976710658, "label": "", "properties": {"n": 100}}::vertex'::agtype ?& '[null,null,"n"]'::agtype;
SELECT '{"id": 281474976710658, "label": "", "properties": {"n": 100}}::vertex'::agtype ?& '["n",null]'::agtype;
SELECT '{"id": 1688849860263937, "label": "EDGE", "end_id": 1970324836974593, "start_id": 1407374883553281, "properties": {"n": 100}}::edge'::agtype ?& '[null]';
SELECT '{"id": 1688849860263937, "label": "EDGE", "end_id": 1970324836974593, "start_id": 1407374883553281, "properties": {"n": 100}}::edge'::agtype ?& '[]';
SELECT '{"id": 1688849860263937, "label": "EDGE", "end_id": 1970324836974593, "start_id": 1407374883553281, "properties": {"n": 100}}::edge'::agtype ?& '[null,null]'::agtype;
SELECT '{"id": 1688849860263937, "label": "EDGE", "end_id": 1970324836974593, "start_id": 1407374883553281, "properties": {"n": 100}}::edge'::agtype ?& '[null,null,"n"]'::agtype;
SELECT '{"id": 1688849860263937, "label": "EDGE", "end_id": 1970324836974593, "start_id": 1407374883553281, "properties": {"n": 100}}::edge'::agtype ?& '["n",null]'::agtype;
SELECT '[1,2,3]'::agtype ?& '[1,2,3]';
SELECT '[1,2,3]'::agtype ?& '[1,2,3,null]';
SELECT '[1,2,3]'::agtype ?& '[null, null]';
SELECT '[1,2,3]'::agtype ?& '[null, null, null]';
SELECT '[1,2,3]'::agtype ?& '[]';
SELECT '{"a":null, "b":"qq"}'::agtype ?& '[]';
SELECT '[null]'::agtype ?& '[null]'::agtype;
SELECT agtype_exists_all('{"id": 1}', array['id']);
SELECT '{"id": 1}'::agtype ?& array['id'];

-- should return 'f'
SELECT '{"a":null, "b":"qq"}'::agtype ?& '["c","a"]';
SELECT '{"a":null, "b":"qq"}'::agtype ?& '["a","b", "c"]';
SELECT '{"a":null, "b":"qq"}'::agtype ?& '["c","d"]'::agtype;
SELECT '[1,2,3]'::agtype ?& '[1,2,3,4]';
SELECT '{"a":null, "b":"qq"}'::agtype ?& '[["a"]]';
SELECT '{"a":null, "b":"qq"}'::agtype ?& '[["a"], ["b"]]';
SELECT '{"a":null, "b":"qq"}'::agtype ?& '[["a"], ["b"], ["c"]]';
SELECT '{"id": 281474976710658, "label": "", "properties": {"n": 100}}'::agtype ?& '[null, "idk"]';
SELECT '{"id": 281474976710658, "label": "", "properties": {"n": 100}}'::agtype ?& '[""]';
SELECT '{"id": 281474976710658, "label": "", "properties": {"n": 100}}::vertex' ?& '[null,"idk"]'::agtype;
SELECT '{"id": 281474976710658, "label": "", "properties": {"n": 100}}::vertex'::agtype ?& '[null,null,"idk"]'::agtype;
SELECT '{"id": 281474976710658, "label": "", "properties": {"n": 100}}::vertex'::agtype ?& '["idk",null]'::agtype;
SELECT '{"id": 281474976710658, "label": "", "properties": {"n": 100}}'::agtype ?& '[1,"id"]'::agtype;
SELECT '{"id": 281474976710658, "label": "", "properties": {"n": 100}}'::agtype ?& '[true,"id"]'::agtype;
SELECT '{"id": 1688849860263937, "label": "EDGE", "end_id": 1970324836974593, "start_id": 1407374883553281, "properties": {"n": 100}}::edge'::agtype ?& '[null, "idk"]';
SELECT '{"id": 1688849860263937, "label": "EDGE", "end_id": 1970324836974593, "start_id": 1407374883553281, "properties": {"n": 100}}::edge'::agtype ?& '[[""]]';
SELECT '{"id": 1688849860263937, "label": "EDGE", "end_id": 1970324836974593, "start_id": 1407374883553281, "properties": {"n": 100}}::edge'::agtype ?& '[null,null, "idk"]'::agtype;
SELECT '{"id": 1688849860263937, "label": "EDGE", "end_id": 1970324836974593, "start_id": 1407374883553281, "properties": {"n": 100}}::edge'::agtype ?& '[null,null,"idk"]'::agtype;
SELECT '{"id": 1688849860263937, "label": "EDGE", "end_id": 1970324836974593, "start_id": 1407374883553281, "properties": {"n": 100}}::edge'::agtype ?& '["start_idk",null]'::agtype;
SELECT '{"a":null, "b":"qq"}'::agtype ?& '[null, "c", "a"]';
SELECT agtype_exists_all('{"id": 1}', array['not_id']);
SELECT '{"id": 1}'::agtype ?& array['not_id'];

-- errors out
SELECT '{"a":null, "b":"qq"}'::agtype ?& '"d"';
SELECT '{"a":null, "b":"qq"}'::agtype ?& '"a"';
SELECT '{"a":null, "b":"qq"}'::agtype ?& '" "';
SELECT '{"a":null, "b":"qq"}'::agtype ?& '""';
SELECT '{"a":null, "b":"qq"}'::agtype ?& '"null"';
SELECT '{"a":null, "b":"qq"}'::agtype ?& '{"a", "b", "c"}';
SELECT '{"a":null, "b":"qq"}'::agtype ?& '{}';
SELECT '{"a":null, "b":"qq"}'::agtype ?& '{""}';
SELECT '{"a":null, "b":"qq"}'::agtype ?& '{null}';
SELECT '{"a":null, "b":"qq"}'::agtype ?& '{"null"}';

--
-- concat || operator
--
SELECT i, pg_typeof(i) FROM (SELECT '[0, 1]'::agtype || '[0, 1]'::agtype as i) a;

SELECT i, pg_typeof(i) FROM (SELECT '2'::agtype || '[0, 1]'::agtype as i) a;
SELECT i, pg_typeof(i) FROM (SELECT '[0, 1]'::agtype || '2'::agtype as i) a;

SELECT i, pg_typeof(i) FROM (SELECT '{"a": 1}'::agtype || '[0, 1]'::agtype as i) a;
SELECT i, pg_typeof(i) FROM (SELECT '[0, 1]'::agtype || '{"a": 1}'::agtype as i) a;

SELECT i, pg_typeof(i) FROM (SELECT '[]'::agtype || '[0, 1]'::agtype as i) a;
SELECT i, pg_typeof(i) FROM (SELECT '[0, 1]'::agtype || '[]'::agtype as i) a;
SELECT i, pg_typeof(i) FROM (SELECT 'null'::agtype || '[0, 1]'::agtype as i) a;
SELECT i, pg_typeof(i) FROM (SELECT '[0, 1]'::agtype || 'null'::agtype as i) a;
SELECT i, pg_typeof(i) FROM (SELECT '[null]'::agtype || '[0, 1]'::agtype as i) a;
SELECT i, pg_typeof(i) FROM (SELECT '[0, 1]'::agtype || '[null]'::agtype as i) a;

SELECT i, pg_typeof(i) FROM (SELECT NULL || '[0, 1]'::agtype as i) a;
SELECT i, pg_typeof(i) FROM (SELECT '[0, 1]'::agtype || NULL as i) a;

-- both operands are objects
SELECT '{"aa":1 , "b":2, "cq":3}'::agtype || '{"cq":"l", "b":"g", "fg":false}';
SELECT '{"aa":1 , "b":2, "cq":3}'::agtype || '{"aq":"l"}';
SELECT '{"aa":1 , "b":2, "cq":3}'::agtype || '{"aa":"l"}';
SELECT '{"aa":1 , "b":2, "cq":3}'::agtype || '{}';
SELECT '{"aa":1 , "b":2, "cq":3, "cj": {"fg": true}}'::agtype || '{"cq":"l", "b":"g", "fg":false}';
SELECT '{"a": 13}'::agtype || '{"a": 13}'::agtype;
SELECT '{}'::agtype || '{"a":"b"}'::agtype;
SELECT '{}'::agtype || '{}'::agtype;

-- both operands are arrays
SELECT '["a", "b"]'::agtype || '["c"]';
SELECT '["a", "b"]'::agtype || '["c", "d"]';
SELECT '["a", "b"]'::agtype || '["c", "d", "d"]';
SELECT '["c"]' || '["a", "b"]'::agtype;
SELECT '[]'::agtype || '["a"]'::agtype;
SELECT '[]'::agtype || '[]'::agtype;

SELECT '["a", "b"]'::agtype || '"c"';
SELECT '"c"' || '["a", "b"]'::agtype;
SELECT '[]'::agtype || '"a"'::agtype;
SELECT '"b"'::agtype || '"a"'::agtype;
SELECT '3'::agtype || '[]'::agtype;
SELECT '3'::agtype || '4'::agtype;
SELECT '3'::agtype || '[4]';
SELECT '3::numeric'::agtype || '[[]]'::agtype;
SELECT null::agtype || null::agtype;

-- array and object as operands
SELECT '{"aa":1 , "b":2, "cq":3}'::agtype || '[{"aa":"l"}]';
SELECT '{"aa":1 , "b":2, "cq":3}'::agtype || '[{"aa":"l", "aa": "k"}]';
SELECT '{"a": 13}'::agtype || '[{"a": 13}]'::agtype;
SELECT '[]'::agtype || '{"a":"b"}'::agtype;
SELECT '{"a":"b"}'::agtype || '[]'::agtype;
SELECT '[]'::agtype || '{}'::agtype;
SELECT '[3]'::agtype || '{}'::agtype;
SELECT '{}'::agtype || '[null]'::agtype;
SELECT '[null]'::agtype || '{"a": null}'::agtype;
SELECT '""'::agtype || '[]'::agtype;

-- vertex/edge/path as operand(s)
SELECT '{"id": 1688849860263937, "label": "EDGE", "end_id": 1970324836974593, "start_id": 1407374883553281, "properties": {"a": "xyz", "b" : true, "c": -19.888, "e": {"f": "abcdef", "g": {}, "h": [[], {}]}, "i": {"j": 199, "k": {"l": "mnopq"}}}}::edge'::agtype || '"id"';
SELECT '{"id": 1688849860263937, "label": "EDGE", "end_id": 1970324836974593, "start_id": 1407374883553281, "properties": {"a": "xyz", "b" : true, "c": -19.888, "e": {"f": "abcdef", "g": {}, "h": [[], {}]}, "i": {"j": 199, "k": {"l": "mnopq"}}}}::edge'::agtype || '"m"';
SELECT '{"id": 1688849860263937, "label": "EDGE", "end_id": 1970324836974593, "start_id": 1407374883553281, "properties": {"a": "xyz", "b" : true, "c": -19.888, "e": {"f": "abcdef", "g": {}, "h": [[], {}]}, "i": {"j": 199, "k": {"l": "mnopq"}}}}::edge'::agtype || '{"m": []}';
SELECT '{"id": 844424930131969, "label": "v", "properties": {}}::vertex'::agtype || '{"id": 844424930131971, "label": "v", "properties": {"key": "value"}}::vertex'::agtype;
SELECT '{"id": 844424930131969, "label": "v", "properties": {}}::vertex'::agtype || '[]'::agtype;
SELECT '{"id": 844424930131969, "label": "v", "properties": {}}::vertex'::agtype || '{}'::agtype;
SELECT '{}'::agtype || '{"id": 844424930131969, "label": "v", "properties": {}}::vertex'::agtype;
SELECT '"id"'::agtype || '{"id": 844424930131969, "label": "v", "properties": {}}::vertex'::agtype;
SELECT '{"id": 844424930131969, "label": "v", "properties": {}}::vertex'::agtype || '{"id": 1688849860263950, "label": "e_var", "end_id": 281474976710662, "start_id": 281474976710661, "properties": {}}::edge'::agtype;
SELECT '[{"id": 281474976710672, "label": "", "properties": {}}::vertex, {"id": 1688849860263960, "label": "e_var", "end_id": 281474976710673, "start_id": 281474976710672, "properties": {}}::edge, {"id": 281474976710673, "label": "", "properties": {}}::vertex]::path'::agtype || '{"id": 844424930131969, "label": "v", "properties": {}}::vertex'::agtype;
SELECT '[{"id": 281474976710672, "label": "", "properties": {}}::vertex, {"id": 1688849860263960, "label": "e_var", "end_id": 281474976710673, "start_id": 281474976710672, "properties": {}}::edge, {"id": 281474976710673, "label": "", "properties": {}}::vertex]::path'::agtype || '[{"id": 281474976710672, "label": "", "properties": {}}::vertex, {"id": 1688849860263960, "label": "e_var", "end_id": 281474976710673, "start_id": 281474976710672, "properties": {}}::edge, {"id": 281474976710673, "label": "", "properties": {}}::vertex]::path'::agtype;

-- using concat more than once in a query
SELECT '{}'::agtype || '{}'::agtype || '[{}]'::agtype;
SELECT '{"y": {}}'::agtype || '{"b": "5"}'::agtype || '{"a": {}}'::agtype || '{"z": []}'::agtype;
SELECT '{"y": {}}'::agtype || '{"b": "5"}'::agtype || '{"a": {}}'::agtype || '{"z": []}'::agtype || '[]'::agtype;
SELECT '{"y": {}}'::agtype || '{"b": "5"}'::agtype || '{"a": {}}'::agtype || '{"z": []}'::agtype || '[]'::agtype || '{}';
SELECT '"e"'::agtype || '1'::agtype || '{}'::agtype;
SELECT ('"e"'::agtype || '1'::agtype) || '{"[]": "p"}'::agtype;
SELECT '{"{}": {"a": []}}'::agtype || '{"{}": {"[]": []}}'::agtype || '{"{}": {}}'::agtype;
SELECT '{}'::agtype || '{}'::agtype || '[{}]'::agtype || '[{}]'::agtype || '{}'::agtype;

-- should give an error
SELECT '{"a": 13}'::agtype || 'null'::agtype;
SELECT '"a"'::agtype || '{"a":1}';
SELECT '3'::agtype || '{}'::agtype;
SELECT '{"a":1}' || '"a"'::agtype;
SELECT '{"b": [1, 2, {"[{}, {}]": "a"}, {"1": {}}]}'::agtype || true::agtype;
SELECT '{"b": [1, 2, {"[{}, {}]": "a"}, {"1": {}}]}'::agtype || 'true'::agtype;
SELECT '{"b": [1, 2, {"[{}, {}]": "a"}, {"1": {}}]}'::agtype || age_agtype_sum('1', '2');
SELECT ('{"a": "5"}'::agtype || '{"a": {}}'::agtype) || '5'::agtype;
SELECT ('{"a": "5"}'::agtype || '{"a": {}}'::agtype || '5') || '[5]'::agtype;
-- both operands have to be of agtype
SELECT '3'::agtype || 4;
SELECT '3'::agtype || true;

--
-- jsonb operators inside cypher queries
--
SELECT create_graph('jsonb_operators');

SELECT * FROM cypher('jsonb_operators',$$CREATE ({list:['a', 'b', 'c'], json:{a:1, b:['a', 'b'], c:{d:'a'}}})$$) as (a agtype);

/*
 * ?, ?|, ?& key existence operators
 */

-- Exists (?)

-- should return true
SELECT * FROM cypher('jsonb_operators',$$MATCH (n) return n ? 'list' $$) as (a agtype);
SELECT * FROM cypher('jsonb_operators',$$MATCH (n) return n.json ? 'a' $$) as (a agtype);
SELECT * FROM cypher('jsonb_operators',$$MATCH (n) return n.list ? 'c' $$) as (a agtype);
SELECT * FROM cypher('jsonb_operators',$$MATCH (n) return n.json.b ? 'a' $$) as (a agtype);
SELECT * FROM cypher('jsonb_operators',$$MATCH (n) return n.json.c ? 'd' $$) as (a agtype);

-- should return false
SELECT * FROM cypher('jsonb_operators',$$MATCH (n) return n ? 'a' $$) as (a agtype);
SELECT * FROM cypher('jsonb_operators',$$MATCH (n) return n.json ? 'd' $$) as (a agtype);
SELECT * FROM cypher('jsonb_operators',$$MATCH (n) return n.list ? 'd' $$) as (a agtype);
SELECT * FROM cypher('jsonb_operators',$$MATCH (n) return n.json.b ? 'c' $$) as (a agtype);
SELECT * FROM cypher('jsonb_operators',$$MATCH (n) return n.json.c ? 'e' $$) as (a agtype);
SELECT * FROM cypher('jsonb_operators',$$MATCH (n) return n ? [] $$) as (a agtype);
SELECT * FROM cypher('jsonb_operators',$$MATCH (n) return n ? ['d'] $$) as (a agtype);
SELECT * FROM cypher('jsonb_operators',$$MATCH (n) return n ? {d: 'e'} $$) as (a agtype);

-- Exists (?|)

-- should return true
SELECT * FROM cypher('jsonb_operators',$$MATCH (n) return n ?| ['list'] $$) as (a agtype);
SELECT * FROM cypher('jsonb_operators',$$MATCH (n) return n ?| ['list', 'd'] $$) as (a agtype);
SELECT * FROM cypher('jsonb_operators',$$MATCH (n) return n ?| ['json', 'a'] $$) as (a agtype);
SELECT * FROM cypher('jsonb_operators',$$MATCH (n) return n ?| ['list', 'json'] $$) as (a agtype);
SELECT * FROM cypher('jsonb_operators',$$MATCH (n) return n.json.b ?| ['a'] $$) as (a agtype);
SELECT * FROM cypher('jsonb_operators',$$MATCH (n) return n.json.b ?| ['a', 'b'] $$) as (a agtype);
SELECT * FROM cypher('jsonb_operators',$$MATCH (n) return n.json.c ?| ['d'] $$) as (a agtype);
SELECT * FROM cypher('jsonb_operators',$$MATCH (n) return n.json.c ?| ['d', 'e'] $$) as (a agtype);
SELECT * FROM cypher('jsonb_operators',$$MATCH (n) return n ?| keys(n) $$) as (a agtype);
SELECT * FROM cypher('jsonb_operators',$$MATCH (n) return n.json ?| keys(n.json) $$) as (a agtype);
SELECT * FROM cypher('jsonb_operators',$$MATCH (n) return [n.json ?| keys(n.json)] ?| [true] $$) as (a agtype);

-- should return false
SELECT * FROM cypher('jsonb_operators',$$MATCH (n) return n ?| [] $$) as (a agtype);
SELECT * FROM cypher('jsonb_operators',$$MATCH (n) return n ?| ['a', 'b'] $$) as (a agtype);
SELECT * FROM cypher('jsonb_operators',$$MATCH (n) return n.json.b ?| [] $$) as (a agtype);
SELECT * FROM cypher('jsonb_operators',$$MATCH (n) return n.json.c ?| ['c'] $$) as (a agtype);
SELECT * FROM cypher('jsonb_operators',$$MATCH (n) return n ?| [['list']] $$) as (a agtype);
SELECT * FROM cypher('jsonb_operators',$$MATCH (n) return n ?| keys(n.json) $$) as (a agtype);
SELECT * FROM cypher('jsonb_operators',$$MATCH (n) return n.json ?| keys(n) $$) as (a agtype);
SELECT * FROM cypher('jsonb_operators',$$MATCH (n) return [n.json ?| keys(n.json)] ?| [false] $$) as (a agtype);

-- errors out
SELECT * FROM cypher('jsonb_operators',$$MATCH (n) return n ?| 'list' $$) as (a agtype);
SELECT * FROM cypher('jsonb_operators',$$MATCH (n) return n ?| n $$) as (a agtype);
SELECT * FROM cypher('jsonb_operators',$$MATCH (n) return n ?& 1 $$) as (a agtype);
SELECT * FROM cypher('jsonb_operators',$$MATCH (n) return n ?& '' $$) as (a agtype);
SELECT * FROM cypher('jsonb_operators',$$MATCH (n) return n ?& '1' $$) as (a agtype);
SELECT * FROM cypher('jsonb_operators',$$MATCH (n) return n ?& '{}' $$) as (a agtype);
SELECT * FROM cypher('jsonb_operators',$$MATCH (n) return n ?& n.json $$) as (a agtype);


-- Exists (?&)

-- should return true
SELECT * FROM cypher('jsonb_operators',$$MATCH (n) return n ?& ['list', 'json'] $$) as (a agtype);
SELECT * FROM cypher('jsonb_operators',$$MATCH (n) return n.json.b ?& ['a', 'b'] $$) as (a agtype);
SELECT * FROM cypher('jsonb_operators',$$MATCH (n) return n.json.c ?& ['d'] $$) as (a agtype);
SELECT * FROM cypher('jsonb_operators',$$MATCH (n) return n ?& keys(n) $$) as (a agtype);
SELECT * FROM cypher('jsonb_operators',$$MATCH (n) return n.json ?& keys(n.json) $$) as (a agtype);

-- should return false
SELECT * FROM cypher('jsonb_operators',$$MATCH (n) return n ?& [] $$) as (a agtype);
SELECT * FROM cypher('jsonb_operators',$$MATCH (n) return n ?& ['a', 'b'] $$) as (a agtype);
SELECT * FROM cypher('jsonb_operators',$$MATCH (n) return n.json.b ?& [] $$) as (a agtype);
SELECT * FROM cypher('jsonb_operators',$$MATCH (n) return n.json.b ?& ['a', 'b', 'c'] $$) as (a agtype);
SELECT * FROM cypher('jsonb_operators',$$MATCH (n) return n.json.c ?& ['d', 'e'] $$) as (a agtype);
SELECT * FROM cypher('jsonb_operators',$$MATCH (n) return n ?& [['list']] $$) as (a agtype);

-- errors out
SELECT * FROM cypher('jsonb_operators',$$MATCH (n) return n ?& 'list' $$) as (a agtype);
SELECT * FROM cypher('jsonb_operators',$$MATCH (n) return n ?& 1 $$) as (a agtype);
SELECT * FROM cypher('jsonb_operators',$$MATCH (n) return n ?& '' $$) as (a agtype);
SELECT * FROM cypher('jsonb_operators',$$MATCH (n) return n ?& '1' $$) as (a agtype);
SELECT * FROM cypher('jsonb_operators',$$MATCH (n) return n ?& n $$) as (a agtype);
SELECT * FROM cypher('jsonb_operators',$$MATCH (n) return n ?& '{}' $$) as (a agtype);
SELECT * FROM cypher('jsonb_operators',$$MATCH (n) return n ?& n.json $$) as (a agtype);

--
-- concat || operator
--
SELECT * FROM cypher('jsonb_operators', $$ RETURN [1,2] || 2 $$) AS (result agtype);
SELECT * FROM cypher('jsonb_operators', $$ RETURN true || false $$) AS (result agtype);
SELECT * FROM cypher('jsonb_operators', $$ RETURN true || false || {a: 'string'} $$) AS (result agtype);
SELECT * FROM cypher('jsonb_operators', $$ RETURN true || false || {a: 'string'} || true $$) AS (result agtype);

SELECT * FROM cypher('jsonb_operators', $$ WITH [1,2,3] AS m WITH m, m || 'string' AS n RETURN n $$) AS (result agtype);
SELECT * FROM cypher('jsonb_operators', $$ WITH [1,2,3] AS m WITH m, m || {a: 1::numeric} AS n RETURN n $$) AS (result agtype);
SELECT * FROM cypher('jsonb_operators', $$ WITH {a: [1,2,3]} AS m WITH m, m || {a: 1::numeric} AS n RETURN n $$) AS (result agtype);
SELECT * FROM cypher('jsonb_operators', $$ WITH {b: [1,2,3]} AS m WITH m, m || {a: 1::numeric} AS n RETURN n $$) AS (result agtype);

SELECT * FROM cypher('jsonb_operators', $$ MATCH(n) RETURN n || 1 || 'string' $$) AS (result agtype);
SELECT * FROM cypher('jsonb_operators', $$ MATCH(n) RETURN n || {list: [true, null]} $$) AS (result agtype);
SELECT * FROM cypher('jsonb_operators', $$ MATCH (n) MATCH(m) RETURN n || m $$) AS (result agtype);
SELECT * FROM cypher('jsonb_operators', $$ MATCH (n) RETURN n.list || [1, 2, 3] $$) AS (result agtype);
SELECT * FROM cypher('jsonb_operators', $$ MATCH (n) RETURN n.json || [1, 2, 3] $$) AS (result agtype);
SELECT * FROM cypher('jsonb_operators', $$ MATCH (n) RETURN n.json || n.json $$) AS (result agtype);
SELECT * FROM cypher('jsonb_operators', $$ MATCH (n) RETURN n.json || n $$) AS (result agtype);

-- should give an error
SELECT * FROM cypher('jsonb_operators', $$ RETURN true || {a: 'string'} || true $$) AS (result agtype);
SELECT * FROM cypher('jsonb_operators', $$ WITH 'b' AS m WITH m, m || {a: 1} AS n RETURN n $$) AS (result agtype);
SELECT * FROM cypher('jsonb_operators', $$ MATCH (n) RETURN n.json || 1 $$) AS (result agtype);

-- clean up
SELECT drop_graph('jsonb_operators', true);