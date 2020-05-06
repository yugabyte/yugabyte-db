---
title: jsonb_array_elements() and  json_array_elements()
linkTitle: jsonb_array_elements()
headerTitle: jsonb_array_elements() and json_array_elements()
description: Transform JSON values of a JSON array into a SQL table of jsonb values using jsonb_array_elements() and  json_array_elements().
menu:
  latest:
    identifier: jsonb-array-elements
    parent: functions-operators
    weight: 60
isTocNested: true
showAsideToc: true
---
**Purpose:** Transform the JSON values of a JSON _array_ into a SQL table of (i.e., `setof`) `jsonb` values.

**Signature:** For the `jsonb` variant:

```
input value:       jsonb
return value:      SETOF jsonb
```

**Notes:** The functions in this pair require that the supplied JSON value is an _array_. They are the counterparts, for an _array_, to `jsonb_populate_recordset()` for a JSON _object_.

```postgresql
do $body$
declare
  j_array constant jsonb := '["cat", "dog house", 42, true, {"x": 17}, null]';
  j jsonb;

  elements jsonb[];
  expected_elements constant jsonb[] :=
    array[
      '"cat"'::jsonb,
      '"dog house"'::jsonb,
      '42'::jsonb,
      'true'::jsonb,
      '{"x": 17}'::jsonb,
      'null'::jsonb
    ];

  n int := 0;
begin
  for j in (select jsonb_array_elements(j_array)) loop
    n := n + 1;
    elements[n] := j;
  end loop;

  assert
    elements = expected_elements,
  'unexpected';
end;
$body$;
```
