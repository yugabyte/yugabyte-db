-- Strings.
SELECT '""'::jsonb;				-- OK.
SELECT $$''$$::jsonb;			-- ERROR, single quotes are not allowed
SELECT '"abc"'::jsonb;			-- OK
SELECT '"abc'::jsonb;			-- ERROR, quotes not closed
SELECT '"abc
def"'::jsonb;					-- ERROR, unescaped newline in string constant
SELECT '"\n\"\\"'::jsonb;		-- OK, legal escapes
SELECT '"\v"'::jsonb;			-- ERROR, not a valid JSON escape
-- see json_encoding test for input with unicode escapes

-- Numbers.
SELECT '1'::jsonb;				-- OK
SELECT '0'::jsonb;				-- OK
SELECT '01'::jsonb;				-- ERROR, not valid according to JSON spec
SELECT '0.1'::jsonb;				-- OK
SELECT '9223372036854775808'::jsonb;	-- OK, even though it's too large for int8
SELECT '1e100'::jsonb;			-- OK
SELECT '1.3e100'::jsonb;			-- OK
SELECT '1f2'::jsonb;				-- ERROR
SELECT '0.x1'::jsonb;			-- ERROR
SELECT '1.3ex100'::jsonb;		-- ERROR

-- Arrays.
SELECT '[]'::jsonb;				-- OK
SELECT '[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]'::jsonb;  -- OK
SELECT '[1,2]'::jsonb;			-- OK
SELECT '[1,2,]'::jsonb;			-- ERROR, trailing comma
SELECT '[1,2'::jsonb;			-- ERROR, no closing bracket
SELECT '[1,[2]'::jsonb;			-- ERROR, no closing bracket

-- Objects.
SELECT '{}'::jsonb;				-- OK
SELECT '{"abc"}'::jsonb;			-- ERROR, no value
SELECT '{"abc":1}'::jsonb;		-- OK
SELECT '{1:"abc"}'::jsonb;		-- ERROR, keys must be strings
SELECT '{"abc",1}'::jsonb;		-- ERROR, wrong separator
SELECT '{"abc"=1}'::jsonb;		-- ERROR, totally wrong separator
SELECT '{"abc"::1}'::jsonb;		-- ERROR, another wrong separator
SELECT '{"abc":1,"def":2,"ghi":[3,4],"hij":{"klm":5,"nop":[6]}}'::jsonb; -- OK
SELECT '{"abc":1:2}'::jsonb;		-- ERROR, colon in wrong spot
SELECT '{"abc":1,3}'::jsonb;		-- ERROR, no value

-- Miscellaneous stuff.
SELECT 'true'::jsonb;			-- OK
SELECT 'false'::jsonb;			-- OK
SELECT 'null'::jsonb;			-- OK
SELECT ' true '::jsonb;			-- OK, even with extra whitespace
SELECT 'true false'::jsonb;		-- ERROR, too many values
SELECT 'true, false'::jsonb;		-- ERROR, too many values
SELECT 'truf'::jsonb;			-- ERROR, not a keyword
SELECT 'trues'::jsonb;			-- ERROR, not a keyword
SELECT ''::jsonb;				-- ERROR, no value
SELECT '    '::jsonb;			-- ERROR, no value

-- make sure jsonb is passed through json generators without being escaped
SELECT array_to_json(ARRAY [jsonb '{"a":1}', jsonb '{"b":[2,3]}']);

-- to_jsonb, timestamps

select to_jsonb(timestamp '2014-05-28 12:22:35.614298');

BEGIN;
SET LOCAL TIME ZONE 10.5;
select to_jsonb(timestamptz '2014-05-28 12:22:35.614298-04');
SET LOCAL TIME ZONE -8;
select to_jsonb(timestamptz '2014-05-28 12:22:35.614298-04');
COMMIT;

select to_jsonb(date '2014-05-28');

select to_jsonb(date 'Infinity');
select to_jsonb(date '-Infinity');
select to_jsonb(timestamp 'Infinity');
select to_jsonb(timestamp '-Infinity');
select to_jsonb(timestamptz 'Infinity');
select to_jsonb(timestamptz '-Infinity');

-- jsonb extraction functions
CREATE TABLE test_jsonb (
       json_type text,
       test_json jsonb
);

INSERT INTO test_jsonb VALUES
('scalar','"a scalar"'),
('array','["zero", "one","two",null,"four","five", [1,2,3],{"f1":9}]'),
('object','{"field1":"val1","field2":"val2","field3":null, "field4": 4, "field5": [1,2,3], "field6": {"f1":9}}');

SELECT test_json -> 'x' FROM test_jsonb WHERE json_type = 'scalar';
SELECT test_json -> 'x' FROM test_jsonb WHERE json_type = 'array';
SELECT test_json -> 'x' FROM test_jsonb WHERE json_type = 'object';
SELECT test_json -> 'field2' FROM test_jsonb WHERE json_type = 'object';

SELECT test_json ->> 'field2' FROM test_jsonb WHERE json_type = 'scalar';
SELECT test_json ->> 'field2' FROM test_jsonb WHERE json_type = 'array';
SELECT test_json ->> 'field2' FROM test_jsonb WHERE json_type = 'object';

