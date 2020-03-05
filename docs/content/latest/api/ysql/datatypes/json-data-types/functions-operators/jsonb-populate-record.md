




### _jsonb_populate_record_ and _json_populate_record_

Here is the signature for the `jsonb` variant:

```
input value        anyelement, jsonb
return value       anyelement
```

The functions in this pair require that the supplied JSON value is an _object_. They translate the JSON _object_ into the equivalent SQL` record`. The data type of the `record` must be defined as a schema-level `type` whose name is passed via the function's first formal parameter using the locution `null:type_identifier`. The JSON value is passed via the second formal parameter. Use this _ysqlsh_ script to create the required types `t1` and `t2`, and then to execute the `assert`.

```
create type t1 as ( d int, e text);
create type t2 as (a int, b text[], c t1);

do $body$
declare
  nested_object constant jsonb :=
    '{"a": 42, "b": ["cat", "dog"], "c": {"d": 17, "e": "frog"}}';

  result constant t2 := jsonb_populate_record(null::t2, nested_object);

  expected_a constant int := 42;
  expected_b constant text[] := array['cat', 'dog'];
  expected_c constant t1 := (17, 'frog');
  expected_result constant t2 := (expected_a, expected_b, expected_c);
begin
  assert
    result = expected_result,
  'unexpected';
end;
$body$;
```

