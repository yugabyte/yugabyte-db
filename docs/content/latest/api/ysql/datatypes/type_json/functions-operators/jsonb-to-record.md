---
title: jsonb_to_record() and json_to_record()
headerTitle: jsonb_to_record() and json_to_record()
linkTitle: jsonb_to_record()
description: Convert a JSON object into the equivalent SQL record. Offers no practical advantage over the jsonb_populate_record() variant.
menu:
  latest:
    identifier: jsonb-to-record
    parent: functions-operators
    weight: 230
isTocNested: true
showAsideToc: true
---

**Purpose:** Convert a JSON _object_ into the equivalent SQL `record`.

**Signature** For the `jsonb` variant:

```
input value:       jsonb
return value:      record
```

**Notes:** The `jsonb_to_record()` function is a syntax variant of the same functionality that [`jsonb_populate_record`](../jsonb-populate-record/) provides. It doesn't need a schema-level type but, rather, uses the special SQL locution `select... as on_the_fly(<record definition>)`.

Use this _ysqlsh_ script to create the  type `t` that just `jsonb_populate_record()` requires, to convert the input `jsonb` into a SQL `record` using each of  `jsonb_populate_record()` and `jsonb_to_record`, and then to execute the `assert`. Notice that `on_the_fly` is a nonce name, made up for this example. Anything will suffice.

```postgresql
create type t as (a int, b text);

do $body$
declare
  object constant jsonb :=
    '{"a": 42, "b": "dog"}';

  result_1 constant t := jsonb_populate_record(null::t, object);
  result_2 t;
  expected_result constant t := (42, 'dog');
begin
  select a, b
  into strict result_2
  from jsonb_to_record(object)
  as on_the_fly(a int, b text);

  assert
    (result_1 = expected_result) and 
    (result_2 = expected_result),
  'unexpected';
end;
$body$;
```

The nominal advantage of `jsonb_to_record()`, that it doesn't need a schema-level type, is lost when the input JSON _object_ has another _object_ as the value of one of its keys. Consider this _ysqlsh_ script:

```postgresql
create type t1 as ( d int, e text);
create type t2 as (a int, b text[], c t1);

do $body$
declare
  nested_object constant jsonb :=
    '{"a": 42, "b": ["cat", "dog"], "c": {"d": 17, "e": "frog"}}';

  result_1 constant t2 := jsonb_populate_record(null::t2, nested_object);
  result_2 t2;

  expected_a constant int := 42;
  expected_b constant text[] := array['cat', 'dog'];
  expected_c constant t1 := (17, 'frog');
  expected_result constant t2 := (expected_a, expected_b, expected_c);
begin
  select a, b, c
  into strict result_2
  from jsonb_to_record(nested_object)
  as on_the_fly(a int, b text[], c t1);

  assert
    (result_1 = expected_result) and 
    (result_2 = expected_result),
  'unexpected';
end;
$body$;
```

It does show that `jsonb_populate_record()` and `jsonb_to_record()` both produce the same result from the same input. But, here, the `on_the_fly` type definition in the `as` clause

So the outer type `t2` can be defined on the fly in the `as` clause but it references the inner schema-level type `t1`. It isn't possible to absorb `t1`'s definition into the `as` clause. Moreover, the fact that `jsonb_to_record()` cannot be used in an ordinary assignment but requires a SQL `select ... into` statement is a serious drawback.

The `jsonb_to_record()` syntax variant therefore has no practical advantage over the `jsonb_populate_record()` variant.