SELECT test_json -> 2 FROM test_jsonb WHERE json_type = 'scalar';
SELECT test_json -> 2 FROM test_jsonb WHERE json_type = 'array';
SELECT test_json -> 9 FROM test_jsonb WHERE json_type = 'array';
SELECT test_json -> 2 FROM test_jsonb WHERE json_type = 'object';

SELECT test_json ->> 6 FROM test_jsonb WHERE json_type = 'array';
SELECT test_json ->> 7 FROM test_jsonb WHERE json_type = 'array';

SELECT test_json ->> 'field4' FROM test_jsonb WHERE json_type = 'object';
SELECT test_json ->> 'field5' FROM test_jsonb WHERE json_type = 'object';
SELECT test_json ->> 'field6' FROM test_jsonb WHERE json_type = 'object';

SELECT test_json ->> 2 FROM test_jsonb WHERE json_type = 'scalar';
SELECT test_json ->> 2 FROM test_jsonb WHERE json_type = 'array';
SELECT test_json ->> 2 FROM test_jsonb WHERE json_type = 'object';

SELECT jsonb_object_keys(test_json) FROM test_jsonb WHERE json_type = 'scalar';
SELECT jsonb_object_keys(test_json) FROM test_jsonb WHERE json_type = 'array';
SELECT jsonb_object_keys(test_json) FROM test_jsonb WHERE json_type = 'object';

-- nulls
SELECT (test_json->'field3') IS NULL AS expect_false FROM test_jsonb WHERE json_type = 'object';
SELECT (test_json->>'field3') IS NULL AS expect_true FROM test_jsonb WHERE json_type = 'object';
SELECT (test_json->3) IS NULL AS expect_false FROM test_jsonb WHERE json_type = 'array';
SELECT (test_json->>3) IS NULL AS expect_true FROM test_jsonb WHERE json_type = 'array';

-- corner cases
select '{"a": [{"b": "c"}, {"b": "cc"}]}'::jsonb -> null::text;
select '{"a": [{"b": "c"}, {"b": "cc"}]}'::jsonb -> null::int;
select '{"a": [{"b": "c"}, {"b": "cc"}]}'::jsonb -> 1;
select '{"a": [{"b": "c"}, {"b": "cc"}]}'::jsonb -> 'z';
select '{"a": [{"b": "c"}, {"b": "cc"}]}'::jsonb -> '';
select '[{"b": "c"}, {"b": "cc"}]'::jsonb -> 1;
select '[{"b": "c"}, {"b": "cc"}]'::jsonb -> 3;
select '[{"b": "c"}, {"b": "cc"}]'::jsonb -> 'z';
select '{"a": "c", "b": null}'::jsonb -> 'b';
select '"foo"'::jsonb -> 1;
select '"foo"'::jsonb -> 'z';

select '{"a": [{"b": "c"}, {"b": "cc"}]}'::jsonb ->> null::text;
select '{"a": [{"b": "c"}, {"b": "cc"}]}'::jsonb ->> null::int;
select '{"a": [{"b": "c"}, {"b": "cc"}]}'::jsonb ->> 1;
select '{"a": [{"b": "c"}, {"b": "cc"}]}'::jsonb ->> 'z';
select '{"a": [{"b": "c"}, {"b": "cc"}]}'::jsonb ->> '';
select '[{"b": "c"}, {"b": "cc"}]'::jsonb ->> 1;
select '[{"b": "c"}, {"b": "cc"}]'::jsonb ->> 3;
select '[{"b": "c"}, {"b": "cc"}]'::jsonb ->> 'z';
select '{"a": "c", "b": null}'::jsonb ->> 'b';
select '"foo"'::jsonb ->> 1;
select '"foo"'::jsonb ->> 'z';

-- equality and inequality
SELECT '{"x":"y"}'::jsonb = '{"x":"y"}'::jsonb;
SELECT '{"x":"y"}'::jsonb = '{"x":"z"}'::jsonb;

SELECT '{"x":"y"}'::jsonb <> '{"x":"y"}'::jsonb;
SELECT '{"x":"y"}'::jsonb <> '{"x":"z"}'::jsonb;

-- containment
SELECT jsonb_contains('{"a":"b", "b":1, "c":null}', '{"a":"b"}');
SELECT jsonb_contains('{"a":"b", "b":1, "c":null}', '{"a":"b", "c":null}');
SELECT jsonb_contains('{"a":"b", "b":1, "c":null}', '{"a":"b", "g":null}');
SELECT jsonb_contains('{"a":"b", "b":1, "c":null}', '{"g":null}');
SELECT jsonb_contains('{"a":"b", "b":1, "c":null}', '{"a":"c"}');
SELECT jsonb_contains('{"a":"b", "b":1, "c":null}', '{"a":"b"}');
SELECT jsonb_contains('{"a":"b", "b":1, "c":null}', '{"a":"b", "c":"q"}');
SELECT '{"a":"b", "b":1, "c":null}'::jsonb @> '{"a":"b"}';
SELECT '{"a":"b", "b":1, "c":null}'::jsonb @> '{"a":"b", "c":null}';
SELECT '{"a":"b", "b":1, "c":null}'::jsonb @> '{"a":"b", "g":null}';
SELECT '{"a":"b", "b":1, "c":null}'::jsonb @> '{"g":null}';
SELECT '{"a":"b", "b":1, "c":null}'::jsonb @> '{"a":"c"}';
SELECT '{"a":"b", "b":1, "c":null}'::jsonb @> '{"a":"b"}';
SELECT '{"a":"b", "b":1, "c":null}'::jsonb @> '{"a":"b", "c":"q"}';

