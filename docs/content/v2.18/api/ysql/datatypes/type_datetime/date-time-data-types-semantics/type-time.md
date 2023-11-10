---
title: The time data type [YSQL]
headerTitle: The time data type
linkTitle: Time data type
description: The semantics of the time data type. [YSQL]
menu:
  v2.18:
    identifier: type-time
    parent: date-time-data-types-semantics
    weight: 20
type: docs
---

Values whose data type is _time_ represent the time-of-day component of some moment (the hours and minutes as integer values and the seconds as a real number value with microsecond precision) in the "local" (a.k.a. "wall-clock") regime. _Time_ values know nothing about timezones, and represent the time of day at some unspecified location. You can picture a _time_ value as the number of microseconds from the exact midnight that starts the day. And because _time_ values know nothing about timezones, they know nothing about Daylight Savings regimes, either: every day ought to run from midnight (inclusive) through the next midnight (exclusive). But notice this quirk:

```plpgsql
select
  ('00:00:00.000000'::time)::text as "starting midnight",
  ('23:59:59.999999'::time)::text as "just before ending midnight",
  ('24:00:00.000000'::time)::text as "ending midnight";
```

This is the result:

```output
 starting midnight | just before ending midnight | ending midnight
-------------------+-----------------------------+-----------------
 00:00:00          | 23:59:59.999999             | 24:00:00
```

The fact that this is allowed is a minor annoyance. The two times test as _unequal_"

```plpgsql
select (
  '00:00:00.000000'::time =
  '24:00:00.000000'::time
  )::text;
```

The result is _false_. But because the _time_ value carries no date information, you'd need to define and advertise auxiliary application semantics to interpret _24:00:00_ as the end of a particular day rather than the start of the day that immediately follows the previous day.

Notice that subtracting one _time_ value from another _time_ value uses _clock-time-semantics_. Try this:

```plpgsql
select
  ('02:00:00'::time - '01:00:00'::time)::text as "interval 1",
  ('24:00:00'::time - '00:00:00'::time)::text as "interval 2",
  justify_hours('24:00:00'::time - '00:00:00'::time)::text as "interval 3";
```

This is the result:

```output
 interval 1 | interval 2 | interval 3
------------+------------+------------
 01:00:00   | 24:00:00   | 1 day
```

See the [_interval_ data type](../type-interval/) section. This section explains the mental model that allows _24 hours_ to be different (in a subtle way) from _1 day_.

Here's another quirk: adding an _interval_ value to a _date_ value can cause silent wrap-around. Try this:

```plpgsql
select '24:00:00'::time + '1 second'::interval;
```

This is the result:

```output
 00:00:01
```

This suggests that you might get the same result from this attempt:

```plpgsql
select '24:00:01'::time;
```

But the attempt causes this error:

```output
22008: date/time field value out of range: "24:00:01"
```

Compare these quirky outcomes with those of corresponding tests that use plain _timestamp_ values. See the section [The plain _timestamp_ and _timestamptz_ data types](../type-timestamp/). You should think very carefully about your reasons to prefer the _time_ data type over the plain _timestamp_ data type in application code and describe these reasons in your application's functional specification document.
