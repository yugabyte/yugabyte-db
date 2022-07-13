---
title: Sensitivity of timestamptz-interval arithmetic to current timezone [YSQL]
headerTitle: Sensitivity of timestamptz-interval arithmetic to the current timezone
linkTitle: Pure 'day' interval arithmetic
description: Explains the sensitivity of timestamptz-interval arithmetic to current timezone for pure days intervals. [YSQL]
menu:
  preview:
    identifier: timestamptz-interval-day-arithmetic
    parent: timezone-sensitive-operations
    weight: 20
type: docs
---

The [moment-moment overloads of the "-" operator for _timestamptz_, _timestamp_, and _time_](../../../date-time-data-types-semantics/type-interval/interval-arithmetic/moment-moment-overloads-of-minus/) section recommends that you avoid arithmetic that uses _hybrid interval_ semantics—in other words that you perform _interval_ arithmetic using only values that have just one of the fields of the internal _[\[mm, dd, ss\]](../../../date-time-data-types-semantics/type-interval/interval-representation/)_ representation tuple non-zero. The section [Custom domain types for specializing the native _interval_ functionality](../../../date-time-data-types-semantics/type-interval/custom-interval-domains/) explains a coding practice that supports this recommendation.

Following the recommendation, this demonstration uses only pure days _interval_ values and pure seconds _interval_ values.

It shows how the outcome of adding or subtracting the day component of a pure days _interval_ value to or from a _timestamptz_ value is critically dependent on the session's _TimeZone_ setting. In particular, the outcome is defined by special rules when, and only when, the starting and resulting _timestamptz_ values straddle the "spring forward" moment using a timezone that respects Daylight Savings Time. (In the same way, special rules apply, too, when, the starting and resulting values straddle the "fall back" moment.

The demonstration shows that, in contrast, the outcome of corresponding arithmetic that uses a pure seconds _interval_ value is independent of the session's _TimeZone_ setting. This is also the case when a pure months _interval_ value is used. But the demonstration's pedagogy doesn't need to illustrate this. Its focus is the special rules for a pure days _interval_ value that crosses a Daylight Savings Time boundary.

## The philosophy of the demonstration's design

When you run a query that selects a _timestamptz_ value at the _ysqlsh_ prompt, you'll see a _text_ rendition whose spelling depends on the session's _TimeZone_ setting. This behavior is critical to the data type's usefulness. But it can confound the interpretation of demonstrations that, like the present one, aim to show what happens to actual internally represented _timestamptz_ values under critical operations. You can adopt the practice always to observe results with a current _TimeZone_ setting of _UTC_. But the most robust test of your understanding is always to use a PL/pgSQL encapsulation that uses _assert_ statement(s) to check that the actual outcome of a test agrees with what your mental model predicts. The demonstration that is presented on this page uses the _assert_ approach. Critically, the entire test uses only _timestamptz_ values (and, of course, _interval_ values) to avoid conflating the outcome with the effects of data type conversions to _text_—supposedly to allow the human to use what is seen to confirm understanding of the rules.

Further, by using a table function encapsulation, the demonstration also displays the results—_as long as the assertions all hold_. It has two display modes:

- Display all the results using _UTC_.
- Display the results that were computed with a session timezone set to _X_ using that same timezone _X_.

To ensure that the starting _timestamptz_ values, and the expected result _timestamptz_ values that the _assert_ statements check, are maximally free of extraneous conversion effects, these are all assigned as _constants_ using the _double precision_ overload of the _to_timestamp()_ built-in function. The input for this overload is the number of seconds from the so-called start of the _epoch_. Do this:

```plpgsql
set timezone = 'UTC';
select
  pg_typeof(to_timestamp(0)) as "data type",
  to_timestamp(0)            as "start of epoch";
```

This is the result:

```output
        data type         |     start of epoch
--------------------------+------------------------
 timestamp with time zone | 1970-01-01 00:00:00+00
```

The demonstration uses, in turn, five starting moments for the _timestamptz_-_interval_ addition tests. The first four are 20:00 on the Saturday evening before the "spring forward" moment in the small hours of the immediately following Sunday morning in a timezone of interest. The timezones are chosen so that two are in the Northern Hemisphere (located one to the west and one to the east of the Greenwich Meridian) and so that two are in the Southern Hemisphere. One of these, relatively unusually, "springs forward" by just thirty minutes. Each of the other three "springs forward" by the much more common amount of one hour. Here's the list. The unusual one is called out,

- _America/Los_Angeles_
- _Europe/Amsterdam_
- _Australia/Sydney_
- _Australia/Lord_Howe_ (_DST_ is, unusually, only 30 min ahead of _Standard Time_)

