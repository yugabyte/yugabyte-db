---
title: array_agg, string_agg, jsonb_agg, jsonb_object_agg
linkTitle: array_agg, jsonb_agg, jsonb_object_agg, string_agg, range_agg
headerTitle: Aggregating arrays, strings, json and range
description: Describes the functionality of the array_agg, string_agg, jsonb_agg, jsonb_object_agg YSQL aggregate functions
menu:
  v2025.1_api:
    identifier: array-string-jsonb-jsonb-object-agg
    parent: aggregate-function-syntax-semantics
    weight: 20
type: docs
---

The aggregate functions `array_agg()`, `jsonb_agg()`, `jsonb_object_agg()`, `string_agg()`, and `range_agg` are described in this same section because each produces, as a single value, a _list_ of the values that are aggregated.  (The term "list" is used informally and somewhat loosely here. The examples make the sense in which the word is used clear.)

The notion "list" implies that the list members are ordered. (In contrast, "set" brings no such ordering notion.) For `array_agg()`, `string_agg()`, `jsonb_agg()`, and `json_agg()`, the list order is determined by the `ORDER BY` clause in the `SELECT` list expression that invokes the aggregate function. Because the single value produced by `jsonb_object_agg()` (and `json_object_agg()`) is a JSON _object_, and because the elements of such a value have no order, the `ORDER BY` clause in the `SELECT` list expression has no effect. (The elements are accessed by field name.)

This makes  `array_agg()`, `string_agg()`, and `jsonb_agg()` unique among ordinary aggregate functions. Usually, as the example of `sum()` demonstrates, the order of the aggregated values is of no consequence. (The term _"ordinary" aggregate function_ is used for the ones that are invoked with the `GROUP BY` syntax or the `OVER` syntax. The formal terms _"within-group ordered-set" aggregate function_ and _"within-group hypothetical-set" aggregate function_ are used for the "extraordinary" kinds.)

## array_agg

**Signature:**

```yaml
input value:       anynonarray
                   anyarray

return value:      anyarray
```