SELECT '[1,2]'::jsonb @> '[1,2,2]'::jsonb;
SELECT '[1,1,2]'::jsonb @> '[1,2,2]'::jsonb;
SELECT '[[1,2]]'::jsonb @> '[[1,2,2]]'::jsonb;
SELECT '[1,2,2]'::jsonb <@ '[1,2]'::jsonb;
SELECT '[1,2,2]'::jsonb <@ '[1,1,2]'::jsonb;
SELECT '[[1,2,2]]'::jsonb <@ '[[1,2]]'::jsonb;

SELECT jsonb_contained('{"a":"b"}', '{"a":"b", "b":1, "c":null}');
SELECT jsonb_contained('{"a":"b", "c":null}', '{"a":"b", "b":1, "c":null}');
SELECT jsonb_contained('{"a":"b", "g":null}', '{"a":"b", "b":1, "c":null}');
SELECT jsonb_contained('{"g":null}', '{"a":"b", "b":1, "c":null}');
SELECT jsonb_contained('{"a":"c"}', '{"a":"b", "b":1, "c":null}');
SELECT jsonb_contained('{"a":"b"}', '{"a":"b", "b":1, "c":null}');
SELECT jsonb_contained('{"a":"b", "c":"q"}', '{"a":"b", "b":1, "c":null}');
SELECT '{"a":"b"}'::jsonb <@ '{"a":"b", "b":1, "c":null}';
SELECT '{"a":"b", "c":null}'::jsonb <@ '{"a":"b", "b":1, "c":null}';
SELECT '{"a":"b", "g":null}'::jsonb <@ '{"a":"b", "b":1, "c":null}';
SELECT '{"g":null}'::jsonb <@ '{"a":"b", "b":1, "c":null}';
SELECT '{"a":"c"}'::jsonb <@ '{"a":"b", "b":1, "c":null}';
SELECT '{"a":"b"}'::jsonb <@ '{"a":"b", "b":1, "c":null}';
SELECT '{"a":"b", "c":"q"}'::jsonb <@ '{"a":"b", "b":1, "c":null}';
-- Raw scalar may contain another raw scalar, array may contain a raw scalar
SELECT '[5]'::jsonb @> '[5]';
SELECT '5'::jsonb @> '5';
SELECT '[5]'::jsonb @> '5';
-- But a raw scalar cannot contain an array
SELECT '5'::jsonb @> '[5]';
-- In general, one thing should always contain itself. Test array containment:
SELECT '["9", ["7", "3"], 1]'::jsonb @> '["9", ["7", "3"], 1]'::jsonb;
SELECT '["9", ["7", "3"], ["1"]]'::jsonb @> '["9", ["7", "3"], ["1"]]'::jsonb;
-- array containment string matching confusion bug
SELECT '{ "name": "Bob", "tags": [ "enim", "qui"]}'::jsonb @> '{"tags":["qu"]}';

-- array length
SELECT jsonb_array_length('[1,2,3,{"f1":1,"f2":[5,6]},4]');
SELECT jsonb_array_length('[]');
SELECT jsonb_array_length('{"f1":1,"f2":[5,6]}');
SELECT jsonb_array_length('4');

-- each
SELECT jsonb_each('{"f1":[1,2,3],"f2":{"f3":1},"f4":null}');
SELECT jsonb_each('{"a":{"b":"c","c":"b","1":"first"},"b":[1,2],"c":"cc","1":"first","n":null}'::jsonb) AS q;
SELECT * FROM jsonb_each('{"f1":[1,2,3],"f2":{"f3":1},"f4":null,"f5":99,"f6":"stringy"}') q;
SELECT * FROM jsonb_each('{"a":{"b":"c","c":"b","1":"first"},"b":[1,2],"c":"cc","1":"first","n":null}'::jsonb) AS q;

SELECT jsonb_each_text('{"f1":[1,2,3],"f2":{"f3":1},"f4":null,"f5":"null"}');
SELECT jsonb_each_text('{"a":{"b":"c","c":"b","1":"first"},"b":[1,2],"c":"cc","1":"first","n":null}'::jsonb) AS q;
SELECT * FROM jsonb_each_text('{"f1":[1,2,3],"f2":{"f3":1},"f4":null,"f5":99,"f6":"stringy"}') q;
SELECT * FROM jsonb_each_text('{"a":{"b":"c","c":"b","1":"first"},"b":[1,2],"c":"cc","1":"first","n":null}'::jsonb) AS q;

-- exists
SELECT jsonb_exists('{"a":null, "b":"qq"}', 'a');
SELECT jsonb_exists('{"a":null, "b":"qq"}', 'b');
SELECT jsonb_exists('{"a":null, "b":"qq"}', 'c');
SELECT jsonb_exists('{"a":"null", "b":"qq"}', 'a');
SELECT jsonb '{"a":null, "b":"qq"}' ? 'a';
SELECT jsonb '{"a":null, "b":"qq"}' ? 'b';
SELECT jsonb '{"a":null, "b":"qq"}' ? 'c';
SELECT jsonb '{"a":"null", "b":"qq"}' ? 'a';

