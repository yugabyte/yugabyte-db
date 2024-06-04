---
title: User-defined interval utility functions [YSQL]
headerTitle: User-defined interval utility functions
linkTitle: Interval utility functions
description: Presents copy-and-paste ready code to create utility functions that support learning about and using the interval data type. [YSQL]
menu:
  v2.20:
    identifier: interval-utilities
    parent: type-interval
    weight: 100
type: docs
---

{{< tip title="Download the '.zip' file to create the reusable code that supports the pedagogy of the overall 'date-time' major section." >}}
Download and install the code as the instructions [here](../../../download-date-time-utilities/) explain. The code that this page presents is included in the kit.
{{< /tip >}}

The code presented on this page defines two user-defined types, _interval_parameterization_t_ and _interval_mm_dd_ss_t_. They model, respectively, the conventional parameterization of an _interval_ value as a _[yy, mm, dd, hh, mi, ss]_ tuple and the internal representation of an _interval_ value as a _[\[mm, dd, ss\]](../interval-representation/)_ tuple. Together with the _interval_ data type itself, this makes three types—and therefore three possible pairs and six mutual transformation functions. Here is the summary:

```output
┌———————————————————————————————————————————————————————————————————┬————————————————————————————————————————————————┐
│ type interval_parameterization_t as(                              │ type interval_mm_dd_ss_t as(                   │
│   yy double precision, mm double precision, dd double precision,  │   mm int, dd int, ss double precision(1000,6)) │
│   hh double precision, mi double precision, ss double precision)  │                                                │
└———————————————————————————————————————————————————————————————————┴————————————————————————————————————————————————┘

┌—————————————————————————————————————————————————┬—————————————————————————————————————————————————————┐
│ interval <-> interval_parameterization_t        │ interval_mm_dd_ss_t <-> interval_parameterization_t │
├—————————————————————————————————————————————————┼—————————————————————————————————————————————————————┤
│ function interval_value(                        │ function interval_mm_dd_ss(                         │
│  p in interval_parameterization_t               │   p in interval_parameterization_t)                 │
│  returns interval                               │   returns interval_mm_dd_ss_t                       │
├—————————————————————————————————————————————————┼—————————————————————————————————————————————————————┤
│  function parameterization(                     │  function parameterization(                         │
│    i in interval)                               │    i in interval_mm_dd_ss_t)                        │
│    returns interval_parameterization_t          │    returns interval_parameterization_t              │
└—————————————————————————————————————————————————┴—————————————————————————————————————————————————————┘

┌————————————————————————————─———————————————————————————————┐
│             interval <─> interval_mm_dd_ss_t               │
├————————————————————————————┬———————————————————————————————┤
│ function interval_value(   │ function interval_mm_dd_ss(   │
│  i in interval_mm_dd_ss_t) │   i in interval)              │
│  returns interval          │   returns interval_mm_dd_ss_t │
└————————————————————————————┴———————————————————————————————┘
```

The code that the remainder of this section presents defines the _interval_parameterization_t_ and _interval_mm_dd_ss_t_ types, and all but one of the six mutual transformation functions. The design of the five functions that are shown here is straightforward—but it does depend on knowing that the internal representation of an _interval_ value is a _[\[mm, dd, ss\]](../interval-representation/)_ tuple that uses four-byte integers to record the _months_ and _days_ fields and an eight-byte integer to record the _seconds_ field as a microseconds value.

The section [Modeling the internal representation and comparing the model with the actual implementation](../interval-representation/internal-representation-model/) defines and tests the function that the present page doesn't show: _function interval_mm_dd_ss(interval_parameterization_t)_. It implements, using PL/pgSQL, the rather complex algorithm that PostgreSQL, and therefore YSQL, use in their C code to transform a value, presented as a _text_ literal, to the internal _interval_ representation.

Here's an example of such a _text_ literal:

```output
'
     -9.123456 years,
     18.123456 months,
  -700.123456 days,
     97.123456 hours,
    -86.123456 minutes,
     75.123456 seconds
'
```

## Generic helper function to test for the equality of two double precision values

PostgreSQL defines two fundamentally different data type families for representing real numbers and for supporting arithmetic using their values. Of course, YSQL inherits this regime.

