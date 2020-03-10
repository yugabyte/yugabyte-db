---
<<<<<<< HEAD:docs/content/latest/api/ysql/datatypes/type_json/functions-operators/containment-operators.md
title: Containment
linkTitle: '@> and <@ (containment)'
summary: Containment (@> & <@)
description: '@> and <@ (containment)'
=======
title: Containment: the `@>` and `<@` operators
linktitle: Containment: the `@>` and `<@` operators
summary: Concatenation: the `||` operator
description: Concatenation: the `||` operator
>>>>>>> Add front matter and link pages:docs/content/latest/api/ysql/datatypes/json-data-types/functions-operators/containment-operators.md
menu:
  latest:
    identifier: containment-operators
    parent: functions-operators
    weight: 16
isTocNested: true
showAsideToc: true
---

The `@>` operator tests if the left-hand JSON value contains the right-hand JSON value. And the `<@` operator tests if the right-hand JSON value contains the left-hand JSON value. These operators require that the inputs are presented as `jsonb` values. They don't have overloads for `json`.

```postgresql
do $body$
declare
  j_left  constant jsonb := '{"a": 1, "b": 2}';
  j_right constant jsonb := '{"b" :2}';
begin
  assert
    (j_left @> j_right) and
    (j_right <@ j_left),
 'assert failed';
end;
$body$;
```
