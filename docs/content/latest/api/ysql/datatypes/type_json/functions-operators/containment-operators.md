---
title: "@> and <@ (containment operators) [JSON]"
headerTitle: "@> and <@ (containment operators)"
linkTitle: "@> and <@ (containment)"
description:  Test whether one jsonb value contains another jsonb value using the JSON containment operators (@> and <@).
menu:
  latest:
    identifier: containment-operators
    parent: functions-operators
    weight: 16
isTocNested: true
showAsideToc: true
---

**Purpose:** the `@>` operator tests if the left-hand JSON value contains the right-hand JSON value. The `<@` operator tests if the right-hand JSON value contains the left-hand JSON value.

**Signatures:**

```
input values:       jsonb @> jsonb
return value:       boolean
```
and:
```
input values:       jsonb <@ jsonb
return value:       boolean
```

**Notes:** these operators require that the inputs are presented as `jsonb` values. They don't have overloads for `json`.

```postgresql
do $body$
declare
  j_left  constant jsonb := '{"a": 1, "b": 2}';
  j_right constant jsonb := '{"b" :2}';
begin
  assert
    (j_left @> j_right) and
    (j_right <@ j_left),
 'unexpected';
end;
$body$;
```
