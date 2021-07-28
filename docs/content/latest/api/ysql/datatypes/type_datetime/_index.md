---
title: Date and time data types [YSQL]
headerTitle: Date and time data types
linkTitle: Date and time
description: YSQL supports the date, time, timestamp, and interval data types together with interval arithmetic.
image: /images/section_icons/api/ysql.png
menu:
  latest:
    identifier: api-ysql-datatypes-datetime
    parent: api-ysql-datatypes
isTocNested: true
showAsideToc: true
---
## Synopsis

YSQL supports the following data types for values that represent a date, a time of day, a date-and-time-of-day pair, or a duration. These data types will be referred to jointly as the _date-time_ data types.

| Data type                                                                  | Purpose                           | Internal format         | Min      | Max        | Resoltion     |
| -------------------------------------------------------------------------- | --------------------------------- | ----------------------- | -------- | ---------- | ------------- |
| [date](./date-time-data-types-semantics/type-date/)                        | date moment (wall-clock)          | 4-bytes                 | 4713 BC  | 5874897 AD | 1 day         |
| [time](./date-time-data-types-semantics/type-time/) [(p)]                  | time moment (wall-clock)          | 8-bytes                 | 00:00:00 | 24:00:00   | 1 microsecond |
| [timetz](#avoid-timetz) [(p)]                                              | _[avoid this](#avoid-timetz)_     |                         |          |            |               |
| [timestamp](./date-time-data-types-semantics/type-timestamp/) [(p)]        | date-and-time moment (wall-clock) | 12-bytes                | 4713 BC  | 294276 AD  | 1 microsecond |
| [timestamptz](./date-time-data-types-semantics/type-timestamp/) [(p)]      | date-and-time moment (absolute)   | 12-bytes                | 4713 BC  | 294276 AD  | 1 microsecond |
| [interval](./date-time-data-types-semantics/type-interval/) [fields] [(p)] | duration between two moments      | 16-bytes 3-field struct |          |            | 1 microsecond |

The optional _(p)_ qualifier, where _p_ is a literal integer value in _0..6_, specifies the precision, in microseconds, with which values will be recorded. (It has no effect on the size of the internal representation.) The optional _fields_ qualifier, valid only in an _interval_ declaration, is explained in the [_interval_ data type](./date-time-data-types-semantics/type-interval/) section.

The spelling _timestamptz_ is an alias, defined by PostgreSQL and inherited by YSQL, for what the SQL Standard spells as _timestamp with time zone_. The unadorned spelling, _timestamp_, is defined by the SQL Standard and may, optionally, be spelled as _timestamp without time zone_. A corresponding account applies to _timetz_ and _time_.

Because of their brevity, the forms (plain) _time_, _timetz_, (plain) _timestamp_, and _timestamptz_ are used throughout this _"Date and time data data types"_ main section rather than the verbose forms that spell the names using _without time zone_ and _with time zone_.

A value of the _interval_ data type represents a _duration_. In contrast, a value of one of the other five data types each represents a _point in time_ (a.k.a. a _moment_).

<p id="avoid-timetz">Subtraction between a pair of moment values with the same data type produces, with one exception, an <i>interval</i> value. Exceptionally, subtracting one <i>date</i> value from another produces an <i>integer</i> value.</p>

{{< tip title="Avoid using the 'timetz' data type." >}}
The <a href="https://www.postgresql.org/docs/11/datatype-datetime.html#DATATYPE-DATETIME-TABLE" target="_blank">PostgreSQL documentation <i class="fas fa-external-link-alt"></i></a> recommends against using the _timetz_ (a.k.a. _time with time zone_) data type. This text is slightly reworded:

> The data type _time with time zone_ is defined by the SQL standard, but the definition exhibits properties which lead to questionable usefulness. In most cases, a combination of _date_, (plain) _time_, (plain) _timestamp_, and _timestamptz_ should provide the complete range of _date-time_ functionality that any application could require.

The thinking is that a notion that expresses only what a clock might read in a particular timezone gives only part of the picture. For example when a clock reads 20:00 in _UTC_, it reads 03:00 in China Standard Time. But 20:00 _UTC_ is the evening of one day and 03:00 is in the small hours of the morning of the _next day_ in China Standard Time. (Neither _UTC_ nor China Standard Time adjusts its clocks for Daylight Savings.) The data type _timestamptz_ represents both the time of day and the date and so it handles the present use case naturally. No further reference will be made to _timetz_.
{{< /tip >}}

{{< note title="Maximum and minimum supported values" >}}
You might discover that you can define an earlier _timestamp_ value than _4713-01-01 00:00:00 BC_, or a later one than  _294276-01-01 00:00:00_, without error. But you should not rely on this. Rather, you should accept that the values in the "Min" and "Max" columns in the table above specify the _supported_ range.

Notice the "approx" qualifier by the minimum and maximum _interval_ values.You need to understand how an _interval_ value is represented internally as a three-field _[mm, dd, ss]_ tuple to appreciate that the limits must be expressed individually in terms of these fields. The section [_interval_ value limits](./date-time-data-types-semantics/type-interval/interval-limits/) explains all this.
{{< /note >}}

Modern applications almost always are designed for global deployment. This means that they must accommodate timezones—and that it will be the norm therefore to use the _timestamptz_ data type and not _date_, plain _time_, or plain _timestamp_. Application code will therefore need to be aware of, and to set, the timezone. It's not uncommon to expose the ability to set the timezone to the user so that _date-time_ moments can be shown differently according to the user's present purpose.

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