Internet search easily finds the 2021 "spring forward" moments and amounts for these timezones. And simple tests like this confirm that the facts are correct with respect to YugabyteDB's internal representation of the _[tz database](https://en.wikipedia.org/wiki/Tz_database)_:

```plpgsql
set timezone = 'Australia/Lord_Howe';
select
  to_char('2021-10-03 01:59:59'::timestamptz, 'hh24:mi:ss (UTC offset = TZH:TZM)') as "Before 'spring forward'",
  to_char('2021-10-03 02:30:01'::timestamptz, 'hh24:mi:ss (UTC offset = TZH:TZM)') as "After 'spring forward'";
```

This is the result:

```output
    Before 'spring forward'     |     After 'spring forward'
--------------------------------+--------------------------------
 01:59:59 (UTC offset = +10:30) | 02:30:01 (UTC offset = +11:00)
```

The "spring forward" moment is _02:00_. So 01:59:59 is still in Winter Time, but only just, with a _UTC offset_ of _+10:30_. Somebody in this timezone watching the self-adjusting clock on their smartphone would see it jump, in two seconds of elapsed wall-clock time, from _01:59:59_ to _02:30:01_, showing that Summer Time has now arrived, bringing the new _UTC offset_ of _+11:00_.

The test also uses midsummer's eve in _UTC_ in a control test. By definition, _UTC_ does not respect Daylight Savings Time.

These _ad hoc_ queries determine the seconds from the start of the epoch for the four chosen "spring forward" moments, and for midsummer's eve in _UTC_.

```plpgsql
select
  (select extract(epoch from '2021-03-13 20:00:00 America/Los_Angeles' ::timestamptz)) as "Los Angeles DST start",
  (select extract(epoch from '2021-03-27 20:00:00 Europe/Amsterdam'    ::timestamptz)) as "Amsterdam DST start",
  (select extract(epoch from '2021-10-02 20:00:00 Australia/Sydney'    ::timestamptz)) as "Sydney DST start",
  (select extract(epoch from '2021-10-02 20:00:00 Australia/Lord_Howe' ::timestamptz)) as "Lord Howe DST start",
  (select extract(epoch from '2021-06-23 20:00:00 UTC'                 ::timestamptz)) as "UTC mid-summer";
```

This is the result:

```output
 Los Angeles DST start | Amsterdam DST start | Sydney DST start | Lord Howe DST start | UTC mid-summer
-----------------------+---------------------+------------------+---------------------+----------------
            1615694400 |          1616871600 |       1633168800 |          1633167000 |     1624478400
```

These values are used, as manifest constants, in the test table function's source code. And the reports show that they were typed correctly.

## The demonstration