**Purpose:** Returns an array whose elements are the individual values that are aggregated. The to-be-aggregated values must be homogeneous: _either_ all nonarray values; _or_ all array values with the same dimensionality, as returned by [`array_ndims()`](../../../../datatypes/type_array/functions-operators/properties/#array-ndims), as each other. See also the account of [`array_agg()`](../../../../datatypes/type_array/functions-operators/array-agg-unnest/#array-agg) in the dedicated [Array data types and functionality](../../../../datatypes/type_array/) section.

The order of the resulting array elements (i.e. the mapping of element value to index value) is determined by the order defined by the `GROUP BY` or the `OVER` invocation syntax. When nonarray (i.e. scalar) values are aggregated, the result is a 1-dimensional array. When anyarray values with dimensionality _N_ are aggregated, the result is an array with dimensionality _(N + 1)_.

## jsonb_agg

This aggregate function, together with `json_agg()`, are described fully in the [`jsonb_agg()`](../../../../datatypes/type_json/functions-operators/jsonb-agg/) section within the overall [JSON](../../../../datatypes/type_json/) section.

**Signature:**

```yaml{.nocopy}
input value:       anyelement

return value:      jsonb
```

**Purpose:** Returns a JSON _array_ whose elements are the JSON values formed from each of the to-be-aggregated values.

## jsonb_object_agg

This aggregate function, together with `json_object_agg()`, are described fully in the [`jsonb_object_agg()`](../../../../datatypes/type_json/functions-operators/jsonb-object-agg/) section within the overall [JSON](../../../../datatypes/type_json/) section.

**Signature:**

```yaml{.nocopy}
input value:       "any", "any"

return value:      jsonb
```

**Purpose:** Returns a JSON _object_ whose fields are the JSON elements formed by each of the to-be-aggregated value pairs. The first value in the pair provides the name (key) of the field and the second value in the pair provides its value. Because the order of key-value pairs in a JSON _object_ is undefined (values are addressed by their key), the aggregation order has no effect on the result.

## string_agg

**Signature:**

```yaml{.nocopy}
input value:       text, text
                   bytea, bytea

return value:      text
                   bytea
```

**Purpose:** Returns a single value produced by concatenating the aggregated values (first argument) separated by a mandatory separator (second argument). The first overload has `text` inputs and returns `text`. The second overload has `bytea` inputs and returns `bytea`. (The PostgreSQL documentation describes the `bytea` data type in section [8.4. Binary Data Types](https://www.postgresql.org/docs/15/datatype-binary.html).

Here's a basic example:

```plpgsql
drop table if exists t cascade;
create table t(k int primary key, vt text not null, vb bytea not null);
insert into t(k, vt, vb) values
  (1, 'm', 'm'::bytea),
  (2, 'o', 'o'::bytea),
  (3, 'u', 'u'::bytea),
  (4, 's', 's'::bytea),
  (5, 'e', 'e'::bytea);

select vt, vb from t order by k;
```

This is the result:

```caddyfile{.nocopy}
 vt |  vb
----+------
 m  | \x6d
 o  | \x6f
 u  | \x75
 s  | \x73
 e  | \x65
```

Now try this:

```plpgsql
with a as (
  select
    string_agg(vt, null       order by k) as text_agg,
    string_agg(vb, '.'::bytea order by k) as bytea_agg
  from t)
select
  text_agg,
  bytea_agg,
  convert_from(bytea_agg, 'utf-8') as bytea_agg_text
from a;
```

This is the result:

```caddyfile{.nocopy}
 text_agg |      bytea_agg       | bytea_agg_text
----------+----------------------+----------------
 mouse    | \x6d2e6f2e752e732e65 | m.o.u.s.e
```

## range_agg

The range_agg function is used to aggregate a set of ranges into a single range or an array of ranges. It's particularly useful for summarizing sequences of continuous or discrete values, such as timestamps or numerical data. It helps condense multiple ranges into a more concise representation. For example, if you have several time intervals, range_agg can combine overlapping or adjacent intervals into larger ones.

If the input ranges are contiguous or overlapping, range_agg can merge them into a single encompassing range. If there are gaps or overlaps that are not allowed, it will raise an error. If the input ranges have gaps or overlaps that you want to preserve, range_agg can return an array of ranges. You can control whether gaps and overlaps are permitted using optional parameters.

```yaml{.nocopy}
input value:       anyrange
                   anymultirange

return value:      anymultirange
```

For example:

```plpgsql
DROP TABLE IF EXISTS event_times CASCADE;
CREATE TABLE event_times (
    event_id INT,
    time_range tsrange
);

INSERT INTO event_times (event_id, time_range) VALUES
(1, '[2025-01-01 09:00, 2025-01-01 10:00)'::tsrange),
(2, '[2025-01-01 10:00, 2025-01-01 11:00)'::tsrange),
(3, '[2025-01-01 12:00, 2025-01-01 13:00)'::tsrange),
(4, '[2025-01-01 14:00, 2025-01-01 15:00)'::tsrange),
(5, '[2025-01-01 14:30, 2025-01-01 15:30)'::tsrange);

-- Note Event 5 overlaps with event 4
```

To combine 2 ranges, you can use `range_agg` as,

```sql
SELECT range_agg(time_range)
FROM event_times
WHERE event_id IN (1, 2);
```

This would create a combined range as,

```caddyfile{.nocopy}
                    range_agg
-------------------------------------------------
 {["2025-01-01 09:00:00","2025-01-01 11:00:00")}
```

You can control the order of ranges within the resulting array using ORDER BY.

```sql
SELECT range_agg(time_range ORDER BY event_id DESC)
FROM event_times
WHERE event_id IN (1, 3);
```

Now the ranges will be ordered by event_id.

```caddyfile{.nocopy}
                                           range_agg
-----------------------------------------------------------------------------------------------
 {["2025-01-01 09:00:00","2025-01-01 10:00:00"),["2025-01-01 12:00:00","2025-01-01 13:00:00")}
```

## Examples

Each of these aggregate functions is invoked by using the same syntax—either the [`GROUP BY` syntax](./#group-by-syntax) or the [`OVER` syntax](./#over-syntax). First create and populate the test table:

```plpgsql
drop table if exists t cascade;
create table t(
  k     int   primary key,
  class int   not null,
  v     text  not null);

insert into t(k, class, v)
select
  (1 + s.v),
  case (s.v) < 3
    when true then 1
              else 2
  end,
  chr(97 + s.v)
from generate_series(0, 5) as s(v);

select k, class, v from t order by k;
```

This is the result:

```caddyfile{.nocopy}
 k | class | v
---+-------+---
 1 |     1 | a
 2 |     1 | b
 3 |     1 | c
 4 |     2 | d
 5 |     2 | e
 6 |     2 | f
```

### GROUP BY syntax

Try this:

```plpgsql
select
  class,
  array_agg(v            order by k desc) filter (where v <> 'b') as "array_agg(v)",
  string_agg(v, ' ~ '    order by k desc) filter (where v <> 'e') as "string_agg(v)",
  jsonb_agg(v            order by v desc) filter (where v <> 'b') as "jsonb_agg",
  jsonb_object_agg(v, k  order by v desc) filter (where v <> 'e') as "jsonb_object_agg(v, k)"
from t
group by class
order by class;
```

This is the result:

```caddyfile{.nocopy}
 class | array_agg(v) | string_agg(v) |    jsonb_agg    |  jsonb_object_agg(v, k)
-------+--------------+---------------+-----------------+--------------------------
     1 | {c,a}        | c ~ b ~ a     | ["c", "a"]      | {"a": 1, "b": 2, "c": 3}
     2 | {f,e,d}      | f ~ d         | ["f", "e", "d"] | {"d": 4, "f": 6}
```
As promised, the result produced by `jsonb_object_agg()` is insensitive to order.

### OVER syntax

Try this:

```plpgsql
select
  class,
  (array_agg(v)          filter (where v <> 'b')   over w) as "array_agg(v)",
  (string_agg(v, ' ~ ')  filter (where v <> 'b')   over w) as "string_agg(v)",
  (jsonb_agg(v)          filter (where v <> 'b')   over w) as "string_agg(v)",
  (jsonb_object_agg(v, k) filter (where v <> 'e')  over w) as  "jsonb_object_agg(v, k)"
from t
window w as (
  partition by class
  order by k desc
  range between unbounded preceding and current row)
order by 1;
```

This is the result:

```caddyfile{.nocopy}
 class | array_agg(v) | string_agg(v) |  string_agg(v)  |  jsonb_object_agg(v, k)
-------+--------------+---------------+-----------------+--------------------------
     1 | {c}          | c             | ["c"]           | {"c": 3}
     1 | {c}          | c             | ["c"]           | {"b": 2, "c": 3}
     1 | {c,a}        | c ~ a         | ["c", "a"]      | {"a": 1, "b": 2, "c": 3}
     2 | {f}          | f             | ["f"]           | {"f": 6}
     2 | {f,e}        | f ~ e         | ["f", "e"]      | {"f": 6}
     2 | {f,e,d}      | f ~ e ~ d     | ["f", "e", "d"] | {"d": 4, "f": 6}
```
