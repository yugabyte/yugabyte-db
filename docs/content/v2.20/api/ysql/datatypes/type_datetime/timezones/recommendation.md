---
title: Recommended practice for specifying the UTC offset [YSQL]
headerTitle: Recommended practice for specifying the UTC offset
linkTitle: Recommended practice
description: Recommends a practice to for specifying the offset from the UTC Time Standard safely. [YSQL]
menu:
  v2.20:
    identifier: recommendation
    parent: timezones
    weight: 80
type: docs
---

{{< tip title="Write user-defined functions to wrap 'set timezone' and the overloads of the built-in function 'timezone()'." >}}
Yugabyte recommends that you create two user-defined function overload sets, thus:

- _set_timezone()_, with _(text)_ and _(interval)_ overloads to wrap _set timezone_ to ensure that safe, approved arguments are used.

- _at_timezone()_ with _(text, timestamp)_, _(interval, timestamp)_, _(text, timestamptz)_, _(interval, timestamptz)_ overloads to wrap the corresponding overloads of the _timezone()_ built-in function to ensure that safe, approved arguments are used.

"Safe, approved arguments" means:

- When a timezone is specified using its name, this is checked against a list of approved names—a subset of the rows in _pg_timezone_names.name_
- When a timezone is specified using an _interval_ value, this is checked to ensure that it lies in the range defined by the overall maximum and minimum values of _utc_offset_ columns in the _pg_timezone_names_ and _pg_timezone_abbrevs_ catalog views. It's also checked to ensure that it's an integral multiple of fifteen minutes, respecting the convention followed by every timezone shown by _pg_timezone_names_.

Following these recommendations protects you from the many opportunities to go wrong brought by using the native functionality with no constraints; and yet doing so still allows you all the functionality that you could need.
{{< /tip >}}</br>

{{< tip title="Download and install the date-time utilities code." >}}
The code on this page depends on the [_extended_timezone_names_ view](../extended-timezone-names/). It also depends on the [custom _interval_ domains](../../date-time-data-types-semantics/type-interval/custom-interval-domains/) code. And this, in turn, depends on the [user-defined _interval_ utilities](../../date-time-data-types-semantics/type-interval/interval-utilities/).

These components are all included in the larger [code kit](../../download-date-time-utilities/) that includes all of the reusable code that the overall _[date-time](../../../type_datetime/)_ section describes and uses.

The code on that this page defines is intended for reuse. It, too, is therefore included in the _date-time utilities_ downloadable code kit.
{{< /tip >}}

## The approved_timezone_names view

