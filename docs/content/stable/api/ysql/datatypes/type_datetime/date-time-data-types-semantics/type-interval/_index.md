---
title: The interval data type [YSQL]
headerTitle: The interval data type
linkTitle: Interval data type
description: The semantics of the interval and data type and its variants. [YSQL]
image: /images/section_icons/api/subsection.png
menu:
  stable:
    identifier: type-interval
    parent: date-time-data-types-semantics
    weight: 40
type: indexpage
---

## Why does the interval data type exist?

Briefly, and trivially, the _interval_ data type exists because the SQL Standard prescribes it. Of course, it does this for a reason: because the semantics of _interval_ arithmetic is rather special and reflect real-world requirements that arise from the [difference between _clock-time-semantics_ and _calendar-time-semantics_](../../conceptual-background/#two-ways-of-conceiving-of-time-calendar-time-and-clock-time).

### The SQL Standard prescribes support for the interval data type

Try this:

```plpgsql
drop function if exists f() cascade;

create function f()
  returns table(t text)
  language plpgsql
as $body$
declare
  d1 constant date           := '2021-01-13';
  d2 constant date           := '2021-02-17';

  t1 constant time           := '13:23:17.000000';
  t2 constant time           := '15:37:43.123456';

  ts1 constant timestamp     := '2021-01-13 13:23:17.000000';
  ts2 constant timestamp     := '2021-02-17 15:37:43.123456';

  tstz1 constant timestamptz := '2021-01-13 13:23:17.000000 +04:00';
  tstz2 constant timestamptz := '2021-02-17 15:37:43.123456 -01:00';
begin
  t := 'date:        '||(pg_typeof(d2    - d1   ))::text; return next;
  t := '';                                                return next;
  t := 'time:        '||(pg_typeof(t2    - t1   ))::text; return next;
  t := 'timestamp:   '||(pg_typeof(ts2   - ts1  ))::text; return next;
  t := 'timestamptz: '||(pg_typeof(tstz2 - tstz1))::text; return next;
end;
$body$;

select t from f();
```

This is the result:

```output
 date:        integer

 time:        interval
 timestamp:   interval
 timestamptz: interval
```

Subtraction isn't supported for _timetz_ values—yet another reason not to use that data type.

Subtracting _date_ values produces an _int_ value: the number of days between them. In contrast, subtracting _time_, _timestamp_ , and _timestamptz_ values produces an _interval_ value. The SQL Standard prescribes this outcome for these newer data types but not for the earlier _date_ data type.

### Interval arithmetic semantics

Try this to see the actual _interval_ value that subtracting _timestamptz_ values produces:

```plpgsql
select
  (
    '2020-03-10 13:47:19.7':: timestamp -
    '2020-03-10 12:31:13.5':: timestamp)  ::text as "interval 1",
  (
    '2020-03-10 00:00:00':: timestamp -
    '2020-02-10 00:00:00':: timestamp)    ::text as "interval 2";
```

This is the result:

```output
 interval 1 | interval 2
------------+------------
 01:16:06.2 | 29 days
```

The section [How does YSQL represent an _interval_ value?](./interval-representation/) explains that this _text_ display is not the visualization of just a scalar number of seconds; rather, an _interval_ value is represented as a three-field _[mm, dd, ss]_ tuple. (The first two fields are integers and the last represents a real number of seconds with microsecond precision.) And it explains the reasoning behind this design. The story is complemented by the examples, and the explanations of what they show, in the section [_Interval_ arithmetic](./interval-arithmetic/).

- The _seconds_ component is externalized as an integral number of _hours_, an integral number of minutes, and a real number of _seconds_.
- The _days_ component is externalized as an integral number of _days_.
- And the _months_ component is externalized as an integral number of _years_ and an integral number of _months_.

Briefly, the rules for adding or subtracting an _interval_ value to a _timestamptz_ value are different when the value defines a non-zero value for only the _seconds_ component, only the _days_ component, or only the _months_ component. The rule differences are rooted in convention. (The rules are therefore complex when an _interval_ value has more than one non-zero component—so complex that it's very difficult to state requirements that imply such hybrid _interval_ values, and to implement application code that meets such requirements reliably.)

Here is a sufficient example to illustrate the conceptual difficulty. First, try this:

```plpgsql
select ('1 day'::interval = '24 hours'::interval)::text;
```

The result is _true_. (The implementation of the _interval-interval_ overload of the `=` operator is explained and discussed in the section [Comparing two _interval_ values](./interval-arithmetic/interval-interval-comparison/).)

Now try this:

```plpgsql
drop function if exists dd_versus_ss() cascade;

create function dd_versus_ss()
  returns table(x text)
  language plpgsql
as $body$
begin
  set timezone = 'America/Los_Angeles';
  declare
    i_1_day     constant interval := '1 day';
    i_24_hours  constant interval := '24 hours';

    -- Just before DST Starts at 02:00 on Sunday 14-Mar-2021.
    t0                constant timestamptz := '2021-03-13 20:00:00';

    t0_plus_1_day     constant timestamptz := t0 + i_1_day;
    t0_plus_24_hours  constant timestamptz := t0 + i_24_hours;
  begin
    x := 't0 + ''1 day'':    '||t0_plus_1_day    ::text; return next;
    x := 't0 + ''24 hours'': '||t0_plus_24_hours ::text; return next;
  end;
end;
$body$;

select x from dd_versus_ss();
```

This is the result:

```output
 t0 + '1 day':    2021-03-14 20:00:00-07
 t0 + '24 hours': 2021-03-14 21:00:00-07
```

How can it be that, while _'1 day'_ is equal to _'24 hours'_, _t0 + '1 day'_ is _not_ equal to _t0 + '24 hours'_? The short answer, of course, is that  _'1 day'_ is _not_ equal to _'24 hours'_ when _interval_ equality is defined strictly. The native _interval-interval_ overload of the `=` operator implements only a loose notion of _interval_ equality. You also need a _strict_ _interval_ equality notion. The section [The "strict equals" operator](./interval-arithmetic/interval-interval-comparison/#the-strict-equals-interval-interval-operator) shows you how to do this.

In the present contrived but crucial example, _t0_ is just before the "spring forward" moment in the _America/Los_Angeles_ timezone. And the loosely, but not strictly, equal durations of _'1 day'_ and _'24 hours'_ are both long enough to take you from _Pacific Standard Time_ to _Pacific Daylight Savings Time_. Bearing in mind the _[\[mm, dd, ss\]](./interval-representation/)_ internal representation, you can immediately see this:

- The semantics of _interval_ arithmetic is different for the _dd_ field of the internal representation than for the _ss_ field.

This does reflect convention. Are you postponing an appointment by one day? Here you expect the re-scheduled appointment to be at the same time on the next day, whether or not a start or end of Daylight Savings Time intervenes. Or are you making a journey (like a flight) that you know takes twenty-four hours? Here, whether or not a start or end of Daylight Savings Time occurs during the flight crucially affects the arrival time.

A similar contrived test that uses _interval_ values of _'1 month'_ and _'30 days'_ with a starting moment just before the last day of February in a leap year shows this:

- The semantics of _interval_ arithmetic is different for the _mm_ field than for the _dd_ field.

Try this:

```plpgsql
select ('1 month'::interval = '30 days'::interval)::text;
```

The result is _true_. Now try this:

```plpgsql
drop function if exists mm_versus_dd() cascade;

create function mm_versus_dd()
  returns table(x text)
  language plpgsql
as $body$
begin
  set timezone = 'UTC';
  declare
    i_1_month  constant interval := '1 month';
    i_30_days  constant interval := '30 days';

    -- Just before 29-Feb in a leap year.
    t0               constant timestamptz := '2020-02-26 12:00:00';

    t0_plus_30_days  constant timestamptz := t0 + i_30_days;
    t0_plus_1_month  constant timestamptz := t0 + i_1_month;
  begin
    x := 't0 + 1 month: '||t0_plus_1_month ::text; return next;
    x := 't0 + 30 days: '||t0_plus_30_days ::text; return next;
  end;
end;
$body$;

select x from mm_versus_dd();
```

This is the result:

```output
 t0 + 1 month: 2020-03-26 12:00:00+00
 t0 + 30 days: 2020-03-27 12:00:00+00
```

This outcome, too, does reflect convention. Are you setting up reminders to tell you to water your hardy succulents every month? Here, you simply want a reminder on, say, the 10th of each calendar month. Or are you taking a package tour (starting, say, mid-week)  that is advertised to last thirty days? Here, whether or not 29-February in a leap year occurs during the vacation affects when you get back home.

Everything that explains these differing semantics, and the philosophy of why they should differ, is explained in the section [_Interval_ arithmetic](interval-arithmetic) and its child pages.

## Organization of the rest of the interval section

The notions that the account of the _interval_ data type explains are interdependent. The ordering of the following subsections aims to introduce the notions with the minimum dependency on notions yet to be introduced. The account is split into the following main subsections:

- [How does YSQL represent an _interval_ value?](./interval-representation/)
- [_Interval_ value limits](interval-limits)
- [Declaring _intervals_](declaring-intervals)
- [_Interval_ arithmetic](interval-arithmetic)
- [Defining and using custom domain types to specialize the native _interval_ functionality](custom-interval-domains)
- [User-defined _interval_ utility functions](interval-utilities)

See the generic section [Typecasting between date-time values and text values](../../typecasting-between-date-time-and-text/) for the account of the ways to construct and to read values for all of the _date-time_ data types—including, therefore, the _interval_ data type.
