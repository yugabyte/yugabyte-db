---
title: row_to_json()
linkTitle: row_to_json() 
summary: row_to_json() 
description: row_to_json()
menu:
  latest:
    identifier: row-to-json
    parent: functions-operators
    weight: 270
isTocNested: true
showAsideToc: true
---

This has one variant that returns a `json` value. Here is the signature:

```
input value        record
pretty             boolean
return value       json
```

The first (mandatory) formal parameter is any SQL `record` whose fields might be compound values. The second formal parameter is optional. When it is _true_, line feeds are added between fields. Use this _ysqlsh_ script to create the required type `t` and then to execute the `assert`.

```postgresql
create type t as (a int, b text);

do $body$
declare
  row constant t := (42, 'dog');
  j_false constant json := row_to_json(row, false);
  j_true  constant json := row_to_json(row, true);
  expected_j_false constant json := '{"a":42,"b":"dog"}';
  expected_j_true  constant json := 
'{"a":42,
 "b":"dog"}';
begin
  assert
    (j_false::text = expected_j_false::text) and
    (j_true::text  = expected_j_true::text),
  'unexpected';
end;
$body$;
```

The `row_to_json()` function has no practical advantage over `to_json()` or `to_jsonb()` and is restricted because it explicitly handles a SQL `record` and cannot handle a SQL `array` (at top level). If you want to pretty-print the text representation of the JSON value result, you can use the `::text` typecast or `jsonb_pretty()`.
