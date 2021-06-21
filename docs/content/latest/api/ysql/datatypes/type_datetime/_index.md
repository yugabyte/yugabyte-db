---
title: Date and time data types [YSQL]
headerTitle: Date and time data types
linkTitle: Date and time
description: YSQL supports the date, time, timestamp, and interval data types.
image: /images/section_icons/api/ysql.png
menu:
  latest:
    identifier: api-ysql-datatypes-datetime
    parent: api-ysql-datatypes
isTocNested: true
showAsideToc: true
---
<p id="download">&nbsp;</p>

{{< tip title="Download the '.zip' file to create the reusable code that this overall major section describes." >}}

These four sections each describe re-useable code that you might find useful:

- [User-defined interval utility functions](./date-time-data-types-semantics/type-interval/interval-utilities/)
- [Defining and using custom domain types to specialize the native interval functionality](./date-time-data-types-semantics/type-interval/custom-interval-domains/)
- [The _extended_timezone_names view_](./timezones/extended-timezone-names/)
- [Recommended practice for specifying the _UTC offset_](./timezones/recommendation/)

Moreover, some of the code examples depend on some of this code. Yugabyte recommends therefore that you download and install the entire kit into the database that you use to these code examples.

[Download date-and-time-utilities.zip](https://raw.githubusercontent.com/yugabyte/yugabyte-db/master/sample/date-and-time-utilities/date-and-time-utilities.zip) to any convenient directory and unzip it. It creates a small directory hierarchy.

You'll find _README.pdf_ at the top level. It describes the rest of the content and tells you simply to start the master script _0.sql_ at the _psql_ or the _ysqlsh_ prompt. You can run it time and again. It will always finish silently.
{{< /tip >}}

Many users of all kinds of SQL databases have reported that they find everything about the _date-time_ story complex and confusing. This explains why this overall section is rather big and the hierarchy of pages and child pages is both wide and deep. The pages can be read in a linear order, just like the sections and subsections in a printed text book. And the overall pedagogy has been designed with this in mind. The order of the items in the navigation side-bar reflects this reading order—and it is highly recommended, therefore, that you (at least once) read the whole story from start to finish in that order.

## Synopsis

YSQL supports the following data types for values that represent a date, a time, a _date-time_ pair, or a duration:

| Data type                                                                               | Alias             | Description                            | Min                        | Max                     |
| --------------------------------------------------------------------------------------- | ----------------- | -------------------------------------- | -------------------------- | ----------------------- |
| [date](./date-time-data-types-semantics/type-date/)                                     |                   | date (4-bytes)                         | 4713 BC                    | 5874897 AD              |
| [time](./date-time-data-types-semantics/type-time/) [(p)] [without time zone]           | time [(p)]        | time of day (8-bytes)                  | 00:00:00                   | 24:00:00                |
| time [(p) with time zone                                                                | timetz [(p)]      | time of day (12-bytes)                 | 00:00:00+14:59             | 24:00:00-14:59          |
| [timestamp](./date-time-data-types-semantics/type-timestamp/) [(p)] [without time zone] | timestamp [(p)]   | date and time (8-bytes)                | 4713 BC                    | 294276 AD               |
| [timestamp](./date-time-data-types-semantics/type-timestamp/) [(p)] with time zone      | timestamptz [(p)] | date and time (8-bytes)                | 4713 BC                    | 294276 AD               |
| [interval](./date-time-data-types-semantics/type-interval/) [fields] [(p)]              |                   | duration (16-bytes 3-field struct)     | -178000000 years (approx) | 178000000 years (approx) |

A value of one of the _date_, _time_, or _timestamp_ data types each represents a _point in time_ (a.k.a. a _moment_). In contrast, a value of the _interval_ data type represents a _duration_. These six data types will be referred to jointly as the _date-time_ data types.

**Note:** The [PostgreSQL documentation](https://www.postgresql.org/docs/11/datatype-datetime.html#DATATYPE-DATETIME-TABLE) recommends against using the _time with time zone_ (a.k.a. _timetz_) data type

> The type _time with time zone_ is defined by the SQL standard, but the definition exhibits properties which lead to questionable usefulness. In most cases, a combination of _date_, _time_, _timestamp without time zone_, and _timestamp with time zone_ should provide a complete range of _date-time_ functionality required by any application.

The thinking is that a notion that expresses only what a clock might read in a particular timezone gives only part of the picture. For example when a clock reads 20:00 in _UTC_, it reads 03:00 in China Standard Time. But 20:00 _UTC_ is the evening of one day and 03:00 is in the small hours of the morning of the _next day_ in China Standard Time. (Neither _UTC_ nor China Standard Time adjusts its clocks for Daylight Savings.) The data type _timestamptz_ represents both the time of day and the date and so it handles the present use case naturally. No further reference will be made to _timetz_.

**Note:** Because of their brevity, the aliases (plain) _timestamp_ and _timestamptz_ will be preferred in the rest of this main section to the respective verbose forms _timestamp without time zone_ and _timestamp with time zone_.

**Note:** You might discover that you can define an earlier _timestamp_ value than _4713-01-01 00:00:00 BC_, or a later one than  _294276-01-01 00:00:00_, without error. But you should not rely on this. Rather, you should accept that the values in the "Min" and "Max" columns in the table above specify the _supported_ range.

{{< note title="Notice the 'approx' qualifier by the minimum and maximum interval values." >}}
You need to understand how an _interval_ value is represented internally as a three-field _[mm, dd, ss]_ tuple to appreciate that the limits must be expressed individually in terms of these fields. The section [Understanding and discovering the upper and lower limits for _interval_ values](./date-time-data-types-semantics/type-interval/interval-limits/) explains all this.
{{< /note >}}

<p id="table-of-five"> If you follow the advice (abve) and avoid <i>timetz</i>, then these _date-time_ data types remain:</p>

| Data type                                                             | Comment                                                                                                          |
| --------------------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------- |
| [_date_](./date-time-data-types-semantics/type-date/)                 | wall-clock-time (local date) moment                                                                              |
| [plain _time_](./date-time-data-types-semantics/type-time/)           | wall-clock-time (local time) moment                                                                              |
| [plain _timestamp_](./date-time-data-types-semantics/type-timestamp/) | wall-clock-time (local _date-time_) moment                                                                       |
| [_timestamptz_](./date-time-data-types-semantics/type-timestamp/)     | absolute _date-time_ moment                                                                                      |
| [_interval_](./date-time-data-types-semantics/type-interval/)         | duration between EITHER two plain _time_ moments, OR two plain _timestamp_ moments, OR two _timestamptz_ moments |

Modern applications almost always are designed for global deployment. This means that they must accommodate timezones—and that it will be the norm to use the _timestamptz_ data type and not _date_, plain _time_, or plain _timestamp_. Application code will therefore need to be aware of, and to set, the timezone. It's not uncommon to expose the ability to set the timezone to the user so that _date-time_ moments can be shown differently according to the user's present purpose.


## The organization of the rest of this section

[Conceptual background](./conceptual-background) provides the background for the accounts of the _date-time_ data types that the table shown in the [Synopsis](#synopsis) lists.

[Specifying the offset from the UTC Time Standard](./specify-timezone/) explains the _set timezone_ and _show timezone_ SQL statements, the _at time zone_ operator for a _timestamptz_ expression, and the various other ways that the time timezone of interest is specified.

[The semantics of the _date-time_ data types](./date-time-data-types-semantics/) defines the semantics of the _date_ data type, the _time_ data type, the plain _timestamp_ and _timestamptz_ data types, the _interval_ data type..

[Miscellaneous operations for the _date-time_ data types](./misc-date-time-operations/) describes how to construct _date-time_ values, how to inspect them, how to convert between them, and other related operations like getting the session's current time.