The demonstration uses the [_interval_arithmetic_results()_](#interval-arithmetic-results) table function. Its design is very similar to that of the [_plain_timestamp_to_from_timestamp_tz()_](../timestamptz-plain-timestamp-conversion/#plain-timestamp-to-from-timestamp-tz) table function, presented in the _"sensitivity of the conversion between timestamptz and plain timestamp to the UTC offset"_ section.

The _interval_arithmetic_results()_ function depends on some helper functions. First create a trivial wrapper for _to_char()_ to improve the readability of the output without cluttering the code by repeating the verbose format mask.

```plpgsql
drop function if exists fmt(timestamptz) cascade;
create function fmt(t in timestamptz)
  returns text
  language plpgsql
as $body$
begin
  return to_char(t, 'Dy dd-Mon hh24:mi TZH:TZM');
end;
$body$;
```

Now create a _type_ to represent the facts about one timezone: _20:00_ on the Saturday evening before the "spring forward" moment; the name of the timezone for which this is the "spring forward" moment; and the size of the "spring forward" amount.

```plppsql
drop type if exists rt cascade;
create type rt as (
  -- On the Saturday evening before the "spring forward" moment
  -- as seconds from to_timestamp(0).
  s double precision,

  -- The timezone in which "s" has its meaning.
  tz  text,

  -- "spring forward" amount in minutes.
  spring_fwd_amt int);
```

<a name="interval-arithmetic-results"></a>

Create and execute the test table function thus. You can easily confirm, with _ad hoc_ tests, that it is designed so that its behavior is independent of the session's _TimeZone_ setting. The design establishes the expected resulting _timestamptz_ values, after adding either _'24 hours'::interval_ or _'1 day'::interval_ to the "spring forward" moments, crossing the Daylight Savings Time transition.

```plpgsql
drop function if exists interval_arithmetic_results(boolean) cascade;

create function interval_arithmetic_results(at_utc in boolean)
  returns table(z text)
  language plpgsql
as $body$
declare
  set_timezone       constant text             not null := $$set timezone = '%s'$$;
  tz_on_entry        constant text             not null := current_setting('timezone');

  secs_pr_hour       constant double precision not null := 60*60;

  interval_24_hours  constant interval         not null := '24 hours';
  interval_1_day     constant interval         not null := '1 day';
begin
  z := '--------------------------------------------------------------------------------';        return next;
  if at_utc then
    z := 'Displaying all results using UTC.';                                                     return next;
  else
    z := 'Displaying each set of results using the timezone in which they were computed.';        return next;
  end if;
  z := '--------------------------------------------------------------------------------';        return next;

  declare
    -- 20:00 (local time) on the Saturday before the "spring forward" moments in a selection of timezones.
    r                      rt not null   := (0, '', 0);
    start_moments constant rt[] not null := array [
                                                    (1615694400, 'America/Los_Angeles', 60)::rt,
                                                    (1616871600, 'Europe/Amsterdam',    60)::rt,
                                                    (1633168800, 'Australia/Sydney',    60)::rt,
                                                    (1633167000, 'Australia/Lord_Howe', 30)::rt,

                                                    -- Nonce element. Northern midsummer's eve.
                                                    (1624478400, 'UTC',                  0)::rt
                                                  ];
  begin
    foreach r in array start_moments loop
      execute format(set_timezone, r.tz);
      declare
        t0                         constant timestamptz not null := to_timestamp(r.s);
        t0_plus_24_hours           constant timestamptz not null := t0 + interval_24_hours;
        t0_plus_1_day              constant timestamptz not null := t0 + interval_1_day;

        expected_t0_plus_24_hours  constant timestamptz not null := to_timestamp(r.s + 24.0*secs_pr_hour);

        expected_t0_plus_1_day     constant timestamptz not null :=
          case r.spring_fwd_amt
            when 60 then                                          to_timestamp(r.s + 23.0*secs_pr_hour)
            when 30 then                                          to_timestamp(r.s + 23.5*secs_pr_hour)
            when  0 then                                          to_timestamp(r.s + 24.0*secs_pr_hour)
          end;
      begin
        assert
          t0_plus_24_hours = expected_t0_plus_24_hours,
        'Bad "t0_plus_24_hours"';

        assert
          t0_plus_1_day = expected_t0_plus_1_day,
        'Bad "t0_plus_1_day"';

        /* Display the internally represented values:
             EITHER: using 'UTC' to show what they "really" are
             OR:     using the timezone in which they were computed to show
                     the intended usability benefit for the local observer. */
        if at_utc then
          execute format(set_timezone, 'UTC');
         -- Else, leave the timezone set to "r.tz".
        end if;

        z := r.tz;                                                                                  return next;
        z := '';                                                                                    return next;
        z := 't0:               '||fmt(t0);                                                         return next;
        z := 't0_plus_24_hours: '||fmt(t0_plus_24_hours);                                           return next;
        z := 't0_plus_1_day:    '||fmt(t0_plus_1_day);                                              return next;
        z := '--------------------------------------------------';                                  return next;
      end;
    end loop;
  end;

  execute format(set_timezone, tz_on_entry);
end;
$body$;

select z from interval_arithmetic_results(true);
```

This is the result:

```output
 --------------------------------------------------------------------------------
 Displaying all results using UTC.
 --------------------------------------------------------------------------------
 America/Los_Angeles

 t0:               Sun 14-Mar 04:00 +00:00
 t0_plus_24_hours: Mon 15-Mar 04:00 +00:00
 t0_plus_1_day:    Mon 15-Mar 03:00 +00:00
 --------------------------------------------------
 Europe/Amsterdam

 t0:               Sat 27-Mar 19:00 +00:00
 t0_plus_24_hours: Sun 28-Mar 19:00 +00:00
 t0_plus_1_day:    Sun 28-Mar 18:00 +00:00
 --------------------------------------------------
 Australia/Sydney

 t0:               Sat 02-Oct 10:00 +00:00
 t0_plus_24_hours: Sun 03-Oct 10:00 +00:00
 t0_plus_1_day:    Sun 03-Oct 09:00 +00:00
 --------------------------------------------------
 Australia/Lord_Howe

 t0:               Sat 02-Oct 09:30 +00:00
 t0_plus_24_hours: Sun 03-Oct 09:30 +00:00
 t0_plus_1_day:    Sun 03-Oct 09:00 +00:00
 --------------------------------------------------
 UTC

 t0:               Wed 23-Jun 20:00 +00:00
 t0_plus_24_hours: Thu 24-Jun 20:00 +00:00
 t0_plus_1_day:    Thu 24-Jun 20:00 +00:00
 --------------------------------------------------
```

The execution finishes without error, confirming that the assertions hold.

## Interpretation and statement of the rules

Recall that when a _timestamptz_ value is observed using _UTC_, you see the actual _yyyy-mm-dd hh24:mi:ss_ value that the internal representation holds.

You can see clearly that the rule for adding the pure seconds _'24 hours'::interval_ value is unremarkable. _Clock-time-semantics_ is used to produce a value that is simply exactly _24_ hours later than the starting _timestamptz_ value. On the other hand, the resulting _timestamptz_ values when a pure days _'1 day'::interval_ value is used follow these rules—according, critically, to which timezone is the session's current value:

- If, in the reigning timezone, the addition does not cross a Daylight Savings Time transition, then the result is given simply by adding _24_ hours, just as it is when a pure seconds _interval_ value is used.

- If, in the reigning timezone, the addition _does_ cross the "spring forward" moment, then the result is given by adding _less than_ _24_ hours. The delta is equal to the size of the "spring forward" amount.

In other words, when _timestamptz-interval_ arithmetic uses a pure days _interval_ value in a current timezone that causes crossing the Daylight Savings Time transition, the resulting _timestamptz_ value is calculated using _calendar-time-semantics_. The rule to add less than _24_ hours aligns exactly with the human experience. If you go to bed at your normal time on the Saturday evening before the "spring forward" moment (in a region whose timezone observes Daylight Savings Time with a one hour "spring forward" amount), and if you get up after your normal number of hours in bed, then the self-adjusting clock on your smart phone will read _one hour later_ than it usually does—hence the mnemonic "spring _forward_". In other words, you'll experience a waking day on the Sunday that's _one hour shorter_ than usual—just _twenty-three_ hours.

You might find that the displayed results feel counter-intuitive until you've fully grasped all the central concepts here. But things usually feel satisfyingly natural when you observe the very same results using the timezone that was in force when the _interval_ arithmetic was performed.

Invoke the table function again to show the results this way—in other words, to emphasize the intended usability benefit, for the local observer, of the special rules for pure days _interval_ arithmetic:

```plpgsql
select z from interval_arithmetic_results(false);
```

This is the new result:

```output
 --------------------------------------------------------------------------------
 Displaying each set of results using the timezone in which they were computed.
 --------------------------------------------------------------------------------
 America/Los_Angeles

 t0:               Sat 13-Mar 20:00 -08:00
 t0_plus_24_hours: Sun 14-Mar 21:00 -07:00
 t0_plus_1_day:    Sun 14-Mar 20:00 -07:00
 --------------------------------------------------
 Europe/Amsterdam

 t0:               Sat 27-Mar 20:00 +01:00
 t0_plus_24_hours: Sun 28-Mar 21:00 +02:00
 t0_plus_1_day:    Sun 28-Mar 20:00 +02:00
 --------------------------------------------------
 Australia/Sydney

 t0:               Sat 02-Oct 20:00 +10:00
 t0_plus_24_hours: Sun 03-Oct 21:00 +11:00
 t0_plus_1_day:    Sun 03-Oct 20:00 +11:00
 --------------------------------------------------
 Australia/Lord_Howe

 t0:               Sat 02-Oct 20:00 +10:30
 t0_plus_24_hours: Sun 03-Oct 20:30 +11:00
 t0_plus_1_day:    Sun 03-Oct 20:00 +11:00
 --------------------------------------------------
 UTC

 t0:               Wed 23-Jun 20:00 +00:00
 t0_plus_24_hours: Thu 24-Jun 20:00 +00:00
 t0_plus_1_day:    Thu 24-Jun 20:00 +00:00
 --------------------------------------------------
```

From this perspective, adding one day takes you to the same wall-clock time on the next day. But watching a stop watch until it reads twenty-four hours, takes you to the next day at a moment where the wall-clock reads _one hour_ (or _thirty minutes_ in one of the unusual timezones) _later_ than when you started the stop watch.

{{< tip title="Observe what happens at the 'fall back' moments" >}}
You might like to redefine the _start_moments_ array in the _interval_arithmetic_results()_ function's source code to use the "fall back" moments for each of the timezones. Internet search finds these easily. Doing this will show you that pure days _interval_ arithmetic semantics respects the feeling you get on the Sunday after the transition that you have one hour _more_ than usual of waking time—hence the mnemonic "fall _back_".

The resulting _timestamptz_ values when a pure days _'1 day'::interval_ value is used follow these rules—according, critically, to which timezone is the session's current value:

- If, in the reigning timezone, the addition does not cross a Daylight Savings Time transition, then the result is given simply by adding _24_ hours, just as it is when a pure seconds _interval_ value is used.
- If, in the reigning timezone, the addition _does_ cross the "fall back" moment, then the result is given by adding _more than_ _24_ hours. The delta is equal to the size of the "fall back" amount.
{{< /tip >}}
