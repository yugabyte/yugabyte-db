---
title: User-defined interval utility functions [YSQL]
headerTitle: User-defined interval utility functions
linkTitle: interval utility functions
description: Presents copy-and-paste ready code to create utility functions that support learning about and using the interval data type. [YSQL]
menu:
  v2.6:
    identifier: interval-utilities
    parent: type-interval
    weight: 100
isTocNested: true
showAsideToc: true
---

{{< tip title="Download the kit to create all of the code that this section describes." >}}
The code is included in a larger set of useful re-useable _date-time_ code. See [Download the _.zip_ file to create the reusable code that this overall major section describes](../../../intro/#download).
{{< /tip >}}

The code presented on this page was designed to support the pedagogy of the code examples in the other sections in the enclosing section [The _interval_ data type and its variants](../../type-interval/). You can install it all, in the order presented here, at any time. It depends only on built-in SQL functions and operators and, for some of the functions, on code that was created previously in the installation order that this page shows.

The code defines two user-defined types, _interval_parameterization_t_ and _interval_mm_dd_ss_t_. They model, respectively, the conventional parameterization of an _interval_ value as a _[yy, mm, dd, hh, mi, ss]_ tuple and the internal representation of an _interval_ value as a _[mm, dd, ss]_ tuple. Together with the _interval_ data type itself, this makes three types—and therefore three possible pairs and six mutual transformation functions. Here is the summary:

```output
┌————————————————————————————————————————┬———————————————————————————————————————┐
│ type interval_parameterization_t as(   │ type interval_mm_dd_ss_t as(          │
│   yy numeric, mm numeric, dd numeric,  │   mm int, dd int, ss numeric(1000,6)) │
│   hh numeric, mi numeric, ss numeric)  │                                       │
└————————————————————————————————————————┴———————————————————————————————————————┘

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

The code that the remainder of this section presents defines the _interval_parameterization_t_ and _interval_mm_dd_ss_t_ types, and all but one of the six mutual transformation functions. The design of the five functions that are shown here is straightforward—but it does depend on knowing that the internal representation of an _interval_ value is a _[mm, dd, ss]_ tuple that uses four-byte integers to record the _months_ and _days_ fields and an eight-byte integer to record the _seconds_ field as a microseconds value.

The code that defines the function _interval_mm_dd_ss(interval_parameterization_t)_ implements the algorithm that PostgreSQL, and therefore YSQL, uses in its C code to transform a value, presented as a _text_ literal, to the internal _interval_ representation. Here's an example of the presented _text_ literal:

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

This algorithm is rather complex and is the focus of the section [Modeling the internal representation and comparing the model with the actual implementation](../interval-representation/internal-representation-model/) which defines and tests the _interval_mm_dd_ss(interval_parameterization_t)_ function.

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

Each approach allows other parameters to specify, for example, _centuries_, _weeks_, or _microseconds_. But the six parameters shown above are the most-commonly used. They are jointly more than sufficient to define non-zero values for each of the three fields of the internal representation. The bare minimum that gets an obviously predictable result is just _months_, _days_, and _seconds_. (See the section [How does YSQL represent an _interval_ value?](../interval-representation))

Omitting one of the parameters has the same effect as specifying zero for it. Create the type _interval_parameterization_t_ to represent this six-field tuple.

```plpgsql
drop type if exists interval_parameterization_t cascade;

create type interval_parameterization_t as(
  yy numeric,
  mm numeric,
  dd numeric,
  hh numeric,
  mi numeric,
  ss numeric);
```

Other functions, below, have a single formal parameter with data type _interval_parameterization_t_ or are defined to return an instance of that type.

### function interval_parameterization (yy, mm, dd, hh, mi, ss) returns interval_parameterization_t

You can't define default values for the fields of a user-defined type. To avoid verbose code when you want to specify non-zero values for only some, or especially just one, of the six fields, the helper function _interval_parameterization()_ is defined with default values for its corresponding six formal parameters.

```plpgsql
drop function if exists interval_parameterization(numeric, numeric, numeric, numeric, numeric, numeric);

create function interval_parameterization(
  yy in numeric default 0,
  mm in numeric default 0,
  dd in numeric default 0,
  hh in numeric default 0,
  mi in numeric default 0,
  ss in numeric default 0)
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
  yy numeric          not null := extract(years   from i);
  mm numeric          not null := extract(months  from i);
  dd numeric          not null := extract(days    from i);
  hh numeric          not null := extract(hours   from i);
  mi numeric          not null := extract(minutes from i);
  ss numeric(1000, 6) not null := extract(seconds from i);
begin
  return (yy, mm, dd, hh, mi, ss)::interval_parameterization_t;
end;
$body$;
```

Test it like this:

```plpgsql
select parameterization('2 months 13 days 19:20:38.4')::text;
```

This is the result:

```output
 parameterization 
--------------------------------------
 (0,2,13,19,20,38.400000)
```

### type interval_mm_dd_ss_t as (mm, dd, ss)

The type _interval_mm_dd_ss_t_ models the internal representation of an _interval_ value. It's central to the pedagogy of the sections [How does YSQL represent an _interval_ value?](../interval-representation) and [Interval arithmetic](../interval-arithmetic/). It is also used to implement the user-defined _domains_ described in the section [Defining and using custom domain types to specialize the native interval functionality](../custom-interval-domains/).

```plpgsql
drop type if exists interval_mm_dd_ss_t cascade;

create type interval_mm_dd_ss_t as(
  mm int, dd int, ss numeric(1000,6));
```

### function interval_mm_dd_ss (interval) returns interval_mm_dd_ss_t

Create the function thus:

```plpgsql
drop function if exists interval_mm_dd_ss(interval) cascade;

create function interval_mm_dd_ss(i in interval)
  returns interval_mm_dd_ss_t
  language plpgsql
as $body$
declare
  mm constant int not null := (extract(years from i))*12 +
                              extract(months from i);

  dd constant int not null := extract(days from i);

  ss constant numeric(1000,6) not null := (extract(hours from i))*60*60 +
                              extract(minutes from i)*60 +
                              extract(seconds from i);
begin
  return (mm, dd, ss);
end;
$body$;
```

Test it like this:

```plpgsql
select interval_mm_dd_ss('2.345 months 3.456 days 19:20:38.423456')::text;
```

This is the result:

```output
 (2,13,139276.823456)
```

### function interval_value (interval_mm_dd_ss_t) returns interval

Create the function thus:

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
  yy constant int              := trunc(i.mm/12);
  mm constant int              := i.mm - yy*12;
  dd constant int              := i.dd;
  hh constant int              := trunc(i.ss/(60.0*60.0));
  mi constant int              := trunc((i.ss - hh*60.0*60)/60.0);
  ss constant numeric(1000, 6) := i.ss - (hh*60.0*60.0 + mi*60.0);
begin
  return (
    yy::numeric,
    mm::numeric,
    dd::numeric,
    hh::numeric,
    mi::numeric,
    ss)::interval_parameterization_t;
end;
$body$;
```

Test it like this:

```plpgsql
select parameterization((123, 234, 34567.123456)::interval_mm_dd_ss_t)::text;
```

This is the result:

```output
 parameterization 
-----------------------------------------
 (10,3,234,9,36,7.123456)
```

Compare the result with that from using _parameterization(interval)_ on an actual interval value:

```plpgsql
select parameterization('123 months 234 days 34567.123456 seconds'::interval)::text;
```
The result is identical.

### function to_timestamp_without_tz (double precision) returns timestamp

Strangely, there is no native _plain timestamp_ equivalent of the _to_timestamp()_ function. But it's easy to write your own. Notice that a pair of functions cannot be overload-distinguished when they differ only in the return type. So your implementation of the missing functionality must have a new unique name.

```plpgsql
drop function if exists to_timestamp_without_tz(double precision);

create function to_timestamp_without_tz(ss_from_epoch in double precision)
  returns /* plain */ timestamp
  language plpgsql
as $body$
declare
  current_tz text not null := '';
begin
  -- Save present TimeZone setting.
  show timezone into current_tz;
  assert length(current_tz) > 0, 'undefined timezone';
  set timezone = 'UTC';
  declare
    t_tz constant timestamptz := to_timestamp(ss_from_epoch);
    t    constant timestamp   := t_tz at time zone 'UTC';
  begin
    -- Restore the saved TimeZone setting.
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

### function to_time (numeric) returns time

Strangely, there is no native _to_time()_ function to transform some number of seconds to a _time_ value. But it's easy to write your own.

```plpgsql
drop function if exists to_time(numeric) cascade;

create function to_time(ss in numeric)
  returns time
  language plpgsql
as $body$
declare
  -- Notice the ss_from_midnight can be bigger than ss_per_day.
  ss_per_day        constant numeric not null := 24.0*60.0*60.0;
  ss_from_midnight  constant numeric not null := mod(ss, ss_per_day);
  t                 constant time    not null := make_interval(
                                                   secs=>ss_from_midnight::double precision)::time;
begin
  return t;
end;
$body$;
```

Test it like this:

```plpgsql
select to_time((29*60*60 + 17*60)::numeric + 42.123456::numeric);
```

This is the result:

```output
 05:17:42.123456
```

### The user-defined "strict equals" interval-interval "==" operator

{{< tip title="Use the 'strict equals' operator, '==', rather than the native '=', to compare 'interval' values." >}}
Yugabyte staff members have carefully considered the practical value of the native _interval-interval_ overload of the `=` operator that YSQL inherits from PostgreSQL.

They believe that the use-cases where the functionality will be useful are rare—and that, rather, a "strict equals" notion, that requires pairwise equality of the individual fields of the [_&#91;mm, dd, ss&#93;_ internal representations](../interval-representation/) of the _interval_ values that are compared, will generally be more valuable.
{{< /tip >}}

See the section [Comparing two interval values for equality](../interval-arithmetic/interval-interval-equality/) for the larger discussion on this topic. 

Create the _strict_equals()_ function thus:

```plpgsql
drop function if exists strict_equals(interval, interval) cascade;

create function strict_equals(i1 in interval, i2 in interval)
  returns boolean
  language plpgsql
as $body$
declare
  mm_dd_ss_1 constant interval_mm_dd_ss_t := interval_mm_dd_ss(i1);
  mm_dd_ss_2 constant interval_mm_dd_ss_t := interval_mm_dd_ss(i2);
begin
  return mm_dd_ss_1 = mm_dd_ss_2;
end;
$body$;
```

And map it to the `==` operator thus:

```plpgsql
create operator == (
    leftarg   = interval,
    rightarg  = interval,
    procedure = strict_equals
);
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
  mm_dd_ss constant interval_mm_dd_ss_t := interval_mm_dd_ss(i);
begin
  return
    mm_dd_ss.mm::text||' months ' ||
    mm_dd_ss.dd::text||' days '   ||
    mm_dd_ss.ss::text||' seconds' ;
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
  p constant interval_parameterization_t := parameterization(i);
begin
  return
    p.yy::text||' years '   ||
    p.mm::text||' months '  ||
    p.dd::text||' days '    ||
    p.hh::text||' hours '   ||
    p.mi::text||' minutes ' ||
    p.ss::text||' seconds';
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
  p constant interval_parameterization_t := parameterization(i);
begin
  return
    p.yy::text||' years '   ||
    p.mm::text||' months '  ||
    p.dd::text||' days '    ||
    p.hh::text||' hours '   ||
    p.mi::text||' minutes ' ||
    p.ss::text||' seconds';
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
    '2.345 months 3.456 days 19:20:38.4'::interval = '2 mons 13 days 38:41:16.8'::interval
    and
    '2.345 months 3.456 days 19:20:38.4'::interval = '2 months 13 days 139276.800000 seconds'::interval
  )
  ::text as "all the same";
```

This is the result:

```output
 all the same 
--------------
 true
```