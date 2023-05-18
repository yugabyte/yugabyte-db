---
title: Range data types [YSQL]
headerTitle: Range data types
linkTitle: Range
description: YSQL supports the following built-in range data types.int4range for integer; int8range for bigint; numrange for numeric; tsrange for timestamp without time zone; tstzrange for timestamp with time zone; daterange for date.
menu:
  v2.16:
    identifier: api-ysql-datatypes-range
    parent: api-ysql-datatypes
type: docs
---

YSQL supports six built-in range data types. Each defines a range of values of a specific underlying data type. (The [PostgreSQL documentation](https://www.postgresql.org/docs/11/rangetypes.html) uses the term "subtype" for "underlying data type".) It's also possible to create user-defined range data types. A later version of the YSQL documentation will explain this.

## Synopsis

Range data type | Underlying data type |
----------|-------------|
`int4range` | `integer` (a.k.a. `int`) |
`int8range` | `bigint` |
`numrange` | `numeric` |
`tsrange` | `timestamp without time zone` (a.k.a. `timestamp`) |
`tstzrange` | `timestamp with time zone` (a.k.a. `timestamptz`) |
`daterange` | `date` |

## Description

The underlying data type of a range data type must be orderable. Each of the six built-in range data types meets this requirement. A range value is defined by its (smaller) start value and the (larger) end value. A range value therefore corresponds to the "interval" notion in mathematics. (Don't confuse this with the YSQL data type `interval` which denotes an amount of elapsed time and which is defined only by its absolute size.)

### The "interval" notion in mathematics

See the Wikipedia article [Interval (mathematics)](https://en.wikipedia.org/wiki/Interval_(mathematics)).

The following notions, and their notation, from mathematics carry over to the YSQL range data types.

- An _open_ interval excludes its endpoints, and is denoted with parentheses. For example, _(0,1)_ means greater than _0_ and less than _1_.
- A _closed_ interval includes all its endpoints, and is denoted with square brackets. For example, _[0,1]_ means greater than or equal to _0_ and less than or equal to _1_.
- A _half-open_ interval includes only one of its endpoints, and is denoted by mixing the notations for open and closed intervals. For example, _(0,1]_ means greater than _0_ and less than or equal to _1_; and _[0,1)_ means greater than or equal to _0_ and less than _1_.

### Range values in YSQL

A range value can be specified either as a literal or using a constructor function.

First, create a table:

```plpgsql
create table t(k int primary key, r1 tsrange, r2 tsrange);
```

#### Specify range values using literals

The same approach is used to specify range values of all range data types using literals. A text value is defined as follows:

- It starts with an opening parenthesis or an opening square bracket.

- It is followed by the `::text` typecast representation of the lower bound, a comma and then the `::text` typecast representation of the upper bound.

- It finishes with a closing parenthesis or a closing square bracket.

This example uses `tsrange`.

```plpgsql
insert into t(k, r1, r2)
values (
  1,
  '[2010-01-01 14:30, 2010-01-01 15:30)',
  '(2010-01-01 15:00, 2010-01-01 16:00]');
```
Inspect the bounds:

```plpgsql
select lower(r1), upper(r1) from t where k = 1;
```
The reported values are insensitive to whether the bound is inclusive or exclusive. This is the result:
```
        lower        |        upper
---------------------+---------------------
 2010-01-01 14:30:00 | 2010-01-01 15:30:00
```

Are the bounds inclusive (defined using a square bracket) or not inclusive (defined using a parenthesis):

```plpgsql
select
  (lower_inc(r1))::text as "is r1's lower inclusive?",
  (upper_inc(r1))::text as "is r1's upper inclusive?",
  (lower_inc(r2))::text as "is r2's lower inclusive?",
  (upper_inc(r2))::text as "is r2's upper inclusive?"
from t where k = 1;
```
This is the result:

```
 is r1's lower inclusive? | is r1's upper inclusive? | is r2's lower inclusive? | is r2's upper inclusive?
--------------------------+--------------------------+--------------------------+--------------------------
 true                     | false                    | false                    | true
```

#### Specify "tsrange" values using the constructor function

The constructor function for a particular range data type has the same name as the range data type. The first and second formal parameters are the lower and upper bounds. And the third, optional, formal parameter specifies the inclusive status of each bound as one of these text values:

````
'()'   '(]'   '[)'   '[]'
````

When the constructor is invoked without supplying a value for the third formal parameter, the value `'[)'` is used.

This `INSERT` statement establishes the same values for _"r1"_ and _"r2"_ in the row with _"k = 2"_ that the row with _"k = 1"_ has:

```plpgsql
insert into t(k, r1, r2)
values (
  2,
  tsrange(
    make_timestamp(2010, 1, 1, 14, 30, 0.0),
    make_timestamp(2010, 1, 1, 15, 30, 0.0)
    ),
  tsrange(
    make_timestamp(2010, 1, 1, 15, 0, 0.0),
    make_timestamp(2010, 1, 1, 16, 0, 0.0),
    '(]'));
```

This `SELECT` statement shows that the values in the row with _"k = 2"_ are indeed the same as the row with _"k = 1"_ has:

```postgresql
select (
    (select r1 from t where k = 1) = (select r1 from t where k = 2)

    and

    (select r2 from t where k = 1) = (select r2 from t where k = 2)

  )::text "result is as expected?";
```

This is the result:

```
 result is as expected?
------------------------
 true
```

#### Unbounded ranges

Either, or both of, the lower bound and the upper bound of a range value can be set to express the semantics "unbounded". The [PostgreSQL documentation](https://www.postgresql.org/docs/11/rangetypes.html#RANGETYPES-INFINITE) uses "unbounded" and "infinite" interchangeably to denote such a range. Yugabyte recommends always using the term "unbounded" and avoiding the term "infinite". The reason for this is explained later in this section.

- When a range value is defined using a literal, an unbounded lower or upper bound is specified as unbounded simply by omitting the value, like this:

```plpgsql
select ('(, 5]'::int4range)::text as "canonicalized literal";
```
This is the result:

```
 canonicalized literal
-----------------------
 (,6)
```

Notice that the opening punctuation mark has been canonicalized to `(` rather than to `[` — as would be the form for a finite bound. The bound value is still omitted because "unbounded" means the same when the bound is exclusive as when it's inclusive. The closing punctuation mark has been canonicalized to `)` in the normal way, bringing the consequence that the bound is rendered as _"6"_ instead of as _"5"_. (See the section [Discrete range data types and the canonical forms of their literals](#discrete-range-data-types-and-the-canonical-forms-of-their-literals) below.)

Notice that no discretion is allowed for the use of whitespace: to denote that the lower bound is unbounded, there must be _no_ whitespace between the opening punctuation mark and the comma; and to denote that the upper bound is unbounded, there must be _no_ whitespace between the comma and the closing punctuation mark.

- When a range value is defined using a constructor, an unbounded lower or upper bound is specified as unbounded by using `NULL`, like this:

```plpgsql
select (
    '(, 5]'::int4range = int4range(null, 5, '(]')
  )::text as "literal and constructor produce the same result?";
```

This is the result:

```
 literal and constructor produce the same result?
--------------------------------------------------
 true
```

You can test the "unbounded" status of a bound with the `boolean` functions `lower_inf()` and `upper_inf()`like this:

```plpgsql
with a as (select '(,)'::int4range as v)
select lower_inf(v)::text, upper_inf(v)::text from a;
```

This is the result:

```
 lower_inf | upper_inf
-----------+-----------
 true      | true
```

The example illustrates the point that, as a special case, a range might be doubly unbounded. This could be useful in a scenario where user-input determines if a query is to be restricted using a predicate on some column or if there is to be no such restriction. Dynamic SQL can be avoided by writing the query as fixed static text using the "contains" operator (see the section [Is a value of a range's underlying data type contained within a range?](#is-a-value-of-a-range-s-underlying-data-type-contained-within-a-range) below) and by binding in the value of the range at run time.

**Note:** Some data types, like for example `timestamp`, support a special `infinity` value which can also be used to define a range. Try this:



```plpgsql
select
  ('infinity'::timestamp)             ::text as "infinity timestamp",
  ('[2020-01-01, infinity]'::tsrange) ::text as "infinity upper bound tsrange";
```

This is the result:

```
 infinity timestamp |   infinity upper bound tsrange
--------------------+----------------------------------
 infinity           | ["2020-01-01 00:00:00",infinity]
```

However, a range with an upper bound equal to positive infinity turns out to be different from a range that is unbounded at that end. The same holds for a range that has a lower bound equal to negative infinity. These two tests confirm this. First try this:

```plpgsql
select (upper_inf('[2020-01-01, infinity]'::tsrange))::text as "upper bound set to infinity tests as infinity?";
```

This is the result:

```
 upper bound set to infinity tests as infinity?
------------------------------------------------
 false
```

And now try this:

```plpgsql
select (
    tsrange('2020-01-01'::timestamp, 'infinity'::timestamp)
    =
    tsrange('2020-01-01'::timestamp, null      ::timestamp)
  )::text as "infinity bound same as unbounded bound?";
```

This is the result:

```
 infinity bound same as unbounded bound?
-----------------------------------------
 false
```

{{< tip title="Yugabyte recommends always specifying an unbounded bound by omitting the value (in a literal) or by using NULL (in a constructor).">}}

Using the special value `infinity` brings the following disadvantages:

- Not all of the underlying data types for which there are built-in range data types support an `infinity` notion. In fact, only `date`, plain `timestamp` and `timestamptz` (in the class for which there are built-in range data types) do support an `infinity` notion.
- The semantics are unclear. This is shown most dramatically by the fact that the `lower_inf()` and `upper_inf()` functions return `FALSE` when the corresponding bound is set to (negative or positive) `infinity`. Correspondingly, a pair of range values where one bound is set ordinarily to a normal value and where the other bound is set either to `infinity` or to "unbounded" compare as unequal.
- There are yet other reasons. But the two that have been explained in this "recommendation" section are sufficient reason to avoid the special negative or positive `infinity`  value.

The earlier recommendation to avoid the term "infinite" when describing a bound and to use only "unbounded" should now be clear: all inflexions of "infinity" serve only to bring confusion.

{{< /tip >}}


### Operations on range values and values of the underlying data type

You can find out if a single range value is empty or if two range values intersect; you can derive a new range value as the intersection of two range values; and you can find out if a value of the underlying data type is contained within a range value.

#### Is a range value empty?

Try this:

```plpgsql
select isempty(r1)::text from t where k = 1;
```

The return data type of `isempty()` is `boolean`. This is the result:

```
 isempty
---------
 false
```

Now try this:

```plpgsql
select isempty(numrange(1.5, 1.5, '[)'))::text;
```

The third actual argument specifies the default for the corresponding formal parameter as a self-documentation device. This is the result:

```
 isempty
---------
 true
```

Here is an alternative formulation of the same test:

```plpgsql
select (numrange(1.5, 1.5, '[)') = 'empty'::numrange)::text as "is empty?";
```

This (of course) is the result:

```
 is empty?
----------
 true
```

In other words, the literal `'empty'::<some range data type>` represents the special "empty range" value for that range data type.

If the third actual is changed to `'[]'` to denote that _both_ ends of the range are inclusive, then the result becomes `false`. Notice that a range's upper bound must be greater than, or equal to, the lower bound. Try this:

```plpgsql
select isempty(numrange(1.5, 1.0))::text;
```

The attempt fails with the `22000` error. (This maps to the `data_exception` exception.)

#### Do two range values intersect?

Try this:

```plpgsql
select (numrange(1.0, 2.0) && numrange(1.5, 2.5))::text as "range values intersect?";
```

The return data type of the `&&` intersection operator is `boolean`. This is the result:

```
 range values intersect?
-------------------------
 true
```

It you change the lower and upper bounds of the second range value to _3.0_ and _4.0_, then the result becomes `false`, of course.

#### Produce a new range value as the intersection of two range values

Try this:

```plpgsql
select (numrange(1.0, 2.0) * numrange(1.5, 2.5))::text as "the intersection";
```

This is the result:

```
[1.5,2.0)
```

If the two operands of the `*` range intersection operator don't intersect, then the result is the empty range. Try this:

```plpgsql
do $body$
declare
  e      constant numrange not null := 'empty';
  r1     constant numrange not null := numrange(1.0, 2.0);
  r2     constant numrange not null := numrange(3.0, 4.0);
  result constant numrange not null := r1 * r2;
begin
  assert result = e, 'assert failed';
end;
$body$;
```
The block completes silently showing that the assertion holds.

#### Is a value of a range's underlying data type contained within a range?

Try this:

```plpgsql
select (17::int <@ int4range(11, 42))::text as "is value within range?";
```

This is the result:

```
 is value within range?
------------------------
 true
```

The query can be formulated the other way round, thus:

```plpgsql
select (int4range(11, 42) @> 17::int)::text as "does range contain value?";
```

This is the result:

```
 does range contain value?
---------------------------
 true
```

## Discrete range data types and the canonical forms of their literals

The underlying data types of the `int4range`, `int8range`, and `daterange` data types are discrete: `int4` and `int8` accommodate only exact integer steps; and `date` accommodates only steps of exact whole days. This means that the definitions of an inclusive bound and an exclusive bound lead to the possibility of defining identical ranges in different ways. Consider the `int` range that includes _3_ as its lower bound and that includes _7_ as its upper bound. It can be written in four different ways:

```plpgsql
create table t(
  k int primary key,
  r1 int4range not null,
  r2 int4range not null,
  r3 int4range not null,
  r4 int4range not null);

insert into t(k, r1, r2, r3, r4) values(1, '[3, 8)', '[3, 7]', '(2, 8)', '(2, 7]');

select (
  (select r1 = r2 from t where k = 1) and
  (select r1 = r3 from t where k = 1) and
  (select r1 = r4 from t where k = 1)
  )::text as "all the same?";
```

This is the result:

```
 all the same?
---------------
 true
```
Now do this:

```plpgsql
select r1::text, r2::text, r3::text, r4::text from t where k = 1;
```

This is the result:

```
  r1   |  r2   |  r3   |  r4
-------+-------+-------+-------
 [3,8) | [3,8) | [3,8) | [3,8)
```

Of course, because as has been seen, the values of _"r1"_, _"r2"_, _"r3"_, and _"r4"_ are identical, it's of no consequence how these were established. The canonical form of the literal is the form that is used to display the value. The test results show that this is the form where the lower bound is _inclusive_ (denoted by the square bracket) and where the upper bound is _exclusive_ (denoted by the parenthesis).

## Current restriction

See [GitHub Issue #7353](https://github.com/yugabyte/yugabyte-db/issues/7353). It tracks the fact that you cannot create an index on a column list that includes a column with a range data type. Correspondingly, you cannot define a primary key constraint on such a column list.