SELECT jsonb_exists_any('{"a":null, "b":"qq"}', ARRAY['a','b']);
SELECT jsonb_exists_any('{"a":null, "b":"qq"}', ARRAY['b','a']);
SELECT jsonb_exists_any('{"a":null, "b":"qq"}', ARRAY['c','a']);
SELECT jsonb_exists_any('{"a":null, "b":"qq"}', ARRAY['c','d']);
SELECT jsonb_exists_any('{"a":null, "b":"qq"}', '{}'::text[]);
SELECT jsonb '{"a":null, "b":"qq"}' ?| ARRAY['a','b'];
SELECT jsonb '{"a":null, "b":"qq"}' ?| ARRAY['b','a'];
SELECT jsonb '{"a":null, "b":"qq"}' ?| ARRAY['c','a'];
SELECT jsonb '{"a":null, "b":"qq"}' ?| ARRAY['c','d'];
SELECT jsonb '{"a":null, "b":"qq"}' ?| '{}'::text[];

SELECT jsonb_exists_all('{"a":null, "b":"qq"}', ARRAY['a','b']);
SELECT jsonb_exists_all('{"a":null, "b":"qq"}', ARRAY['b','a']);
SELECT jsonb_exists_all('{"a":null, "b":"qq"}', ARRAY['c','a']);
SELECT jsonb_exists_all('{"a":null, "b":"qq"}', ARRAY['c','d']);
SELECT jsonb_exists_all('{"a":null, "b":"qq"}', '{}'::text[]);
SELECT jsonb '{"a":null, "b":"qq"}' ?& ARRAY['a','b'];
SELECT jsonb '{"a":null, "b":"qq"}' ?& ARRAY['b','a'];
SELECT jsonb '{"a":null, "b":"qq"}' ?& ARRAY['c','a'];
SELECT jsonb '{"a":null, "b":"qq"}' ?& ARRAY['c','d'];
SELECT jsonb '{"a":null, "b":"qq"}' ?& ARRAY['a','a', 'b', 'b', 'b'];
SELECT jsonb '{"a":null, "b":"qq"}' ?& '{}'::text[];

-- typeof
SELECT jsonb_typeof('{}') AS object;
SELECT jsonb_typeof('{"c":3,"p":"o"}') AS object;
SELECT jsonb_typeof('[]') AS array;
SELECT jsonb_typeof('["a", 1]') AS array;
SELECT jsonb_typeof('null') AS "null";
SELECT jsonb_typeof('1') AS number;
SELECT jsonb_typeof('-1') AS number;
SELECT jsonb_typeof('1.0') AS number;
SELECT jsonb_typeof('1e2') AS number;
SELECT jsonb_typeof('-1.0') AS number;
SELECT jsonb_typeof('true') AS boolean;
SELECT jsonb_typeof('false') AS boolean;
SELECT jsonb_typeof('"hello"') AS string;
SELECT jsonb_typeof('"true"') AS string;
SELECT jsonb_typeof('"1.0"') AS string;

-- jsonb_build_array, jsonb_build_object, jsonb_object_agg

SELECT jsonb_build_array('a',1,'b',1.2,'c',true,'d',null,'e',json '{"x": 3, "y": [1,2,3]}');
SELECT jsonb_build_array('a', NULL); -- ok
SELECT jsonb_build_array(VARIADIC NULL::text[]); -- ok
SELECT jsonb_build_array(VARIADIC '{}'::text[]); -- ok
SELECT jsonb_build_array(VARIADIC '{a,b,c}'::text[]); -- ok
SELECT jsonb_build_array(VARIADIC ARRAY['a', NULL]::text[]); -- ok
SELECT jsonb_build_array(VARIADIC '{1,2,3,4}'::text[]); -- ok
SELECT jsonb_build_array(VARIADIC '{1,2,3,4}'::int[]); -- ok
SELECT jsonb_build_array(VARIADIC '{{1,4},{2,5},{3,6}}'::int[][]); -- ok

SELECT jsonb_build_object('a',1,'b',1.2,'c',true,'d',null,'e',json '{"x": 3, "y": [1,2,3]}');

SELECT jsonb_build_object('{a,b,c}'::text[]); -- error
SELECT jsonb_build_object('{a,b,c}'::text[], '{d,e,f}'::text[]); -- error, key cannot be array
SELECT jsonb_build_object('a', 'b', 'c'); -- error
SELECT jsonb_build_object(NULL, 'a'); -- error, key cannot be NULL
SELECT jsonb_build_object('a', NULL); -- ok
SELECT jsonb_build_object(VARIADIC NULL::text[]); -- ok
SELECT jsonb_build_object(VARIADIC '{}'::text[]); -- ok
SELECT jsonb_build_object(VARIADIC '{a,b,c}'::text[]); -- error
SELECT jsonb_build_object(VARIADIC ARRAY['a', NULL]::text[]); -- ok
SELECT jsonb_build_object(VARIADIC ARRAY[NULL, 'a']::text[]); -- error, key cannot be NULL
SELECT jsonb_build_object(VARIADIC '{1,2,3,4}'::text[]); -- ok
SELECT jsonb_build_object(VARIADIC '{1,2,3,4}'::int[]); -- ok
SELECT jsonb_build_object(VARIADIC '{{1,4},{2,5},{3,6}}'::int[][]); -- ok

-- empty objects/arrays
SELECT jsonb_build_array();

SELECT jsonb_build_object();

