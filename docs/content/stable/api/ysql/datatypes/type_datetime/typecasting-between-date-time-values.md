---
title: Typecasting between values of different date-time datatypes [YSQL]
headerTitle: Typecasting between values of different date-time datatypes
linkTitle: Typecasting between date-time datatypes
description: Describes how to typecast date-time values of different date-time datatypes. [YSQL]
menu:
  stable:
    identifier: typecasting-between-date-time-values
    parent: api-ysql-datatypes-datetime
    weight: 70
type: docs
---

See the [table](../../type_datetime/#synopsis) at the start of the overall "Date and time data types" section. It lists six data types, but quotes the [PostgreSQL documentation](https://www.postgresql.org/docs/11/datatype-datetime.html#DATATYPE-DATETIME-TABLE) that recommends that you avoid using the _timetz_ datatype. This leaves five _date-time_ data types that are recommended for use. Each of the two axes of the [Summary table](#summary-table) below lists these five data types  along with the _text_ data type—so there are _thirty-six_ cells.

The cells on the diagonal represent the typecast from _some_type_ to the same type—and so they are tautologically uninteresting. This leaves _thirty_ cells and therefore _thirty_ rules to understand. See the section [Code to fill out the thirty interesting table cells](#code-to-fill-out-the-thirty-interesting-table-cells). This shows that _ten_ of the remaining typecasts are simply unsupported. The attempts cause the _42846_ error. This maps in PL/pgSQL code to the _cannot_coerce_ exception. You get one of these messages:

```output
cannot cast type date to time without time zone
cannot cast type date to interval
cannot cast type time without time zone to date
cannot cast type time without time zone to timestamp without time zone
cannot cast type time without time zone to timestamp with time zone
cannot cast type timestamp without time zone to interval
cannot cast type timestamp with time zone to interval
cannot cast type interval to date
cannot cast type interval to timestamp without time zone
cannot cast type interval to timestamp with time zone
```

Each of the messages simply spells out the logical conundrum. For example, how could you possibly derive a _date_ value from a _time_ value in a way that was useful?

There remain therefore only _twenty_ typecasts whose semantics you need to understand. These are indicated by the twenty links in the table to the subsections that explain the rules.

- _Thirteen_ of the typecasts do not involve _timestamptz_—neither as the source or the target datatype. The rules for these are insensitive to the reigning _UTC offset_.
- There remain, therefore, _seven_ typecasts that do involve _timestamptz_ as either the source or the target datatype. These are shown, of course, by the non-empty cells in the row and the column labeled with that data type. Here, the rules _are_ sensitive to the reigning _UTC offset_.
  - The _two_ typecasts, plain _timestamp_ to _timestamptz_ and _timestamptz_ to plain _timestamp_, are critical to defining the rules of the sensitivity to the reigning _UTC offset_.
  - The _UTC offset_-sensitivity rules for the remaining _five_ typecasts can each be understood in terms of the rules for the mutual plain _timestamp_ to/from _timestamptz_ typecasts.

This is the way to see the final point:

To _timestamptz_:

```output
data_type_x_value::timestamptz == (data_type_x_value::timestamp)::timestamptz
```

From _timestamptz_:

```output
timestamptz_value::data_type_x == (timestamptz_value::timestamp)::data_type_x
```

This rests on these critical facts:

- If _data_type_x_ to _timestamptz_ is supported, then _data_type_x_ to _timestamp_ is supported too.

- If _timestamptz_ to _data_type_x_ is supported, then  _timestamp_ to _data_type_x_ is supported too.

You can see the correctness of these critical facts from the summary table. The _five_ typecasts in question are found: _three_ in vertically adjacent cells in the plain _timestamp_ and _timestamptz_ rows; and _two_ in horizontally adjacent cells in the plain _timestamp_ and _timestamptz_ columns.

The account on this page demonstrates that the decomposition rules for _data_type_x_ to _timestamptz_ and _timestamptz_ to _data_type_x_ hold; it demonstrates the outcomes for the typecasts between plain _timestamp_ values and _timestamptz_ values; and it shows that they are the same as when you use the _at time zone_ operator.

{{< tip title="The semantics for the mutual conversions between 'plain timestamp' and 'timestamptz' is defined elsewhere." >}}
The outcomes depend mandatorily on specifying the value of the _UTC offset_. There's more than one way to specify this. See the section [Four ways to specify the _UTC offset_](../timezones/ways-to-spec-offset/). The actual conversion semantics is explained in the section [Sensitivity of converting between _timestamptz_ and plain _timestamp_ to the _UTC offset_](../timezones/timezone-sensitive-operations/timestamptz-plain-timestamp-conversion/).
{{< /tip >}}

The outcomes of the typecasts between _date-time_ values and _text_ values, corresponding to all the cells in the bottom row and the right-most column, depend on the current setting of the _DateStyle_ or _IntervalStyle_ session parameters. This is explained in the section [Typecasting between date-time values and text values](../typecasting-between-date-time-and-text/).

## Summary table

|                     |                                                         |                                                         |                                                                       |                                                                       |                                           |                                                         |
| ------------------- | ------------------------------------------------------- | ------------------------------------------------------- | --------------------------------------------------------------------- | --------------------------------------------------------------------- | ----------------------------------------- | ------------------------------------------------------- |
| _from\to_           | **DATE**                                                | **TIME**                                                | **PLAIN TIMESTAMP**                                                   | **TIMESTAMPTZ**                                                       | **INTERVAL**                              | **TEXT**                                                |
| **DATE**            |                                                         |                                                         | [_date_ to plain _timestamp_](#date-to-plain-timestamp)               | [_date_ to _timestamptz_](#date-to-timestamptz)                       |                                           | [_date_ to _text_](#date-to-text)                       |
| **TIME**            |                                                         |                                                         |                                                                       |                                                                       | [_time_ to _interval_](#time-to-interval) | [_time_ to _text_](#time-to-text)                       |
| **PLAIN TIMESTAMP** | [plain _timestamp_ to _date_](#plain-timestamp-to-date) | [plain _timestamp_ to _time_](#plain-timestamp-to-time) |                                                                       | [plain _timestamp_ to _timestamptz_](#plain-timestamp-to-timestamptz) |                                           | [plain _timestamp_ to _text_](#plain-timestamp-to-text) |
| **TIMESTAMPTZ**     | [_timestamptz_ to _date_](#timestamptz-to-date)         | [_timestamptz_ to _time_](#timestamptz-to-time)         | [_timestamptz_ to plain _timestamp_](#timestamptz-to-plain-timestamp) |                                                                       |                                           | [_timestamptz_ to _text_](#timestamptz-to-text)         |
| **INTERVAL**        |                                                         | [_interval_ to _time_](#interval-to-time)               |                                                                       |                                                                       |                                           | [_interval_ to _text_](#interval-to-text)               |
| **TEXT**            | [_text_ to _date_](#from-text)                          | [_text_ to _time_](#from-text)                          | [_text_ to plain _timestamp_](#from-text)                             | [_text_ to _timestamptz_](#from-text)                                 | [_text_ to _interval_](#from-text)        |                                                         |

## The twenty supported typecasts

### From _date_

Before doing the "from _date_" tests, do this:

```plpgsql
drop function if exists date_value() cascade;
create function date_value()
  returns date
  language sql
as $body$
  select make_date(2021, 6, 1);
$body$;
```

#### _date_ to plain _timestamp_

Try this

```plpgsql
select date_value()::timestamp;
```

This is the result:

```output
2021-06-01 00:00:00
```

Notice that what you see is actually the result of a [plain _timestamp_ to _text_](#plain-timestamp-to-text) typecast.

The typecast _date_ value becomes the date component of the _timestamptz_ value; and the time-of-day component of the _timestamptz_ value is set to _00:00:00_.

#### _date_ to _timestamptz_

Try this:

```plpgsql
set timezone = 'UTC';
select  date_value()::timestamptz;
```

This is the result:

```output
 2021-06-01 00:00:00+00
```

You're actually seeing the _::text_ typecast of the resulting _timestamptz_ value.

This best defines the semantics:

```plpgsql
select
  (
    (select  date_value()::timestamptz) =
    (select (date_value()::timestamp)::timestamptz)
  )::text;
```

The result is _true_. The outcome of typecasting a _date_ value to a _timestamptz_ value is the same as that of typecasting a _date_ value to a plain _timestamp_ value and then typecasting that intermediate value to a _timestamptz_ value. If you know the rules for the  _date_ to plain _timestamp_ typecast and for the plain _timestamp_ to _timestamptz_ typecast, then you can predict the outcome of a _date_ to _timestamptz_ typecast.

#### _date_ to _text_

Try this:

```plpgsql
set datestyle = 'ISO, DMY';
select date_value()::text;
```

This is the result:

```output
 2021-06-01
```

See the section [Typecasting between date-time values and text values](../typecasting-between-date-time-and-text/). Briefly, the typecast outcomes are sensitive to the current setting of the _'datestyle'_ parameter. Yugabyte recommends that you always use _'ISO, DMY'_.

### From _time_

Before doing the "from _time_" tests, do this:

```plpgsql
drop function if exists time_value() cascade;
create function time_value()
  returns time
  language sql
as $body$
  select make_time(12, 13, 42.123456);
$body$;
```

#### _time_ to _interval_

Try this:

```plpgsql
select time_value()::interval;
```

This is the result:

```output
 12:13:42.123456
```

You're actually seeing the _::text_ typecast of the resulting _interval_ value.

The rule here is trivial. The to-be-typecast _time_ value is taken as a real number of seconds—counting, of course, from midnight. Suppose that the answer is _n_. The resulting _interval_ value is given by _make_interval(n)_. test the rule like this:

```plpgsql
select (
    (select time_value()::interval) =
    (
      select make_interval(secs=>
        (
          extract(hours   from time_value())*60.0*60.0 +
          extract(minutes from time_value())*60.0 +
          extract(seconds from time_value())
        ))
    )
  )::text;
```

The result is _true_.

#### _time_ to _text_

Try this:

```plpgsql
set datestyle = 'ISO, DMY';
select time_value()::text;
```

This is the result:

```output
 12:00:00.123456
```

See the section [Typecasting between date-time values and text values](../typecasting-between-date-time-and-text/) for more detail.

### From plain _timestamp_

Before doing the "from _timestamp_" tests, do this:

```plpgsql
drop function if exists plain_timestamp_value() cascade;
create function plain_timestamp_value()
  returns timestamp
  language sql
as $body$
  select make_timestamp(2021, 6, 1, 12, 13, 19.123456);
$body$;
```

#### plain _timestamp_ to _date_

Try this:

```plpgsql
select plain_timestamp_value()::date;
```

This is the result:

```output
 2021-06-01
```

You're actually seeing the _::text_ typecast of the resulting _date_ value.

The typecast simply ignores the time-of-day component of the to-be-typecast _timestamp_ value and uses its date component to set the resulting _date_ value.

You might prefer to understand it like this:

```plpgsql
select (
    (select plain_timestamp_value()::date) =
    (
      select (
        extract(year  from plain_timestamp_value())::text||'-'||
        extract(month from plain_timestamp_value())::text||'-'||
        extract(day   from plain_timestamp_value())::text
        )::date
    )
  )::text;
```

The result is _true_.

#### plain _timestamp_ to _time_

Try this:

```plpgsql
select plain_timestamp_value()::time;
```

This is the result:

```output
12:13:19.123456
```

You're actually seeing the _::text_ typecast of the resulting _time_ value.

The typecast simply ignores the date component of the to-be-typecast _timestamp_ value and uses its time-of-day component to set the resulting _time_ value.

You might prefer to understand it like this:

```plpgsql
select (
    (select plain_timestamp_value()::time) =
    (
    select (
      extract(hours   from plain_timestamp_value())::text||':'||
      extract(minutes from plain_timestamp_value())::text||':'||
      extract(seconds from plain_timestamp_value())::text
      )::time
    )
  )::text;
```

The result is _true_.

#### plain _timestamp_ to _timestamptz_

Try this:

```plpgsql
set timezone = 'UTC';
select plain_timestamp_value()::timestamptz;
```

This is the result:

```output
 2021-06-01 12:13:19.123456+00
```

You're actually seeing the _::text_ typecast of the resulting _timestamptz_ value.

This best defines the semantics:

```plpgsql
set time zone interval '-7 hours';
select (
    (select plain_timestamp_value()::timestamptz) =
    (select plain_timestamp_value() at time zone interval '-7 hours')
  )::text;
```

The result is _true_. See the section [Sensitivity of converting between _timestamptz_ and plain _timestamp_ to the _UTC offset_](../timezones/timezone-sensitive-operations/timestamptz-plain-timestamp-conversion/) for the full explanation of the semantics.

#### plain _timestamp_ to _text_

Try this:

```plpgsql
select plain_timestamp_value()::text;
```

This is the result:

```output
 2021-06-01 12:13:19.123456
```

See the section [Typecasting between date-time values and text values](../typecasting-between-date-time-and-text/) for more detail.

### From _timestamptz_

Before doing the "from _timestamptz_" tests, do this:

```plpgsql
drop function if exists timestamptz_value() cascade;
create function timestamptz_value()
  returns timestamptz
  language sql
as $body$
  select make_timestamptz(2021, 6, 1, 20, 13, 19.123456, 'America/Los_Angeles');
$body$;
```

#### _timestamptz_ to _date_

Try this:

```plpgsql
set timezone = 'UTC';
select timestamptz_value()::date;
```

This is the result:

```output
 2021-06-02
```

You're actually seeing the _::text_ typecast of the resulting _date_ value.

Notice that the date displayed here, _2-June_, is later that the date that defines the _timestamptz_ value. The test was contrived to produce this result. You need to have a solid mental model of the joint semantics of the plain _timestamp_ and the _timestamptz_ data types in order confidently to predict this outcome.

This best defines the semantics:

```plpgsql
set timezone = 'UTC';
select
  (
    (select  timestamptz_value()::date) =
    (select (timestamptz_value()::timestamp)::date)
  )::text;
```

The result is _true_. In other words, if you understand the rules for the [_timestamptz_ to plain _timestamp_](#timestamptz-to-plain-timestamp) typecast, and the rules for the plain _timestamp_ to _date_ typecast, then you understand the rules for the _timestamptz_ to _date_ typecast.

#### _timestamptz_ to _time_

Try this:

```plpgsql
set timezone = 'UTC';
select timestamptz_value()::time;
```

This is the result:

```output
 03:13:19.123456
```

You're actually seeing the _::text_ typecast of the resulting _time_ value.

This best defines the semantics:

```plpgsql
set timezone = 'UTC';
select
  (
    (select  timestamptz_value()::time) =
    (select (timestamptz_value()::timestamp)::time)
  )::text;
```

The result is _true_. In other words, if you understand the rules for the [_timestamptz_ to plain _timestamp_](#timestamptz-to-plain-timestamp) typecast, and the rules for the plain _timestamp_ to _time_ typecast, then you understand the rules for the _timestamptz_ to _time_ typecast.

####  _timestamptz_ to plain _timestamp_

Try this:

```plpgsql
set timezone = 'UTC';
select timestamptz_value()::timestamp;
```

This is the result:

```output
 2021-06-02 03:13:19.123456
```

You're actually seeing the _::text_ typecast of the resulting plain _timestamp_ value.

This best defines the semantics:

```plpgsql
set time zone interval '13 hours';
select
  (
    (select  timestamptz_value()::timestamp) =
    (select  timestamptz_value() at time zone interval '13 hours')
  )::text;
```

The result is _true_.

Notice that the date displayed here, 2-June, is later that the date that defines the _timestamptz_ value. This is the effect that was referred to in the section [_timestamptz_ to _date_](#timestamptz-to-date). See the section [Sensitivity of converting between _timestamptz_ and plain _timestamp_ to the _UTC offset_](../timezones/timezone-sensitive-operations/timestamptz-plain-timestamp-conversion/) for the full explanation of the semantics.

#### _timestamptz_ to _text_

Try this:

```plpgsql
set timezone = 'UTC';
select timestamptz_value()::text;
```

This is the result:

```output
 2021-06-02 03:13:19.123456+00
```

<a name="tz-function"></a>
{{< tip title="Create the 'tz(timestamp)' function" >}}
The code that follows

 - "This best defines the semantics of the _::text_ typecast of a _timestamptz_ value" below, [here](#uses-tz-1), and
 - "Think of it like this" below, [here](#text-to-timestamptz)

depends on the user-defined function _tz(timestamp)_. It shows the session's current _UTC offset_ as a text string. It turns out that the _::text_ typecast that the present subsection addresses uses rather whimsical rules to format the _UTC offset_. Briefly, the sign is always shown, even when it is positive; and when the minutes component of the offset is zero, this is elided. (A _UTC offset_ never has a non-zero seconds component.)

```plpgsql
drop function if exists tz(timestamp) cascade;

create function tz(t in timestamp)
  returns text
  language plpgsql
as $body$
declare
  i  constant interval not null := to_char((t::text||' UTC')::timestamptz, 'TZH:TZM')::interval;
  hh constant int      not null := extract(hours   from i);
  mm constant int      not null := extract(minutes from i);
  t  constant text     not null := to_char(hh, 's00')||
                                     case
                                       when mm <> 0 then ':'||ltrim(to_char(mm, '00'))
                                       else               ''
                                     end;
begin
  return t;
end;
$body$;
```

Notice that this

```output
to_char((t::text||' UTC')::timestamptz, 'TZH:TZM')::interval
```

is borrowed from this:

```output
to_char('2021-01-01 12:00:00 UTC'::timestamptz, 'TZH:TZM')::interval
```

in the implementation of the _[ jan_and_jul_tz_abbrevs_and_offsets() table function](../timezones/catalog-views/#the-jan-and-jul-tz-abbrevs-and-offsets-table-function)_ function.

Test it like this:

```plpgsql
set timezone = 'America/Los_Angeles';
select tz('2021-01-01 12:00:00'::timestamp);
select tz('2021-07-01 12:00:00'::timestamp);

set timezone = 'UTC';
select tz('2021-01-01 12:00:00'::timestamp);
select tz('2021-07-01 12:00:00'::timestamp);

set timezone = 'Australia/Lord_Howe';
select tz('2021-01-01 12:00:00'::timestamp);
select tz('2021-07-01 12:00:00'::timestamp);
```

These are the results—just as is required:

```output
 -08
 -07

 +00
 +00

 +11
 +10:30
```
{{< /tip >}}

<a name="uses-tz-1"></a>This best defines the semantics of the _::text_ typecast of a _timestamptz_ value:

```plpgsql
set timezone = 'Asia/Tehran';
select
  (
    (select  timestamptz_value()::text) =
    (select (timestamptz_value()::timestamp)::text||tz(timestamptz_value()::timestamp))
  )::text;
```

This code asks to see the _timestamptz_ value expressed as the local _date-time_ in the reigning timezone decorated with the _UTC offset_ of that reigning timezone.

The result is _true_. In other words, if you understand the rules for the [_timestamptz_ to plain _timestamp_](#timestamptz-to-plain-timestamp) typecast, then you understand the rules for the _timestamptz_ to _text_ typecast.

See the section [Typecasting between date-time values and text values](../typecasting-between-date-time-and-text/) for more detail.

### From _interval_

Before doing the "from _interval_" tests, do this:

```plpgsql
drop function if exists interval_value() cascade;
create function interval_value()
  returns interval
  language sql
as $body$
  select make_interval(hours=>10, mins=>17);
$body$;
```

#### _interval_ to _time_

Try this:

```plpgsql
select interval_value()::time;
```

This is the result:

```output
 10:17:00
```

You're actually seeing the _::text_ typecast of the resulting _time_ value.

The rule here is trivial. But to see it as such you have to understand what the section [How does YSQL represent an _interval_ value?](../date-time-data-types-semantics/type-interval/interval-representation/) explains. The reason is that the values of the _mm_ and _dd_ fields of the internal _[mm, dd, ss]_ tuple are simply ignored by the _interval_ to _time_ typecast. You can confirm that like this:

```plpgsql
select ('7 years 5 months 13 days 10:17:00'::interval)::time;
```

The result is _10:17:00_, just as it was for the typecast of the _interval_ value created as _make_interval(hours=>10, mins=>17)_.

The _time_ value is derived simply by treating the_ss_ value from the _interval_ value's _[hh, mm, ss]_ internal tuple as the number of seconds since midnight. The arithmetic is easiest to implement by using the SQL _extract_ operator and the _make_Time()_ built-in function.

Test the rule like this:

```plpgsql
select (
    (select interval_value()::time) =
    (
      with v as (
        select
          extract(hours   from interval_value())::int as hh,
          extract(minutes from interval_value())::int as mi,
          extract(seconds from interval_value())      as ss)
      select make_time(hh, mi, ss)
      from v
    )
  )::text;
```

The result is _true_.

#### _interval_ to _text_

Try this:

```plpgsql
select make_interval(years=>2, months=>7)::text;
```

This is the result:

```output
 2 years 7 mons
```

See the section [Typecasting between date-time values and text values](../typecasting-between-date-time-and-text/) for more detail.

### From _text_

All of the _text_ to _date_, _text_ to _time_, _text_ to plain _timestamp_, _text_ to _timestamptz_, and _text_ to _interval_ typecasts can be demonstrated with a single query:

```plpgsql
\x on
select
  '2021-06-01'                    ::date        as "date value",
  '12:13:42.123456'               ::time        as "time value",
  '2021-06-01 12:13:19.123456'    ::timestamp   as "plain timestamp value value",
  '2021-06-02 03:13:19.123456+03' ::timestamptz as "timestamptz value value",
  '10 hours 17 minutes'           ::interval    as "interval value value";
\x off
```

This is the result:

```output
date value                  | 2021-06-01
time value                  | 12:13:42.123456
plain timestamp value value | 2021-06-01 12:13:19.123456
timestamptz value value     | 2021-06-02 00:13:19.123456+00
interval value value        | 10:17:00
```

In all cases but the _text_ to _timestamptz_ typecast, the rules are obvious:

- _either_ you specify every datum that's needed to define the target value (for example, the _year_, _month_, and _date_ for a _date value_ or the _hours_, _minutes_, and _seconds_ for a _time_ value);
- _or_ you leave out any such field for which a default is defined.

Here's an example of defaulting:

```plpgsql
\x on
select
  (select '23:00'::time)            as "Partically defaulted 'time' value ",
  (select '17':: interval)          as "Partically defaulted 'interval' value",
  (select '1984-03-01'::timestamp)  as "Partically defaulted 'timestamp' value";
  \x off
```

This is the result:

```output
Partically defaulted 'time' value      | 23:00:00
Partically defaulted 'interval' value  | 00:00:17
Partically defaulted 'timestamp' value | 1984-03-01 00:00:00
```

The third example is better seen as an example of typecasting from _date_ to _timestamp_:

```plpgsql
select (
    (select '1984-03-01'::timestamp) =
    (select ('1984-03-01'::date)::timestamp)
  )::text;
```

The result is _true_.

#### _text_ to _timestamptz_

As you'd expect, the _text_ to _timestamptz_ typecast needs some special discussion. A _UTC offset_ value is logically required. But you can elide this within the text literal and leave it to be defined using the rule that maps the current timezone to a _UTC offset_ value. The rules that determine the outcomes of the examples below are underpinned by the semantics of the [sensitivity of converting between _timestamptz_ and plain _timestamp_ to the _UTC offset_](../timezones/timezone-sensitive-operations/timestamptz-plain-timestamp-conversion/).


Think of it like this:

```plpgsql
deallocate all;
prepare qry as
with
  txt as (
    select
      '2021-01-01 12:00:00'::text as winter_txt,
      '2021-07-01 12:00:00'::text as summer_txt
    ),

  tz as (
    select
       tz((select winter_txt from txt)::timestamp) as winter_tz,
       tz((select summer_txt from txt)::timestamp) as summer_tz
    )

select
  (  (  (select winter_txt from txt)||(select winter_tz from tz)  )::timestamptz  ) as "winter",
  (  (  (select summer_txt from txt)||(select summer_tz from tz)  )::timestamptz  ) as "summer";
```

First try this:

```plpgsql
set timezone = 'Asia/Tehran';
execute qry;
```

This is the first result:

```output
          winter           |          summer
---------------------------+---------------------------
 2021-01-01 12:00:00+03:30 | 2021-07-01 12:00:00+04:30
```

Now try this:

```plpgsql
set timezone = 'Europe/Helsinki';
execute qry;
```

This is the second result:

```output
         winter         |         summer
------------------------+------------------------
 2021-01-01 12:00:00+02 | 2021-07-01 12:00:00+03
```

(See [Create the _tz(timestamp)_ function](#tz-function) for its definition.)

## Code to fill out the thirty interesting table cells

The following anonymous block executes the typecast that corresponds to each of the table's thirty interesting table cells in rows-first order. When the code executes an unsupported typecast, the exception is handled and the error text is checked to confirm that it is what is expected.

```plpgsql
do $body$
declare
  msg                           text        not null := '';

  d0                   constant date        not null := make_date(2021, 6, 1);
  t0                   constant time        not null := make_time(12, 13, 42.123456);
  ts0                  constant timestamp   not null := make_timestamp(2021, 6, 1, 12, 13, 19.123456);
  tstz0                constant timestamptz not null := make_timestamptz(2021, 6, 1, 20, 13, 19.123456, 'America/Los_Angeles');
  i0                   constant interval    not null := make_interval(hours=> 5, mins=>17, secs=>42.123456);

  d                             date        not null := d0;
  t                             time        not null := t0;
  ts                            timestamp   not null := ts0;
  tstz                          timestamptz not null := tstz0;
  i                             interval    not null := i0;
  txt                           text        not null := '';
begin
  -- Cell 1.2.
  begin
    t := d0::time;
    assert false, 'Unexpected success';
  exception when cannot_coerce then
    get stacked diagnostics msg = message_text;
    assert msg = 'cannot cast type date to time without time zone', 'Unexpected error';
  end;

  -- Cell 1.3.
  begin
    ts := d0::timestamp;
  end;

  -- Cell 1.4.
  begin
    tstz := d0::timestamptz;
  end;

  -- Cell 1.5.
  begin
    i := d0::interval;
    assert false, 'Unexpected success';
  exception when cannot_coerce then
    get stacked diagnostics msg = message_text;
    assert msg = 'cannot cast type date to interval', 'Unexpected error';
  end;

  -- Cell 1.6.
  begin
    txt := d0::text;
  end;

  -- Cell 2.1.
  begin
    d := t0::date;
    assert false, 'Unexpected success';
  exception when cannot_coerce then
    get stacked diagnostics msg = message_text;
    assert msg = 'cannot cast type time without time zone to date', 'Unexpected error';
  end;

  -- Cell 2.3.
  begin
    ts := t0::timestamp;
    assert false, 'Unexpected success';
  exception when cannot_coerce then
    get stacked diagnostics msg = message_text;
    assert msg = 'cannot cast type time without time zone to timestamp without time zone', 'Unexpected error';
  end;

  -- Cell 2.4.
  begin
    ts := t0::timestamptz;
    assert false, 'Unexpected success';
  exception when cannot_coerce then
    get stacked diagnostics msg = message_text;
    assert msg = 'cannot cast type time without time zone to timestamp with time zone', 'Unexpected error';
  end;

  -- Cell 2.5.
  begin
    i := t0::interval;
  end;

  -- Cell 2.6.
  begin
    txt := t0::interval;
  end;

  -- Cell 3.1.
  begin
    d := ts0::date;
  end;

  -- Cell 3.2.
  begin
    t := ts0::time;
  end;

  -- Cell 3.4.
  begin
    tstz := ts0::timestamptz;
  end;

  -- Cell 3.5.
  begin
    i := ts0::interval;
    assert false, 'Unexpected success';
  exception when cannot_coerce then
    get stacked diagnostics msg = message_text;
    assert msg = 'cannot cast type timestamp without time zone to interval', 'Unexpected error';
  end;

  -- Cell 3.6.
  begin
    txt := ts0::text;
  end;

  -- Cell 4.1.
  begin
    d := tstz0::date;
  end;

  -- Cell 4.2.
  begin
    t := tstz0::time;
  end;

  -- Cell 4.3.
  begin
    ts := tstz0::timestamp;
  end;

  -- Cell 4.5.
  begin
    i := tstz0::interval;
    assert false, 'Unexpected success';
  exception when cannot_coerce then
    get stacked diagnostics msg = message_text;
    assert msg = 'cannot cast type timestamp with time zone to interval', 'Unexpected error';
  end;

  -- Cell 4.6.
  begin
    txt := tstz0::text;
  end;

  -- Cell 5.1.
  begin
    d := i0::date;
    assert false, 'Unexpected success';
  exception when cannot_coerce then
    get stacked diagnostics msg = message_text;
    assert msg = 'cannot cast type interval to date', 'Unexpected error';
  end;

  -- Cell 5.2.
  begin
    t := i0::time;
  end;

  -- Cell 5.3.
  begin
    ts := i0::timestamp;
    assert false, 'Unexpected success';
  exception when cannot_coerce then
    get stacked diagnostics msg = message_text;
    assert msg = 'cannot cast type interval to timestamp without time zone', 'Unexpected error';
  end;

  -- Cell 5.4.
  begin
    tstz := i0::timestamptz;
    assert false, 'Unexpected success';
  exception when cannot_coerce then
    get stacked diagnostics msg = message_text;
    assert msg = 'cannot cast type interval to timestamp with time zone', 'Unexpected error';
  end;

  -- Cell 5.6.
  begin
    txt := i0::text;
  end;

  -- Cell 6.1.
  begin
    d := '2021-06-01'::date;
  end;

  -- Cell 6.2.
  begin
    t := '12:00:00'::time;
  end;

  -- Cell 6.3.
  begin
    ts := '2021-06-01 12:00:00'::timestamp;
  end;

  -- Cell 6.4.
  begin
    tstz := '2021-06-01 12:00:00 UTC'::timestamptz;
  end;

  -- Cell 6.5.
  begin
    i := '2 hours 3 minutes 4.56 seconds'::interval;
  end;
end;
$body$;
```

The block finishes without error confirming that the typecasts are, or are not, supported as the table above lists.
