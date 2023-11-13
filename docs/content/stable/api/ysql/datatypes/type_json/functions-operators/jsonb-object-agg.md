---
title: jsonb_object_agg()
headerTitle: jsonb_object_agg()
linkTitle: jsonb_object_agg()
description: Aggregate a SETOF values into a JSON object.
menu:
  stable:
    identifier: jsonb-object-agg
    parent: json-functions-operators
    weight: 155
type: docs
---

**Purpose:** This is an aggregate function. (Aggregate functions compute a single result from a `SETOF` input values.) It creates a JSON _object_ whose values are the JSON representations of the aggregated SQL values. It is most useful when these to-be-aggregated values are _"row"_ type values with two fields. The first represents the _key_ and the second represents the _value_ of the intended JSON _object_'s _key-value_ pair.

**Signature:**

```
input value:       anyelement
return value:      jsonb
```

**Notes:** The syntax _"order by... nulls first"_ within the parentheses of the aggregate function (a generic feature of aggregate functions) isn't useful here because the order of the _key-value_ pairs of a JSON _object_ has no semantic significance.

{{< note title="JSON objects and their keys." >}}
A _JSON object_ is a set of key-value pairs where each key is (taken to be) unique and the order is undefined and insignificant. (See also the accounts of the [`jsonb_set()` and `jsonb_insert()`](../jsonb-set-jsonb-insert) functions.) 

This means that if a _key-value_ pair is specified more than once, in any context that defines an _object_ value, then the one that is most recently specified wins. Here's a simple example:

```plpgsql
select ('{"f2": 42, "f7": 7, "f2": null}'::jsonb)::text;
```
This is the result:

```output
 {"f2": null, "f7": 7}
```
(In the other direction, the `::text` typecast of a `jsonb` _object_ uses the convention of ordering the pairs alphabetically by the _key_.)
{{< /note >}}

Try this demonstration of the `jsonb_object_agg()` function:

```plpgsql
do $body$
declare
  object_agg jsonb not null := '"?"';
  expected_object_agg constant jsonb not null :=
    '{"f1": 1, "f2": 2, "f3": null, "f4": 4}'::jsonb;
begin
  with tab as (
    values
      ('f4'::text, 4::int),
      ('f1'::text, 1::int),
      ('f3'::text, null::int),
      ('f2'::text, 2::int))
  select
    jsonb_object_agg(column1, column2)
    into strict object_agg
  from tab;

  assert (object_agg = expected_object_agg), 'unexpected';
end;
$body$;
```

It finishes silently, showing that the `assert` holds.

Now try this counter-example. The `DO` block specifies both the value for _key "f2_" and the value for _key "f7_" twice:

```plpgsql
do $body$
declare
  object_agg jsonb not null := '"?"';
  expected_object_agg constant jsonb not null :=
    '{"f2": null, "f7": 7}'::jsonb;
begin
  with tab as (
    values
      ('f2'::text, 4::int),
      ('f7'::text, 7::int),
      ('f2'::text, 1::int),
      ('f2'::text, null::int))
  select
    jsonb_object_agg(column1, column2)
    into strict object_agg
  from tab;

  assert (object_agg = expected_object_agg), 'unexpected';
end;
$body$;
```

It, too, finishes silently, showing that, again,  the `assert` holds.