-- make sure keys are quoted
SELECT jsonb_build_object(1,2);

-- keys must be scalar and not null
SELECT jsonb_build_object(null,2);

SELECT jsonb_build_object(r,2) FROM (SELECT 1 AS a, 2 AS b) r;

SELECT jsonb_build_object(json '{"a":1,"b":2}', 3);

SELECT jsonb_build_object('{1,2,3}'::int[], 3);

-- handling of NULL values
SELECT jsonb_object_agg(1, NULL::jsonb);
SELECT jsonb_object_agg(NULL, '{"a":1}');

CREATE TABLE foo (serial_num int, name text, type text);
INSERT INTO foo VALUES (847001,'t15','GE1043');
INSERT INTO foo VALUES (847002,'t16','GE1043');
INSERT INTO foo VALUES (847003,'sub-alpha','GESS90');

SELECT jsonb_build_object('turbines',jsonb_object_agg(serial_num,jsonb_build_object('name',name,'type',type)))
FROM foo;

SELECT jsonb_object_agg(name, type) FROM foo;

INSERT INTO foo VALUES (999999, NULL, 'bar');
SELECT jsonb_object_agg(name, type) FROM foo;

-- jsonb_object

-- empty object, one dimension
SELECT jsonb_object('{}');

-- empty object, two dimensions
SELECT jsonb_object('{}', '{}');

-- one dimension
SELECT jsonb_object('{a,1,b,2,3,NULL,"d e f","a b c"}');

-- same but with two dimensions
SELECT jsonb_object('{{a,1},{b,2},{3,NULL},{"d e f","a b c"}}');

-- odd number error
SELECT jsonb_object('{a,b,c}');

-- one column error
SELECT jsonb_object('{{a},{b}}');

-- too many columns error
SELECT jsonb_object('{{a,b,c},{b,c,d}}');

-- too many dimensions error
SELECT jsonb_object('{{{a,b},{c,d}},{{b,c},{d,e}}}');

--two argument form of jsonb_object

select jsonb_object('{a,b,c,"d e f"}','{1,2,3,"a b c"}');

-- too many dimensions
SELECT jsonb_object('{{a,1},{b,2},{3,NULL},{"d e f","a b c"}}', '{{a,1},{b,2},{3,NULL},{"d e f","a b c"}}');

-- mismatched dimensions

select jsonb_object('{a,b,c,"d e f",g}','{1,2,3,"a b c"}');

select jsonb_object('{a,b,c,"d e f"}','{1,2,3,"a b c",g}');

-- null key error

select jsonb_object('{a,b,NULL,"d e f"}','{1,2,3,"a b c"}');

-- empty key is allowed

select jsonb_object('{a,b,"","d e f"}','{1,2,3,"a b c"}');



-- extract_path, extract_path_as_text
SELECT jsonb_extract_path('{"f2":{"f3":1},"f4":{"f5":99,"f6":"stringy"}}','f4','f6');
SELECT jsonb_extract_path('{"f2":{"f3":1},"f4":{"f5":99,"f6":"stringy"}}','f2');
SELECT jsonb_extract_path('{"f2":["f3",1],"f4":{"f5":99,"f6":"stringy"}}','f2',0::text);
SELECT jsonb_extract_path('{"f2":["f3",1],"f4":{"f5":99,"f6":"stringy"}}','f2',1::text);
SELECT jsonb_extract_path_text('{"f2":{"f3":1},"f4":{"f5":99,"f6":"stringy"}}','f4','f6');
SELECT jsonb_extract_path_text('{"f2":{"f3":1},"f4":{"f5":99,"f6":"stringy"}}','f2');
SELECT jsonb_extract_path_text('{"f2":["f3",1],"f4":{"f5":99,"f6":"stringy"}}','f2',0::text);
SELECT jsonb_extract_path_text('{"f2":["f3",1],"f4":{"f5":99,"f6":"stringy"}}','f2',1::text);

-- extract_path nulls
SELECT jsonb_extract_path('{"f2":{"f3":1},"f4":{"f5":null,"f6":"stringy"}}','f4','f5') IS NULL AS expect_false;
SELECT jsonb_extract_path_text('{"f2":{"f3":1},"f4":{"f5":null,"f6":"stringy"}}','f4','f5') IS NULL AS expect_true;
SELECT jsonb_extract_path('{"f2":{"f3":1},"f4":[0,1,2,null]}','f4','3') IS NULL AS expect_false;
SELECT jsonb_extract_path_text('{"f2":{"f3":1},"f4":[0,1,2,null]}','f4','3') IS NULL AS expect_true;

-- extract_path operators
SELECT '{"f2":{"f3":1},"f4":{"f5":99,"f6":"stringy"}}'::jsonb#>array['f4','f6'];
SELECT '{"f2":{"f3":1},"f4":{"f5":99,"f6":"stringy"}}'::jsonb#>array['f2'];
SELECT '{"f2":["f3",1],"f4":{"f5":99,"f6":"stringy"}}'::jsonb#>array['f2','0'];
SELECT '{"f2":["f3",1],"f4":{"f5":99,"f6":"stringy"}}'::jsonb#>array['f2','1'];

