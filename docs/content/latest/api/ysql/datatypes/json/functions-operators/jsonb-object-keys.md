---
title: jsonb_object_keys 
linkTitle: jsonb_object_keys 
summary: jsonb_object_keys  and json_object_keys 
description: jsonb_object_keys  and json_object_keys 
menu:
  latest:
    identifier: jsonb-object-keys
    parent: functions-operators
    weight: 160
isTocNested: true
showAsideToc: true
---

Here is the signature for the `jsonb` variant:

```
input value        jsonb
return value       SETOF text
```

The functions in this pair require that the supplied JSON value is an _object_. They transform the list of key names into a set (i.e. table) of `text` values. Notice that the returned keys are ordered alphabetically.

```
do $body$
declare
  object constant jsonb :=
    '{"b": 1, "c": true, "a": {"p":1, "q": 2}}';

  keys text[] := null;

  expected_keys constant text[] :=
    array['a', 'b', 'c'];

  k text;
  n int := 0;
begin
  for k in (
    select * from jsonb_object_keys(object)
   )
  loop
    n := n + 1;
    keys[n] := k;
  end loop;

  assert
    keys = expected_keys,
  'unexpected';
end;
$body$;
```