This is the union of the [Real timezones that observe Daylight Savings Time](../extended-timezone-names/canonical-real-country-with-dst/) view, the [Real timezones that don't observe Daylight Savings Time](../extended-timezone-names/canonical-real-country-no-dst/) view, and the single row that specifies the facts about the _UTC Time Standard_.

```plpgslq
drop view if exists approved_timezone_names cascade;

create view approved_timezone_names as
select
  name,
  std_abbrev,
  dst_abbrev,
  std_offset,
  dst_offset
from extended_timezone_names
where
  name = 'UTC'

  or

  (
    lower(status) = 'canonical'                                        and
    country_code is not null                                           and
    country_code <> ''                                                 and
    lat_long is not null                                               and
    lat_long <> ''                                                     and

    name not like '%0%'                                                and
    name not like '%1%'                                                and
    name not like '%2%'                                                and
    name not like '%3%'                                                and
    name not like '%4%'                                                and
    name not like '%5%'                                                and
    name not like '%6%'                                                and
    name not like '%7%'                                                and
    name not like '%8%'                                                and
    name not like '%9%'                                                and

    lower(name) not in (select lower(abbrev) from pg_timezone_names)   and
    lower(name) not in (select lower(abbrev) from pg_timezone_abbrevs)
  );
```

## Common procedures to assert the approval of a timezone name and an interval value

These two _"assert"_ procedures are used by both the _[set_timezone()](#the-set-timezone-procedure-overloads)_ and the _[at_timezone()](#the-at-timezone-function-overloads)_ user-defined function overloads. They depend upon some code described in the section [Custom domain types for specializing the native _interval_ functionality](../../date-time-data-types-semantics/type-interval/custom-interval-domains/). And these, in turn, depend on some code described in the [User-defined _interval_ utility functions](../../date-time-data-types-semantics/type-interval/interval-utilities/) section.

### assert_approved_timezone_name()

```plpgsql
drop procedure if exists assert_approved_timezone_name(text) cascade;

create procedure assert_approved_timezone_name(tz in text)
  language plpgsql
as $body$
declare
  bad constant boolean not null :=
    (select count(*) from approved_timezone_names where lower(name) = lower(tz)) <> 1;
begin
  if bad then
    declare
      code  constant text not null := '22023';
      msg   constant text not null := 'Invalid value for parameter TimeZone "'||tz||'"';
      hint  constant text not null := 'Use a name that''s found exactly once in "approved_timezone_names"';
    begin
      raise exception using
        errcode = code,
        message = msg,
        hint    = hint;
    end;
  end if;
end;
$body$;
```

### assert_acceptable_timezone_interval()

```plpgsql
drop procedure if exists assert_acceptable_timezone_interval(interval) cascade;

create procedure assert_acceptable_timezone_interval(i in interval)
  language plpgsql
as $body$
declare
  min_utc_offset constant interval not null := (
    select least(
        (select min(utc_offset) from pg_timezone_names),
        (select min(utc_offset) from pg_timezone_abbrevs)
      )
    );

  max_utc_offset constant interval not null := (
    select greatest(
        (select max(utc_offset) from pg_timezone_names),
        (select max(utc_offset) from pg_timezone_abbrevs)
      )
    );

  -- Check that the values are "pure seconds" intervals.
  min_i constant interval_seconds_t not null := min_utc_offset;
  max_i constant interval_seconds_t not null := max_utc_offset;

  -- The interval value must not have a seconds component.
  bad constant boolean not null :=
    not(
        (i between min_i and max_i) and
        (extract(seconds from i) = 0.0)
      );
begin
  if bad then
    declare
      code  constant text not null := '22023';
      msg   constant text not null := 'Invalid value for interval: "'||i::text||'"';
      hint  constant text not null := 'Use a value between "'||min_i||'" and "'||max_i||'" with seconds cpt = zero';
    begin
      raise exception using
        errcode = code,
        message = msg,
        hint    = hint;
    end;
  end if;
end;
$body$;
```

Should you be concerned about the performance of this check, you can rely on the fact that the limits for acceptable _interval_ values that it discovers on every invocation can simply be declared as _constants_ in the functions source code. The safest way to do this is to write a generator procedure to create [or replace] the _assert_acceptable_timezone_interval()_ procedure and to document the practice that requires that this generator be run whenever the YugabyteDB version (or the PostgreSQL version) is created or changed. (Your practice rule would need to be stated more carefully if you allow changes to the configuration files that determine the contents that the _pg_timezone_names_ view and the _pg_timezone_abbrevs_ view expose.)

## The set_timezone() procedure overloads

```plpgsql
drop procedure if exists set_timezone(text) cascade;

create procedure set_timezone(tz in text)
  language plpgsql
as $body$
begin
  call assert_approved_timezone_name(tz);
  declare
    stmt constant text not null := 'set timezone = '''||tz||'''';
  begin
    execute stmt;
  end;
end;
$body$;


drop procedure if exists set_timezone(interval) cascade;

create procedure set_timezone(i in interval)
  language plpgsql
as $body$
begin
  call assert_acceptable_timezone_interval(i);
  declare
    stmt constant text not null := 'set time zone interval '''||i::text||'''';
  begin
    execute stmt;
  end;
end;
$body$;
```

## The at_timezone() function overloads

Do this to see the overloads of interest of the _timezone()_ built-in function.

```plpgsql
\df timezone()
```

This is the result:

```output
      Result data type       |          Argument data types
-----------------------------+--------------------------------------
 ...
 timestamp without time zone | interval, timestamp with time zone
 timestamp with time zone    | interval, timestamp without time zone
 ...
 timestamp without time zone | text, timestamp with time zone
 timestamp with time zone    | text, timestamp without time zone
```

(The output also lists overloads for the _timetz_ data type. This has been elided because, following the [PostgreSQL documentation](https://www.postgresql.org/docs/11/datatype-datetime.html#DATATYPE-DATETIME-TABLE), Yugabyte recommends that you don't use this data type.)

Create wrapper functions for the four listed built-in functions:

```plpgsql
-- plain timestamp in, timestamptz out.
drop function if exists at_timezone(text, timestamp) cascade;

create function at_timezone(tz in text, t in timestamp)
  returns timestamptz
  language plpgsql
as $body$
begin
  call assert_approved_timezone_name(tz);
  return timezone(tz, t);
end;
$body$;

-- This overload is almost textually identical to the preceding one.
-- The data types of the second formal and the return have
-- simply been exchanged.
-- timestamptz in, plain timestamp out.
drop function if exists at_timezone(text, timestamptz) cascade;

create function at_timezone(tz in text, t in timestamptz)
  returns timestamp
  language plpgsql
as $body$
begin
  call assert_approved_timezone_name(tz);
  return timezone(tz, t);
end;
$body$;

-- interval in, timestamptz out.
drop function if exists at_timezone(interval, timestamp) cascade;

create function at_timezone(i in interval, t in timestamp)
  returns timestamptz
  language plpgsql
as $body$
begin
  call assert_acceptable_timezone_interval(i);
  return timezone(i, t);
end;
$body$;

-- This overload is almost textually identical to the preceding one.
-- The data types of the second formal and the return have
-- simply been exchanged.
-- interval in, plain timestamp out.
drop function if exists at_timezone(interval, timestamptz) cascade;

create function at_timezone(i in interval, t in timestamptz)
  returns timestamp
  language plpgsql
as $body$
begin
  call assert_acceptable_timezone_interval(i);
  return timezone(i, t);
end;
$body$;
```

## Test the set_timezone() and the at_timezone() overloads

The following tests contain some commented out _raise info_ statements. They show the error messages that you get when you supply a timezone name that isn't approved or, or an _interval_ value that isn't acceptable.

### Test the set_timezone(text) overload

Do this:

```plpgsql
do $body$
declare
  tz_in  text not null := '';
  tz_out text not null := '';

  good_zones constant text[] := array[
    'UTC',
    'Asia/Kathmandu',
    'Europe/Amsterdam'];
begin
  foreach tz_in in array good_zones loop
    call set_timezone(tz_in);
    show timezone into tz_out;
    declare
      msg constant text not null := tz_in||' assert failed';
    begin
      assert tz_out = tz_in, msg;
    end;
  end loop;

  begin
    call set_timezone('Bad');
    assert false, 'Logic error';
  exception when invalid_parameter_value then
    declare
      msg  text not null := '';
      hint text not null := '';
    begin
      get stacked diagnostics
        msg     = message_text,
        hint    = pg_exception_hint;

      /*
      raise info '%', msg;
      raise info '%', hint;
      */
    end;
  end;
end;
$body$;
```

The block finishes silently, showing that all of the assertions hold. Uncomment the _raise info_ statements and repeat the test. You'll see this information:

```output
INFO:  Invalid value for parameter TimeZone "Bad"
INFO:  Use a name that's found exactly once in "approved_timezone_names"
```

### Test the set_timezone(interval) overload

Do this:

```plpgsql
do $body$
declare
  tz_out text not null := '';
begin
  call set_timezone(make_interval(hours=>-7));
  show timezone into tz_out;
  assert tz_out= '<-07>+07', 'Assert <-07>+07 failed';

  call set_timezone(make_interval(hours=>-5, mins=>45));
  show timezone into tz_out;
  assert tz_out= '<-04:15>+04:15', 'Assert <-04:15>+04:15 failed';

  begin
    call set_timezone(make_interval(hours=>19));
    assert false, 'Logic error';
  exception when invalid_parameter_value then
    declare
      msg  text not null := '';
      hint text not null := '';
    begin
      get stacked diagnostics
        msg     = message_text,
        hint    = pg_exception_hint;

      /*
      raise info '%', msg;
      raise info '%', hint;
      */
    end;
  end;
end;
$body$;
```

The block finishes silently, showing that all of the assertions hold. Uncomment the _raise info_ statements and repeat the test. You'll see this information:

```output
INFO:  Invalid value for interval "19:00:00"
INFO:  Use a value between "-12:00:00" and "14:00:00"
```

### Test the at_timezone(text, timestamp) overload

Do this:

```plpgsql
do $body$
declare
  t_text       constant text        not null := '2021-05-31 12:00:00';
  t_plain      constant timestamp   not null := t_text;
  tz_result             timestamptz not null := t_text||' UTC'; -- Satisfy the constraints.
  tz_expected           timestamptz not null := t_text||' UTC'; -- The values will be overwritten
  tz                    text        not null := '';

  good_zones constant text[] := array[
    'UTC',
    'Asia/Kathmandu',
    'Europe/Amsterdam'];
begin
  foreach tz in array good_zones loop
    tz_result   := at_timezone(tz, t_plain);
    tz_expected := t_text||' '||tz;

    declare
      msg constant text not null := tz||' assert failed';
    begin
      assert tz_result = tz_expected, msg;
    end;
  end loop;
end;
$body$;
```

The block finishes silently, showing that all of the assertions hold. There's no value in including a bad-value negative test because doing so would simply be a repeat, and therefore redundant, test of the [assert_approved_timezone_name()](#assert-approved-timezone-name) procedure.

### Test the at_timezone(interval, timestamp) overload

Do this:

```plpgsql
do $body$
declare
  t_text       constant text        not null := '2021-05-31 12:00:00';
  t_plain      constant timestamp   not null := t_text;
  tz_result             timestamptz not null := t_text||' UTC'; -- Satisfy the constraints.
  tz_expected           timestamptz not null := t_text||' UTC'; -- The values will be overwritten
  i                     interval    not null := make_interval();
  hh                    text        not null := 0;
  mm                    text        not null := 0;

  i_vals       constant interval[]  not null := array[
                                                    make_interval(),
                                                    make_interval(hours=>4, mins=>30),
                                                    make_interval(hours=>-7)
                                                  ];
begin
  foreach i in array i_vals loop
    hh := ltrim(to_char(extract(hour   from i), 'SG09')); -- Essential to prefix with the sign.
    mm := ltrim(to_char(extract(minute from i), '09'));
    tz_result   := at_timezone(i, t_plain);
    tz_expected := t_text||' '||hh||':'||mm;
    declare
      msg constant text not null := i::text||' assert failed';
    begin
      assert tz_result = tz_expected, msg;
    end;
  end loop;
end;
$body$;
```

The block finishes silently, showing that all of the assertions hold. There's no value in including a bad-value negative test because doing so would simply be a repeat, and therefore redundant, test of the [assert_acceptable_timezone_interval()](#assert-acceptable-timezone-interval) procedure.