SELECT '{"f2":{"f3":1},"f4":{"f5":99,"f6":"stringy"}}'::jsonb#>>array['f4','f6'];
SELECT '{"f2":{"f3":1},"f4":{"f5":99,"f6":"stringy"}}'::jsonb#>>array['f2'];
SELECT '{"f2":["f3",1],"f4":{"f5":99,"f6":"stringy"}}'::jsonb#>>array['f2','0'];
SELECT '{"f2":["f3",1],"f4":{"f5":99,"f6":"stringy"}}'::jsonb#>>array['f2','1'];

-- corner cases for same
select '{"a": {"b":{"c": "foo"}}}'::jsonb #> '{}';
select '[1,2,3]'::jsonb #> '{}';
select '"foo"'::jsonb #> '{}';
select '42'::jsonb #> '{}';
select 'null'::jsonb #> '{}';
select '{"a": {"b":{"c": "foo"}}}'::jsonb #> array['a'];
select '{"a": {"b":{"c": "foo"}}}'::jsonb #> array['a', null];
select '{"a": {"b":{"c": "foo"}}}'::jsonb #> array['a', ''];
select '{"a": {"b":{"c": "foo"}}}'::jsonb #> array['a','b'];
select '{"a": {"b":{"c": "foo"}}}'::jsonb #> array['a','b','c'];
select '{"a": {"b":{"c": "foo"}}}'::jsonb #> array['a','b','c','d'];
select '{"a": {"b":{"c": "foo"}}}'::jsonb #> array['a','z','c'];
select '{"a": [{"b": "c"}, {"b": "cc"}]}'::jsonb #> array['a','1','b'];
select '{"a": [{"b": "c"}, {"b": "cc"}]}'::jsonb #> array['a','z','b'];
select '[{"b": "c"}, {"b": "cc"}]'::jsonb #> array['1','b'];
select '[{"b": "c"}, {"b": "cc"}]'::jsonb #> array['z','b'];
select '[{"b": "c"}, {"b": null}]'::jsonb #> array['1','b'];
select '"foo"'::jsonb #> array['z'];
select '42'::jsonb #> array['f2'];
select '42'::jsonb #> array['0'];

select '{"a": {"b":{"c": "foo"}}}'::jsonb #>> '{}';
select '[1,2,3]'::jsonb #>> '{}';
select '"foo"'::jsonb #>> '{}';
select '42'::jsonb #>> '{}';
select 'null'::jsonb #>> '{}';
select '{"a": {"b":{"c": "foo"}}}'::jsonb #>> array['a'];
select '{"a": {"b":{"c": "foo"}}}'::jsonb #>> array['a', null];
select '{"a": {"b":{"c": "foo"}}}'::jsonb #>> array['a', ''];
select '{"a": {"b":{"c": "foo"}}}'::jsonb #>> array['a','b'];
select '{"a": {"b":{"c": "foo"}}}'::jsonb #>> array['a','b','c'];
select '{"a": {"b":{"c": "foo"}}}'::jsonb #>> array['a','b','c','d'];
select '{"a": {"b":{"c": "foo"}}}'::jsonb #>> array['a','z','c'];
select '{"a": [{"b": "c"}, {"b": "cc"}]}'::jsonb #>> array['a','1','b'];
select '{"a": [{"b": "c"}, {"b": "cc"}]}'::jsonb #>> array['a','z','b'];
select '[{"b": "c"}, {"b": "cc"}]'::jsonb #>> array['1','b'];
select '[{"b": "c"}, {"b": "cc"}]'::jsonb #>> array['z','b'];
select '[{"b": "c"}, {"b": null}]'::jsonb #>> array['1','b'];
select '"foo"'::jsonb #>> array['z'];
select '42'::jsonb #>> array['f2'];
select '42'::jsonb #>> array['0'];

-- array_elements
SELECT jsonb_array_elements('[1,true,[1,[2,3]],null,{"f1":1,"f2":[7,8,9]},false]');
SELECT * FROM jsonb_array_elements('[1,true,[1,[2,3]],null,{"f1":1,"f2":[7,8,9]},false]') q;
SELECT jsonb_array_elements_text('[1,true,[1,[2,3]],null,{"f1":1,"f2":[7,8,9]},false,"stringy"]');
SELECT * FROM jsonb_array_elements_text('[1,true,[1,[2,3]],null,{"f1":1,"f2":[7,8,9]},false,"stringy"]') q;

-- jsonb_strip_nulls

select jsonb_strip_nulls(null);

select jsonb_strip_nulls('1');

select jsonb_strip_nulls('"a string"');

select jsonb_strip_nulls('null');

select jsonb_strip_nulls('[1,2,null,3,4]');

select jsonb_strip_nulls('{"a":1,"b":null,"c":[2,null,3],"d":{"e":4,"f":null}}');

select jsonb_strip_nulls('[1,{"a":1,"b":null,"c":2},3]');

-- an empty object is not null and should not be stripped
select jsonb_strip_nulls('{"a": {"b": null, "c": null}, "d": {} }');


select jsonb_pretty('{"a": "test", "b": [1, 2, 3], "c": "test3", "d":{"dd": "test4", "dd2":{"ddd": "test5"}}}');
select jsonb_pretty('[{"f1":1,"f2":null},2,null,[[{"x":true},6,7],8],3]');
select jsonb_pretty('{"a":["b", "c"], "d": {"e":"f"}}');

select jsonb_concat('{"d": "test", "a": [1, 2]}', '{"g": "test2", "c": {"c1":1, "c2":2}}');

