---
title: jsonb_typeof()
linkTitle: jsonb_typeof()
summary: jsonb_typeof() and json_typeof()
description: jsonb_typeof() and json_typeof()
menu:
  latest:
    identifier: jsonb-typeof
    parent: functions-operators
    weight: 250
isTocNested: true
showAsideToc: true
---

These functions return the type of the JSON value as a SQL `text` value. Here is the signature for the `jsonb` variant:

```
input value        jsonb
return value       text
```

Possible types are _string_, _number_, _boolean_, _null_,  _object_, and _array_ — as follows.

```postgresql
do $body$
declare
  j_string   constant jsonb := '"dog"';
  j_number   constant jsonb := '42';
  j_boolean  constant jsonb := 'true';
  j_null     constant jsonb := 'null';
  j_object   constant jsonb := '{"a": 17, "b": "x", "c": true}';
  j_array    constant jsonb := '[17, "x", true]';
begin
  assert
    jsonb_typeof(j_string)   = 'string'  and
    jsonb_typeof(j_number)   = 'number'  and
    jsonb_typeof(j_boolean)  = 'boolean' and
    jsonb_typeof(j_null)     = 'null'    and
    jsonb_typeof(j_object)   = 'object'  and
    jsonb_typeof(j_array)    = 'array',
 'assert failed';
end;
$body$;
```
