---
title: The date data type [YSQL]
headerTitle: The date data type
linkTitle: Date data type
description: The semantics of the date data type. [YSQL]
menu:
  stable:
    identifier: type-date
    parent: date-time-data-types-semantics
    weight: 10
type: docs
---

Values whose data type is _date_ represent the date component of some moment (the year, the month, and the day) in the "local" (a.k.a. "wall-clock") regime. _Date_ values know nothing about timezones, and represent the date at some unspecified location. You can picture a _date_ value as the number of days (or microseconds, if you prefer) to the start of the day in question from the start of some epoch—which number is then mapped using the [proleptic Gregorian calendar](https://www.postgresql.org/docs/11/datetime-units-history.html) to a date in the familiar year-month-day representation.

Notice that subtracting one _date_ value from another _date_ value produces an _integer_ value: the number of days between the two specified days. The value is sensitive to Gregorian calendar leap years. Try this:

```plpgsql
select pg_typeof('2019-02-14'::date - '2018-02-14'::date)::text as "data type of difference between dates";
```

This is the result:

```output
 data type of difference between dates
---------------------------------------
 integer
```

Notice that this example takes advantage of the fact that the value of any data type, _d_,  can be _::text_ typecast to a canonical representation and that a _text_ value that uses the canonical representation format can be _::d_ typecast to a _d_ value—and that the round trip is non-lossy.

Now try this:

```plpgsql
select
  '2019-02-14'::date - '2017-02-14'::date as "not spanning a leap year",
  '2021-02-14'::date - '2019-02-14'::date as "spanning a leap year";
```

This is the result:

```output
 not spanning a leap year | spanning a leap year
--------------------------+----------------------
                      730 |                  731
```
