---
title: The moment-interval overloads of the "+" and "-" operators [YSQL]
headerTitle: The moment-interval overloads of the "+" and "-" operators for timestamptz, timestamp, and time
linkTitle: Moment-interval overloads of "+" and "-"
description: Explains the semantics of the moment-interval overloads of the "+" and "-" operators for the timestamptz, timestamp, and time data types. [YSQL]
menu:
  v2.16:
    identifier: moment-interval-overloads-of-plus-and-minus
    parent: interval-arithmetic
    weight: 50
type: docs
---

{{< tip title="Download and install the date-time utilities code." >}}
The code on this page depends on the code presented in the section [User-defined _interval_ utility functions](../../interval-utilities/). This is included in the larger [code kit](../../../../download-date-time-utilities/) that includes all of the reusable code that the overall _[date-time](../../../../../type_datetime/)_ section describes and uses.
{{< /tip >}}

This section presents PL/pgSQL implementations that model the semantics of the _interval_-moment-overload of the `+` operator for three distinct kinds of _interval_ value: _pure seconds_, _pure days_, and _pure months_. The semantics of the _interval_-moment-overloads of the `-` operator is implied by the semantics of the overload of the `+` operator.

The code examples that this page shows elaborate on those that are shown on the main parent page [The _interval_ data type and its variants](../../../type-interval/).

## Defining three kinds of interval value: pure seconds, pure days, and pure months.

First, do this:

```plpgsql
select (
    '30 days '::interval = '720 hours'::interval and
    ' 1 month'::interval =   '30 days'::interval and
    ' 1 month'::interval = '720 hours'::interval
  )::text;
```

The result is _true_. Now set up the test like this:

```plpgsql
drop table if exists t;
create table t(
   t0               timestamptz primary key,
  "t0 + 720 hours"  timestamptz,
  "t0 + 30 days"    timestamptz,
  "t0 + 1 month"    timestamptz);

-- Insensitive to the session TimeZone setting.
insert into t(t0) values ('2021-02-19 12:00:00 America/Los_Angeles');

deallocate all;

prepare update_table as
update t set
  "t0 + 720 hours" = t0 + '720 hours'::interval,
  "t0 + 30 days"   = t0 +   '30 days'::interval,
  "t0 + 1 month"   = t0 +   '1 month'::interval;

prepare inspect_result as
select
  t0,
  "t0 + 720 hours",
  "t0 + 30 days",
  "t0 + 1 month"
from t;
```

Now execute the test with the session timezone set to one that respects Daylight savings Time. In the 'America/Los_Angeles' zone, this starts at 02:00 in the small hours of Sunday morning on 14-March-2021—and so, with the chosen starting moment, each of the differently spelled _interval_ values spans the "spring forward" moment. Notice, too, that the starting moment is chosen so that the duration contains the end of February and that February never has thirty days, leap year or not.

```plpgsql
set timezone = 'America/Los_Angeles';
execute update_table;
execute inspect_result;
```

This is the result:

```output
           t0           |     t0 + 720 hours     |      t0 + 30 days      |      t0 + 1 month
------------------------+------------------------+------------------------+------------------------
 2021-02-19 12:00:00-08 | 2021-03-21 13:00:00-07 | 2021-03-21 12:00:00-07 | 2021-03-19 12:00:00-07
```

All the three results are displayed with a negative seven hours _UTC offset_ reflecting the fact that they occur during the Daylight Savings Time period in the United States. The time of day for _"t0 + 720 hours"_ differs by one hour from that for _"t0 + 30 days"_, but the date component is the same for each. The time of day for _"t0 + 30 days"_ is the same as that for _"t0 + 1 month"_, but the date component differs by two days.

You have to think quite hard to understand displayed _timestamptz_ values when you inspect them using a session whose _TimeZone_ setting is _not_ _UTC_ because that setting affects the values that you see. This is very much intended and reflects the _raison d’être_ of the data type. See the section [The plain _timestamp_ and _timestamptz_ data types](../../../type-timestamp/). In particular, this is why the moment before "spring forward" is displayed with an offset of negative _eight_ hours while the moments after "spring forward" are displayed with an offset of negative _seven_ hours.

The semantics that brings the outcome that you see here was designed for a common use case:

