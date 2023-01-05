---
title: Date and time data types and functionality [YSQL]
headerTitle: Date and time data types and functionality
linkTitle: Date and time
description: Learn about YSQL support for the date, time, timestamp, and interval data types and their functions and operators.
image: /images/section_icons/api/ysql.png
menu:
  v2.12:
    identifier: api-ysql-datatypes-datetime
    parent: api-ysql-datatypes
type: indexpage
---
## Synopsis

YSQL supports the following data types for values that represent a date, a time of day, a date-and-time-of-day pair, or a duration. These data types will be referred to jointly as the _date-time_ data types.

| Data type                                                                                          | Purpose                           | Internal format         | Min      | Max        | Resolution    |
| -------------------------------------------------------------------------------------------------- | --------------------------------- | ----------------------- | -------- | ---------- | ------------- |
| [date](./date-time-data-types-semantics/type-date/)                                                | date moment (wall-clock)          | 4-bytes                 | 4713 BC  | 5874897 AD | 1 day         |
| [time](./date-time-data-types-semantics/type-time/) [(p)]                                          | time moment (wall-clock)          | 8-bytes                 | 00:00:00 | 24:00:00   | 1 microsecond |
| [timetz](#avoid-timetz) [(p)]                                                                      | _[avoid this](#avoid-timetz)_     |                         |          |            |               |
| [timestamp](./date-time-data-types-semantics/type-timestamp/#the-plain-timestamp-data-type) [(p)]  | date-and-time moment (wall-clock) | 12-bytes                | 4713 BC  | 294276 AD  | 1 microsecond |
| [timestamptz](./date-time-data-types-semantics/type-timestamp/#the-timestamptz-data-type) [(p)]    | date-and-time moment (absolute)   | 12-bytes                | 4713 BC  | 294276 AD  | 1 microsecond |
| [interval](./date-time-data-types-semantics/type-interval/) [fields] [(p)]                         | duration between two moments      | 16-bytes 3-field struct |          |            | 1 microsecond |

The optional _(p)_ qualifier, where _p_ is a literal integer value in _0..6_, specifies the precision, in microseconds, with which values will be recorded. (It has no effect on the size of the internal representation.) The optional _fields_ qualifier, valid only in an _interval_ declaration, is explained in the [_interval_ data type](./date-time-data-types-semantics/type-interval/) section.

The spelling _timestamptz_ is an alias, defined by PostgreSQL and inherited by YSQL, for what the SQL Standard spells as _timestamp with time zone_. The unadorned spelling, _timestamp_, is defined by the SQL Standard and may, optionally, be spelled as _timestamp without time zone_. A corresponding account applies to _timetz_ and _time_.

Because of their brevity, the forms (plain) _time_, _timetz_, (plain) _timestamp_, and _timestamptz_ are used throughout this _"Date and time data types"_ main section rather than the verbose forms that spell the names using _without time zone_ and _with time zone_.

A value of the _interval_ data type represents a _duration_. In contrast, a value of one of the other five data types each represents a _point in time_ (a.k.a. a _moment_).

<a name="avoid-timetz"></a>Subtraction between a pair of moment values with the same data type produces, with one exception, an _interval_ value. Exceptionally, subtracting one _date_ value from another produces an _integer_ value.

{{< tip title="Avoid using the 'timetz' data type." >}}
The <a href="https://www.postgresql.org/docs/11/datatype-datetime.html#DATATYPE-DATETIME-TABLE" target="_blank">PostgreSQL documentation <i class="fa-solid fa-up-right-from-square"></i></a> recommends against using the _timetz_ (a.k.a. _time with time zone_) data type. This text is slightly reworded:

> The data type _time with time zone_ is defined by the SQL standard, but the definition exhibits properties which lead to questionable usefulness. In most cases, a combination of _date_, (plain) _time_, (plain) _timestamp_, and _timestamptz_ should provide the complete range of _date-time_ functionality that any application could require.

The thinking is that a notion that expresses only what a clock might read in a particular timezone gives only part of the picture. For example when a clock reads 20:00 in _UTC_, it reads 03:00 in China Standard Time. But 20:00 _UTC_ is the evening of one day and 03:00 is in the small hours of the morning of the _next day_ in China Standard Time. (Neither _UTC_ nor China Standard Time adjusts its clocks for Daylight Savings.) The data type _timestamptz_ represents both the time of day and the date and so it handles the present use case naturally. No further reference will be made to _timetz_.
{{< /tip >}}
<a name="maximum-and-minimum-supported-values"></br></a>
{{< note title="Maximum and minimum supported values." >}}
You can discover that you can define an earlier _timestamp[tz]_ value than _4713-01-01 00:00:00 BC_, or a later one than  _294276-01-01 00:00:00_, without error. Try this:

```plpgsql
-- The domain "ts_t" is a convenient single point of maintenance to allow
-- choosing between "plain timestamp" and "timestamptz" for the test.
drop domain if exists ts_t cascade;
create domain ts_t as timestamptz;

drop function if exists ts_limits() cascade;
create function ts_limits()
  returns table(z text)
  language plpgsql
as $body$
declare
  one_sec  constant interval not null := make_interval(secs=>1);

  max_ts   constant ts_t     not null := '294276-12-31 23:59:59 UTC AD';
  min_ts   constant ts_t     not null :=   '4714-11-24 00:00:00 UTC BC';
  t                 ts_t     not null :=  max_ts;
begin
  z := 'max_ts: '||max_ts::text;                          return next;
  begin
    t := max_ts + one_sec;
  exception when datetime_field_overflow
    -- 22008: timestamp out of range
    then
      z := 'max_ts overflowed';                           return next;
  end;

  z := '';                                                return next;

  z := 'min_ts: '||min_ts::text;                          return next;
  begin
    t := min_ts - one_sec;
  exception when datetime_field_overflow
    -- 22008: timestamp out of range
    then
      z := 'min_ts underflowed';    return next;
  end;
end;
$body$;

set timezone = 'UTC';
select z from ts_limits();
```

This is the result:

```output
 max_ts: 294276-12-31 23:59:59+00
 max_ts overflowed

 min_ts: 4714-11-24 00:00:00+00 BC
 min_ts underflowed
```

You see the same date and time-of-day values, but without the timezone offset of course, if you define the _ts_t_ domain type using plain _timestamp_.

This test is shown for completeness. Its outcome is of little practical consequence. You can rely on the values in the "Min" and "Max" columns in the table above, copied from the <a href="https://www.postgresql.org/docs/11/datatype-datetime.html#DATATYPE-DATETIME-TABLE" target="_blank">PostgreSQL documentation <i class="fa-solid fa-up-right-from-square"></i></a>, to specify the _supported_ range. Yugabyte recommends that, as a practical compromise, you take these to be the limits for _timestamp[tz]_ values:

```output
['4713-01-01 00:00:00 BC', '294276-12-31 23:59:59 AD']
```

Notice that the minimum and maximum _interval_ values are not specified in the table above. You need to understand how an _interval_ value is represented internally as a three-field _[mm, dd, ss]_ tuple to appreciate that the limits must be expressed individually in terms of these fields. The section [_interval_ value limits](./date-time-data-types-semantics/type-interval/interval-limits/) explains all this.
{{< /note >}}

Modern applications almost always are designed for global deployment. This means that they must accommodate timezones—and that it will be the norm therefore to use the _timestamptz_ data type and not _date_, plain _time_, or plain _timestamp_. Application code will therefore need to be aware of, and to set, the timezone. It's not uncommon to expose the ability to set the timezone to the user so that _date-time_ moments can be shown differently according to the user's present purpose.

## Special date-time manifest constants

PostgreSQL, and therefore YSQL, support the use of several special manifest _text_ constants when they are typecast to specified _date-time_ data types, thus:

| constant    | valid with                         |
| ----------- | ---------------------------------- |
| 'epoch'     | date, plain timestamp              |
| 'infinity'  | date, plain timestamp, timestamptz |
| '-infinity' | date, plain timestamp              |
| 'now'       | date, plain time, plain timestamp  |
| 'today'     | date, plain timestamp              |
| 'tomorrow'  | date, plain timestamp              |
| 'yesterday' | date, plain timestamp              |
| 'allballs'  | plain time                         |

Their meanings are given in section [8.5.1.4. Special Values](https://www.postgresql.org/docs/11/datatype-datetime.html#DATATYPE-DATETIME-SPECIAL-VALUES) in the PostgreSQL documentation.

{{< tip title="Avoid using all of these special constants except for 'infinity' and '-infinity'." >}}

The implementation of the function [random_test_report_for_modeled_age()](./functions/miscellaneous/age/#function-random-test-report-for-modeled-age) shows a common locution where _'infinity'_ and _'-infinity'_ are used to initialize maximum and minimum values for _timestamp_ values that are updated as new _timestamp_ values arise during a loop's execution.

The constants _'infinity'_ and _'-infinity'_ can be also used to define _range_ values that are unbounded at one end. But this effect can be achieved with more clarity simply by omitting the value at the end of the range that you want to be unbounded.

The remaining special constants have different kinds of non-obvious results. See the recommendation [Don't use the special manifest constant 'now'](./functions/current-date-time-moment/#avoid-constant-now) on the 'Functions that return the current date-time moment' page. The constants _'today'_, _'tomorrow'_. and _'yesterday'_ all bring analogous risks to those brought by _'now'_. And the intended effects of _'epoch'_ and _'allballs'_ are brought with optimal clarity for the reader by typecasting an appropriately spelled literal value to the required data type, whatever it might be.

Yugabyte recommends that you avoid using all of the special manifest _text_ _date-time_ constants except for _'infinity'_ and _'-infinity'_.
{{< /tip >}}

{{< note title="Even 'infinity' and '-infinity' can't be used everywhere that you might expect." >}}
Try this test:

```plpgsql
select 'infinity'::timestamptz - clock_timestamp();
```

It causes the _22008_ error, _cannot subtract infinite timestamps_. Normally, the difference between two _timestamptz_ values is an _interval_ value. So you might think that the result here would be an infinite interval. But there is no such thing. This attempt:

```plpgsql
select 'infinity'::interval;
```

causes the _22007_ error, _invalid input syntax for type interval: "infinity"_.
{{< /note >}}

## How to use the date-time data types major section

Many users of all kinds of SQL databases have reported that they find everything about the _date-time_ story complex and confusing. This explains why this overall section is rather big and why the hierarchy of pages and child pages is both wide and deep. The order presented in the left-hand navigation menu was designed so that the pages can be read just like the sections and subsections in a book. The overall pedagogy was designed with this reading order in mind. It is highly recommended, therefore, that you (at least once) read the whole story from start to finish in this order.

If you have to maintain extant application code, you'll probably need to understand everything that this overall section explains. This is likely to be especially the case when the legacy code is old and has, therefore, been migrated from PostgreSQL to YugabyteDB.

However, if your purpose is only to write brand-new application code, and if you're happy simply to accept Yugabyte's various recommendations without studying the reasoning that supports these, then you'll need to read only a small part of this overall major section. This is what you need:

- **[Conceptual background](./conceptual-background/)**
- **[Real timezones that observe Daylight Savings Time](./timezones/extended-timezone-names/canonical-real-country-with-dst/)**
- **[Real timezones that don't observe Daylight Savings Time](./timezones/extended-timezone-names/canonical-real-country-no-dst/)**
- **[The plain timestamp and timestamptz data types](./date-time-data-types-semantics/type-timestamp/)**
- **[Sensitivity of converting between timestamptz and plain timestamp to the UTC offset](./timezones/timezone-sensitive-operations/timestamptz-plain-timestamp-conversion/)**
- **[Sensitivity of timestamptz-interval arithmetic to the current timezone](./timezones/timezone-sensitive-operations/timestamptz-interval-day-arithmetic/)**
- **[Recommended practice for specifying the UTC offset](./timezones/recommendation/)**
- **[Custom domain types for specializing the native interval functionality](./date-time-data-types-semantics/type-interval/custom-interval-domains/)**
