---
title: Equality - the `=` operator
linkTitle: Equality - the `=` operator
summary: Concatenation - the `||` operator
description: Concatenation - the `||` operator
menu:
  latest:
    identifier: equality-operator
    parent: functions-operators
isTocNested: true
showAsideToc: true
---

This operator requires that the inputs are presented as `jsonb` values. It doesn't have an overload for `json`.

```postgresql
do $body$
declare
  j1 constant jsonb := '["a","b","c"]';
  j2 constant jsonb := '
    [
      "a","b","c"
    ]';
begin
  assert
    j1::text = j2::text,
  'unexpected';
end;
$body$;
```

Notice that the text forms of the to-be-compared JSON values may differ in whitespace. Because `jsonb` holds a fully parsed representation of the value, whitespace (exception within primitive JSON _string_ values) has no meaning.

If you need to test two `json` values for equality, then you must `::text` typecast each.

```postgresql 
do $body$
declare
  j1 constant json := '["a","b","c"]';
  j2 constant json := '["a","b","c"]';
begin
  assert
    j1::text = j2::text,
  'unexpected';
end;
$body$;
```