The PostgreSQL documentation uses the terms [Arbitrary Precision Numbers](https://www.postgresql.org/docs/11/datatype-numeric.html#DATATYPE-NUMERIC-DECIMAL) and [Floating-Point Numbers](https://www.postgresql.org/docs/11/datatype-numeric.html#DATATYPE-FLOAT) for these two kinds of data type. The first kind is exemplified by the data type _numeric_. And the second kind is exemplified by _double precision_. There are other variants in each class. But the entire _[date_time](../../../../type_datetime/)_ section uses only these two (and uses only the unconstraint _numeric_ variant). Moreover, it uses _numeric_ only under duress. For example, you can easily round a _numeric_ value to some desired number of decimal places like this:

```plpgsql
select round(12.3456789::numeric, 3::int);
```

It produces the result _12.346_. Try the corresponding operation on a _double precision_ value:

```plpgsql
with c as(
  select 12.3456789::double precision as v)
select round(v, 3::int) from c;
```

It causes the 42883 error:

```output
function round(double precision, integer) does not exist
```

And the hint tells you that you might need to do some typecasting, like this:

```plpgsql
with c as(
  select 12.3456789::double precision as v)
select round(v::numeric, 3::int)::double precision from c;
```

You'll see some examples of typecasting like this throughout the overall _date-time_ major section.

Here, briefly, is how the two kinds of real number representation differ:

- The _numeric_ data type supports values that have a vast number of significant digits and very high accuracy. This is achieved by using an explicit software  representation that differs from anything that underlying hardware might use. As a consequence, arithmetic that uses this representation has to be done using algorithms implemented ordinarily in software—in the C code of the PostgreSQL implementation that YSQL inherits "as is".
- The _double precision_ data type is essentially a direct exposure of the hardware representation and the corresponding hardware implementation of arithmetic that is specified by IEEE Standard 754 for Binary Floating-Point Arithmetic for double precision values. (The native scheme is augmented in software to handle _nulls_.)

You can guess that, in general, the representation of a _double precision_ value uses less space than that of a _numeric_ value and that _double precision_ arithmetic is faster than _numeric_ arithmetic. This is probably why many SQL built-in functions, and certainly those that are relevant for the _date-time_ data types, use _double precision_ to deal with real number values. Here are four examples:

- The _secs_ input formal parameter for _make_interval()_ is _double precision_.
- The _sec_ input formal parameter for _make_timestamp()_ and _make_timestamptz()_ is _double precision_.
- The anonymous input formal parameter for the single-parameter overload of _to_timestamp()_ is _double precision_.
- All of the functions in the *extract()* family return a _double precision_ value.

For this reason, the user-defined interval utility functions that this page presents use _double precision_, and never _numeric_, for real number values.

Notice that equality comparison, using the native `=` operator, between real number values does bring a risk of evaluating to _false_ when you expect it to be _true_ because, of course, of the effect of rounding errors.

- You can often use a simple equality test to confirm that two _numeric_ values that you reason ought to be the same are indeed this. This is due to the data type's capacity for enormous precision and accuracy.
-  The corresponding test that uses the _double precision_ overload for `=` for a pair of putatively identical values will fail more often because the data type has a smaller capacity for precision and a corresponding intrinsic inaccuracy.

However, you must accept that the native equality test, for values of both the _numeric_ and the _double precision_ data types, is unreliable. There's no native solution for this dilemma. But it's easy to implement your own operator. This is what _[function approx_equals(double precision, double precision)](#function-approx-equals-double-precision-double-precision-returns-boolean)_ does. You can easily use it as a model for a _(numeric, numeric)_ overload should you need this.

### function approx_equals (double precision, double precision) returns boolean

In the context of the _date-time_ data types, _double precision_ values always represent seconds. And the internal representations record these only with microsecond precision. It's therefore good enough to test for equality using a 0.1 microsecond tolerance. Create the function thus:

```plpgsql
drop function if exists approx_equals(double precision, double precision) cascade;

create function approx_equals(v1 in double precision, v2 in double precision)
  returns boolean
  language plpgsql
as $body$
declare
  microseconds_diff       constant double precision not null := abs(v1 - v2);
  point_one_microseconds  constant double precision not null := 0.0000001;
  eq                      constant boolean          not null := microseconds_diff < point_one_microseconds;
begin
  return eq;
end;
$body$;
```
And map it to the `~=` operator thus:

```plpgsql
drop operator if exists ~= (double precision, double precision) cascade;

create operator ~= (
  leftarg   = double precision,
  rightarg  = double precision,
  procedure = approx_equals);
```

Here's a simple test:

```plpgsql
with
  c1 as(
    select 10::double precision as base, 234.567::double precision as v),

  c2 as(
    select base, v, log(v) as log_v from c1),

  c3 as(
    select v as orig, base^log_v as recovered from c2)

select
  to_char(orig,      '9999.999999999999999')  as orig,
  to_char(recovered, '9999.999999999999999')  as recovered,
  (recovered =  orig)::text                   as native_equals,
  (recovered ~= orig)::text                   as approx_equals
from c3;
```

This is the result:

```output
        orig        |     recovered      | native_equals | approx_equals
--------------------+--------------------+---------------+---------------
   234.567000000000 |   234.567000000000 | false         | true
```

It's rather strange that the _to_char()_ renditions of the internally represented _double precision_ values that the native `=` operator shows to be different are indistinguishable. Notice that the  _to_char()_ format mask asks for fifteen decimal digits but only twelve are rendered. This is a feature of _to_char()_: if you ask to see more decimal digits than are supported, then you silently see the maximum supported number. The outcome of this test only serves to emphasize the importance of creating your own user-defined equality operator for the comparison of real number values.

## The interval utility functions

### type interval_parameterization_t as (yy, mm, dd, hh, mi, ss)

When you define an _interval_ value using either the _::interval_ typecast of a _text_ literal or the _make_interval()_ built-in function, you specify values for some or all of _years_, _months_, _days_, _hours_, _minutes_, and _seconds_. Try this, using the _::interval_ typecast approach:

```plpgsql
select
  '5 years 4 months'              ::interval as i1,
  '40 days'                       ::interval as i2,
  '9 hours 30 minutes 45 seconds' ::interval as i3;
```

This is the result:

```output
       i1       |   i2    |    i3
----------------+---------+----------
 5 years 4 mons | 40 days | 09:30:45
```

And try this, using the _make_interval()_ approach:

```plpgsql
select
  make_interval(years=>5, months=>4)          as i1,
  make_interval(days=>40)                     as i2,
  make_interval(hours=>9, mins=>30, secs=>45) as i3;
```

It produces the same result as using the _::interval_ typecast approach. Notice that the _::interval_ typecast approach allows real number values for each of the six parameters. In contrast, the _make_interval()_ function defines all of its formal parameters except for _secs_ with the data type _integer_, and It defines _secs_ with the data type _double precision_.

Each approach allows other parameters to specify, for example, _centuries_, _weeks_, or _microseconds_. But the six parameters shown above are the most-commonly used. They are jointly more than sufficient to define non-zero values for each of the three fields of the internal representation. The bare minimum that gets an obviously predictable result is just _months_, _days_, and _seconds_. (See the section [How does YSQL represent an _interval_ value?](../interval-representation).)

Omitting one of the parameters has the same effect as specifying zero for it. Create the type _interval_parameterization_t_ to represent this six-field tuple.

```plpgsql
drop type if exists interval_parameterization_t cascade;

create type interval_parameterization_t as(
  yy double precision,
  mm double precision,
  dd double precision,
  hh double precision,
  mi double precision,
  ss double precision);
```

Other functions, below, have a single formal parameter with data type _interval_parameterization_t_ or are defined to return an instance of that type.

### function interval_parameterization (yy, mm, dd, hh, mi, ss) returns interval_parameterization_t

You can't define default values for the fields of a user-defined type. To avoid verbose code when you want to specify non-zero values for only some, or especially just one, of the six fields, the helper function _interval_parameterization()_ is defined with default values for its corresponding six formal parameters.

```plpgsql
drop function if exists interval_parameterization(
  double precision, double precision, double precision, double precision, double precision, double precision);

create function interval_parameterization(
  yy in double precision default 0,
  mm in double precision default 0,
  dd in double precision default 0,
  hh in double precision default 0,
  mi in double precision default 0,
  ss in double precision default 0)
  returns interval_parameterization_t
  language plpgsql
as $body$
declare
  ok constant boolean :=
    (yy is not null) and
    (mm is not null) and
    (dd is not null) and
    (hh is not null) and
    (mi is not null) and
    (ss is not null);
  p interval_parameterization_t not null :=
   (yy, mm, dd, hh, mi, ss)::interval_parameterization_t;
begin
  assert ok, 'No argument, when provided, may be null';
  return p;
end;
$body$;
```

Test it like this:

```plpgsql
select interval_parameterization(yy=>5, mm=>6)::text;
```

This is the result:

```output
 interval_parameterization
---------------------------
 (5,6,0,0,0,0)
```

### function interval_value (interval_parameterization_t) returns interval

This function constructs an _interval_ value from an _interval_parameterization_t_ instance. Create it thus:

```plpgsql
drop function if exists interval_value(interval_parameterization_t) cascade;

create function interval_value(p in interval_parameterization_t)
  returns interval
  language plpgsql
as $body$
declare
  yy constant interval not null := p.yy::text ||' years';
  mm constant interval not null := p.mm::text ||' months';
  dd constant interval not null := p.dd::text ||' days';
  hh constant interval not null := p.hh::text ||' hours';
  mi constant interval not null := p.mi::text ||' minutes';
  ss constant interval not null := p.ss::text ||' seconds';
begin
  return yy + mm + dd + hh + mi + ss;
end;
$body$;
```

It uses the _::interval_ typecast approach rather than the _make_interval()_ approach because the pedagogy of the section [How does YSQL represent an _interval_ value?](../interval-representation/) depends on being able to specify real number values for each of the six parameters.

Test it like this:

```plpgsql
select interval_value(interval_parameterization(mm=>2.345, dd=>3.456))::text;
```

This is the result:

```output
      interval_value
---------------------------
 2 mons 13 days 19:20:38.4
```

### function parameterization (interval) returns interval_parameterization_t

Create the function thus:

```plpgsql
drop function if exists parameterization(interval) cascade;

create function parameterization(i in interval)
  returns interval_parameterization_t
  language plpgsql
as $body$
declare
  -- All but the seconds value are always integral.
  yy  double precision not null := round(extract(years   from i));
  mm  double precision not null := round(extract(months  from i));
  dd  double precision not null := round(extract(days    from i));
  hh  double precision not null := round(extract(hours   from i));
  mi  double precision not null := round(extract(minutes from i));
  ss  double precision not null :=       extract(seconds from i);
begin
  return (yy, mm, dd, hh, mi, ss)::interval_parameterization_t;
end;
$body$;
```

Test it like this:

```plpgsql
select parameterization('2 months 13 days 19:20:38.4'::interval)::text;
```

This is the result:

```output
 parameterization
--------------------------------------
 (0,2,13,19,20,38.4)
```

### function approx_equals (p1_in in interval_parameterization_t, p2_in in interval_parameterization_t) returns boolean

Create a function to test a pair of "interval_parameterization_t values" for equality. The function _parameterization(i in interval)_ uses _extract()_ and this accesses the internal _[\[mm, dd, ss\]](../interval-representation/)_ representation. There's a risk of rounding errors here. For example, when the _ss_ field corresponds _04:48:00_, this might be extracted as _04:47,59.99999999..._. The _approx_equals()_ implementation needs to accommodate this.

```plpgsql
drop function if exists approx_equals(interval_parameterization_t, interval_parameterization_t) cascade;

create function approx_equals(p1_in in interval_parameterization_t, p2_in in interval_parameterization_t)
  returns boolean
  language plpgsql
as $body$
declare
  -- There's no need (for the present pedagogical purpose) to extend this to
  -- handle NULL inputs. It would be simple to do this.
  p1    constant interval_parameterization_t not null := p1_in;
  p2    constant interval_parameterization_t not null := p2_in;

  mons1 constant double precision            not null := p1.yy*12.0 + p1.mm;
  mons2 constant double precision            not null := p2.yy*12.0 + p2.mm;

  secs1 constant double precision            not null := p1.hh*60.0*60.0 + p1.mi*60.0 + p1.ss;
  secs2 constant double precision            not null := p2.hh*60.0*60.0 + p2.mi*60.0 + p2.ss;

  eq    constant boolean                     not null := (mons1 ~= mons2) and
                                                         (p1.dd ~= p2.dd) and
                                                         (secs1 ~= secs2);
begin
  return eq;
end;
$body$;
```

And map it to the `~=` operator thus:

```plpgsql
drop operator if exists ~= (interval_parameterization_t, interval_parameterization_t) cascade;

create operator ~= (
  leftarg   = interval_parameterization_t,
  rightarg  = interval_parameterization_t,
  procedure = approx_equals);
```

### type interval_mm_dd_ss_t as (mm, dd, ss)

The type _interval_mm_dd_ss_t_ models the internal representation of an _interval_ value. It's central to the pedagogy of the sections [How does YSQL represent an _interval_ value?](../interval-representation) and [Interval arithmetic](../interval-arithmetic/). It is also used to implement the user-defined _domains_ described in the section [Custom domain types for specializing the native _interval_ functionality](../custom-interval-domains/).

```plpgsql
drop type if exists interval_mm_dd_ss_t cascade;

create type interval_mm_dd_ss_t as(
  mm int, dd int, ss double precision);
```

### function interval_mm_dd_ss (interval) returns interval_mm_dd_ss_t

Create a function to create an _interval_mm_dd_ss_t_ value from an _interval_ value:

```plpgsql
drop function if exists interval_mm_dd_ss(interval) cascade;

create function interval_mm_dd_ss(i in interval)
  returns interval_mm_dd_ss_t
  language plpgsql
as $body$
begin
  if i is null then
    return null;
  else
    declare
      mm  constant int              not null := (extract(years from i))*12 +
                                                 extract(months from i);

      dd  constant int              not null := extract(days from i);

      ss  constant double precision not null := (extract(hours   from i))*60*60 +
                                                 extract(minutes from i)*60 +
                                                 extract(seconds from i);
    begin
      return (mm, dd, ss);
    end;
  end if;
end;
$body$;
```

Test it like this:

```plpgsql
select interval_mm_dd_ss('2.345 months 3.456 days 19:20:38.423456'::interval)::text;
```

This is the result:

```output
 (2,13,139276.823456)
```

### function approx_equals (interval_mm_dd_ss_t, interval_mm_dd_ss_t) returns boolean

The seconds field of the _[mm, dd, ss]_ tuple that defines type _[interval_mm_dd_ss_t](#type-interval-mm-dd-ss-t-as-mm-dd-ss)_ is declared as _double precision_. This means that the native `=` test on a pair of values of this type (it uses a naïve field-by-field equality comparison) will be subject to the underlying challenge of [testing for the equality of two double precision values](#generic-helper-function-to-test-for-the-equality-of-two-double-precision-values). This implies the need for a user-defined approximate comparison operator for _interval_mm_dd_ss_t_ values.

Create the function thus using the user-defined [~= operator for _double precision_ values](#function-approx-equals-double-precision-double-precision-returns-boolean):

```plpgsql
drop function if exists approx_equals(interval_mm_dd_ss_t, interval_mm_dd_ss_t) cascade;

create function approx_equals(i1_in in interval_mm_dd_ss_t, i2_in in interval_mm_dd_ss_t)
  returns boolean
  language plpgsql
as $body$
declare
  -- There's no need (for the present pedagogical purpose) to extend this to
  -- handle NULL inputs. It would be simple to do this.
  i1 constant interval_mm_dd_ss_t not null := i1_in;
  i2 constant interval_mm_dd_ss_t not null := i2_in;
  eq constant boolean             not null := (i1.mm =  i2.mm) and
                                              (i1.dd =  i2.dd) and
                                              (i1.ss ~= i2.ss);
begin
  return eq;
end;
$body$;
```

And map it to the `~=` operator thus:

```plpgsql
drop operator if exists ~= (interval_mm_dd_ss_t, interval_mm_dd_ss_t) cascade;

create operator ~= (
  leftarg   = interval_mm_dd_ss_t,
  rightarg  = interval_mm_dd_ss_t,
  procedure = approx_equals);
```

### function interval_value (interval_mm_dd_ss_t) returns interval

Create a function to create an _interval_ value from an _interval_mm_dd_ss_t_ instance:

```plpgsql
drop function if exists interval_value(interval_mm_dd_ss_t) cascade;

create function interval_value(i in interval_mm_dd_ss_t)
  returns interval
  language plpgsql
as $body$
begin
  return make_interval(months=>i.mm, days=>i.dd, secs=>i.ss);
end;
$body$;
```

Test it like this:

```plpgsql
select interval_value((2,13,139276.823456)::interval_mm_dd_ss_t)::text;
```

This is the result:

```output
 2 mons 13 days 38:41:16.823456
```

### function parameterization (interval_mm_dd_ss_t) returns interval_parameterization_t

**Note:** The implementation of the function _parameterization(interval_mm_dd_ss_t)_ documents the algorithm for calculating the _text_ typecast of an _interval_ value (or equivalently the result of using the _extract_ functions ).

Here is an example of using _extract_:

```plpgsql
select extract(hours from '123 months 234 days 34567.123456 seconds'::interval) as "extracted hours";
```

This is the result:

```output
 extracted hours
-----------------
               9
```

The _extract_ functions are used to implement the function _parameterization()_ shown above.

Create the function _parameterization(interval_mm_dd_ss_t)_ thus:

```plpgsql
drop function if exists parameterization(interval_mm_dd_ss_t) cascade;

create function parameterization(i in interval_mm_dd_ss_t)
  returns interval_parameterization_t
  language plpgsql
as $body$
declare
  yy  constant int              := trunc(i.mm/12);
  mm  constant int              := i.mm - yy*12;
  dd  constant int              := i.dd;
  hh  constant int              := trunc(i.ss/(60.0*60.0));
  mi  constant int              := trunc((i.ss - hh*60.0*60)/60.0);
  ss  constant double precision := i.ss - (hh*60.0*60.0 + mi*60.0);
begin
  return (yy, mm, dd, hh, mi, ss)::interval_parameterization_t;
end;
$body$;
```

Test it like this:

```plpgsql
select parameterization((123, 234, 34567.123456)::interval_mm_dd_ss_t)::text;
```

This is the result:

```output
 (10,3,234,9,36,7.12345600000117)
```

Notice the apparent inaccuracy brought by the use of _double precision_. This is inevitable. Compare the result with that from using _parameterization(interval)_ on an actual _interval_ value:

```plpgsql
select parameterization('123 months 234 days 34567.123456 seconds'::interval)::text;
```
This is the result:

```output
  (10,3,234,9,36,7.123456)
```

This emphasizes the need for the user-defined [`~=` operator for _interval_parameterization_t_](#function-approx-equals-p1-in-in-interval-parameterization-t-p2-in-in-interval-parameterization-t-returns-boolean) values. Do this:

```plpgsql
select (
    parameterization((123, 234, 34567.123456)::interval_mm_dd_ss_t) ~=
    parameterization('123 months 234 days 34567.123456 seconds'::interval)
  )::text;
```

The result is _true_.

### function justified_seconds (interval) returns double precision

This function is discussed in the section [The justify() and extract(epoch ...) functions for interval values](../justfy-and-extract-epoch/#the-justified-seconds-user-defined-function). And the section [Comparing two _interval_ values](../interval-arithmetic/interval-interval-comparison/#modeling-the-interval-interval-comparison-test) relies on this function to model the implementation of the comparison algorithm.

The semantics of the _justify_interval()_ built-in function (explained [here](../justfy-and-extract-epoch/#justify-interval) in the section [The _justify()_ and _extract(epoch ...)_ functions for _interval_ values](../justfy-and-extract-epoch/) suggests a scheme to map an _interval_ value (a vector with three components) to  a real number. The same rule of thumb that _justify_interval()_ uses to normalize the _ss_ and the _dd_ fields of the internal representation (_24 hours_ is deemed to be the same as _1 day_ and _30 days_ is deemed to be the same as _1 month_) can be used to compute a number of seconds from an _interval_ value.

You probably won't use this function in application code. But it's used in the section [Comparing two _interval_ values](../interval-arithmetic/interval-interval-comparison/#modeling-the-interval-interval-comparison-test) to model the implementation of the comparison algorithm. It also allows you to understand how it's possible to use an _order by_ predicate with an _interval_ table column (like, for example, _pg_timezone_names.utc_offset_) and to create an index on such a column.

Create the _justified_seconds()_ thus:

```plpgsql
drop function if exists justified_seconds(interval) cascade;

create function justified_seconds(i in interval)
  returns double precision
  language plpgsql
as $body$
begin
  if i is null then
    return null;
  else
    declare
      secs_pr_day    constant double precision    not null := 24*60*60;
      secs_pr_month  constant double precision    not null := secs_pr_day*30;

      r              constant interval_mm_dd_ss_t not null := interval_mm_dd_ss(i);
      ss             constant double precision    not null := r.ss + r.dd*secs_pr_day + r.mm*secs_pr_month;

      rj             constant interval_mm_dd_ss_t not null := interval_mm_dd_ss(justify_interval(i));
      ssj            constant double precision    not null := rj.ss + rj.dd*secs_pr_day + rj.mm*secs_pr_month;
    begin
      assert ss = ssj, 'justified_seconds(): assert failed';
      return ss;
    end;
  end if;
end;
$body$;
```

Notice the _assert_ statement that tests that the computed number of seconds from an "as is" _interval_ value is identical to the computed number of seconds from an _interval_ value that's normalized using _justify_interval()_. You might argue, correctly, that what the _assert_ statement tests can be proved algebraically. The _assert_ is used, here, just as a documentation device.

The [subsection that describes this function](../justfy-and-extract-epoch/#the-justified-seconds-user-defined-function) in the section [The justify() and extract(epoch ...) functions for _interval_ values](../justfy-and-extract-epoch/) demonstrates its result for a selection of input values.

### The user-defined "strict equals" interval-interval "==" operator

{{< tip title="Use the 'strict equals' operator, '==', rather than the native '=', to compare 'interval' values." >}}
Yugabyte staff members have carefully considered the practical value of the native _interval-interval_ overload of the `=` operator that YSQL inherits from PostgreSQL.

They believe that the use-cases where the functionality will be useful are rare—and that, rather, a "strict equals" notion, that requires pairwise equality of the individual fields of the [_\[mm, dd, ss\]_ internal representations](../interval-representation/) of the _interval_ values that are compared, will generally be more valuable.
{{< /tip >}}

See the section [Comparing two _interval_ values](../interval-arithmetic/interval-interval-comparison/) for the larger discussion on this topic.

Create the _strict_equals()_ function thus:

```plpgsql
drop function if exists strict_equals(interval, interval) cascade;

create function strict_equals(i1 in interval, i2 in interval)
  returns boolean
  language plpgsql
as $body$
begin
  if i1 is null or i2 is null then
    return null;
  else
    declare
      mm_dd_ss_1 constant interval_mm_dd_ss_t not null := interval_mm_dd_ss(i1);
      mm_dd_ss_2 constant interval_mm_dd_ss_t not null := interval_mm_dd_ss(i2);
    begin
      return mm_dd_ss_1 ~= mm_dd_ss_2;
    end;
  end if;
end;
$body$;
```

And map it to the `==` operator thus:

```plpgsql
drop operator if exists == (interval, interval) cascade;

create operator == (
  leftarg   = interval,
  rightarg  = interval,
  procedure = strict_equals);
```

### function to_timestamp_without_tz (double precision) returns timestamp

Strangely, there is no native _plain timestamp_ equivalent of the _to_timestamp()_ function. But it's easy to write your own. Notice that a pair of functions cannot be overload-distinguished when they differ only in the return type. So your implementation of the missing functionality must have a new unique name.

```plpgsql
drop function if exists to_timestamp_without_tz(double precision);

create function to_timestamp_without_tz(ss_from_epoch in double precision)
  returns /* plain */ timestamp
  language plpgsql
as $body$
declare
  current_tz text not null := current_setting('TimeZone');
begin
  assert length(current_tz) > 0, 'undefined time zone';
  set timezone = 'UTC';
  declare
    t_tz constant timestamptz := to_timestamp(ss_from_epoch);
    t    constant timestamp   := t_tz at time zone 'UTC';
  begin
    -- Restore the saved time zone setting.
    execute 'set timezone = '''||current_tz||'''';
    return t;
  end;
end;
$body$;
```
Test it like this:

```plpgsql
select to_timestamp_without_tz(42123.456789);
```

This is the result:

```output
 1970-01-01 11:42:03.456789
```

### function to_time (double precision) returns time

Strangely, there is no native _to_time()_ function to transform some number of seconds from midnight to a _time_ value. But it's easy to write your own.

```plpgsql
drop function if exists to_time(double precision) cascade;

-- mod() doesn't have an overload for "double precision" arguments.
create function to_time(ss in double precision)
  returns time
  language plpgsql
as $body$
declare
  -- Notice the ss value can be bigger than ss_per_day.
  ss_per_day        constant  numeric          not null := 24.0*60.0*60.0;
  ss_from_midnight  constant  double precision not null := mod(ss::numeric, ss_per_day);
  t                 constant  time             not null :=
                      make_interval(secs=>ss_from_midnight)::time;
begin
  return t;
end;
$body$;
```

Test it like this:

```plpgsql
select to_time((29*60*60 + 17*60)::double precision + 42.123456::double precision);
```

This is the result:

```output
 05:17:42.123456
```

## Bonus functions

The three functions shown below aren't used anywhere else in the documentation of the _interval_ data type. But they were used extensively for _ad hoc_ experiments and as a tracing tool while the code examples that this overall _interval_ section uses were being developed. You might find them useful for the same reason.

### interval_mm_dd_ss_as_text(interval)

The function _interval_mm_dd_ss_as_text(interval)_ returns the same information that the function _interval_mm_dd_ss(interval)_ returns, but as a _text_ literal for the _interval_ value so that it can be used directly in any context that needs this. Create it thus:

```plpgsql
drop function if exists interval_mm_dd_ss_as_text(interval) cascade;

create function interval_mm_dd_ss_as_text(i in interval)
  returns text
  language plpgsql
as $body$
declare
  mm_dd_ss constant interval_mm_dd_ss_t not null := interval_mm_dd_ss(i);
  ss_text  constant text                not null := ltrim(to_char(mm_dd_ss.ss, '9999999999990.999999'));
begin
  return
    mm_dd_ss.mm::text||' months ' ||
    mm_dd_ss.dd::text||' days '   ||
    ss_text          ||' seconds' ;
end;
$body$;
```

Test it like this:

```plpgsql
select interval_mm_dd_ss_as_text('2 years 3 months 999 days 77 hours 53 min 17.123456 secs'::interval);
```

This is the result:

```output
27 months 999 days 280397.123456 seconds
```

### parameterization_as_text(interval)

The function _parameterization_as_text(interval)_ returns the same information that the function _parameterization(interval)_ returns, but as a _text_ literal for the _interval_ value so that it can be used directly in any context that needs this. Create it thus:

```plpgsql
drop function if exists parameterization_as_text(interval) cascade;

create function parameterization_as_text(i in interval)
  returns text
  language plpgsql
as $body$
declare
  p        constant interval_parameterization_t not null := parameterization(i);
  ss_text  constant text                        not null := ltrim(to_char(p.ss, '9999999999990.999999'));
begin
  return
    p.yy::text||' years '   ||
    p.mm::text||' months '  ||
    p.dd::text||' days '    ||
    p.hh::text||' hours '   ||
    p.mi::text||' minutes ' ||
    ss_text   ||' seconds';
end;
$body$;
```

Test it like this:

```plpgsql
select parameterization_as_text('42 months 999 days 77.12345 hours'::interval);
```

This is the result:

```output
 3 years 6 months 999 days 77 hours 7 minutes 24.420000 seconds
```

### parameterization_as_text(interval_mm_dd_ss_t)

Just as the function _parameterization_as_text(interval)_ does for an _interval_ value, it can be useful, too, to present the same information that the function _parameterization(interval_mm_dd_ss_t)_ returns, again as a _text_ literal for the _interval_ value. The function _parameterization_as_text(interval_mm_dd_ss_t)_ does this. Create it thus:

```plpgsql
drop function if exists parameterization_as_text(interval_mm_dd_ss_t) cascade;

create function parameterization_as_text(i in interval_mm_dd_ss_t)
  returns text
  language plpgsql
as $body$
declare
  p        constant interval_parameterization_t not null := parameterization(i);
  ss_text  constant text                        not null := ltrim(to_char(p.ss, '9999999999990.999999'));
begin
  return
    p.yy::text||' years '   ||
    p.mm::text||' months '  ||
    p.dd::text||' days '    ||
    p.hh::text||' hours '   ||
    p.mi::text||' minutes ' ||
    ss_text   ||' seconds';
end;
$body$;
```

Test it like this:

```plpgsql
select parameterization_as_text((77,13,139276.800000)::interval_mm_dd_ss_t)::text;
```

This is the result:

```output
6 years 5 months 13 days 38 hours 41 minutes 16.800000 seconds
```

Compare this with the output of function _parameterization_as_text(interval)_ for the same input:

```plpgsql
select parameterization_as_text('77 months, 13 days, 139276.800000 secs'::interval )::text;
```

The result is identical.

All this vividly makes the point that a particular actual _interval_ value can be created using many different variants of the corresponding _text_ literal. Try this:

```plpgsql
select
  (
    '2.345 months 3.456 days 19:20:38.4'::interval == '2 mons 13 days 38:41:16.8'::interval
    and
    '2.345 months 3.456 days 19:20:38.4'::interval == '2 months 13 days 139276.800000 seconds'::interval
  )
  ::text as "all the same";
```

This is the result:

```output
 all the same
--------------
 true
```