select '{"aa":1 , "b":2, "cq":3}'::jsonb || '{"cq":"l", "b":"g", "fg":false}';
select '{"aa":1 , "b":2, "cq":3}'::jsonb || '{"aq":"l"}';
select '{"aa":1 , "b":2, "cq":3}'::jsonb || '{"aa":"l"}';
select '{"aa":1 , "b":2, "cq":3}'::jsonb || '{}';

select '["a", "b"]'::jsonb || '["c"]';
select '["a", "b"]'::jsonb || '["c", "d"]';
select '["c"]' || '["a", "b"]'::jsonb;

select '["a", "b"]'::jsonb || '"c"';
select '"c"' || '["a", "b"]'::jsonb;

select '[]'::jsonb || '["a"]'::jsonb;
select '[]'::jsonb || '"a"'::jsonb;
select '"b"'::jsonb || '"a"'::jsonb;
select '{}'::jsonb || '{"a":"b"}'::jsonb;
select '[]'::jsonb || '{"a":"b"}'::jsonb;
select '{"a":"b"}'::jsonb || '[]'::jsonb;

select '"a"'::jsonb || '{"a":1}';
select '{"a":1}' || '"a"'::jsonb;

select '["a", "b"]'::jsonb || '{"c":1}';
select '{"c": 1}'::jsonb || '["a", "b"]';

select '{}'::jsonb || '{"cq":"l", "b":"g", "fg":false}';

select jsonb_delete('{"a":1 , "b":2, "c":3}'::jsonb, 'a');
select jsonb_delete('{"a":null , "b":2, "c":3}'::jsonb, 'a');
select jsonb_delete('{"a":1 , "b":2, "c":3}'::jsonb, 'b');
select jsonb_delete('{"a":1 , "b":2, "c":3}'::jsonb, 'c');
select jsonb_delete('{"a":1 , "b":2, "c":3}'::jsonb, 'd');
select '{"a":1 , "b":2, "c":3}'::jsonb - 'a';
select '{"a":null , "b":2, "c":3}'::jsonb - 'a';
select '{"a":1 , "b":2, "c":3}'::jsonb - 'b';
select '{"a":1 , "b":2, "c":3}'::jsonb - 'c';
select '{"a":1 , "b":2, "c":3}'::jsonb - 'd';

select '["a","b","c"]'::jsonb - 3;
select '["a","b","c"]'::jsonb - 2;
select '["a","b","c"]'::jsonb - 1;
select '["a","b","c"]'::jsonb - 0;
select '["a","b","c"]'::jsonb - -1;
select '["a","b","c"]'::jsonb - -2;
select '["a","b","c"]'::jsonb - -3;
select '["a","b","c"]'::jsonb - -4;

select '{"a":1 , "b":2, "c":3}'::jsonb - '{b}'::text[];
select '{"a":1 , "b":2, "c":3}'::jsonb - '{c,b}'::text[];
select '{"a":1 , "b":2, "c":3}'::jsonb - '{}'::text[];

select jsonb_set('{"n":null, "a":1, "b":[1,2], "c":{"1":2}, "d":{"1":[2,3]}}'::jsonb, '{n}', '[1,2,3]');
select jsonb_set('{"n":null, "a":1, "b":[1,2], "c":{"1":2}, "d":{"1":[2,3]}}'::jsonb, '{b,-1}', '[1,2,3]');
select jsonb_set('{"n":null, "a":1, "b":[1,2], "c":{"1":2}, "d":{"1":[2,3]}}'::jsonb, '{d,1,0}', '[1,2,3]');
select jsonb_set('{"n":null, "a":1, "b":[1,2], "c":{"1":2}, "d":{"1":[2,3]}}'::jsonb, '{d,NULL,0}', '[1,2,3]');

select jsonb_set('{"n":null, "a":1, "b":[1,2], "c":{"1":2}, "d":{"1":[2,3]}}'::jsonb, '{n}', '{"1": 2}');
select jsonb_set('{"n":null, "a":1, "b":[1,2], "c":{"1":2}, "d":{"1":[2,3]}}'::jsonb, '{b,-1}', '{"1": 2}');
select jsonb_set('{"n":null, "a":1, "b":[1,2], "c":{"1":2}, "d":{"1":[2,3]}}'::jsonb, '{d,1,0}', '{"1": 2}');
select jsonb_set('{"n":null, "a":1, "b":[1,2], "c":{"1":2}, "d":{"1":[2,3]}}'::jsonb, '{d,NULL,0}', '{"1": 2}');

select jsonb_set('{"n":null, "a":1, "b":[1,2], "c":{"1":2}, "d":{"1":[2,3]}}'::jsonb, '{b,-1}', '"test"');
select jsonb_set('{"n":null, "a":1, "b":[1,2], "c":{"1":2}, "d":{"1":[2,3]}}'::jsonb, '{b,-1}', '{"f": "test"}');

select jsonb_delete_path('{"n":null, "a":1, "b":[1,2], "c":{"1":2}, "d":{"1":[2,3]}}', '{n}');
select jsonb_delete_path('{"n":null, "a":1, "b":[1,2], "c":{"1":2}, "d":{"1":[2,3]}}', '{b,-1}');
select jsonb_delete_path('{"n":null, "a":1, "b":[1,2], "c":{"1":2}, "d":{"1":[2,3]}}', '{d,1,0}');

