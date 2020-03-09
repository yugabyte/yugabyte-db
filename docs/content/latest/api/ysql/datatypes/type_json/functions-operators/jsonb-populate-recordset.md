---
title: jsonb_populate_recordset()
linkTitle: jsonb_populate_recordset()
summary: jsonb_populate_recordset() and json_populate_recordset()
description: jsonb_populate_recordset() and json_populate_recordset()
menu:
  latest:
    identifier: jsonb-populate-recordset
    parent: functions-operators
    weight: 190
isTocNested: true
showAsideToc: true
---

Here is the signature for the `jsonb` variant:

```
input value        anyelement, jsonb
return value       SETOF anyelement
```

The functions in this pair and are a natural extension of the functionality of `jsonb_populate_record()`.

Each requires that the supplied JSON value is an _array_, each of whose values is an _object_ which is compatible with the specified SQL `record` which is defined as a `type` whose name is passed via the function's first formal parameter using the locution `null:type_identifier`. The JSON value is passed via the second formal parameter. The result is a set (i.e. a table) of `record`s of the specified type.

Use this _ysqlsh_ script to create the  type `t`, and then to execute the `assert`. Notice the because the result is a table, it must be materialized in a `cursor for loop`. Each selected row is accumulated in an array of type `t[]`. The expected result is also established in an array of type `t[]`. The input JSON _array_ has been contrived, by sometimes not having a key `"a"` or a key `"b"` so that the resulting records sometimes have `null` fields. The equility test in the `assert` has to accommodate this. Therefore a helper PL/pgSQL function, `same_as()` is created to encapsulate the logic.

```postgresql
create type t as (a int, b int);

create function same_as(a1 t[], a2 t[]) returns boolean
  immutable
  language plpgsql
as $body$
declare
  v1 t;
  v2 t;
  n int := 0;
  result boolean := true;
begin
  foreach v1 in array a1 loop
    n := n + 1;
    v2 := a2[n];

    result := result and
      ((v1.a = v2.a) or (v1.a is null and v2.a is null))
      and
      ((v1.b = v2.b) or (v1.b is null and v2.b is null));
  end loop;
  return result;
end;
$body$;

do $body$
declare
  array_of_objects constant jsonb :=
    '[
      {"a": 1, "b": 2},
      {"b": 4, "a": 3},
      {"a": 5, "c": 6},
      {"b": 7, "d": 8},
      {"c": 9, "d": 0}
    ]';

  rows t[];
  expected_rows constant t[] :=
    array[
      (   1,    2)::t,
      (   3,    4)::t,
      (   5, null)::t,
      (null,    7)::t,
      (null, null)::t
    ];
  row t;
  n int := 0;
begin
  for row in (
    select a, b
    from jsonb_populate_recordset(null::t, array_of_objects)
    )
  loop
    n := n + 1;
    rows[n] := row;
  end loop;

  assert
    same_as(rows, expected_rows),
  'unexpected';
end;
$body$;
```