-  Look for _"Are you postponing an appointment by one day... or are you making a journey (like a flight) that you know takes twenty-four hours?"_ and, later, _"Are you setting up reminders to tell you to water your hardy succulents every month...or are you taking a package tour that is advertised to last thirty days?"_ in the section [_Interval_ arithmetic semantics](../../../type-interval/#interval-arithmetic-semantics) on the main parent page [The _interval_ data type and its variants](../../../type-interval/).

Here, you conceive of the problem in the timezone where you are (and maybe, too, in the timezone where you'll end up) and you want to know what a clock on the wall where you happen to be will show when you look at it. So the application that implements the problem's solution will ensure that it sets the session's timezone appropriately.

Bear in mind that many different displayed _timestamptz_ values can all correspond to the same underlying representation because, for example, _'2021-02-19 12:00:00-08'_ (noon on the US West Coast in winter) is the same absolute moment as _'2021-02-19 15:00:00-05'_ (three o'clock in the afternoon on the same date on the US East Coast). To save yourself the effort of mental arithmetic, it's best to observe the results of tests like the present one using a session whose timezone is set to _UTC_'—which has an offset of _zero_, meaning that the bare time-of-day conveys all the information.

Inspect the outcome using _UTC_:

```plpgsql
set timezone = 'UTC';
execute inspect_result;
```

This is the result:

```output
           t0           |     t0 + 720 hours     |      t0 + 30 days      |      t0 + 1 month
------------------------+------------------------+------------------------+------------------------
 2021-02-19 20:00:00+00 | 2021-03-21 20:00:00+00 | 2021-03-21 19:00:00+00 | 2021-03-19 19:00:00+00
```

The three _interval_ arithmetic results all differ from each other. in other words, the semantic effect of adding the "same" _interval_ value to the same _timestamptz_ value is different for each of the three nominally interchangeable spellings, _'720 hours'_, _'30 days'_, or _'1 month'_, of the _interval_ value.

Now re-execute the prepared statement after setting the session's timezone to one that does _not_ respect Daylight savings Time.

```plpgsql
set timezone = 'Asia/Shanghai';
execute update_table;
```

Once again, inspect the outcome using _UTC_:

```plpgsql
set timezone = 'UTC';
execute inspect_result;
```

This is the new result:

```output
           t0           |     t0 + 720 hours     |      t0 + 30 days      |      t0 + 1 month
------------------------+------------------------+------------------------+------------------------
 2021-02-19 20:00:00+00 | 2021-03-21 20:00:00+00 | 2021-03-21 20:00:00+00 | 2021-03-19 20:00:00+00
```

This time, the columns _"t0 + 720 hours"_ and _"t0 + 30 days"_ have the same value as each other while these still differ from the _"t0 + 1 month"_ value.

The reason  that the three different _interval_ value spellings can have different effects is given by comparing them using the [user-defined _"strict equals"_ interval-interval `==` operator](../../interval-utilities#the-user-defined-strict-equals-interval-interval-operator).

```plpgsql
select (
    not '30 days '::interval == '720 hours'::interval and
    not ' 1 month'::interval == ' 30 days '::interval and
    not ' 1 month'::interval == '720 hours'::interval
  )::text;
```

The result is _true_—meaning that no two from the three differently spelled _interval_ values are equal in the strict sense. Try this to see yet more vividly what lies behind this outcome:

```postgresql
select
  interval_mm_dd_ss('720 hours'::interval) as "'720 hours'",
  interval_mm_dd_ss(' 30 days '::interval) as "'30 days'",
  interval_mm_dd_ss('  1 month'::interval) as "'1 month'";
```

This is the result:

```output
  '720 hours'  | '30 days' | '1 month'
---------------+-----------+-----------
 (0,0,2592000) | (0,30,0)  | (1,0,0)
```

Each of the different spellings of the "same" _interval_ value ("same, that is, with respect to the criterion for equality that the native _interval-interval_ overload of `=` implements) is either a _pure interval_ value with respectively:

- _either_ just a non-zero _ss_ field (a.k.a. a "pure seconds" _interval_ value)
- _or_ just a non-zero _dd_ field (a.k.a. a "pure days" _interval_ value)
- _or_ just a non-zero _mm_ field (a.k.a. a "pure months" _interval_ value).

This explains why the arithmetic semantics is different for the three kinds of pure _interval_ value.

- The outcomes with a "pure seconds" _interval_ value and a "pure days" _interval_ value will differ from each other when they are used in a session whose _TimeZone_ setting respects Daylight Savings Time and the durations of the _interval_ values span the "spring forward" moment or the "fall back" moment (as long as the moments have the data type _timestamptz_).
- The outcomes with a "pure days" _interval_ value and a "pure months" _interval_ value will differ from each other when the durations of the _interval_ values span the boundaries of months whose number of days differs from thirty. This will be the case irrespective of whether the session's timezone respects Daylight Savings Time and of whether the moments have the data type _timestamptz_ or plain _timestamp_.

## Hybrid interval arithmetic is dangerous

The term _hybrid interval_ value will be used to denote an _interval_ value where more than one of the fields of  the _[\[mm, dd, ss\]](../../interval-representation/)_ internal representation is non-zero.

Try this. It uses hybrid "days-seconds" _interval values.

```plpgsql
drop function if exists hybrid_dd_ss_results() cascade;

create function hybrid_dd_ss_results()
  returns table(x text)
  language plpgsql
as $body$
begin
  set timezone = 'America/Los_Angeles';
  declare
    -- Five hours before DST Start.
    t0 constant timestamptz := '2021-03-13 21:00:00 America/Los_Angeles';

    "1 day"          constant interval := '1 day';
    "9 hours"        constant interval := '9 hours';
    "1 day 9 hours"  constant interval := '1 day 9 hours';
  begin
    x := $$ t0 + '1 day 9 hours'      : $$|| (t0 + "1 day 9 hours")      ::text;  return next;
    x := $$(t0 + '1 day') + '9 hours' : $$||((t0 + "1 day") + "9 hours") ::text;  return next;
    x := $$(t0 + '9 hours') + '1 day' : $$||((t0 + "9 hours") + "1 day") ::text;  return next;
  end;
end;
$body$;

select x from hybrid_dd_ss_results();
```

This is the result:

```output
  t0 + '1 day 9 hours'      : 2021-03-15 06:00:00-07
 (t0 + '1 day') + '9 hours' : 2021-03-15 06:00:00-07
 (t0 + '9 hours') + '1 day' : 2021-03-15 07:00:00-07
```

Notice that the time of day for the third result, _07:00_, is different from that for the first and second results, _06:00_.

Now try this similar test. It uses hybrid "months-days" _interval values.

```plpgsql
drop function if exists hybrid_mm_dd_results() cascade;

create function hybrid_mm_dd_results()
  returns table(x text)
  language plpgsql
as $body$
begin
  set timezone = 'UTC';
  declare
    -- The last day of a short month.
    t0 constant timestamptz := '2021-02-28 12:00:00 UTC';

    "1 month"         constant interval := '1 month';
    "9 days"          constant interval := '9 days';
    "1 month 9 days"  constant interval := '1 month 9 days';
  begin
    x := $$ t0 + '1 month 9 days'      : $$|| (t0 + "1 month 9 days")       ::text;  return next;
    x := $$(t0 + '1 month') + '9 days' : $$||((t0 + "1 month") + "9 days")  ::text;  return next;
    x := $$(t0 + '9 days') + '1 month' : $$||((t0 + "9 days") + "1 month")  ::text;  return next;
  end;
end;
$body$;

select x from hybrid_mm_dd_results();
```

This is the result:

```output
  t0 + '1 month 9 days'      : 2021-04-06 12:00:00+00
 (t0 + '1 month') + '9 days' : 2021-04-06 12:00:00+00
 (t0 + '9 days') + '1 month' : 2021-04-09 12:00:00+00
```

Notice that the date for the third result, _9-April_, is different from that for the first and second results, _6-April_.

You might be tempted to speculate about priority rules for how the fields of a hybrid _interval_ value are acted on. Don't do this. The PostgreSQL documentation doesn't state the rules and application code that used hybrid _interval_ values would be hard to understand and likely, therefore, to be unreliable.

{{< tip title="Avoid arithmetic that uses hybrid 'interval' values." >}}
Yugabyte recommends that you avoid creating and using _hybrid interval_ values; rather, you should adopt the practice that the section [Custom domain types for specializing the native _interval_ functionality](../../custom-interval-domains/) explains.
{{< /tip >}}

## The semantics for the moment-interval overloads of "+" and "-" for pure seconds interval values

Create this function:

```plpgsql
drop function if exists moment_ss_interval_addition(timestamptz, interval) cascade;

create function moment_ss_interval_addition(t timestamptz, i interval)
  returns timestamptz
  language plpgsql
as $body$
declare
  mm_dd_ss constant interval_mm_dd_ss_t := interval_mm_dd_ss(i);
begin
  assert
    mm_dd_ss.ss <> 0 and
    mm_dd_ss.dd =  0 and
    mm_dd_ss.mm =  0,
    'not a pure seconds interval';
  declare
    ss_t     constant double precision not null := extract(epoch from t);
    ss_i     constant double precision not null := mm_dd_ss.ss::double precision;
    i_model  constant timestamptz      not null := to_timestamp(ss_t + ss_i);

    i_actual constant timestamptz      not null := t + i;
  begin
    assert i_model = i_actual, 'assert model does not hold';
    return i_model;
  end;
end;
$body$;
```

Now test it like this:

```plpgsql
set timezone = 'America/Los_Angeles';
select moment_ss_interval_addition('2021-02-19 12:00:00 America/Los_Angeles'::timestamptz, '720 hours'::interval);
```

The function finishes without an _assert_ violation—showing that a pure seconds _interval_ value was supplied and that the semantic model that the function describes holds. This is the result:

```output
 2021-03-21 13:00:00-07
```

The PL/pgSQL code, by simply adding a number of seconds to the start moment to get the end moment in the regime of seconds since the start of the epoch, vividly makes the point that the moment-_interval_ overloads of `+` and `-` for pure seconds _interval_ values honor _clock-time-semantics_.

**Note:** YSQL inherits the fact from the PostgreSQL implementation of the [proleptic Gregorian calendar](https://www.postgresql.org/docs/11/datetime-units-history.html) that [leap seconds](https://www.timeanddate.com/time/leapseconds.html) are not supported. If they were, then the explanation of the semantics of moment-_interval_ would demand yet more care.

Now test it using a timezone that does not respect Daylight Savings time:

```plpgsql
set timezone = 'Asia/Shanghai';
select moment_ss_interval_addition('2021-02-19 12:00:00 America/Los_Angeles'::timestamptz, '720 hours'::interval);
```

Once again, the function finishes without an _assert_ violation—showing that, again, the semantic model that the function describes holds. This is the result:

```output
 2021-03-22 04:00:00+08
```

The overload of the function _moment_ss_interval_addition()_ for a plain _timestamp_ moment is a more-or-less mechanical re-write of the overload for the _timestamptz_ moment.

Create it thus. (The function [to_timestamp_without_tz (double precision) returns timestamp](../../interval-utilities/#function-to-timestamp-without-tz-double-precision-returns-timestamp) is defined in the [User-defined _interval_ utility functions](../../interval-utilities/) section.)

```plpgsql
drop function if exists moment_ss_interval_addition(timestamp, interval) cascade;

create function moment_ss_interval_addition(t timestamp, i interval)
  returns timestamp
  language plpgsql
as $body$
declare
  mm_dd_ss constant interval_mm_dd_ss_t := interval_mm_dd_ss(i);
begin
  assert
    mm_dd_ss.ss <> 0 and
    mm_dd_ss.dd =  0 and
    mm_dd_ss.mm =  0,
    'not a pure seconds interval';
  declare
    ss_t     constant double precision not null := extract(epoch from t);
    ss_i     constant double precision not null := mm_dd_ss.ss::double precision;
    i_model  constant timestamp        not null := to_timestamp_without_tz(ss_t + ss_i);

    i_actual constant timestamp        not null := t + i;
  begin
    assert i_model = i_actual, 'assert model does not hold';
    return i_model;
  end;
end;
$body$;
```

Test it like this:

```plpgsql
select moment_ss_interval_addition('2021-02-19 12:00:00'::timestamp, '720 hours'::interval);
```

Once again, the function finishes without an _assert_ violation—showing that a pure seconds _interval_ value was supplied and that the semantic model that the function describes holds. This is the result:

```output
 2021-03-21 12:00:00
```

The overload of the function _moment_ss_interval_addition()_ for a plain _time_ moment is derived from the overload for a plain _timestamp_ moment. Create it thus.  (The function _[to_time (double precision) returns time](../../interval-utilities/#function-to-time-double-precision-returns-time)_ is defined in the [User-defined _interval_ utility functions](../../interval-utilities/) section.)

```plpgsql
drop function if exists moment_ss_interval_addition(time, interval) cascade;

create function moment_ss_interval_addition(t time, i interval)
  returns time
  language plpgsql
as $body$
declare
  mm_dd_ss constant interval_mm_dd_ss_t := interval_mm_dd_ss(i);
begin
  assert
    mm_dd_ss.ss <> 0 and
    mm_dd_ss.dd =  0 and
    mm_dd_ss.mm =  0,
    'not a pure seconds interval';
  declare
    -- no. of secs since midnight
    ss_t     constant double precision not null := extract(epoch from t);
    ss_i     constant double precision not null := mm_dd_ss.ss;

    i_model  constant time             not null := to_time(ss_t + ss_i);

    i_actual constant time             not null := t + i;
  begin
    assert i_model = i_actual, 'assert model does not hold';
    return i_model;
  end;
end;
$body$;
```

Test it like this:

```plpgsql
select moment_ss_interval_addition('17:43:37.123'::time, '10:13:41'::interval);
```

Once again, the function finishes without an _assert_ violation—showing that a pure seconds _interval_ value was supplied and that the semantic model that the function describes holds. This is the result:

```output
 03:57:18.123
```

## The semantics for the moment-interval overloads of "+" and "-" for pure days interval values

{{< tip title="See the 'sensitivity of timestamptz-interval arithmetic to the current timezone' section for a complementary definition of the semantics of arithmetic that uses pure days 'interval' values." >}}
The explanations that this page presents for the semantics of moment-_interval_ arithmetic for the three different kinds of _interval_ (pure seconds, pure days, and pure months) are oriented to the use-cases that motivate the distinctions. The explanation of the pure days semantics is inextricably bound up with the timezone notion and how this, in turn, determines the _UTC offset_.

See the section [The sensitivity of _timestamptz-interval_ arithmetic to the current timezone](../../../../timezones/timezone-sensitive-operations/timestamptz-interval-day-arithmetic/) in the "Timezones and _UTC offsets_" section for a code example and discussion that models the rules in a different, but ultimately equivalent, way from this present subsection's approach. For a complex topic like this, it helps to solidify your mental model by examining relevant scenarios from different angles.
{{< /tip >}}

Notice that it doesn't make sense to add a duration of one or several days to a _time_ value—even though the attempt doesn't cause an error and does produce a result. This section shows only PL/pgSQL models for the _timestamptz_ and _timestamp_ overloads.

Create this function:

```plpgsql
drop function if exists moment_dd_interval_addition(timestamptz, interval) cascade;

create function moment_dd_interval_addition(t timestamptz, i interval)
  returns timestamptz
  language plpgsql
as $body$
declare
  mm_dd_ss constant interval_mm_dd_ss_t := interval_mm_dd_ss(i);
  current_tz text not null := '';
begin
  assert
    mm_dd_ss.ss =  0 and
    mm_dd_ss.dd <> 0 and
    mm_dd_ss.mm =  0,
    'not a pure days interval';
  declare
    time_part    constant text        not null := to_char(t, 'hh24:mi:ss');
    date_part_0  constant date        not null := t::date;
    date_part    constant date        not null := date_part_0 + mm_dd_ss.dd;
    i_model      constant timestamptz not null := (date_part::text||' '||time_part::text)::timestamptz;

    i_actual     constant timestamptz not null := t + i;
  begin
    assert i_model = i_actual, 'assert model does not hold';
    return i_model;
  end;
end;
$body$;
```

Test it thus:

```plpgsql
set timezone = 'Asia/Shanghai';
select moment_dd_interval_addition('2021-02-19 12:00:00 Asia/Shanghai'::timestamptz, '30 days'::interval);
```

The function finishes without an _assert_ violation—showing that a pure days _interval_ value was supplied and that the semantic model that the function describes holds. This is the result:

```output
 2021-03-21 12:00:00+08
```

The PL/pgSQL code, by separating out the time-of-day before adding the number of days, vividly makes the point that the semantics of the moment-_interval_ overloads of `+` and `-` for pure days _interval_ values honors one flavor of _calendar-time_semantics_.

Now test it using a timezone that does not respect Daylight Savings time:

```plpgsql
set timezone = 'Asia/Shanghai';
select moment_dd_interval_addition('2021-02-19 12:00:00 Asia/Shanghai'::timestamptz, '30 days'::interval);
```

Once again, the function finishes without an _assert_ violation—showing that, again, the semantic model that the function describes holds. This is the result:

```output
 2021-03-21 12:00:00+08
```

Notice that when you do the operation and observe the result in a certain timezone (this example happens to implement doing and observing with the same statement—but this does not need generally to be the case), then adding some number of days takes you to the same time of day that many days later. The PL/pgSQL model does exactly this. This rule holds whether or nor the timezone that your session uses respects Daylight Savings Time and the _interval_ spans the "spring forward" moment or the "fall back" moment.

The overload of the function _moment_dd_interval_addition()_ for a plain _timestamp_ moment is a direct re-write of the overload for the _timestamptz_ moment. Simply replace the text "timestamptz" with the text "timestamp" in the function's source code:

```plpgsql
drop function if exists moment_dd_interval_addition(timestamp, interval) cascade;

create function moment_dd_interval_addition(t timestamp, i interval)
  returns timestamp
  language plpgsql
as $body$
declare
  mm_dd_ss constant interval_mm_dd_ss_t := interval_mm_dd_ss(i);
  current_tz text not null := '';
begin
  assert
    mm_dd_ss.ss =  0 and
    mm_dd_ss.dd <> 0 and
    mm_dd_ss.mm =  0,
    'not a pure days interval';
  declare
    time_part    constant text      not null := to_char(t, 'hh24:mi:ss');
    date_part_0  constant date      not null := t::date;
    date_part    constant date      not null := date_part_0 + mm_dd_ss.dd;
    i_model      constant timestamp not null := (date_part::text||' '||time_part::text)::timestamp;

    i_actual     constant timestamp not null := t + i;
  begin
    assert i_model = i_actual, 'assert model does not hold';
    return i_model;
  end;
end;
$body$;
```

Then test it with trivially modified versions of the tests that were used for the _timestamptz_ overload:

```plpgsql
set timezone = 'Asia/Shanghai';
select moment_dd_interval_addition('2021-02-19 12:00:00'::timestamp, '30 days'::interval);

set timezone = 'Asia/Shanghai';
select moment_dd_interval_addition('2021-02-19 12:00:00'::timestamp, '30 days'::interval);
```

You get the same result for each, independently (of course) of what the sessions' _TimeZone_ setting is:

```output
 2021-03-21 12:00:00
```

## The semantics for the moment-interval overloads of "+" and "-" for pure months interval values

Notice that, here too, it doesn't make sense to add a duration of one or several months to a _time_ value—even though the attempt doesn't cause an error and does produce a result. This section shows only PL/pgSQL models for the _timestamptz_ and _timestamp_ overloads.

Create this function:

```plpgsql
drop function if exists moment_mm_interval_addition(timestamptz, interval) cascade;

create function moment_mm_interval_addition(t timestamptz, i interval)
  returns timestamptz
  language plpgsql
as $body$
declare
  mm_dd_ss constant interval_mm_dd_ss_t := interval_mm_dd_ss(i);
  current_tz text not null := '';
begin
  assert
    mm_dd_ss.ss =  0 and
    mm_dd_ss.dd =  0 and
    mm_dd_ss.mm <> 0,
    'not a pure months interval';
  declare
    time_part    constant text        not null := to_char(t, 'hh24:mi:ss');
    date_part_0  constant date        not null := t::date;
    year_0       constant int         not null := extract(year  from date_part_0);
    month_0      constant int         not null := extract(month from date_part_0);
    day_0        constant int         not null := extract(day   from date_part_0);
    year         constant int         not null := year_0  + trunc(mm_dd_ss.mm/12);
    month        constant int         not null := month_0 + mod(mm_dd_ss.mm, 12);
    -- Check that it's a legal date.
    date_part    constant date        not null := year::text||'-'||month::text||'-'||day_0::text;
    i_model      constant timestamptz not null := (date_part::text||' '||time_part::text)::timestamptz;

    i_actual     constant timestamptz not null := t + i;
  begin
    assert i_model = i_actual, 'assert model does not hold';
    return i_model;
  end;
end;
$body$;
```

Test it thus:

```plpgsql
set timezone = 'America/Los_Angeles';
select moment_mm_interval_addition('2021-02-19 12:00:00 America/Los_Angeles'::timestamptz, '1 year 5 months'::interval);
```

The function finishes without an _assert_ violation—showing that a pure months _interval_ value was supplied and that the semantic model that the function describes holds. This is the result::

```output
 2022-07-19 12:00:00-07
```

The PL/pgSQL code, by separating out the time-of-day before adding the number of months, vividly makes the point that the semantics of the moment-_interval_ overloads of `+` and `-` for pure days _interval_ values honors another flavor of _calendar-time-semantics_.

Now test it using a timezone that does not respect Daylight Savings time:

```plpgsql
set timezone = 'Asia/Shanghai';
select moment_mm_interval_addition('2021-02-19 12:00:00 Asia/Shanghai'::timestamptz, '1 year 5 months'::interval);
```

Once again, the function finishes without an _assert_ violation—showing that, again, the semantic model that the function describes holds. This is the result:

```output
 2022-07-19 12:00:00+08
```

Notice that when you do the operation and observe the result in a certain timezone, adding some number of months takes you to the same day of the month and the same time of day that many months later. The PL/pgSQL model does exactly this. Of course, this rule holds whether or not the timezone that your session uses respects Daylight Savings Time"; "spring forward" or "fall back" moments are irrelevant, given the semantic definition. The definition, and the implemented model, make it crystal clear that the number of days between the starting and ending moments for a certain pure months _interval_ value will vary according to the number of days in the various months that the _interval_ happens to span. (The effect of leap years is implied by this statement.)

The overload of the function _moment_mm_interval_addition()_ for a plain _timestamp_ moment is a direct re-write of the overload for the _timestamptz_ moment. Simply replace the text "timestamptz" with the text "timestamp" in the function's source code:

```plpgsql
drop function if exists moment_mm_interval_addition(timestamp, interval) cascade;

create function moment_mm_interval_addition(t timestamp, i interval)
  returns timestamp
  language plpgsql
as $body$
declare
  mm_dd_ss constant interval_mm_dd_ss_t := interval_mm_dd_ss(i);
  current_tz text not null := '';
begin
  assert
    mm_dd_ss.ss =  0 and
    mm_dd_ss.dd =  0 and
    mm_dd_ss.mm <> 0,
    'not a pure months interval';
  declare
    time_part    constant text      not null := to_char(t, 'hh24:mi:ss');
    date_part_0  constant date      not null := t::date;
    year_0       constant int       not null := extract(year  from date_part_0);
    month_0      constant int       not null := extract(month from date_part_0);
    day_0        constant int       not null := extract(day   from date_part_0);
    year         constant int       not null := year_0  + trunc(mm_dd_ss.mm/12);
    month        constant int       not null := month_0 + mod(mm_dd_ss.mm, 12);
    -- Check that it's a legal date.
    date_part    constant date      not null := year::text||'-'||month::text||'-'||day_0::text;
    i_model      constant timestamp not null := (date_part::text||' '||time_part::text)::timestamp;

    i_actual     constant timestamp not null := t + i;
  begin
    assert i_model = i_actual, 'assert model does not hold';
    return i_model;
  end;
end;
$body$;
```

Then test it with trivially modified versions of the tests that were used for the _timestamptz_ overload:

```plpgsql
set timezone = 'America/Los_Angeles';
select moment_mm_interval_addition('2021-02-19 12:00:00'::timestamp, '1 year 5 months'::interval);

set timezone = 'Asia/Shanghai';
select moment_mm_interval_addition('2021-02-19 12:00:00'::timestamp, '1 year 5 months'::interval);
```

You get the same result for each, independently (of course) of what the session's _TimeZone_ setting is:

```output
 2022-07-19 12:00:00
```