select '{"n":null, "a":1, "b":[1,2], "c":{"1":2}, "d":{"1":[2,3]}}'::jsonb #- '{n}';
select '{"n":null, "a":1, "b":[1,2], "c":{"1":2}, "d":{"1":[2,3]}}'::jsonb #- '{b,-1}';
select '{"n":null, "a":1, "b":[1,2], "c":{"1":2}, "d":{"1":[2,3]}}'::jsonb #- '{b,-1e}'; -- invalid array subscript
select '{"n":null, "a":1, "b":[1,2], "c":{"1":2}, "d":{"1":[2,3]}}'::jsonb #- '{d,1,0}';


-- empty structure and error conditions for delete and replace

select '"a"'::jsonb - 'a'; -- error
select '{}'::jsonb - 'a';
select '[]'::jsonb - 'a';
select '"a"'::jsonb - 1; -- error
select '{}'::jsonb -  1; -- error
select '[]'::jsonb - 1;
select '"a"'::jsonb #- '{a}'; -- error
select '{}'::jsonb #- '{a}';
select '[]'::jsonb #- '{a}';
select jsonb_set('"a"','{a}','"b"'); --error
select jsonb_set('{}','{a}','"b"', false);
select jsonb_set('[]','{1}','"b"', false);
select jsonb_set('[{"f1":1,"f2":null},2,null,3]', '{0}','[2,3,4]', false);

-- jsonb_set adding instead of replacing

-- prepend to array
select jsonb_set('{"a":1,"b":[0,1,2],"c":{"d":4}}','{b,-33}','{"foo":123}');
-- append to array
select jsonb_set('{"a":1,"b":[0,1,2],"c":{"d":4}}','{b,33}','{"foo":123}');
-- check nesting levels addition
select jsonb_set('{"a":1,"b":[4,5,[0,1,2],6,7],"c":{"d":4}}','{b,2,33}','{"foo":123}');
-- add new key
select jsonb_set('{"a":1,"b":[0,1,2],"c":{"d":4}}','{c,e}','{"foo":123}');
-- adding doesn't do anything if elements before last aren't present
select jsonb_set('{"a":1,"b":[0,1,2],"c":{"d":4}}','{x,-33}','{"foo":123}');
select jsonb_set('{"a":1,"b":[0,1,2],"c":{"d":4}}','{x,y}','{"foo":123}');
-- add to empty object
select jsonb_set('{}','{x}','{"foo":123}');
--add to empty array
select jsonb_set('[]','{0}','{"foo":123}');
select jsonb_set('[]','{99}','{"foo":123}');
select jsonb_set('[]','{-99}','{"foo":123}');
select jsonb_set('{"a": [1, 2, 3]}', '{a, non_integer}', '"new_value"');
select jsonb_set('{"a": {"b": [1, 2, 3]}}', '{a, b, non_integer}', '"new_value"');
select jsonb_set('{"a": {"b": [1, 2, 3]}}', '{a, b, NULL}', '"new_value"');


-- jsonb_insert
select jsonb_insert('{"a": [0,1,2]}', '{a, 1}', '"new_value"');
select jsonb_insert('{"a": [0,1,2]}', '{a, 1}', '"new_value"', true);
select jsonb_insert('{"a": {"b": {"c": [0, 1, "test1", "test2"]}}}', '{a, b, c, 2}', '"new_value"');
select jsonb_insert('{"a": {"b": {"c": [0, 1, "test1", "test2"]}}}', '{a, b, c, 2}', '"new_value"', true);
select jsonb_insert('{"a": [0,1,2]}', '{a, 1}', '{"b": "value"}');
select jsonb_insert('{"a": [0,1,2]}', '{a, 1}', '["value1", "value2"]');

-- edge cases
select jsonb_insert('{"a": [0,1,2]}', '{a, 0}', '"new_value"');
select jsonb_insert('{"a": [0,1,2]}', '{a, 0}', '"new_value"', true);
select jsonb_insert('{"a": [0,1,2]}', '{a, 2}', '"new_value"');
select jsonb_insert('{"a": [0,1,2]}', '{a, 2}', '"new_value"', true);
select jsonb_insert('{"a": [0,1,2]}', '{a, -1}', '"new_value"');
select jsonb_insert('{"a": [0,1,2]}', '{a, -1}', '"new_value"', true);
select jsonb_insert('[]', '{1}', '"new_value"');
select jsonb_insert('[]', '{1}', '"new_value"', true);
select jsonb_insert('{"a": []}', '{a, 1}', '"new_value"');
select jsonb_insert('{"a": []}', '{a, 1}', '"new_value"', true);
select jsonb_insert('{"a": [0,1,2]}', '{a, 10}', '"new_value"');
select jsonb_insert('{"a": [0,1,2]}', '{a, -10}', '"new_value"');

-- jsonb_insert should be able to insert new value for objects, but not to replace
select jsonb_insert('{"a": {"b": "value"}}', '{a, c}', '"new_value"');
select jsonb_insert('{"a": {"b": "value"}}', '{a, c}', '"new_value"', true);

select jsonb_insert('{"a": {"b": "value"}}', '{a, b}', '"new_value"');
select jsonb_insert('{"a": {"b": "value"}}', '{a, b}', '"new_value"', true);

DROP TABLE TEST_JSONB;
DROP TABLE FOO;
