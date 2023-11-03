---
title: Functions for creating date-time values [YSQL]
headerTitle: Functions for creating date-time values
linkTitle: Creating date-time values
description: The semantics of the functions for creating date-time values. [YSQL]
menu:
  v2.14:
    identifier: creating-date-time values
    parent: date-time-functions
    weight: 10
type: docs
---

Notice that there is no built-in function to create a _timetz_ value—but this is of no consequence because of the recommendation, stated on the [Date and time data types](../../../type_datetime/#avoid-timetz) section's main page, to avoid using this data type.

## function make_date() returns date

The _make_date()_ built-in function creates a _date_ value from _int_ values for the year, the month-number, and the day-number. Here is the interesting part of the output from \\_df make_date()_:

```output
 Result data type |           Argument data types
------------------+------------------------------------------
 date             | year integer, month integer, day integer
```

Here is an example:

```plpgsql
with c as (
  select make_date(year=>2019, month=>4, day=>22) as d)
select pg_typeof(d)::text as "type", d::text from c;
```

This is the result:

```output
 type |     d
------+------------
 date | 2019-04-22
```

Use a negative value for _year_ to produce a BC result:

```plpgsql
with c as (
  select make_date(year=>-10, month=>1, day=>31) as d)
select pg_typeof(d)::text as "type", d::text from c;
```

This is the result:

```output
 type |       d
------+---------------
 date | 0010-01-31 BC
```

If you specify a non-existent date (like the year _zero_ or, say, _30-February_) then you get this error:

```output
22008: date field value out of range
```

Try this:

```plpgsql
drop procedure if exists confirm_expected_22008(int, int, int) cascade;

create procedure confirm_expected_22008(yy in int, mm in int, dd in int)
  language plpgsql
as $body$
declare
  d date;
begin
  d := make_date(year=>yy, month=>mm, day=>dd);
  assert false, 'Unexpected';

-- 22008: date field value out of range...
exception when datetime_field_overflow then
  null;
end;
$body$;

do $body$
begin
  call confirm_expected_22008( 0,  1, 20);
  call confirm_expected_22008(10, 13, 20);
  call confirm_expected_22008(10,  2, 30);
end;
$body$;
```

The final block finishes silently, demonstrating that the outcomes are as expected.

## function make_time() returns (plain) time

The _make_time()_ built-in function creates a plain _time_ value from _int_ values for the hour and the minutes-past-the-hour, and a real number for the seconds-past-the-minute. Here is the interesting part of the output from \\_df make_time()_:

```output
    Result data type    |               Argument data types
------------------------+-------------------------------------------------
 time without time zone | hour integer, min integer, sec double precision
```

Here is an example:

```plpgsql
with c as (
  select make_time(hour=>13, min=>25, sec=>20.123456) as t)
select pg_typeof(t)::text as "type", t::text from c;
```

This is the result:

```output
          type          |        t
------------------------+-----------------
 time without time zone | 13:25:20.123456
```

If you specify a non-existent time (like, say, 25:00:00) then you get this error:

```output
22008: time field value out of range
```

Try this:

```plpgsql
drop procedure if exists confirm_expected_22008(int, int, double precision) cascade;

create procedure confirm_expected_22008(hh in int, mi in int, ss in double precision)
  language plpgsql
as $body$
declare
  t time;
begin
  t := make_time(hour=>hh, min=>mi, sec=>ss);
  assert false, 'Unexpected';

-- 22008: date field value out of range...
exception when datetime_field_overflow then
  null;
end;
$body$;

do $body$
begin
  call confirm_expected_22008(25, 30, 30.0);
  call confirm_expected_22008(13, 61, 30.0);
  call confirm_expected_22008(13, 30, 61.0);
end;
$body$;
```

The final block finishes silently, demonstrating that the outcomes are as expected.

## function make_timestamp() returns (plain) timestamp

The _make_timestamp()_ built-in function creates a plain _timestamp_ value from _int_ values for the year, the month-number, the day-number, the hour, and the minutes-past-the-hour, and a real number for the seconds-past-the-minute. Here is the interesting part of the output from \\_df make_timestamp()_:

```output
      Result data type       |                                    Argument data types
-----------------------------+--------------------------------------------------------------------------------------------
 timestamp without time zone | year integer, month integer, mday integer, hour integer, min integer, sec double precision
```

Here is an example:

```plpgsql
with c as (
  select make_timestamp(year=>2019, month=>4, mday=>22, hour=>13, min=>25, sec=>20.123456) as ts)
select pg_typeof(ts)::text as "type", ts::text from c;
```

This is the result:

```output
            type             |             ts
-----------------------------+----------------------------
 timestamp without time zone | 2019-04-22 13:25:20.123456
```

Of course, there's a possibility here to cause the _22008_ error. (The messages are spelled _date field value out of range_ or _time field value out of range_ according to what values were specified.) Try this:

```plpgsql
drop procedure if exists confirm_expected_22008(int, int, int, int, int, double precision) cascade;

create procedure confirm_expected_22008(yy in int, mm in int, dd in int, hh in int, mi in int, ss in double precision)
  language plpgsql
as $body$
declare
  t timestamp;
begin
  t := make_timestamp(year=>yy, month=>mm, mday=>dd, hour=>hh, min=>mi, sec=>ss);
  assert false, 'Unexpected';

-- 22008: date field value out of range... OR time field value out of range...
exception when datetime_field_overflow then
  null;
end;
$body$;

do $body$
begin
  call confirm_expected_22008(   0,  6, 10, 13, 30, 30.0);
  call confirm_expected_22008(2019, 13, 10, 13, 30, 30.0);
  call confirm_expected_22008(2019,  6, 31, 13, 30, 30.0);
  call confirm_expected_22008(2019,  6, 29, 25, 30, 30.0);
  call confirm_expected_22008(2019,  6, 29, 13, 61, 30.0);
  call confirm_expected_22008(2019,  6, 29, 13, 30, 61.0);
end;
$body$;
```

The final block finishes silently, demonstrating that the outcomes are as expected.

{{< note title="You cannot use a negative actual argument for the 'year' formal parameter with 'make_timestamp()'." >}}
This stands in obvious contrast to _make_date()_. See the section [Workaround for creating BC plain _timestamp_ and _timestamptz_ values](#workaround-for-creating-bc-plain-timestamp-and-timestamptz-values).
{{< /note >}}

## function make_timestamptz() returns timestamptz

The _make_timestamptz()_ built-in function creates a _timestamptz_ value from _int_ values for the year, the month-number, the day-number, the hour, and the minutes-past-the-hour, and a real number for the seconds-past-the-minute. Here is the interesting part of the output from \\_df make_timestamptz()_:

```output
     Result data type     |                                            Argument data types
--------------------------+----------------------------------------------------------------------------------------------------------
 timestamp with time zone | year integer, month integer, mday integer, hour integer, min integer, sec double precision
 timestamp with time zone | year integer, month integer, mday integer, hour integer, min integer, sec double precision, timezone text
```

You can think of it like this: the function's name, _make_timestamp()_ or _make_timestamptz()_, determines the return data type. The latter has an optional _text_ parameter that, when specified, determines the timezone. If it is omitted, then the session's current timezone setting determines this fact.

Here is an example:

```plpgsql
set timezone = 'UTC';
with c as (
  select make_timestamptz(year=>2019, month=>6, mday=>22, hour=>13, min=>25, sec=>20.123456, timezone=>'Europe/Helsinki') as tstz)
select pg_typeof(tstz)::text as "type", tstz::text from c;
```

This is the result:

```output
           type           |             tstz
--------------------------+-------------------------------
 timestamp with time zone | 2019-06-22 10:25:20.123456+00
```

Helsinki is three hours ahead of UTC during the summer, so the specified one o'clock in the afternoon, Helsinki time, is shown as ten o'clock in the morning UTC.

Of course, there's a possibility here to cause the _22008_ error. (The messages are spelled _date field value out of range_ or _time field value out of range_ according to what values were specified.) There's also a possibility to cause this error:

```
22023: time zone ... not recognized
```

Try this:

```plpgsql
drop procedure if exists confirm_expected_22008_or_22023(int, int, int, int, int, double precision, text) cascade;

create procedure confirm_expected_22008_or_22023(yy in int, mm in int, dd in int, hh in int, mi in int, ss in double precision, tz in text)
  language plpgsql
as $body$
declare
  t timestamp;
begin
  t := make_timestamptz(year=>yy, month=>mm, mday=>dd, hour=>hh, min=>mi, sec=>ss, timezone=>tz);
  assert false, 'Unexpected';

-- 22008: date field value out of range... OR time field value out of range... OR time zone ... not recognized
exception when datetime_field_overflow or invalid_parameter_value then
  null;
end;
$body$;

do $body$
begin
  call confirm_expected_22008_or_22023(   0,  6, 10, 13, 30, 30.0, 'Europe/Helsinki');
  call confirm_expected_22008_or_22023(2019, 13, 10, 13, 30, 30.0, 'Europe/Helsinki');
  call confirm_expected_22008_or_22023(2019,  6, 31, 13, 30, 30.0, 'Europe/Helsinki');
  call confirm_expected_22008_or_22023(2019,  6, 29, 25, 30, 30.0, 'Europe/Helsinki');
  call confirm_expected_22008_or_22023(2019,  6, 29, 13, 61, 30.0, 'Europe/Helsinki');
  call confirm_expected_22008_or_22023(2019,  6, 29, 13, 30, 61.0, 'Europe/Helsinki');
  call confirm_expected_22008_or_22023(2019,  6, 29, 13, 30, 61.0, 'Europe/Helsinki');
  call confirm_expected_22008_or_22023(2019,  6, 29, 13, 30, 30.0, 'Europe/Lillehammer');
end;
$body$;
```

The final block finishes silently, demonstrating that the outcomes are as expected.

{{< note title="You cannot use a negative actual argument for the 'year' formal parameter with 'make_timestamptz()'." >}}
This stands in obvious contrast to _make_date()_. See the section [Workaround for creating BC plain _timestamp_ and _timestamptz_ values](#workaround-for-creating-bc-plain-timestamp-and-timestamptz-values).
{{< /note >}}

## Workaround for creating BC plain timestamp and timestamptz values

You cannot use a negative actual argument for the _year_ formal parameter with _make_timestamp()_ and _make_timestamptz()_. This limitation persists through PostgreSQL Version 13. This seems to be a simple oversight—and PostgreSQL Version 14 removes the restriction. YSQL will suffer from this restriction until a version that's built using the PostgreSQL Version 14 SQL processing layer is released. Meanwhile, you can use this workaround:

```plpgsql
drop function if exists my_make_timestamp(int, int, int, int, int, double precision) cascade;

create function my_make_timestamp(
  year int, month int, mday int, hour int, min int, sec double precision)
  returns timestamp
  language plpgsql
as $body$
declare
  bc  constant boolean   not null := year < 0 ;
  t   constant timestamp not null := make_timestamp(abs(year), month, mday, hour, min, sec);
begin
  return case bc
           when true then (t::text||' BC')::timestamp
           else           t
         end;
end;
$body$;
```

Test it like this:

```plpgsql
select my_make_timestamp (year=>-10, month=>3, mday=>17, hour=>13, min=>42, sec=>19);
```

This is the result:

```output
 0010-03-17 13:42:19 BC
```

You can implement the workaround _my_make_timestamptz()_ using the same approach as for the function _my_make_timestamp()_.

## function to_timestamp() returns timestamptz

The _to_timestamp()_ built-in function has two overloads. Each returns a _timestamptz_ value. Here is the interesting part of the output from \\_df _to_timestamp()_:

```output
     Result data type     | Argument data types
--------------------------+---------------------
 timestamp with time zone | double precision
 timestamp with time zone | text, text
```

The _double precision_ overload interprets the input argument as the Unix epoch (i.e. the number of seconds since _'1970-01-01 00:00:00+00'::timestamptz_). See the section [Date and time formatting functions](../../formatting-functions/#from-to-text-date-time) for the _(text, text)_ overload.

Here is an example:

```plpgsql
set timezone = 'UTC';
with c as (
  -- 100 days after the start of the Unix epoch.
  select to_timestamp((60*60*24*1000)::double precision) as t)
select pg_typeof(t)::text as "type", t::text from c;
```

This is the result:

```output
           type           |           t
--------------------------+------------------------
 timestamp with time zone | 1972-09-27 00:00:00+00
```

## function make_interval() returns interval

The _make_interval()_ built-in function creates an _interval_ value from integral values for the number of years, months, weeks, days, hours, and minutes, and a real number for the number of seconds. Here is the interesting part of the output from \\_df make_interval()_. (Each of the formal parameters has a default of _zero_. But this part of the \\_df_ output is elided here to improve readability.)

```output
 Result data type |                                              Argument data types
------------------+----------------------------------------------------------------------------------------------------------------
 interval         | years integer, months integer, weeks integer, days integer, hours integer, mins integer, secs double precision
```

Here is an example:

```plpgsql
with c as (
  select make_interval(secs=>250000.123456) as i)
select pg_typeof(i)::text as "type", i::text from c;
```

This is the result:

```output
   type   |        i
----------+-----------------
 interval | 69:26:40.123456
```

You can use a negative actual argument for all of the formal parameters:

```plpgsql
with c as (
  select make_interval(
    years=>-1,
    months=>-1,
    weeks=>-1,
    days=>-1,
    hours=>-1,
    mins=>-1,
    secs=>-1.1) as i)
select
  extract(years   from i) as years,
  extract(months  from i) as months,
  extract(days    from i) as days,
  extract(hours   from i) as hours,
  extract(mins    from i) as mins,
  extract(seconds from i) as secs
from c;
```

This is the result:

```output
 years | months | days | hours | mins | secs
-------+--------+------+-------+------+------
    -1 |     -1 |   -8 |    -1 |   -1 | -1.1
```

Notice that while _make_interval()_ has a formal parameter called _weeks_, you cannot use the name _weeks_ to denote a field for _extract(... from interval_value)_. (The attempt causes the _22023_ error.)

{{< tip title="Don't use 'make_interval()' to create hybrid 'interval' values." >}}
This function tempts you to create hybrid _interval_ values (i.e. values where more than one field of the internal _[\[mm, dd, ss\]](../../date-time-data-types-semantics/type-interval/interval-representation/)_ representation is non-zero). Yugabyte recommends that you avoid creating such hybrid values and that, rather, you follow the approach described in the section [Custom domain types for specializing the native interval functionality](../../date-time-data-types-semantics/type-interval/custom-interval-domains/).
{{< /tip >}}
