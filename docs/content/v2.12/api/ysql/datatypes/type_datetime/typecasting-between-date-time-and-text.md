---
title: Typecasting between date-time values and text values [YSQL]
headerTitle: Typecasting between date-time values and text values
linkTitle: Typecasting between date-time and text-values
description: Describes how to typecast date-time values to text values, and vice versa. [YSQL]
menu:
  v2.12:
    identifier: typecasting-between-date-time-and-text
    parent: api-ysql-datatypes-datetime
    weight: 50
type: docs
---

This section and its peer, [Timezones and _UTC offsets_](../timezones/), are placed, with respect to the sequential reading order of the overall _date-time_ time data types section that the [table of contents](../../type_datetime/toc/) presents, before the main treatment of the [semantics of the _date-time_ data types](../date-time-data-types-semantics/) because the code examples in those subsequent sections rely on typecasting between _date-time_ values and _text_ values and on setting the timezone, either as a session parameter or as part of a _date-time_ expression with the _at time zone_ operator.

## Introduction

Typecasting between _date-time_ values and _text_ values, rather than using explicit built-in functions like _to_char()_, _to_timestamp()_, or _to_date()_ allows the demonstration code to be uncluttered and easy to understand. However, as this section shows, the typecast semantics is sensitive to the current settings of the _DateStyle_ and _IntervalStyle_ session parameters.

{{< note title="'Date-time' functions and operators in the PostgreSQL documentation." >}}
PostgreSQL, and therefore YSQL, provide many functions and equivalent syntactical constructs that operate on, or produce, _date-time_ values. These are documented in these dedicated sections within the main section [Functions and operators](../../../exprs/) and its children:

- [Date and time operators](../operators/).
- [General-purpose date and time functions](../functions/).
- [Date and time formatting functions](../formatting-functions/).

The following _to_char_demo()_ code example uses the _to_timestamp()_ function to produce a _timestamptz_ value from a _double precision_ value. The input represents the real number of seconds after, or before, the start of the Unix Epoch (a.k.a. the POSIX Epoch). See the Wikipedia article <a href="https://en.wikipedia.org/wiki/Unix_time" target="_blank">Unix time <i class="fa-solid fa-up-right-from-square"></i></a>. The Unix Epoch begins at midnight on 1-January-1970 _UTC_. Try this:

```plpgsql
set datestyle = 'ISO, DMY';
set timezone = 'UTC';
with a as (select to_timestamp(0::double precision) as v)
select
  pg_typeof(v) as "data type",
  v            as "value"
from a;
```

