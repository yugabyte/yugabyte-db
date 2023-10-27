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
-- jsonb operators in AGE (?, ?&, ?|, ->, ->>, #>, #>>, ||, @>, <@)
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
-- Agtype access operators (->, ->>)
--

SELECT i, pg_typeof(i) FROM (SELECT '{"bool":true, "array":[1,3,{"bool":false, "int":3, "float":3.14},7], "float":3.14}'::agtype->'array'::text->2->'float'::text as i) a;
SELECT i, pg_typeof(i) FROM (SELECT '{"bool":true, "array":[1,3,{"bool":false, "int":3, "float":3.14},7], "float":3.14}'::agtype->'array'::text->2->>'float'::text as i) a;

SELECT i, pg_typeof(i) FROM (SELECT '[0, 1]'::agtype->0 as i) a;
SELECT i, pg_typeof(i) FROM (SELECT '[0, 1]'::agtype->>0 as i) a;

/*
 * access agtype object field or array element (->)
 */

-- LHS is an object
SELECT '{"n":null,"a":1,"b":[1,2],"c":{"1":2},"d":{"1":[2,3]}}'::agtype -> '"n"'::agtype;
SELECT '{"n":null,"a":1,"b":[1,2],"c":{"1":2},"d":{"1":[2,3]}}'::agtype -> '"a"'::agtype;
SELECT '{"n":null,"a":1,"b":[1,2],"c":{"1":2},"d":{"1":[2,3]}}'::agtype -> '"b"'::agtype;
SELECT '{"n":null,"a":1,"b":[1,2],"c":{"1":2},"d":{"1":[2,3]}}'::agtype -> '"c"'::agtype;
SELECT '{"n":null,"a":1,"b":[1,2],"c":{"1":2},"d":{"1":[2,3]}}'::agtype -> '"d"'::agtype;
SELECT '{"n":null,"a":1,"b":[1,2],"c":{"1":2},"d":{"1":[2,3]}}'::agtype -> '"d"'::agtype -> '"1"'::agtype;
SELECT '{"a": [-1, -2, -3]}'::agtype -> 'a'::text;
SELECT '{"a": 9, "b": 11, "c": {"ca": [[], {}, null]}, "d": true, "1": false}'::agtype -> '"1"'::text::agtype;
SELECT '{"a": true, "b": null, "c": -1.99, "d": {"e": []}, "1": [{}, [[[]]]], " ": [{}]}'::agtype -> ' '::text;
SELECT '{"a": true, "b": null, "c": -1.99, "d": {"e": []}, "1": [{}, [[[]]]], " ": [{}]}'::agtype -> '" "'::agtype;

-- should return null
SELECT '{"n":null,"a":1,"b":[1,2],"c":{"1":2},"d":{"1":[2,3]}}'::agtype -> '"e"'::agtype;
SELECT '{"n":null,"a":1,"b":[1,2],"c":{"1":2},"d":{"1":[2,3]}, "1": -19}'::agtype -> '1'::agtype;
SELECT '{"a": [{"b": "c"}, {"b": "cc"}]}'::agtype -> '""'::agtype;
SELECT '{"a": [{"b": "c"}, {"b": "cc"}]}'::agtype -> null::text;
SELECT '{"a": [{"b": "c"}, {"b": "cc"}]}'::agtype -> null::int;
SELECT '{"a": [-1, -2, -3]}'::agtype -> '"a"'::text;
SELECT '{"a": 9, "b": 11, "c": {"ca": [[], {}, null]}, "d": true, "1": false}'::agtype -> '1'::text::agtype;

-- LHS is an array
SELECT '["a","b","c",[1,2],null]'::agtype -> '0'::agtype;
SELECT '["a","b","c",[1,2],null]'::agtype -> 1;
SELECT '["a","b","c",[1,2],null]'::agtype -> '2'::agtype;
SELECT '["a","b","c",[1,2],null]'::agtype -> 3;
SELECT '["a","b","c",[1,2],null]'::agtype -> '3'::agtype -> '1'::agtype;
SELECT '["a","b","c",[1,2],null]'::agtype -> '3'::agtype -> '-1'::agtype;
SELECT '["a","b","c",[1,2],null]'::agtype -> '3'::agtype -> -1;
SELECT '["a","b","c",[1,2],null]'::agtype -> 4;
SELECT '["a","b","c",[1,2],null]'::agtype -> '-1'::agtype;
SELECT '["a","b","c",[1,2],null]'::agtype -> '-5'::agtype;
SELECT '[1, 2, 3]'::agtype -> '1'::text::agtype;
SELECT '[1, 2, 3]'::agtype -> '1'::text::agtype::int;
SELECT '[1, 2, 3]'::agtype -> '1'::text::agtype::int::bool::int;
SELECT '[1, 2, 3]'::agtype -> '1'::text::int;
SELECT '[1, "e", {"a": 1}, {}, [{}, {}, -9]]'::agtype -> true::int;
SELECT '[1, "e", {"a": 1}, {}, [{}, {}, -9]]'::agtype -> 1.9::int;
SELECT '[1, "e", {"a": 1}, {}, [{}, {}, -9]]'::agtype -> 1.1::int;
SELECT 'true'::agtype -> 0.1::int;
SELECT 'true'::agtype -> 0;
SELECT 'true'::agtype -> false::int;
SELECT 'true'::agtype -> 0.1::int::bool::int;
SELECT '[1, 9]'::agtype -> -1.2::int;
SELECT 'true'::agtype -> 0.1::bigint::int;

-- should return null
SELECT '["a","b","c",[1,2],null]'::agtype -> '5'::agtype;
SELECT '["a","b","c",[1,2],null]'::agtype -> '-6'::agtype;
SELECT '["a","b","c",[1,2],null]'::agtype -> '"a"'::agtype;
SELECT '"foo"'::agtype -> '1'::agtype;
SELECT '[1, 2, 3, "string", {"a": 1}, {"a": []}]'::agtype -> '5'::text;
SELECT '[1, 2, 3]'::agtype -> '[1]';
SELECT '[1, "e", {"a": 1}, {}, [{}, {}, -9]]'::agtype -> '{"a": 1}'::agtype;
SELECT 'true'::agtype -> 0::text;
SELECT 'true'::agtype -> false::int::text;

-- LHS is vertex/edge/path
SELECT '{"id": 1125899906842625, "label": "Vertex", "properties": {"a": "xyz", "b": true, "c": -19.888, "e": {"f": "abcdef", "g": {}, "h": [[], {}]}, "i": {"j": 199, "k": {"l": "mnopq"}}}}::vertex'::agtype -> '"a"';
SELECT '{"id": 1125899906842625, "label": "Vertex", "properties": {"a": "xyz", "b": true, "c": -19.888, "e": {"f": "abcdef", "g": {}, "h": [[], {}]}, "i": {"j": 199, "k": {"l": "mnopq"}}}}::vertex'::agtype -> '"e"' -> 'g'::text;
SELECT '{"id": 1125899906842625, "label": "Vertex", "properties": {"a": "xyz", "b": true, "c": -19.888, "e": {"f": "abcdef", "g": {}, "h": [[], {}]}, "i": {"j": 199, "k": {"l": "mnopq"}}}}::vertex'::agtype -> '"e"' -> 'h'::text -> -1;
SELECT '{"id": 1125899906842625, "label": "Vertex", "properties": {"a": "xyz", "b": true, "c": -19.888, "e": {"f": "abcdef", "g": {}, "h": [[], {}]}, "i": {"j": 199, "k": {"l": "mnopq"}}}}::vertex'::agtype -> '"e"' -> 'h'::text -> -1 -> 0;
SELECT '{"id": 1125899906842625, "label": "Vertex", "properties": {"a": "xyz", "b": true, "c": -19.888, "e": {"f": "abcdef", "g": {}, "h": [[], {}]}, "i": {"j": 199, "k": {"l": "mnopq"}}}}::vertex'::agtype -> '"id"';

-- access array element nested inside object or object field nested inside array on LHS
SELECT '{"n":null,"a":1,"b":[1,2],"c":{"1":2},"d":{"1":[2,3]}}'::agtype -> '"d"'::agtype -> '"1"'::agtype -> '1'::agtype;
SELECT '{"a": 9, "b": 11, "c": {"ca": [[], {}, null]}, "d": true}'::agtype -> 'c'::text -> '"ca"'::agtype -> 0;
SELECT '{"a":[1, 2, 3], "b": {"c": ["cc", "cd"]}}'::agtype -> '"b"'::agtype -> 'c'::text -> 1 -> 0;
SELECT '{"a":[1, 2, 3], "b": {"c": 1}}'::agtype -> '"b"'::agtype -> 1;

/*
 * access agtype object field or array element as text (->>)
 */

-- LHS is an object
SELECT '{"n":null,"a":1,"b":[1,2],"c":{"1":2},"d":{"1":[2,3]}}'::agtype ->> '"a"'::agtype;
SELECT '{"n":null,"a":1,"b":[1,2],"c":{"1":2},"d":{"1":[2,3]}}'::agtype ->> '"d"'::agtype;
SELECT '{"1": -1.99, "a": 1, "b": 2, "c": {"d": [{}, [[[], [9]]]]}, "1": true}'::agtype ->> '"1"';
SELECT '{"1": -1.99, "a": 1, "b": 2, "c": {"d": [{}, [[[], [9]]]]}, "1": true}'::agtype ->> '1'::text;

-- should return null
SELECT '{"1": -1.99, "a": 1, "b": 2, "c": {"d": [{}, [[[], [9]]]]}, "1": true}'::agtype ->> 1;
SELECT '{"1": -1.99, "a": 1, "b": 2, "c": {"d": [{}, [[[], [9]]]]}, "1": true}'::agtype ->> '1';
SELECT '{"a": [{"b": "c"}, {"b": "cc"}]}'::agtype ->> null::text;
SELECT '{"a": [{"b": "c"}, {"b": "cc"}]}'::agtype ->> null::int;

-- LHS is an array
SELECT '["a","b","c",[1,2],null]'::agtype ->> 0;
SELECT '["a","b","c",[1,2],null]'::agtype ->> '1'::agtype;
SELECT '["a","b","c",[1,2],null]'::agtype ->> '2'::int;
SELECT '["a","b","c",[1,2],null]'::agtype ->> 3::int;
SELECT '["a","b","c",[1,2],null]'::agtype ->> '4'::agtype;
SELECT '[1, 2, "e", true, null, {"a": true, "b": {}, "c": [{}, [[], {}]]}, null]'::agtype ->> 0.1::float::bigint::int;
SELECT '"foo"'::agtype ->> '0'::agtype;
SELECT '[false, {}]'::agtype ->> 1::bool::int::bigint::int;

-- should return null
SELECT '["a","b","c",[1,2],null]'::agtype ->> '-1'::agtype;
SELECT '[1, 2, "e", true, null, {"a": true, "b": {}, "c": [{}, [[], {}]]}, null]'::agtype ->> 2::text;
SELECT '[1, 2, "e", true, null, {"a": true, "b": {}, "c": [{}, [[], {}]]}, null]'::agtype ->> '2'::text;
SELECT '[1, 2, "e", true, null, {"a": true, "b": {}, "c": [{}, [[], {}]]}, null]'::agtype ->> 'null'::text;
SELECT '"foo"'::agtype ->> '1'::agtype;

-- LHS is vertex/edge/path
SELECT '{"id": 1125899906842625, "label": "Vertex", "properties": {"a": "xyz", "b": true, "c": -19.888, "e": {"f": "abcdef", "g": {}, "h": [[], {}]}, "i": {"j": 199, "k": {"l": "mnopq"}}}}::vertex'::agtype ->> '"a"';
SELECT '{"id": 1125899906842625, "label": "Vertex", "properties": {"a": "xyz", "b": true, "c": -19.888, "e": {"f": "abcdef", "g": {}, "h": [[], {}]}, "i": {"j": 199, "k": {"l": "mnopq"}}}}::vertex'::agtype ->> '"id"';

-- using -> and ->> in single query
SELECT '{"1": -1.99, "a": 1, "b": 2, "c": {"d": [{}, [[[], [9]]]]}}'::agtype -> '"1"' ->> '0';
SELECT '["a","b","c",[1,2],null]'::agtype -> '3'::agtype ->> '1'::agtype;
SELECT ('["a","b","c",[1,2],null]'::agtype -> '3'::agtype) ->> 0;
SELECT '{"n":null,"a":1,"b":[1,2],"c":{"1":2},"d":{"1":[2,3]}}'::agtype -> '"d"'::agtype ->> '1'::agtype;
SELECT '{"id": 1125899906842625, "label": "Vertex", "properties": {"a": "xyz", "b": true, "c": -19.888, "e": {"f": "abcdef", "g": {}, "h": [[], {}]}, "i": {"j": 199, "k": {"l": "mnopq"}}}}::vertex'::agtype -> '"e"' -> 'h'::text -> -1 ->> 0;

-- should give error
SELECT '["a","b","c",[1,2],null]'::agtype ->> '3'::agtype ->> '1'::agtype;
SELECT '["a","b","c",[1,2],null]'::agtype ->> '3'::agtype -> '1'::agtype;
SELECT '[1, 2, "e", true, null, {"a": true, "b": {}, "c": [{}, [[], {}]]}, null]'::agtype ->> 0.1::float::bigint;
SELECT '{"a": [{"b": "c"}, {"b": "cc"}]}'::agtype ->> "'z'"::agtype;

--
-- Agtype path extraction operators (#>, #>>)
--

/*
 * #> operator to return the extracted value as agtype
 */
SELECT pg_typeof('{"a":"b","c":[1,2,3]}'::agtype #> '["a"]');
SELECT pg_typeof('[0,1,2,[3,4],{"5":"five"}]'::agtype #> '[-1,"5"]');

-- left operand is agtype object, right operand should be an array of strings
SELECT '{"a":"b","c":[1,2,3]}'::agtype #> '["a"]';
SELECT '{"a":"b","c":[1,2,3]}'::agtype #> '["c"]';
SELECT '{"a":"b","c":[1,2,3]}'::agtype #> '[]';
SELECT '{"0": true}'::agtype #> '["0"]';
SELECT '{"a":"b","c":{"d": [1,2,3]}}'::agtype #> '["c", "d"]';
SELECT '{"a":"b","c":{"d": {"e": -1}}}'::agtype #> '["c", "d", "e"]';

-- left operand is vertex/edge/path, right operand should be an array of strings
SELECT '{"id": 1125899906842625, "label": "Vertex", "properties": {"a": "xyz", "b": true, "c": -19.888, "e": {"f": "abcdef", "g": {}, "h": [[], {}]}, "i": {"j": 199, "k": {"l": "mnopq"}}}}::vertex'::agtype #> '[]';
SELECT '{"id": 1125899906842625, "label": "Vertex", "properties": {"a": "xyz", "b": true, "c": -19.888, "e": {"f": "abcdef", "g": {}, "h": [[], {}]}, "i": {"j": 199, "k": {"l": "mnopq"}}}}::vertex'::agtype #> '["e", "h", -2]';
SELECT '{"id": 1688849860263937, "label": "EDGE", "end_id": 1970324836974593, "start_id": 1407374883553281, "properties": {"a": "xyz", "b" : true, "c": -19.888, "e": {"f": "abcdef", "g": {}, "h": [[], {}]}, "i": {"j": 199, "k": {"l": "mnopq"}}}}::edge'::agtype #> '[]';
SELECT '{"id": 1688849860263937, "label": "EDGE", "end_id": 1970324836974593, "start_id": 1407374883553281, "properties": {"a": "xyz", "b" : true, "c": -19.888, "e": {"f": "abcdef", "g": {}, "h": [[], {}]}, "i": {"j": 199, "k": {"l": "mnopq"}}}}::edge'::agtype #> '["i", "k", "l"]';

-- left operand is agtype array, right operand should be an array of integers or valid integer strings
SELECT '[0,1,2,[3,4],{"5":"five"}]'::agtype #> '[0]';
SELECT '[0,1,2,[3,4],{"5":"five"}]'::agtype #> '[4]';
SELECT '[0,1,2,[3,4],{"5":"five"}]'::agtype #> '[]';
SELECT '[0,1,2,[3,4],{"5":"five"}]'::agtype #> '[-2]';
SELECT '[0,1,2,[3,4],{"5":"five"}]'::agtype #> '[-2, -1]';
SELECT '[[-3, 1]]'::agtype #> '[0, 1]';
SELECT '[0,1,2,[3,4],{"5":"five"}]'::agtype #> '["0"]';
SELECT '[[-3, 1]]'::agtype #> '["0", "1"]';
SELECT '[[-3, 1]]'::agtype #> '["0", 1]';
SELECT '[[-3, 1]]'::agtype #> '["0", "-1"]';

-- path extraction pattern for arrays nested in object or object nested in array as left operand
-- having object at top level
SELECT '{"a":"b","c":[1,2,3]}'::agtype #> '["c",0]';
SELECT '{"a":"b","c":[1,2,3]}'::agtype #> '["c",-3]';
SELECT '{"a":["b"],"c":[1,2,3]}'::agtype #> '["a", 0]';
SELECT '{"a":"b","c":[1,2,3], "1" : [{}, {}, [[-3]]]}'::agtype #> '["1", 2, 0, 0]';
SELECT '{"a":"b","c":[1,2,3], "1" : [{}, {}, [[-3, {"a": true}]]]}'::agtype #> '["1", -1, -1, -1, "a"]';
SELECT '{"a":"b","c":[1,2,3], "1" : [{}, {}, [[-3, {"a": {"b": {"d": [-1.9::numeric, false]}, "c": "foo"}}]]]}'::agtype #> '["1", -1, -1, -1, "a"]';
SELECT '{"a":"b","c":[1,2,3], "1" : [{}, {}, [[-3, {"a": {"b": {"d": [-1.9::numeric, false]}, "c": "foo"}}]]]}'::agtype #> '["1", -1, -1, -1, "a", "c"]';
SELECT '{"a":"b","c":[1,2,3], "1" : [{}, {}, [[-3, {"a": {"b": {"d": [-1.9::numeric, false]}, "c": "foo"}}]]]}'::agtype #> '["1", -1, -1, -1, "a", "b", "d", -2]';

-- having array at top level
SELECT '[0,1,2,[3,4],{"5":"five"}]'::agtype #> '[4,"5"]';
SELECT '[0,1,2,[3,4],{"5":"five"}]'::agtype #> '[-1,"5"]';
SELECT '[0,1,2,[3,4],{"5":["five", "six"]}]'::agtype #> '[-1,"5",-1]';

-- should return null
SELECT '{"a":"b","c":[1,2,3]}'::agtype #> '[0]';
SELECT '{"a":"b","c":[1,2,3]}'::agtype #> '["c",3]';
SELECT '{"a":"b","c":[1,2,3]}'::agtype #> '["c","3"]';
SELECT '{"a":"b","c":[1,2,3,4]}'::agtype #> '["c",4]';
SELECT '{"a":"b","c":[1,2,3]}'::agtype #> '["c",-4]';
SELECT '{"a":"b","c":[1,2,3]}'::agtype #> '["a","b"]';
SELECT '{"a":"b","c":[1,2,3]}'::agtype #> '["a","c"]';
SELECT '{"a":"b","c":[1,2,3]}'::agtype #> '["a", []]';
SELECT '{"a":["b"],"c":[1,2,3]}'::agtype #> '["a", []]';
SELECT '{"a":"b","c":[1,2,3], "1" : [{}, {}, [[-3, {"a": {"b": {"d": [-1.9::numeric, false]}, "c": "foo"}}]]]}'::agtype #> '["1", -1, -1, -1, "a", "c", "d"]';
SELECT '{"a":"b","c":[1,2,3], "1" : [{}, {}, [[-3, {"a": {"b": {"d": [-1.9::numeric, false]}, "c": "foo"}}]]]}'::agtype #> '["1", -1, -1, -1, "a", "b", "d", -2, 0]';
SELECT '{"0": true}'::agtype #> '[0]';
SELECT '{"a":"b","c":[1,2,3]}'::agtype #> '[null]';
SELECT '{}'::agtype #> '[null]';
SELECT '{}'::agtype #> '[{}]';

SELECT '{"id": 1125899906842625, "label": "Vertex", "properties": {"a": "xyz", "b": true, "c": -19.888, "e": {"f": "abcdef", "g": {}, "h": [[], {}]}, "i": {"j": 199, "k": {"l": "mnopq"}}}}::vertex'::agtype #> '["id"]';
SELECT '{"id": 1688849860263937, "label": "EDGE", "end_id": 1970324836974593, "start_id": 1407374883553281, "properties": {"a": "xyz", "b" : true, "c": -19.888, "e": {"f": "abcdef", "g": {}, "h": [[], {}]}, "i": {"j": 199, "k": {"l": "mnopq"}}}}::edge'::agtype #> '["start_id"]';

SELECT '[0,1,2,[3,4],{"5":"five"}]'::agtype #> '[4,5]';
SELECT '[0,1,2,[3,4],{"5":"five"}]'::agtype #> '[-1,5]';
SELECT '[0,1,2,[3,4],{"5":"five"}]'::agtype #> '[3, -1, 0]';
SELECT '[0,1,2,[3,4],{"5":"five"}]'::agtype #> '[{}]'::agtype;
SELECT '[0,1,2,[3,4],{"5":"five"}]'::agtype #> '[{"5":"five"}]'::agtype;
SELECT '[0,1,2,[3,4],{"5":"five"},6,7]'::agtype #> '["6", "7"]'::agtype;
SELECT '[0,1,2,[3,4],{"5":"five"},6,7]'::agtype #> '[6, 7]'::agtype;
SELECT '[0,1,2,[3,4],{"5":"five"}]'::agtype #> '[null]';
SELECT '[null]'::agtype #> '[null]';
SELECT '[]'::agtype #> '[null]';
SELECT '[[-3, 1]]'::agtype #> '["0", "1.1"]';
SELECT '[[-3, 1]]'::agtype #> '["0", "true"]';
SELECT '[[-3, 1]]'::agtype #> '["0", "string"]';
SELECT '{"a":"b","c":[1,2,3], "1" : [{}, {}, [[-3, {"a": {"b": {"d": [-1.9::numeric, false]}, "c": "foo"}}]]]}'::agtype #> '["1", -1, -1, -1, "a", "b", "d", "false"]';
SELECT '{"a":"b","c":[1,2,3], "1" : [{}, {}, [[-3, {"a": {"b": {"d": [-1.9::numeric, false]}, "c": "foo"}}]]]}'::agtype #> '["1", -1, -1, -1, "a", "b", "d", "a"]';

-- errors out
SELECT '{"0": true}'::agtype #> '"0"';
SELECT '{"n": 1}'::agtype #> '{"n": 1}';
SELECT '[{"n": 1}]'::agtype #> '{"n": 1}';
SELECT '[{"n": 100}]'::agtype #> '{"id": 281474976710658, "label": "", "properties": {"n": 100}}::vertex';
SELECT '-19'::agtype #> '[-1]'::agtype;
SELECT '{"id": 1125899906842625, "label": "Vertex", "properties": {"a": "xyz", "b": true, "c": -19.888, "e": {"f": "abcdef", "g": {}, "h": [[], {}]}, "i": {"j": 199, "k": {"l": "mnopq"}}}}::vertex'::agtype #> '"a"';

/*
 * #>> operator to return the extracted value as text
 */
SELECT pg_typeof('{"a":"b","c":[1,2,3]}'::agtype #>> '["a"]');
SELECT pg_typeof('[0,1,2,[3,4],{"5":"five"}]'::agtype #>> '[-1,"5"]');

/*
 * All the tests added for #> are also valid for #>>
 */

/*
 * test the combination of #> and #>> operators below
 * (left and right operands have to be agtype for #> and #>>,
 * errors out when left operand is a text, i.e., the output of #>> operator)
 */
SELECT '{"a":"b","c":[1,2,3], "1" : [{}, {}, [[-3, {"a": {"b": {"d": [-1.9::numeric, false]}, "c": "foo"}}]]]}'::agtype #> '["1", -1, -1, -1, "a"]' #> '["b", "d", -1]';
SELECT '{"a":"b","c":[1,2,3], "1" : [{}, {}, [[-3, {"a": {"b": {"d": [-1.9::numeric, false]}, "c": "foo"}}]]]}'::agtype #> '["1", -1, -1, -1, "a"]' #> '["b", "d", "-1"]';
SELECT '{"a":"b","c":[1,2,3], "1" : [{}, {}, [[-3, {"a": {"b": {"d": [-1.9::numeric, false]}, "c": "foo"}}]]]}'::agtype #> '["1", -1, -1, -1, "a"]' #>> '["b", "d", -1]';
SELECT '{"a":"b","c":[1,2,3], "1" : [{}, {}, [[-3, {"a": {"b": {"d": [-1.9::numeric, false]}, "c": "foo"}}]]]}'::agtype #> '["1", -1, -1, -1, "a"]' #>> '["b", "d", "-1"]';

SELECT '{"id": 1125899906842625, "label": "Vertex", "properties": {"a": "xyz", "b": true, "c": -19.888, "e": {"f": "abcdef", "g": {}, "h": [[], {}]}, "i": {"j": 199, "k": {"l": "mnopq"}}}}::vertex'::agtype #> '["e"]' #>> '["h", "-1"]';
SELECT '{"id": 1125899906842625, "label": "Vertex", "properties": {"a": "xyz", "b": true, "c": -19.888, "e": {"f": "abcdef", "g": {}, "h": [[], {}]}, "i": {"j": 199, "k": {"l": "mnopq"}}}}::vertex'::agtype #> '["e"]' #> '["h", "-1"]' #>> '[]';

SELECT '[[-3, [1]]]'::agtype #> '["0", "1"]' #>> '[]';
SELECT '[[-3, [1]]]'::agtype #> '["0", "1"]' #>> '[-1]';

-- errors out
SELECT '[0,1,2,[3,4],{"5":"five"}]'::agtype #>> '[-1, "5"]' #> '[]';
SELECT '{"a":"b","c":[1,2,3], "1" : [{}, {}, [[-3, {"a": {"b": {"d": [-1.9::numeric, false]}, "c": "foo"}}]]]}'::agtype #>> '["1", -1, -1, -1, "a"]' #> '["b", "d", -1]';

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
-- Agtype containment operator
--

/*
 * right contains @> operator
 */
-- returns true
SELECT '{"a":"b", "b":1, "c":null}'::agtype @> '{"a":"b"}';
SELECT '{"a":"b", "b":1, "c":null}'::agtype @> '{"a":"b", "c":null}';
SELECT '{"a":"b", "b":1, "c":null}'::agtype @> '{}';
SELECT '{"name": "Bob", "tags": ["enim", "qui"]}'::agtype @> '{"tags":["qui"]}';
SELECT '{"name": "Bob", "tags": ["enim", "qui"]}'::agtype @> '{"tags":[]}';

SELECT '[1,2]'::agtype @> '[1,2,2]'::agtype;
SELECT '[1,1,2]'::agtype @> '[1,2,2]'::agtype;
SELECT '[[1,2]]'::agtype @> '[[1,2,2]]'::agtype;
SELECT '[1,2,2]'::agtype @> '[]'::agtype;
SELECT '[[1,2]]'::agtype @> '[[]]'::agtype;
SELECT '[[1,2]]'::agtype @> '[[1,2,2], []]'::agtype;

SELECT '{"id": 281474976710657, "label": "", "properties": {"name": "A"}}::vertex'::agtype @> '{"name": "A"}';
SELECT '{"name": "A"}' @> '{"id": 281474976710657, "label": "", "properties": {"name": "A"}}::vertex'::agtype;
SELECT '{"id": 281474976710657, "label": "", "properties": {"name": "A"}}::vertex'::agtype @> '{"id": 281474976710657, "label": "", "properties": {"name": "A"}}::vertex';

SELECT agtype_contains('{"id": 1}','{"id": 1}');
SELECT agtype_contains('[1, 2, 3]','[3, 3]');

-- In general, one thing should always contain itself
SELECT '["9", ["7", "3"], 1]'::agtype @> '["9", ["7", "3"], 1]'::agtype;
SELECT '{"a":"b", "b":1, "c":null}'::agtype @> '{"a":"b", "b":1, "c":null}';

-- returns false
SELECT '{"a":"b", "b":1, "c":null}'::agtype @> '{"a":"b", "g":null}';
SELECT '{"a":"b", "b":1, "c":null}'::agtype @> '{"g":null}';
SELECT '{"a":"b", "b":1, "c":null}'::agtype @> '{"a":"c"}';
SELECT '{"a":"b", "b":1, "c":null}'::agtype @> '{"a":"b", "c":"q"}';
SELECT '{"a":"b", "b":1, "c":null}'::agtype @> '[]';
SELECT '{"name": "Bob", "tags": ["enim", "qui"]}'::agtype @> '{"tags":{}}';

SELECT '[1,1,2]'::agtype @> '[1,2,[2]]'::agtype;
SELECT '[1,2,2]'::agtype @> '{}'::agtype;
SELECT '[[1,2]]'::agtype @> '[[{}]]'::agtype;
SELECT '[[1,2]]'::agtype @> '[[1,2,2, []], []]'::agtype;
SELECT '[[1,2]]'::agtype @> '[[1,2,2, []], [[]]]'::agtype;

SELECT agtype_contains('{"id": 1}','{"id": 2}');
SELECT agtype_contains('[1, 2, 3]','[3, 3, []]');

-- Raw scalar may contain another raw scalar, array may contain a raw scalar
SELECT '[5]'::agtype @> '[5]';
SELECT '5'::agtype @> '5';
SELECT '[5]'::agtype @> '5';

-- But a raw scalar cannot contain an array
SELECT '5'::agtype @> '[5]';

-- object/array containment is different from agtype_string_match_contains
SELECT '{ "name": "Bob", "tags": [ "enim", "qui"]}'::agtype @> '{"tags":["qu"]}';

/*
 * left contains <@ operator
 */
-- returns true
SELECT '{"a":"b"}'::agtype <@ '{"a":"b", "b":1, "c":null}';
SELECT '{"a":"b", "c":null}'::agtype <@ '{"a":"b", "b":1, "c":null}';

SELECT '[1,2,2]'::agtype <@ '[1,2]'::agtype;
SELECT '[1,2,2]'::agtype <@ '[1,1,2]'::agtype;
SELECT '[[1,2,2]]'::agtype <@ '[[1,2]]'::agtype;
SELECT '[]'::agtype <@ '[1,2,2]'::agtype;

SELECT '{"name": "A"}' <@ '{"id": 281474976710657, "label": "", "properties": {"name": "A"}}::vertex'::agtype;
SELECT '{"id": 281474976710657, "label": "", "properties": {"name": "A"}}::vertex'::agtype <@ '{"name": "A"}';
SELECT '{"id": 281474976710657, "label": "", "properties": {"name": "A"}}::vertex'::agtype <@ '{"id": 281474976710657, "label": "", "properties": {"name": "A"}}::vertex';

SELECT agtype_contained_by('{"id": 1}','{"id": 1}');

-- returns false
SELECT '[1,2,2]'::agtype <@ '[]'::agtype;

SELECT '{"a":"b", "g":null}'::agtype <@ '{"a":"b", "b":1, "c":null}';
SELECT '{"g":null}'::agtype <@ '{"a":"b", "b":1, "c":null}';
SELECT '{"a":"c"}'::agtype <@ '{"a":"b", "b":1, "c":null}';

SELECT '{"id": 281474976710657, "label": "", "properties": {"name": "A"}}::vertex'::agtype <@ '{"id": 281474976710657, "label": "", "properties": {"name": "B"}}::vertex';

SELECT agtype_contained_by('{"id": 1}','{"id": 2}');

-- In general, one thing should always contain itself
SELECT '["9", ["7", "3"], ["1"]]'::agtype <@ '["9", ["7", "3"], ["1"]]'::agtype;
SELECT '{"a":"b", "b":1, "c":null}'::agtype <@ '{"a":"b", "b":1, "c":null}';

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
-- agtype access operator (->)
--
SELECT * FROM cypher('jsonb_operators', $$
    WITH [1,
        {bool: true, int: 3, array: [9, 11, {bool: false, float: 3.14}, 13]},
        5, 7, 9] as lst
    RETURN lst->-1
$$) AS r(result agtype);

SELECT * FROM cypher('jsonb_operators', $$
    WITH [1,
        {bool: true, int: 3, array: [9, 11, {bool: false, float: 3.14}, 13]},
        5, 7, 9] as lst
    RETURN lst->1
$$) AS r(result agtype);

SELECT * FROM cypher('jsonb_operators', $$
    WITH [1,
        {bool: true, int: 3, array: [9, 11, {bool: false, float: 3.14}, 13]},
        5, 7, 9] as lst
    RETURN lst->-4
$$) AS r(result agtype);

SELECT * FROM cypher('jsonb_operators', $$
    WITH [1,
        {bool: true, int: 3, array: [9, 11, {bool: false, float: 3.14}, 13]},
        5, 7, 9] as lst
    RETURN lst->-4->'array'
$$) AS r(result agtype);

SELECT * FROM cypher('jsonb_operators', $$
    WITH [1,
        {bool: true, int: 3, array: [9, 11, {bool: false, float: 3.14}, 13]},
        5, 7, 9] as lst
    RETURN lst->-4->'array'->-2->'bool'
$$) AS r(result agtype);

SELECT * FROM cypher('jsonb_operators', $$
    WITH [1,
        {bool: true, int: 3, array: [9, 11, {bool: false, float: 3.14}, 13]},
        5, 7, 9] as lst
    RETURN lst->-4->'array'->-2->'bool'->-1
$$) AS r(result agtype);

SELECT * FROM cypher('jsonb_operators', $$
    WITH [1,
        {bool: true, int: 3, array: [9, 11, {bool: false, float: 3.14}, 13]},
        5, 7, 9] as lst
    RETURN lst->(size(lst)-1)
$$) AS r(result agtype);

SELECT * FROM cypher('jsonb_operators', $$
    WITH [1,
        {bool: true, int: 3, array: [9, 11, {bool: false, float: 3.14}, 13]},
        5, 7, 9] as lst
    RETURN lst->(size(lst)%size(lst))
$$) AS r(result agtype);

SELECT * FROM cypher('jsonb_operators', $$
    WITH [1,
        {bool: true, int: 3, array: [9, 11, {bool: false, float: 3.14}, 13]},
        5, 7, 9] as lst
    RETURN lst->-4->'array'->-2->'float'
$$) AS r(result agtype);

SELECT * FROM cypher('jsonb_operators', $$
    WITH [1,
        {bool: true, int: 3, array: [9, 11, {bool: false, float: 3.14}, 13]},
        5, 7, 9] as lst
    RETURN lst->-4->'array'->-2->'float'->0
$$) AS r(result agtype);

SELECT * FROM cypher('jsonb_operators',$$MATCH (n) return n->'json' $$) as (a agtype);
SELECT * FROM cypher('jsonb_operators',$$MATCH (n) return n->'json'->'a' $$) as (a agtype);
SELECT * FROM cypher('jsonb_operators',$$MATCH (n) return n->'json'->'a'->-1 $$) as (a agtype);
SELECT * FROM cypher('jsonb_operators',$$MATCH (n) return n->'json'->'b' $$) as (a agtype);
SELECT * FROM cypher('jsonb_operators',$$MATCH (n) return n->'json'->'b'->-2 $$) as (a agtype);
SELECT * FROM cypher('jsonb_operators',$$MATCH (n) return n->'json'->'b'->1 $$) as (a agtype);
SELECT * FROM cypher('jsonb_operators',$$MATCH (n) return n->'json'->'c' $$) as (a agtype);
SELECT * FROM cypher('jsonb_operators',$$MATCH (n) return n->'json'->'c'->0 $$) as (a agtype);
SELECT * FROM cypher('jsonb_operators',$$MATCH (n) return n->'json'->'c'->'d' $$) as (a agtype);
SELECT * FROM cypher('jsonb_operators',$$MATCH (n) return n->'json'->'c'->'d'->-1->0 $$) as (a agtype);
SELECT * FROM cypher('jsonb_operators',$$MATCH (n) return n->'list' $$) as (a agtype);
SELECT * FROM cypher('jsonb_operators',$$MATCH (n) return n->'list'->-1 $$) as (a agtype);
SELECT * FROM cypher('jsonb_operators',$$MATCH (n) return n->'list'->2->0 $$) as (a agtype);
SELECT * FROM cypher('jsonb_operators',$$MATCH (n) WITH n->'list' AS lst RETURN lst $$) as (a agtype);
SELECT * FROM cypher('jsonb_operators',$$MATCH (n) WITH n->'list' AS lst RETURN lst->0 $$) as (a agtype);
SELECT * FROM cypher('jsonb_operators',$$MATCH (n) WITH n->'list' AS lst RETURN lst->(size(lst)-1) $$) as (a agtype);

-- should return null
SELECT * FROM cypher('jsonb_operators', $$
    WITH [1,
        {bool: true, int: 3, array: [9, 11, {bool: false, float: 3.14}, 13]},
        5, 7, 9] as lst
    RETURN lst->(size(lst))
$$) AS r(result agtype);

SELECT * FROM cypher('jsonb_operators',$$MATCH (n) return n->'json'->'d' $$) as (a agtype);
SELECT * FROM cypher('jsonb_operators',$$MATCH (n) return n->'list'->'c' $$) as (a agtype);
SELECT * FROM cypher('jsonb_operators',$$MATCH (n) WITH n->'list' AS lst RETURN lst->(size(lst)) $$) as (a agtype);
SELECT * FROM cypher('jsonb_operators',$$MATCH (n) return n->'json'->'b'->'a' $$) as (a agtype);

--
-- path extraction #> operator
--
SELECT * FROM cypher('jsonb_operators', $$
    WITH {json: {a: 1, b: ['a', 'b'], c: {d: ['a']}}, list: ['a', 'b', 'c']} AS map
    RETURN map #> []
$$) AS (result agtype);

SELECT * FROM cypher('jsonb_operators', $$
    WITH {json: {a: 1, b: ['a', 'b'], c: {d: ['a']}}, list: ['a', 'b', 'c']} AS map
    RETURN map #> ['json', 'c', 'd']
$$) AS (result agtype);

SELECT * FROM cypher('jsonb_operators', $$
    WITH {json: {a: 1, b: ['a', 'b'], c: {d: ['a']}}, list: ['a', 'b', 'c']} AS map
    RETURN map #> ['json', 'c', 'd', -1]
$$) AS (result agtype);

SELECT * FROM cypher('jsonb_operators', $$
    WITH {json: {a: 1, b: ['a', 'b'], c: {d: ['a']}}, list: ['a', 'b', 'c']} AS map
    RETURN map #> ['json', 'c', 'd', -1, -1]
$$) AS (result agtype);

SELECT * FROM cypher('jsonb_operators', $$
    WITH {json: {a: 1, b: ['a', 'b'], c: {d: ['a']}}, list: ['a', 'b', 'c']} AS map
    RETURN map #> ['list', "-1"]
$$) AS (result agtype);

SELECT * FROM cypher('jsonb_operators', $$
    WITH {json: {a: 1, b: ['a', 'b'], c: {d: ['a']}}, list: ['a', 'b', ['c', 'd']]} AS map
    RETURN map #> ['list', "-1", "-1"]
$$) AS (result agtype);

SELECT * FROM cypher('jsonb_operators', $$
    WITH {json: {a: 1, b: ['a', 'b'], c: {d: ['a']}}, list: ['a', 'b', ['c', 'd']]} AS map
    RETURN map #> ['list', "-1", -1]
$$) AS (result agtype);

SELECT * FROM cypher('jsonb_operators', $$
    WITH [[-3, 1]] AS list
    RETURN list #> []
$$) AS (result agtype);

SELECT * FROM cypher('jsonb_operators', $$
    WITH [[-3, 1]] AS list
    RETURN list #> [0]
$$) AS (result agtype);

SELECT * FROM cypher('jsonb_operators', $$
    WITH [[-3, 1]] AS list
    RETURN list #> [-1, -1]
$$) AS (result agtype);

SELECT * FROM cypher('jsonb_operators', $$
    WITH [[-3, 1]] AS list
    RETURN list #> [-1, -1, -1]
$$) AS (result agtype);

SELECT * FROM cypher('jsonb_operators', $$
    WITH [[-3, 1]] AS list
    RETURN list #> [{}]
$$) AS (result agtype);

SELECT * FROM cypher('jsonb_operators', $$
    WITH [null] AS list
    RETURN list #> []
$$) AS (result agtype);

SELECT * FROM cypher('jsonb_operators', $$
    WITH [null] AS list
    RETURN list #> [-1, -1, -1]
$$) AS (result agtype);

SELECT * FROM cypher('jsonb_operators', $$
    WITH [] AS list
    RETURN list #> []
$$) AS (result agtype);

SELECT * FROM cypher('jsonb_operators', $$
    WITH [] AS list
    RETURN list #> ["a", 1]
$$) AS (result agtype);

SELECT * FROM cypher('jsonb_operators',$$MATCH (n) RETURN n #> []$$) as (a agtype);
SELECT * FROM cypher('jsonb_operators',$$MATCH (n) RETURN n #> ["json"]$$) as (a agtype);
SELECT * FROM cypher('jsonb_operators',$$MATCH (n) RETURN n #> ["jsonb"]$$) as (a agtype);
SELECT * FROM cypher('jsonb_operators',$$MATCH (n) RETURN n #> ["json", "a"]$$) as (a agtype);
SELECT * FROM cypher('jsonb_operators',$$MATCH (n) RETURN n #> ["json", "a", 0]$$) as (a agtype);
SELECT * FROM cypher('jsonb_operators',$$MATCH (n) RETURN n #> ["json", "b"]$$) as (a agtype);
SELECT * FROM cypher('jsonb_operators',$$MATCH (n) RETURN n #> ["json", "b", -1]$$) as (a agtype);
SELECT * FROM cypher('jsonb_operators',$$MATCH (n) RETURN n #> ["json", "b", "-1"]$$) as (a agtype);
SELECT * FROM cypher('jsonb_operators',$$MATCH (n) RETURN n #> ["json", "b", -1, 0]$$) as (a agtype);
SELECT * FROM cypher('jsonb_operators',$$MATCH (n) RETURN n #> ["json", "c"]$$) as (a agtype);
SELECT * FROM cypher('jsonb_operators',$$MATCH (n) RETURN n #> ["json", "c", "d"]$$) as (a agtype);
SELECT * FROM cypher('jsonb_operators',$$MATCH (n) RETURN n #> ["json", "c", "d", -1]$$) as (a agtype);
SELECT * FROM cypher('jsonb_operators',$$MATCH (n) RETURN n #> ['list', -1]$$) as (a agtype);
SELECT * FROM cypher('jsonb_operators',$$MATCH (n) RETURN n #> ['list', 4]$$) as (a agtype);

-- errors out
SELECT * FROM cypher('jsonb_operators',$$MATCH (n) RETURN n #> "json"$$) as (a agtype);
SELECT * FROM cypher('jsonb_operators', $$
    WITH {json: {a: 1, b: ['a', 'b'], c: {d: ['a']}}, list: ['a', 'b', 'c']} AS map
    RETURN map #> 'jsonb'
$$) AS (result agtype);
SELECT * FROM cypher('jsonb_operators', $$
    WITH [[-3, 1]] AS list
    RETURN list #> 0
$$) AS (result agtype);
SELECT * FROM cypher('jsonb_operators', $$
    WITH 3 AS elem
    RETURN elem #> [0]
$$) AS (result agtype);
SELECT * FROM cypher('jsonb_operators', $$
    WITH 'string' AS elem
    RETURN elem #> [0]
$$) AS (result agtype);

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

/*
 * @> and <@ contains operators
 */

-- right contains @> operator
SELECT *
FROM cypher('jsonb_operators', $$
    MATCH (n)
    WHERE n @> {json: {a: 1, b: ["a", "b"], c: {d: "a"}}, list: ["a", "b", "c"]}
    RETURN n
$$) as (a agtype);

SELECT *
FROM cypher('jsonb_operators', $$
    MATCH (n)
    WHERE n.json @> {c: {d: "a"}}
    RETURN n
$$) as (a agtype);

SELECT *
FROM cypher('jsonb_operators', $$
    MATCH (n)
    WHERE n.json @> {c: {}}
    RETURN n
$$) as (a agtype);

SELECT *
FROM cypher('jsonb_operators', $$
    MATCH (n)
    WHERE n.json @> {b: ["a"]}
    RETURN n
$$) as (a agtype);

SELECT *
FROM cypher('jsonb_operators', $$
    MATCH (n)
    WHERE n.json @> {b: ["a", "a"]}
    RETURN n
$$) as (a agtype);

SELECT *
FROM cypher('jsonb_operators', $$
    MATCH (n)
    WHERE n.list @> []
    RETURN n
$$) as (a agtype);

SELECT *
FROM cypher('jsonb_operators', $$
    MATCH (n)
    WHERE n.list[2] @> "c"
    RETURN n
$$) as (a agtype);

SELECT *
FROM cypher('jsonb_operators', $$
    MATCH (n)
    WHERE n @> {}
    RETURN n
$$) as (a agtype);

SELECT *
FROM cypher('jsonb_operators', $$
    MATCH (n)
    RETURN properties(n).json @> {c: {d: "a"}}
$$) as (a agtype);

SELECT *
FROM cypher('jsonb_operators', $$
    MATCH (n)
    RETURN properties(n).json @> {c: {d: "b"}}
$$) as (a agtype);

 SELECT *
FROM cypher('jsonb_operators', $$
    MATCH (n)
    WHERE n.json @> {b: ["e"]}
    RETURN n
$$) as (a agtype);

SELECT *
FROM cypher('jsonb_operators', $$
    MATCH (n)
    WHERE n.list[2] @> []
    RETURN n
$$) as (a agtype);

-- left contains <@ operator
SELECT *
FROM cypher('jsonb_operators', $$
    MATCH (n)
    RETURN  {c: {d: "a"}} <@ properties(n).json
$$) as (a agtype);

SELECT *
FROM cypher('jsonb_operators', $$
    MATCH (n)
    WHERE {c: {d: "a"}} <@ n.json
    RETURN n
$$) as (a agtype);

SELECT *
FROM cypher('jsonb_operators', $$
    MATCH (n)
    WHERE []  <@ n.list
    RETURN n
$$) as (a agtype);

SELECT *
FROM cypher('jsonb_operators', $$
    MATCH (n)
    WHERE {c: {d: "b"}} <@ n.json
    RETURN n
$$) as (a agtype);

SELECT *
FROM cypher('jsonb_operators', $$
    MATCH (n)
    WHERE [] <@ n.json
    RETURN n
$$) as (a agtype);

-- clean up
SELECT drop_graph('jsonb_operators', true);