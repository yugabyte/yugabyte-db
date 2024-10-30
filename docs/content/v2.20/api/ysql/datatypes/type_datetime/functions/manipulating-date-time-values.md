---
title: Functions for manipulating date-time values [YSQL]
headerTitle: Functions for manipulating date-time values
linkTitle: Manipulating date-time values
description: The semantics of the functions manipulating date-time values. [YSQL]
menu:
  v2.20:
    identifier: manipulating-date-time-values
    parent: date-time-functions
    weight: 20
type: docs
---

## function date_trunc() returns plain timestamp \| timestamptz \| interval

The _date_trunc()_ built-in function truncates a _date_time_ value to the specified granularity. It has three overloads. Each returns a value whose data type is the same as that of the second argument. The first argument, chosen from a list of legal values, determines how the second argument will be truncated. Here is the interesting part of the output from \\_df date_trunc()_:

```output
      Result data type       |        Argument data types
-----------------------------+-----------------------------------
 interval                    | text, interval
 timestamp with time zone    | text, timestamp with time zone
 timestamp without time zone | text, timestamp without time zone
```

Here are the legal values for the first argument, in order of increasing size:

```output
microseconds
milliseconds
second
minute
hour
day
week
month
quarter
year
decade
century
millennium
```

If a _date_ value is used for the second argument, then the typecasting rules presented in the [summary table](../../typecasting-between-date-time-values/#summary-table) in the section [Typecasting between values of different date-time data types](../../typecasting-between-date-time-values/) are used. However, a _date_ value can be typecast either to a plan _timestamp_ value or to a _timestamptz_ value; so a rule governs the data type of the return value. It can be determined by experiment like this:

```plpgsql
set timezone = 'UTC';
with c as (
  select date_trunc('year', '2021-07-19'::date) as v)
select pg_typeof(v)::text as "type", v::text from c;
```

This is the result:

```output
           type           |           v
--------------------------+------------------------
 timestamp with time zone | 2021-01-01 00:00:00+00
```

Of course, you should never rely on implicit typecasting but, rather, should always write the typecast that you intend explicitly. Try this:

```plpgsql
with c as (
  select date_trunc('year', ('2021-07-19'::date)::timestamp) as v)
select pg_typeof(v)::text as "type", v::text from c;
```

This is the result:

```output
            type             |          v
-----------------------------+---------------------
 timestamp without time zone | 2021-01-01 00:00:00
```

A plain _time_ value cannot be typecast to a plain _timestamp_ value or to a _timestamptz_ value. But it can be typecast to an _interval_ value. Try this:

```plpgsql
with c as (
  select date_trunc('hour', ('13:42:37.123456'::time)::interval) as v)
select pg_typeof(v)::text as "type", v::text from c;
```

This is the result:

```output
   type   |    v
----------+----------
 interval | 13:00:00
```

Notice that when a hybrid _interval_ value is supplied to _date_trunc()_, the result might surprise the uninitiated. Try this:

```plpgsql
with c as (
  -- 5000000 hours is close to 600 years.
  select make_interval(hours=>5000000) as i)
select i, date_trunc('years', i)  as "result" from c;
```

This is the result:

```output
       i       |  result
---------------+----------
 5000000:00:00 | 00:00:00
```

The result (truncating a value of close to 600 years to a granularity of one year gets a result of _zero_) emphasizes the point that hybrid _interval_ values can be dangerous. This is explained in the section [Custom domain types for specializing the native interval functionality](../../date-time-data-types-semantics/type-interval/custom-interval-domains/).

## function justify_days() \| justify_hours() \| justify_interval returns interval

Briefly, these functions manipulate the fields of the internal _[\[mm, dd, ss\]](../../date-time-data-types-semantics/type-interval/interval-representation/)_ representation of an _interval_ value by using a rule of thumb to compute and apply increments to the _mm_ and _dd_ fields from, respectively, the _dd_ and _ss_ fields. Nominally, they make _interval_ values easier to comprehend. Here's an example:

```plpgsql
\x on
with
  c1 as (
    select
      '2017-05-16 12:00:00'::timestamp            as t,
      make_interval(days=>1000000, secs=>1000000) as i),
  c2 as (
    select
      t                                           as t,
      i                                           as i,
      justify_interval(i)                         as j
    from c1)
select
  t                                        ::text as "t",
  i                                        ::text as "i",
  (t + i)                                  ::text as "t + i",
  j                                        ::text as "justify_interval(i)",
  (t + j)                                  ::text as "t + justify_interval(i)"
from c2;
\x off
```

This is the result:

```output
t                       | 2017-05-16 12:00:00
i                       | 1000000 days 277:46:40
t + i                   | 4755-04-25 01:46:40
justify_interval(i)     | 2777 years 9 mons 21 days 13:46:40
t + justify_interval(i) | 4795-03-10 01:46:40
```

Notice that _justify_interval(i)_ changed the addition semantics of _i_. This is dramatically different from justifying, for example, _18 inches_ to _1 foot 6 inches_ where the two presentation forms have identical distance semantics.

The section [Custom domain types for specializing the native interval functionality](../../date-time-data-types-semantics/type-interval/custom-interval-domains/) explains the potential dangers (dramatically changing the semantics of _interval_ arithmetic) that using these functions brings.

These three functions are described in the section [The justify_hours(), justify_days(), and justify_interval() built-in functions](../../date-time-data-types-semantics/type-interval/justfy-and-extract-epoch/#the-justify-hours-justify-days-and-justify-interval-built-in-functions) within the section [The justify() and extract(epoch ...) functions for interval values](../../date-time-data-types-semantics/type-interval/justfy-and-extract-epoch/).
