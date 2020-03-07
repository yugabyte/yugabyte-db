---
title: jsonb_build_object()
linktitle: jsonb_build_object()
summary: jsonb_build_object() and json_build_object()
description: jsonb_build_object() and json_build_object()
menu:
  latest:
    identifier: jsonb-build-object
    parent: functions-operators
isTocNested: true
showAsideToc: true
---

Here is the signature for the `jsonb` variant:

```
input value        VARIADIC "any"
return value       jsonb
```

The `jsonb_build_object()` variadic function is the obvious counterpart to `jsonb_build_array()`. The argument list has the form:

```
key1::text, value1::the_data type1,
key1::text, value2::the_data type2,
...
keyN::text, valueN::the_data typeN
```

Use this _ysqlsh_ script to create the required type `t` and then to execute the `assert`.

```postgresql
create type t as (a int, b text);

do $body$
declare
  v_17 constant int := 17;
  v_dog constant text := 'dog';
  v_true constant boolean := true;
  v_t constant t := (17::int, 'cat'::text);

  j constant jsonb := jsonb_build_object(
    'a', v_17,
    'b', v_dog,
    'c', v_true,
    'd', v_t);
  expected_j constant jsonb := 
    '{"a": 17, "b": "dog", "c": true, "d": {"a": 17, "b": "cat"}}';
begin
  assert
    j = expected_j,
  'unexpected';
end;
$body$;
```

Just as with `jsonb_build_array())`, using `jsonb_build_object()` is straightforward when you know the structure of your target JSON value statically, and just discover the values dynamically. But again, it doesn't accommodate the case that you discover the desired structure dynamically.

The following _ysqlsh_ script shows a feasible general workaround for this use case. The helper function `f()` generates the variadic argument list as the text representation of a comma-separated list of manifest constants of various data types. Then it invokes `jsonb_build_object()` dynamically. Obviously this brings a performance cost. But you might not have an alternative.

```postgresql
create function f(variadic_array_elements in text) returns jsonb
  immutable
  language plpgsql
as $body$
declare
  stmt text := '
    select jsonb_build_object('||variadic_array_elements||')';
  j jsonb;
begin
  execute stmt into j;
  return j;
end;
$body$;

do $body$
declare
  v_17 constant int := 17;
  v_dog constant text := 'dog';
  v_true constant boolean := true;
  v_t constant t := (17::int, 'cat'::text);

  j constant jsonb :=  f(
    $$
      'a'::text, 17::integer,
      'b'::text, 'dog'::text,
      'c'::text, true::boolean,
      'd'::text, (17::int, 'cat'::text)::t
    $$);
  expected_j constant jsonb := 
    '{"a": 17, "b": "dog", "c": true, "d": {"a": 17, "b": "cat"}}';
begin
  assert
    j = expected_j,
  'unexpected';
end;
$body$;
```