See the Wikipedia article [ISO 8601](https://en.wikipedia.org/wiki/ISO_8601). The [next section](#the-datestyle-session-parameter) explains the significance of the _DateStyle_ session parameter. And the section [Timezones and _UTC offsets_](../timezones/) explains the significance of the _TimeZone_ session parameter.

This is the result:

```output
        data type         |         value
--------------------------+------------------------
 timestamp with time zone | 1970-01-01 00:00:00+00
```

The _to_char_demo()_ function casts the _to_timestamp()_ result to a plain _timestamp_ value that represents what a wall-clock located on the Greenwich Meridian would read. The immediately following code example casts it to what a wall-clock in Paris would read. Try this:

```plpgsql
set datestyle = 'ISO, DMY';
deallocate all;
prepare stmt(timestamptz, text) as
with a as (select $1 at time zone $2 as v)
select
  pg_typeof(v) as "data type",
  v            as "value"
from a;
execute stmt(to_timestamp(0::double precision), 'Europe/Paris');
```

This is the result:

```output
          data type          |        value
-----------------------------+---------------------
 timestamp without time zone | 1970-01-01 01:00:00
```

The _at time zone_ clause has _function syntax_ equivalent:

```output
timezone(timestamptz_value=>$1, timezone=>$2)
```

Create and execute the _to_char_demo()_ function like this:

```plpgsql
drop function if exists to_char_demo() cascade;

create function to_char_demo()
  returns table(z text)
  language plpgsql
as $body$
declare
  -- Counted from midnight 1-Jan-1970 UTC.
  secs   constant double precision not null := 94996411200.456789;
  t      constant timestamp        not null := to_timestamp(-secs) at time zone 'UTC';
  fmt_1  constant text             not null := 'TMDay / TMMonth';
  fmt_2  constant text             not null := 'TMDy dd-TMMon-yyyy hh24:mi:ss.us BC';
begin
  set lc_time = 'en_US';
  z := to_char(t, fmt_1);           return next;
  z := to_char(t, fmt_2);           return next;
  z := '';                          return next;

  set lc_time = 'it_IT';
  z := to_char(t, fmt_1);           return next;
  z := to_char(t, fmt_2);           return next;
  z := '';                          return next;

  set lc_time = 'fi_FI';
  z := to_char(t, fmt_1);           return next;
  z := to_char(t, fmt_2);           return next;
  z := '';                          return next;
end;
$body$;

select z from to_char_demo();
```

Because this uses the _to_char()_ function, and not typecasting, the result is not sensitive to the _DateStyle_ setting. PostgreSQL documents the various components, like _'TMDay'_, _'TMMonth'_, _'yyyy'_, _dd_, and so on that define the format that _to_char()_ produces in <a href="https://www.postgresql.org/docs/11/functions-formatting.html#FUNCTIONS-FORMATTING-DATETIME-TABLE" target="_blank">Table 9.24. Template Patterns for Date/Time Formatting <i class="fa-solid fa-up-right-from-square"></i></a>.

And because _to_char_demo()_ uses the _at time zone_ operator, it is not sensitive to the current _TimeZone_ setting. This is the result:

```output
 Friday / September
 Fri 07-Sep-1042 11:59:59.543216 BC

 Venerdì / Settembre
 Ven 07-Set-1042 11:59:59.543216 BC

 Perjantai / Syyskuu
 Pe 07-Syy-1042 11:59:59.543216 BC
```

As you see, the _lc_time_ session parameter determines the national language that is used for the spellings of the short and long day and month names. The PostgreSQL documentation describes this parameter in the section <a href="https://www.postgresql.org/docs/11/locale.html" target="_blank">23.1. Locale Support <i class="fa-solid fa-up-right-from-square"></i></a> Notice that this section, in turn, references the section <a href="https://www.postgresql.org/docs/11/runtime-config-client.html#RUNTIME-CONFIG-CLIENT-FORMAT" target="_blank">23.1. 19.11.2. Locale and Formatting <i class="fa-solid fa-up-right-from-square"></i></a>.

In short, a setting like _'fi_FI'_ is operating-system-dependent and may, or may not, be available according to what local support files have been installed. You can see what's available on a Unix-like system with this shell command:

```output
locale -a
```

The _'TM'_ prefix, used in the function _to_char_demo()_ above, is documented as "print localized day and month names based on lc_time" and so it works only in the _to_char()_ output direction and not in the _to_timestamp()_ input direction. This example makes the point without it:

```plpgsql
-- Setting to something other than 'ISO, DMY', here, just to hint at the effect.
set datestyle = 'German, DMY';

select to_timestamp(
  '07-09-1042 11:59:59.543216 BC',
  'dd-mm-yyyy hh24:mi:ss.us BC') at time zone 'UTC';
```

This is the result:

```output
 07.09.1042 11:59:59.543216 BC
```
{{< /note >}}

## Two syntaxes for typecasting

**Approach One:** You can write the name of the target data type _after_ the to-be-typecast value using the notation exemplified by _::timestamptz_. Try these examples:

```postgresql
drop table if exists t cascade;
create table t(
  c1  text        primary key,
  c2  timestamptz not null,
  c3  timestamp   not null,
  c4  date        not null,
  c5  time        not null,
  c6  interval    not null);

insert into t(c1, c2, c3, c4, c5, c6) values (
  to_timestamp(1577200000)  ::text,
  '2019-12-24 16:42:47 UTC' ::timestamptz,
  '2019-12-24 16:42:47'     ::timestamp,
  '2019-12-24'              ::date,
  '16:42:47'                ::time,
  '2 years 1 month'         ::interval);
```

The test silently succeeds.

**Approach Two:** You can write the bare name of the target data type _before_ the to-be-typecast value. Try these examples:

```plpgsql
insert into t(c1, c2, c3, c4, c5, c6) values (
  text        (to_timestamp(1577300000)),
  timestamptz '2019-12-24 16:42:47 UTC',
  timestamp   '2019-12-24 16:42:47',
  date        '2019-12-24',
  time        '16:42:47',
  interval    '2 years 1 month');
```

Again, the test silently succeeds. Notice that the parentheses are necessary for the _text_ example. Try this:

```plpgsql
select text to_timestamp(1577200000);
```

It causes this error:

```output
42601: syntax error at or near "("
```

Approach One is used consistently throughout the whole of the [Date and time data types](../../type_datetime/) section.

## The DateStyle session parameter

See the PostgreSQL documentation section [19.11.2. Locale and Formatting](https://www.postgresql.org/docs/11/runtime-config-client.html#RUNTIME-CONFIG-CLIENT-FORMAT). The _DateStyle_ session parameter determines the format of the _::text_ typecast of a _date-time_ value. It also, but in a subtle fashion, determines how a _text_ value is interpreted when it's typecast to a _date-time_ value. It has two orthogonal components: the _style_ and the _substyle_. The _style_ has these legal values:

```output
ISO
SQL
PostgreSQL
German
```

And the _substyle_ has these legal values:

```output
DMY (with the synonyms Euro and European)
MDY (with the synonyms NonEuro, NonEuropean, and US)
YMD
```

The components can be set together, like this:

```plpgsql
set datestyle = 'PostgreSQL, YMD';
show datestyle;
```

This is the result:

```output
 Postgres, YMD
```

Or they can be set  separately like this:

```plpgsql
set datestyle = 'German';
set datestyle = 'DMY';
show datestyle;
```

This is the result:

```output
 German, DMY
```

Create the _DateStyle_ demo like this:

```plpgsql
drop table if exists results;
create table results(
  datestyle       text primary key,
  tstamp_as_text  text not null,
  tstamp          timestamp);

drop procedure if exists datestyle_demo() cascade;
create procedure datestyle_demo()
  language plpgsql
as $body$
declare
  -- Counted from midnight 1-Jan-1970 UTC.
  secs           constant double precision not null := 94996411200.456789;
  t              constant timestamp        not null := to_timestamp(-secs) at time zone 'UTC';
  set_datestyle  constant text             not null := $$set datestyle = '%s, %s'$$;
  d                       text             not null := '';
  s                       text             not null := '';
  d_shown                 text             not null := '';
  styles         constant text[]           not null := array['ISO', 'SQL', 'PostgreSQL', 'German'];
  substyles      constant text[]           not null := array['DMY', 'MDY', 'YMD'];
begin
  foreach d in array styles loop
    foreach s in array substyles loop
      execute format(set_datestyle, d, s);
      show datestyle into d_shown;
      insert into results(datestyle, tstamp_as_text) values (d_shown, t::text);
    end loop;
  end loop;
end;
$body$;

call datestyle_demo();

-- Set the same datestyle for the ::timestamp typecast of all the different text representations.
set datestyle = 'ISO, DMY';
update results set tstamp = tstamp_as_text::timestamp;

select datestyle, tstamp_as_text, tstamp::text
from results
order by datestyle;
```

This is the result:

```output
 German, DMY   | 07.09.1042 11:59:59.543216 BC      | 1042-09-07 11:59:59.543216 BC
 German, MDY   | 07.09.1042 11:59:59.543216 BC      | 1042-09-07 11:59:59.543216 BC
 German, YMD   | 07.09.1042 11:59:59.543216 BC      | 1042-09-07 11:59:59.543216 BC

 ISO, DMY      | 1042-09-07 11:59:59.543216 BC      | 1042-09-07 11:59:59.543216 BC
 ISO, MDY      | 1042-09-07 11:59:59.543216 BC      | 1042-09-07 11:59:59.543216 BC
 ISO, YMD      | 1042-09-07 11:59:59.543216 BC      | 1042-09-07 11:59:59.543216 BC

 Postgres, DMY | Fri 07 Sep 11:59:59.543216 1042 BC | 1042-09-07 11:59:59.543216 BC
 Postgres, MDY | Fri Sep 07 11:59:59.543216 1042 BC | 1042-09-07 11:59:59.543216 BC
 Postgres, YMD | Fri Sep 07 11:59:59.543216 1042 BC | 1042-09-07 11:59:59.543216 BC

 SQL, DMY      | 07/09/1042 11:59:59.543216 BC      | 1042-09-07 11:59:59.543216 BC
 SQL, MDY      | 09/07/1042 11:59:59.543216 BC      | 1042-07-09 11:59:59.543216 BC
 SQL, YMD      | 09/07/1042 11:59:59.543216 BC      | 1042-07-09 11:59:59.543216 BC
```

The blank lines were added manually to improve the readability. Notice the following:

- For each of the four different _style_ values, the _'YMD'_ _substyle_ has the identical effect as does the _'MDY'_ _substyle_. It is therefore pointless, and directly confusing, to use the _'YMD'_ _substyle_. Yugabyte recommends that you simply avoid doing this.
- For the two _style_ values _'ISO'_ and _German'_, the _'MDY'_ _substyle_ has the identical effect as does the _'DMY'_ _substyle_ in both the _::text_ direction and the _::timestamp_ direction. Yugabyte recommends that you always use the _'DMY'_ _substyle_ in this scenario because this corresponds to the order that is actually produced.
- For the two _style_ values _'Postgres'_ and _SQL'_, the _'MDY'_ _substyle_ has the effect that the mnemonic suggests in the _::text_ direction.  However, for the _Postgres_ _style_, it has _no effect_ in the _::timestamp_ direction. This is the only feasible behavior because the _Postgres_ _style_ renders the day numerically and the month alphabetically—so it's impossible to take _'Sep'_ as a day, even though _'07'_ can be taken as a month. In contrast, because the _'SQL'_ _style_ renders  both the day and the month numerically, it's impossible to interpret _'07/09/1042'_ and _'09/07/1042'_ reliably unless _'DMY'_ or _'MDY_' specify the rule.

Try this:

```plpgsql
-- Test One.
set datestyle = 'SQL, MDY';
select 'The American exceptionalism way: '||to_char('07/09/2000 12:00:00'::timestamp, 'Mon-dd-yyyy');
```

This is the result:

```output
 The American exceptionalism way: Jul-09-2000
```

Now try it the other way round like this:

```plpgsql
-- Test Two.
set datestyle = 'SQL, DMY';
select 'The sensible way: '||to_char('07/09/2000 12:00:00'::timestamp, 'dd-Mon-yyyy');
```

The result is now this:

```output
 The sensible way: 07-Sep-2000
```

The operand of the _::timestamp_ typecast is spelled the same in _Test One_ as it is in _Test Two_. But the resulting dates are different—in the famously confusing way.

Even when a nominal month has an illegal number like _'19'_, and this could be used for automatic disambiguation, the _'DMY'_ or '_MDY_' _substyle_ is taken as a non-negotiable directive. Try this:

```postgresql
-- Test Three.
set datestyle = 'SQL, MDY';
select to_char('19/09/2000 12:00:00'::timestamp, 'dd-mm-yyyy');

-- Test Four.
set datestyle = 'SQL, DMY';
select to_char('07/19/2000 12:00:00'::timestamp, 'dd-mm-yyyy');
```

Each of _Test Three_ and _Test Four_ produces the same error:

```output
22008: date/time field value out of range ...
```

{{< tip title="Never rely on typecasting between 'text' and 'date-time' values unless you set 'DateStyle' explicitly." >}}
Yugabyte recommends that application code should convert between _text_ values and _date-time_ values using explicit conversion functions that use a format specification. You might be reading from a file that client-side code ingests that simply comes with a non-negotiable pre-determined format. Or you might be processing human input that comes from a UI that allows the user to choose the _date-time_ format from a list.

- To produce a _timestamptz_ value, use _to_timestamp(text, text)_.
- To produce a plain _timestamp_ value, use _to_timestamp(text, text)_ with _at time zone 'UTC'_.
- To produce a _date_ value, use _to_date(text, text)_.
- To produce a plain _time_ value, do what this code example models:

```plpgsql
  drop table if exists t cascade;
  create table t(k int primary key, t1 time not null, t2 time not null);
  insert into t(k, t1, t2) values(1, '00:00:00'::time, '00:00:00'::time);

  deallocate all;
  prepare s_1(text) as
  update t set t1 = to_timestamp($1, 'hh24:mi:ss')::time
  where k = 1;

  prepare s_2(text) as
  update t set t2 = to_timestamp($1, 'hh24:mi:ss')::time
  where k = 1;

  set timezone = 'UTC';
  execute s_1('13:00:56');

  set timezone = 'America/Los_Angeles';
  execute s_2('13:00:56');

  select (t1 = t2)::text from t where k = 1;
```

- The result is _true_, showing that the method is insensitive to the current _TimeZone_ setting.

- To convert a _date-time_ value to a _text_ value, use the appropriate _to_char()_ overload as has been illustrated above.

Of course, it's safe to use the typecasting approach in _ad hoc_ tests where you can set _DateStyle_ to whatever you want to without worrying that it might affect the behavior of existing application code that doesn't set the parameter explicitly. The same applies to small stand-alone code examples that support documentation.

The YSQL documentation assumes that the _DateStyle_ _style_ component is set to _'ISO'_ unless it's explicitly set otherwise. (The _substyle_ setting has no effect with the _'ISO'_ _style_.)
{{< /tip >}}

## The IntervalStyle session parameter

The _IntervalStyle_ session parameter controls the format of the result of the _::text_ typecast operator on an _interval_ value. It has no effect on the outcome of the _::interval_ typecast operator on a _text_ value. There are just four legal choices. It's easy to see the list by making a deliberate error:

```postgresql
set intervalstyle = 'oops, I did a typo';
```

This is the result:

```output
ERROR:  22023: invalid value for parameter "intervalstyle": "oops, I did a typo"
HINT:  Available values: postgres, postgres_verbose, sql_standard, iso_8601.
```

The _IntervalStyle_ demo is a straight copy, paste, and massage derivative of the _DateStyle_ demo. Create it like this:

```postgresql
drop table if exists results;
create table results(
  intervalstyle  text primary key,
  i_as_text      text not null,
  i              interval);


drop procedure if exists intervalstyle_demo() cascade;
create procedure intervalstyle_demo()
  language plpgsql
as $body$
declare
  i                  constant interval not null := make_interval(
                                                     years  => 1,
                                                     months => 2,
                                                     days   => 3,
                                                     hours  => 4,
                                                     mins   => 5,
                                                     secs   => 6.345678);

  set_intervalstyle  constant text     not null := $$set intervalstyle = '%s'$$;
  s                           text     not null := '';
  s_shown                     text     not null := '';
  styles             constant text[]   not null := array['postgres', 'postgres_verbose', 'sql_standard', 'iso_8601'];
begin
  foreach s in array styles loop
    execute format(set_intervalstyle, s);
    show intervalstyle into s_shown;
    insert into results(intervalstyle, i_as_text) values (s_shown, i::text);
  end loop;
end;
$body$;

call intervalstyle_demo();

-- Set the same intervalstyle for the ::interval typecast of all the different text representations.
set intervalstyle = 'postgres';
update results set i = i_as_text::interval;

select intervalstyle, i_as_text, i::text
from results
order by intervalstyle;
```

This is the result:

```output
  intervalstyle   |                      i_as_text                      |                  i
------------------+-----------------------------------------------------+--------------------------------------
 iso_8601         | P1Y2M3DT4H5M6.345678S                               | 1 year 2 mons 3 days 04:05:06.345678
 postgres         | 1 year 2 mons 3 days 04:05:06.345678                | 1 year 2 mons 3 days 04:05:06.345678
 postgres_verbose | @ 1 year 2 mons 3 days 4 hours 5 mins 6.345678 secs | 1 year 2 mons 3 days 04:05:06.345678
 sql_standard     | +1-2 +3 +4:05:06.345678                             | 1 year 2 mons 3 days 04:05:06.345678
```

The results are consistent with the fact that the _IntervalStyle_ setting has no effect on the outcome of the _::interval_ typecast operator on a _text_ value. This is because the syntax rules for each of the four different _IntervalStyle_ settings allow automatic disambiguation.

{{< tip title="Never rely on typecasting from 'interval' values to 'text' values unless you set 'IntervalStyle' explicitly." >}}
Yugabyte recommends that application code should convert between _text_ values and _interval_ values using explicit conversion functions.

The _make_interval()_ built-in function creates an _interval_ value using explicitly specified values in the units that you prefer—like _years_, _days_, _hours_, or _weeks_. Yugabyte recommends always using this approach and never using the _::interval_ typecast. This advice rests on ideas developed in the section [Interval arithmetic](../date-time-data-types-semantics/type-interval/interval-arithmetic/). The recommended approach is formalized in the section [Custom domain types for specializing the native _interval_ functionality](../date-time-data-types-semantics/type-interval/custom-interval-domains/).

The _extract_ SQL functionality lets you assign values like the _years_ or _days_ components of an _interval_ value to dedicated destinations. This approach is used in the definition of [function interval_mm_dd_ss (interval) returns interval_mm_dd_ss_t](../date-time-data-types-semantics/type-interval/interval-utilities/#function-interval-mm-dd-ss-interval-returns-interval-mm-dd-ss-t), described in the [User-defined _interval_ utility functions](../date-time-data-types-semantics/type-interval/interval-utilities/) section.

Of course, it's safe to use the typecasting approach in _ad hoc_ tests where you can set _IntervalStyle_ to whatever you want to without worrying that it might affect the behavior of existing application code that doesn't set the parameter explicitly. The same applies to small stand-alone code examples that support documentation.

The YSQL documentation assumes that _IntervalStyle_ is set to _'postgres'_ unless it's explicitly set otherwise.
{{< /tip >}}
