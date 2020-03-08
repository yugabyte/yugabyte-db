---
title: jsonb_strip_nulls()
linkTitle: jsonb_strip_nulls()
summary: jsonb_strip_nulls() and json_strip_nulls()
description: jsonb_strip_nulls() and json_strip_nulls()
menu:
  latest:
    identifier: jsonb-strip-nulls
    parent: functions-operators
isTocNested: true
showAsideToc: true
---

Here is the signature for the `jsonb` variant:

```
input value        jsonb
return value       jsonb
```

Each function returns a value of the same data type as that of the actual with which it is invoked. They find all key-value pairs at any depth in the hierarchy of the supplied JSON compound value (such a pair can occur only as an element of an _object_) and return a JSON value where each pair whose value is _null_. has been removed. By definition, they leave _null_ values within _arrays_ untouched.

```postgresql
do $body$
declare
  j constant jsonb :=
    '{
      "a": 17,
      "b": null,
      "c": {"x": null, "y": "dog"},
      "d": [42, null, "cat"]
    }';

  stripped_j constant jsonb := jsonb_strip_nulls(j);

  expected_stripped_j constant jsonb :=
    '{
      "a": 17,
      "c": {"y": "dog"},
      "d": [42, null, "cat"]
    }';
begin
  assert
    stripped_j = expected_stripped_j,
  'unexpected';
end;
$body$;
```
