---
title: The time data type [YSQL]
headerTitle: The time data type
linkTitle: time data type
description: The semantics of the time data type. [YSQL]
menu:
  latest:
    identifier: type-time
    parent: date-time-data-types-semantics
    weight: 20
isTocNested: true
showAsideToc: true
---

Values whose data type is _time_ represent the time-of-day component of some moment (the hours and minutes as integer values and seconds as a real number value) in the "local" (a.k.a. "wall-clock") regime. _Time_ values know nothing about timezones, and represent the time of day at some unspecified location. You can picture a _time_ value as the number of microseconds from the exact midnight that starts the day. And because _time_ values know nothing about timezones, they know nothing about Daylight Savings regimes, either: every day ought to run from midnight (inclusive) through the next midnight (exclusive). But notice this quirk:

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

The fact that this is allowed is a minor annoyance.

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
