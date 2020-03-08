---
title: array_to_json
linkTitle: array_to_json
summary: 
description: array_to_json
menu:
  latest:
    identifier: array-to-json
    parent: functions-operators
    weight: 10
isTocNested: true
showAsideToc: true
---

This has one variant that returns a `json` value. Here is the signature:

```
input value        anyarray
pretty             boolean
return value       json
```

The first (mandatory) formal parameter is any SQL `array` whose elements might be compound values. The second formal parameter is optional. When it is _true_, line feeds are added between dimension-1 elements.

```postgresql
do $body$
declare
  sql_array constant text[] := array['a', 'b', 'c'];

  j_false constant json := array_to_json(sql_array, false);
  j_true  constant json := array_to_json(sql_array, true);

  expected_j_false constant json := '["a","b","c"]';
  expected_j_true  constant json := 
'["a",
 "b",
 "c"]';
begin
  assert
    (j_false::text = expected_j_false::text) and
    (j_true::text  = expected_j_true::text),
  'unexpected';
end;
$body$;
```

The `array_to_json()` function has no practical advantage over `to_json()` or `to_jsonb()` and is restricted because it explicitly handles a SQL `array` and cannot handle a SQL `record` (at top level). Moreover, it can produce only a JSON _array_ whose values all have the same data type. If you want to pretty-print the text representation of the result JSON value, you can use the `::text` typecast or `jsonb_pretty()`.
