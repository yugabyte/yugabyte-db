---
title: The plain timestamp and timestamptz data types [YSQL]
headerTitle: The plain timestamp and timestamptz data types
linkTitle: Plain timestamp and timestamptz
description: The semantics of the plain timestamp and timestamptz data types. [YSQL]
menu:
  stable:
    identifier: type-timestamp
    parent: date-time-data-types-semantics
    weight: 30
type: docs
---

{{< tip title="Understanding this section depends on understanding the 'Timezones' section." >}}
To understand the _timestamptz_ data type, and converting its values to/from plain _timestamp_ values, you need to understand what the [Timezones and _UTC offsets_](../../timezones/) section explains.
{{< /tip >}}

The plain _timestamp_ data type and the _timestamptz_ data type are cousins. But there are critical differences:

- Both a plain _timestamp_ datum and a _timestamptz_ datum have the identical internal representation. You can picture it as the real number of seconds (with microsecond precision) from a reference moment (_12:00_ on _1-Jan-1970_, _UTC_). The _extract(epoch from t)_ function, where _t_ is either a plain _timestamp_ value or a _timestamptz_ value, returns this number. Moreover, the result is independent of the session's current _TimeZone_ setting for both of these data types. (See the subsection [Interpretation and statement of the rules](#interpretation-and-statement-of-the-rules) below.)
- The difference is in the _metadata_ that describes the datum: each knows which kind it is. And the difference is significant when a datum is recorded or read back.

You need a clear understanding of the differences so that you can make the appropriate choice between these two data types according to the use case.

{{< tip title="You need a very good reason to prefer plain 'timestamp' to 'timestamptz'." >}}
You should definitely consider _timestamptz_ to be your default choice for a persistent representation. And you should be able to write down a clear account of your reasoning when you decide that plain _timestamp_ is the better choice in the present application design context.

For example, you might choose plain _timestamp_ to send the representation of a moment of interest to a client (or to receive it back from a client) where the scenario in which the value is used has pre-defined the reigning timezone as a defining property for the ongoing scenario. Here, you'd convert from/to the ultimate _timestamptz_ value to/from plain _timestamp_ at the timezone of interest and then you'd convert this to/from _text_, using the specified representation format, to send to, or receive from, the client.
{{< /tip >}}

## The plain timestamp data type

Plain _timestamp_ values represent the date and the time-of-day of some moment in a [local time](../../conceptual-background/#wall-clock-time-and-local-time) regime. There is no timezone-sensitivity, neither when a plain _timestamp_ value is created, nor when it is read. Such a value therefore represents a moment at some unspecified location—just as a clockwork wall-clock that shows the date does. You can picture a _timestamp_ value as the number of microseconds to the present moment from the start of some epoch.

Because there's no timezone-sensitivity, there's no sensitivity to Daylight Savings regimes either: every day runs from midnight (inclusive) through the next midnight (exclusive). (PostgresSQL, and therefore YSQL, don't support [leap seconds](https://www.timeanddate.com/time/leapseconds.html).) However the same quirk that allows _'24:00:00'_ as a _time_ value allows this as the time-of-day component when you specify a _timestamp_ value. Try this:

```plpgsql
select
  ('2021-02-14 00:00:00.000000'::timestamp)::text as "starting midnight",
  ('2021-02-14 23:59:59.999999'::timestamp)::text as "just before ending midnight",
  ('2021-02-14 24:00:00.000000'::timestamp)::text as "ending midnight";
```

This is the result:

```output
  starting midnight  | just before ending midnight |   ending midnight
---------------------+-----------------------------+---------------------
 2021-02-14 00:00:00 | 2021-02-14 23:59:59.999999  | 2021-02-15 00:00:00
```

The fact that this is allowed is less of an annoyance than it is with _time_ values because, now that the date is part of the picture, the obvious convention can be used to render a time-of-day component given as _'24:00:00'_ on one day as _'00:00:00'_ on the next day. This convention has a knock on effect on the result of subtracting one _timestamp_ value from another _timestamp_ value. Try this:

```plpgsql
select
  ('2021-02-14 24:00:00'::timestamp - '2021-02-14 00:00:00'::timestamp)::text as "the interval";
```

This is the result:

```output
 the interval
--------------
 1 day
```

## The timestamptz data type

A _timestamptz_ value represents an [absolute](../../conceptual-background/#absolute-time-and-the-utc-time-standard) time. Such a value can therefore be deterministically expressed as the [local time](../../conceptual-background/#wall-clock-time-and-local-time) in the _UTC_ timezone—or, more carefully stated, as the local time at an offset of _'0 seconds'::interval_ with respect to the _UTC Time Standard_.

Though the representations of an actual plain _timestamp_ value and an actual _timestamptz_ value are the same, the semantics, and therefore the _interpretation_, is different for a _timestamptz_ value than for a plain _timestamp_ value because of the difference in the _metadata_ of the values.

- When a _timestamptz_ value is assigned, the _UTC offset_ must also be specified. Once a _timestamptz_ value has been recorded (for example, in a table column with that data type), the representation doesn't remember what the offset was at the moment of recording. That information is entirely consumed when the incoming value is normalized to _UTC_.
- When a _timestamptz_ value is converted to a _text_ value for display (either implicitly using the _::text_ typecast or explicitly using the _to_char()_ built-in function), the conversion normalizes the date-time component so that it shows the local time with respect to the _UTC offset_ that the current timezone specifies.

The _UTC offset_ may be specified implicitly (using the session's current _TimeZone_ setting) or explicitly—either within the text of the _timestamptz_ literal, or using the _at time zone_ operator. Further, the specification of the offset may be explicit, as an _interval_ value, or implicit using the _name_ of a timezone. When a timezone observes Daylight Savings Time, it's name denotes different offsets during the Standard Time period and the Summer Time period.

The rules for this, and examples that show all of the possible ways to assign a _timestamptz_ value, are given in the section [Timezones and _UTC offsets_](../../timezones/) and its subsections.

### A small illustration

Create a test table with a *timestamptz* column, insert one row, and view the result using, successively, two different values for the session's current timezone setting.


```plpgsql
drop table if exists t cascade;
create table t(k int primary key, v timestamptz not null);

-- This setting has no effect on the inserted value.
set timezone = 'Europe/Paris';
insert into t(k, v) values(1, '2021-02-14 13:30:35+03:00'::timestamptz);

set timezone = 'America/Los_Angeles';
select v::text as "timestamptz value" from t where k = 1;

set timezone = 'Asia/Shanghai';
select v::text as "timestamptz value" from t where k = 1;
```

This is the result of the first query:

```output
 2021-02-14 02:30:35-08
```

And this is the result of the second query:

```output
 2021-02-14 18:30:35+08
```

This outcome needs careful interpretation. It turns out that, using ordinary SQL, there is no _direct_ way to inspect what is actually held by the internal representation as an easily-readable _date-time_ value.

- You can of course, apply the _at time zone 'UTC'_ operator to the value of interest. But this implies understanding what you see in the light of a rule that you must accept. And this section aims to demonstrate that the rule in question is correct, given that you know already what value is internally represented—in other words, you'd be "proving" that you understand correctly by assuming that you have!
- The better way is to use the _extract(epoch from timestamptz_value)_ function. But even this requires an act of faith (or lots of empirical testing): you must be convinced that the result of _extract()_ here is not sensitive to the current _TimeZone_ setting. The [demonstration](#the-demonstration) shows that you can indeed rely on this.

Usually, you "see" the value represented only indirectly. In the present case—for example as the _::text_ typecast of the value. And the evaluation of this typecast _is_ sensitive to the current _TimeZone_ setting.

You can readily understand that the three values _'2021-02-14 13:30:35+03:00'_, _' 2021-02-14 02:30:35-08'_, and _'2021-02-14 18:30:35+08'_ all represent the very same actual absolute time. This is like the way that _(10 + 1)_ and _(14 - 3)_ both represent the same value. However, the apparent redundancy brought by the possibility of many different _::text_ typecasts of the same underlying value is useful for human readability in a scenario like this:

> We'll talk next Tuesday at _08:00 my time_ (i.e. _UTC-8_)—in other words _17:00  your time_ (i.e. _UTC+1_).

The meeting partners both have a background knowledge of their timezone. But the important fact for each, for the day of the meeting, is what time to set the reminder on their clock (which setting is done only in terms of the local time of day): respectively _08:00_ and _17:00_.

Notice that when a timezone respects Daylight Savings Time, this is taken account of just like it is in the example above.

### A minimal simulation of a calendar application

Consider this scenario:

- Rickie, who lives in Los Angeles, has constraints set by her family—and she controls the meeting. She can manage only eight o'clock in the morning. It's unimportant to her whether Daylight Savings Time is in force or not because her local constraining events (like when school starts) are all fixed in local time—and only eight in the morning local time works for her. She needs to fix two Tuesday meetings that happen to straddle the "spring forward" moment in Los Angeles—and then to see each listed as at eight o'clock in _her_ online calendar.
- Vincent, who lives in Amsterdam, needs to see when these meetings will take place in _his_ online calendar.

Simulate a very specialized calendar application that deals with just this single scenario. (Never mind that it's unrealistic. It makes the desired teaching point.)

```plpgsql
-- This is done when the app is installed.
drop table if exists meetings cascade;
create table meetings(k int primary key, t timestamptz);

deallocate all;

prepare cr_mtg(int, text, int, text) as
insert into meetings (k, t) values
  ($1, $2::timestamptz),
  ($3, $4::timestamptz);

prepare qry_mtg as
select
  k                                                                  as "Mtg",
  to_char(t, 'Dy hh24-mi on dd-Mon-yyyy TZ ["with offset" TZH:TZM]') as "When"
from meetings
order by k;
```

Now simulate Rickie creating the meetings. She has the timezone _America/Los_Angeles_ set in her calendar preferences:

```plpgsql
set timezone = 'America/Los_Angeles';
execute cr_mtg(
  1, '2021-03-09 08:00:00',
  2, '2021-03-16 08:00:00');
execute qry_mtg;
```

This is Rickie's result:

```output
 Mtg |                       When
-----+---------------------------------------------------
   1 | Tue 08-00 on 09-Mar-2021 PST [with offset -08:00]
   2 | Tue 08-00 on 16-Mar-2021 PDT [with offset -07:00]
```

Rickie gets a gentle reminder that the meetings do happen to straddle her "spring forward" moment. But she doesn't really care. She confirms that each meeting will happen at eight.

Now simulate Vincent attending the meetings. He has the timezone _Europe/Amsterdam_ set in his calendar preferences:

```plpgsql
set timezone = 'Europe/Amsterdam';
execute qry_mtg;
```

This is Vincent's result:

```output
 Mtg |                       When
-----+---------------------------------------------------
   1 | Tue 17-00 on 09-Mar-2021 CET [with offset +01:00]
   2 | Tue 16-00 on 16-Mar-2021 CET [with offset +01:00]
```

Because Europe's "spring forward" moment is two weeks after it is in the US, Vincent sees that the second meeting is one hour earlier than the first while the timezone specification is unchanged. If he doesn't know that the US is out of step on Daylight Savings Time, he might think that Rickie has simply done this on a whim. But this hardly matters: he knows when he has to attend each meeting.

Imagine trying to write the logic that brings the correct, and useful, functionality that the code above demonstrates if you used the bare _timestamp_ data type. The code would be voluminous, obscure, and very likely to be buggy. In contrast, the _timestamptz_ data type brought the required functionality with no application code except to set the timezone specifically for each user.

Another way to "see" a _timestamptz_ value is to compare it with what you reason it will be. Try this:

```plpgsql
drop table if exists t cascade;
create table t(k int primary key, v timestamptz not null);

set timezone = 'Europe/Helsinki';
-- The timezone of the inserted value is set implicitly.
insert into t(k, v) values(1, '2021-02-14 13:30:35'::timestamptz);
select (
    (select v from t where k = 1) = '2021-02-14 11:30:35 UTC'::timestamptz
  )::text;
```

The result is _true_.

### More Daylight Savings Time examples

The US recognizes Daylight Savings Time. It starts, in 2021, in the 'America/Los_Angeles' zone, on 14-Mar at 02:00. Watch what a clock that automatically adjusts according to Daylight Savings Time start/end moments (like on a smartphone) does. It goes from _'01:59:59'_ to _'03:00:00'_. Try this:

```plpgsql
set timezone = 'America/Los_Angeles';
select
  '2021-03-14 01:30:00 America/Los_Angeles'::timestamptz as "before",
  '2021-03-14 02:30:00 America/Los_Angeles'::timestamptz as "wierd",
  '2021-03-14 03:30:00 America/Los_Angeles'::timestamptz as "after";
```

This is the result:

```output
         before         |         wierd          |         after
------------------------+------------------------+------------------------
 2021-03-14 01:30:00-08 | 2021-03-14 03:30:00-07 | 2021-03-14 03:30:00-07
```

<a name="just-after-fall-back"></a>The value in the column with the alias _"weird"_ is weird because _'2021-03-14 02:30:00'_ doesn't exist. The design could have made the attempt to set this cause an error. But it was decided that it be forgiving.

Daylight Savings Time in the America/Los_Angeles timezone ends, in 2021, on 7-Nov at 02:00:00. Watch what a clock that automatically adjusts according to Daylight Savings Time start/end does now. It falls back from _'01:59:59'_ to _'01:00:00'_. This means that, for example, _'01:30:00'_ on 7-Nov is ambiguous. If you ring your room-mate latish on Saturday evening 6-Nov to say that you'll won't be back home until the small hours, probably about one-thirty, they won't know what you mean because the clock will read this time _twice_. It's easiest to see this in reverse. Try this:

```plpgsql
set timezone = 'America/Los_Angeles';
select
  to_char('2021-11-07 08:30:00 UTC'::timestamptz, 'hh24:mi:ss TZ (OF)') as "1st 1:30",
  to_char('2021-11-07 09:30:00 UTC'::timestamptz, 'hh24:mi:ss TZ (OF)') as "2nd 1:30";
```

This is the result:

```output
      1st 1:30      |      2nd 1:30
--------------------+--------------------
 01:30:00 PDT (-07) | 01:30:00 PST (-08)
```

So you have to tell them that you'll be back in an elaborate way:

- _either_ at about one-thirty _PDT_ (or about one-thirty _PST_)
- _or equivalently_ at about one-thirty _before_ the fall back moment (or one-thirty _after_ the fall back moment)
- _or_ at about one-thirty _UTC-7_ (or about one-thirty _UTC-8_)
- _or even_ at about eight-thirty _UTC_ (or nine-thirty _UTC_).

Confused? Your room-mate soon will be!

This strange (but logically unavoidable) consequence of observing Daylight Savings Time means that you have to be careful in the other direction too. Try this:

```plpgsql
select
  to_char(('2021-11-07 01:30:00 America/Los_Angeles'::timestamptz) at time zone 'UTC', 'hh24:mi:ss') as "Ambiguous";
```

This is the result:

```output
 Ambiguous
-----------
 09:30:00
```

PostgreSQL (and therefore YSQL) resolve the ambiguity by convention: the later moment is chosen. Express what you mean explicitly to avoid the ambiguity:

```plpgsql
set timezone = 'UTC';
select
  to_char('2021-11-07 01:30:00 -07:00'::timestamptz, 'hh24:mi:ss TZ') as "Before fallback",
  to_char('2021-11-07 01:30:00 -08:00'::timestamptz, 'hh24:mi:ss TZ') as "After fallback";
```

This is the result:

```output
 Before fallback | After fallback
-----------------+----------------
 08:30:00 UTC    | 09:30:00 UTC
```

Or, according to the display that best suits your purpose, you might do this instead:

```plpgsql
set timezone = 'America/Los_Angeles';
select
  to_char('2021-11-07 01:30:00 -07:00'::timestamptz, 'hh24:mi:ss TZ') as "Before fallback",
  to_char('2021-11-07 01:30:00 -08:00'::timestamptz, 'hh24:mi:ss TZ') as "After fallback";
```

This is the now result:

```output
 Before fallback | After fallback
-----------------+----------------
 01:30:00 PDT    | 01:30:00 PST
```

{{< tip title="The mapping 'abbrev' to 'utc_offset' in the 'pg_timezone_names' view isn't unique." >}}
Try this:

```plpgsql
select name, abbrev, lpad(utc_offset::text, 9) as "UTC offset"
from pg_timezone_names
where abbrev in ('PST', 'PDT')
order by utc_offset;
```

This is the result:

```output
         name         | abbrev | UTC offset
----------------------+--------+------------
 Canada/Yukon         | PDT    | -07:00:00
 ...
 America/Los_Angeles  | PDT    | -07:00:00
 ...
 Canada/Pacific       | PDT    | -07:00:00


 Asia/Manila          | PST    |  08:00:00
```

Some results were elided. The blank lines were added manually to improve the readability.

However, the _[name, abbrev]_ tuple _does_ uniquely identify a _utc_offset_ value.

You might be tempted to write _PDT_ and _PST_ in the example above in place of _-07:00_ and _-08:00_. That would work, in this specific use, but it won't work reliably in general because the abbreviations are looked up internally in _pg_timezone_abbrevs.abbrev_. But such abbreviations are never looked up in _pg_timezone_names.abbrev_. However, some abbreviations are found _only_ in  _pg_timezone_names.abbrev_. This can confuse the application programmer and lead, in some unfortunate circumstances, even to wrong results. The complex rules in this space are explained in the section [Rules for resolving a string that's intended to identify a _UTC offset_](../../timezones/ways-to-spec-offset/name-res-rules/).

Yugabyte recommends that you program defensively to avoid these pitfalls and follow the approach described in the section [Recommended practice for specifying the _UTC offset_](../../timezones/recommendation/).
{{< /tip >}}

## Demonstrating the rule for displaying a timestamptz value in a timezone-insensitive way

The code blocks above, and especially those in the section [More Daylight Savings Time examples](#more-daylight-savings-time-examples), are just that: _examples_ that show the functional benefit that the _timestamptz_ data type brings. The outcomes that are shown accord with intuition. But, so that you can write reliable application code, you must also understand the _rules_ that explain, and let you reliably predict, these beneficial outcomes.

### The philosophy of the demonstration's design

The demonstration uses the [_timestamptz_vs_plain_timestamp()_](#timestamptz-vs-plain-timestamp) table function. The overall shape of this is very similar to that of the table function
[_plain_timestamp_to_from_timestamp_tz()_](../../timezones/timezone-sensitive-operations/timestamptz-plain-timestamp-conversion/#plain-timestamp-to-from-timestamp-tz) presented in the _"sensitivity of the conversion between timestamptz and plain timestamp to the UTC offset"_ section.

The demonstration does two things:

- It shows that the result produced by the _extract(epoch from timestamp-[tz]-value)_ function is insensitive to the session's _Time_Zone_ setting—for both data types.

- It tests the correctness of tersest statement the underlying semantic rules for [the _text_ to _timestamptz_](../../typecasting-between-date-time-values/#text-to-timestamptz) conversion.

The demonstration that follows is designed like this:

- A table function, _timestamptz_vs_plain_timestamp()_, is used to enable a convenient "running commentary" visualization.

- Two _constant_ values, one with data type plain _timestamp_ and one with data type _timestamptz_ are initialized so that the internal representations (as opposed to the metadata) are identical. Look:

  ```output
  ts_plain    constant timestamp   not null := make_timestamp  (yyyy, mm, dd, hh, mi, ss);
  ts_with_tz  constant timestamptz not null := make_timestamptz(yyyy, mm, dd, hh, mi, ss, 'UTC');
  ```

- Each uses the same _constant int_ values, _yyyy_, _mm_, _dd_, _hh_, _mi_, and _ss_, to define the identical _date-and-time_ part for each of the two moments. The fact that _UTC_ is used for the _timezone_ argument of the _make_timestamptz()_ invocation ensures the required identity of the internal representations of the two moments—actually, both as plain _timestamp_ values.

- The _extract(epoch from ... )_ function is used to get the numbers of seconds, as _constant double precision_ values, from the start of the epoch for the two moment values. An _assert_ statement confirms that these two numbers of seconds are identical.

- A _constant_ array,  _timezones_, is populated by this query:

  ```plpgsql
  select name
  from pg_timezone_names
  where name like 'Etc/GMT%'
  and utc_offset <> make_interval()
  order by utc_offset;
  ```

- The query produces timezones that are listed on the _[synthetic timezones](../../timezones/extended-timezone-names/canonical-no-country-no-dst/)_ page. This is a convenient way to define a set of _UTC offset_ values, independently of when during the winter or summer you execute the query, that span the range from _-12:00_ to _+14:00_ in steps of _one hour_.

- A _foreach_ loop is run thus:

  ```output
  foreach z in array timezones loop
  ```

- At each loop iteration:
  - The session's _TimeZone_ setting is set to the value that the iterand, _z_, specifies.
  - The _extract(epoch from ... )_ function is used again on the reference plain _timestamp_ and _timestamptz_ values (_ts_plain_, and _ts_with_tz_) at the present _TimeZone_ setting. An _assert_ statement shows that these newly-extracted values are always identical to the initially extracted values—in other words that they are unaffected by that setting. Notice that the initially extracted values were obtained at whatever the session's _TimeZone_ setting happens to be on entry to the function.

  - Using the _utc_offset()_ user-defined function (it looks up the _UTC offset_ for the timezone _z_ in the _pg_timezone_names_ catalog view) these values are obtained:

    ```output
    t1             double precision := extract(epoch from ts_plain);
    t2             double precision := extract(epoch from ts_with_tz);
    tz_of_timezone interval         := utc_offset(z);
    tz_display     interval         := to_char(ts_with_tz, 'TZH:TZM');
    ts_display     timestamp        := to_char(ts_with_tz, 'yyyy-mm-dd hh24:mi:ss');
    delta          interval         := ts_display - ts_plain;
    ```

  - These _assert_ statements are executed:

    ```output
    assert (t1 = ts_plain_epoch),         'Assert #1 failed';
    assert (t2 = ts_with_tz_epoch),       'Assert #2 failed';
    assert (tz_display = tz_of_timezone), 'Assert #3 failed';
    assert (tz_display = delta),          'Assert #4 failed';
    ```

  - Running commentary output is generated thus:

    ```output
    report_line(z, ts_plain, ts_with_tz);
    ```

- Finally, after the loop completes and before exiting, the session's _TimeZone_ setting is restored to the value that it had on entry to the function. (It's always good practice to do this for any settings that your programs need, temporarily, to change.)

### The demonstration

The _timestamptz_vs_plain_timestamp()_ table function uses two helper functions. The function _utc_offset()_ gets the _UTC offset_, as an _interval_ value, for the specified timezone thus:

```plpgsql
drop function if exists utc_offset(text) cascade;

create function utc_offset(tz_name in text)
  returns interval
  language plpgsql
as $body$
declare
  i constant interval not null := (
                                    select a.utc_offset from
                                    pg_timezone_names a
                                    where a.name = tz_name
                                  );
begin
  return i;
end;
$body$;
```

The function _report_line()_ formats the name of the session's current _TimeZone_ setting and the reference _constant timestamp_ and _constant timestamptz_ values (_ts_plain_ and _ts_with_tz_) for maximally easily readable output.

**Critical comment:** It's this formatting action that demonstrates the sensitivity of the display of a _timestamptz_ value to the value of the reigning _UTC offset_.

```plpgsql
drop function if exists report_line(text, timestamp, timestamptz) cascade;
create function report_line(z in text, ts_plain in timestamp, ts_with_tz in timestamptz)
  returns text
  language plpgsql
as $body$
declare
  t constant text not null :=
      rpad(z, 15)||'   '||
      rpad(to_char(ts_plain,   'Dy hh24:mi'        ),  9)||'   '||
      rpad(to_char(ts_with_tz, 'Dy hh24:mi TZH:TZM'), 16);
begin
  return t;
end;
$body$;
```

<a name="timestamptz-vs-plain-timestamp"></a>Create and execute the _timestamptz_vs_plain_timestamp()_ table function thus:

```plpgsql
drop function if exists timestamptz_vs_plain_timestamp() cascade;
create function timestamptz_vs_plain_timestamp()
  returns table(t text)
  language plpgsql
as $body$
declare
  set_timezone      constant text             not null := $$set timezone = '%s'$$;
  tz_on_entry       constant text             not null := current_setting('timezone');

  yyyy              constant int              not null := 2000;
  mm                constant int              not null := 1;
  dd                constant int              not null := 1;
  hh                constant int              not null := 11;
  mi                constant int              not null := 0;
  ss                constant int              not null := 0;
  ts_plain          constant timestamp        not null := make_timestamp  (yyyy, mm, dd, hh, mi, ss);
  ts_with_tz        constant timestamptz      not null := make_timestamptz(yyyy, mm, dd, hh, mi, ss, 'UTC');
  ts_plain_epoch    constant double precision not null := extract(epoch from ts_plain);
  ts_with_tz_epoch  constant double precision not null := extract(epoch from ts_with_tz);
begin
  assert (ts_with_tz_epoch = ts_plain_epoch), 'Assert "ts_with_tz_epoch = ts_plain_epoch" failed';

  t := rpad('Timezone', 15)||'   '||rpad('ts_plain', 9)||'   '||rpad('ts_with_tz',  16);          return next;
  t := rpad('-', 15, '-')  ||'   '||rpad('-',  9, '-') ||'   '||rpad('-', 16, '-');               return next;

  declare
    z                  text   not null := '';
    timezones constant text[] not null := (
                                            select array_agg(name order by utc_offset)
                                            from pg_timezone_names
                                            where name like 'Etc/GMT%'
                                            and utc_offset <> make_interval()
                                          );
  begin
    z := 'UTC';
    execute format(set_timezone, z);
    t := report_line(z, ts_plain, ts_with_tz);                                                    return next;
    t := '';                                                                                      return next;

    foreach z in array timezones loop
      execute format(set_timezone, z);
      declare
        t1              constant double precision not null := extract(epoch from ts_plain);
        t2              constant double precision not null := extract(epoch from ts_with_tz);
        tz_of_timezone  constant interval         not null := utc_offset(z);
        tz_display      constant interval         not null := to_char(ts_with_tz, 'TZH:TZM');
        ts_display      constant timestamp        not null := to_char(ts_with_tz, 'yyyy-mm-dd hh24:mi:ss');
        delta           constant interval         not null := ts_display - ts_plain;
      begin
        assert (t1 = ts_plain_epoch),          'Assert #1 failed';
        assert (t2 = ts_with_tz_epoch),        'Assert #2 failed';
        assert (tz_display = tz_of_timezone), 'Assert #3 failed';
        assert (tz_display = delta),            'Assert #4 failed';
      end;
      t := report_line(z, ts_plain, ts_with_tz);                                                  return next;
    end loop;
  end;

  execute format(set_timezone, tz_on_entry);
end;
$body$;

select t from timestamptz_vs_plain_timestamp();
```

This is the result:

```output
Timezone          ts_plain    ts_with_tz
---------------   ---------   ----------------
UTC               Sat 11:00   Sat 11:00 +00:00

Etc/GMT+12        Sat 11:00   Fri 23:00 -12:00
Etc/GMT+11        Sat 11:00   Sat 00:00 -11:00
Etc/GMT+10        Sat 11:00   Sat 01:00 -10:00
Etc/GMT+9         Sat 11:00   Sat 02:00 -09:00
Etc/GMT+8         Sat 11:00   Sat 03:00 -08:00
Etc/GMT+7         Sat 11:00   Sat 04:00 -07:00
Etc/GMT+6         Sat 11:00   Sat 05:00 -06:00
Etc/GMT+5         Sat 11:00   Sat 06:00 -05:00
Etc/GMT+4         Sat 11:00   Sat 07:00 -04:00
Etc/GMT+3         Sat 11:00   Sat 08:00 -03:00
Etc/GMT+2         Sat 11:00   Sat 09:00 -02:00
Etc/GMT+1         Sat 11:00   Sat 10:00 -01:00
Etc/GMT-1         Sat 11:00   Sat 12:00 +01:00
Etc/GMT-2         Sat 11:00   Sat 13:00 +02:00
Etc/GMT-3         Sat 11:00   Sat 14:00 +03:00
Etc/GMT-4         Sat 11:00   Sat 15:00 +04:00
Etc/GMT-5         Sat 11:00   Sat 16:00 +05:00
Etc/GMT-6         Sat 11:00   Sat 17:00 +06:00
Etc/GMT-7         Sat 11:00   Sat 18:00 +07:00
Etc/GMT-8         Sat 11:00   Sat 19:00 +08:00
Etc/GMT-9         Sat 11:00   Sat 20:00 +09:00
Etc/GMT-10        Sat 11:00   Sat 21:00 +10:00
Etc/GMT-11        Sat 11:00   Sat 22:00 +11:00
Etc/GMT-12        Sat 11:00   Sat 23:00 +12:00
Etc/GMT-13        Sat 11:00   Sun 00:00 +13:00
Etc/GMT-14        Sat 11:00   Sun 01:00 +14:00
```

The execution finishes without error, confirming that the four tested assertions hold.

### Interpretation and statement of the rules

- The _assert_ statements confirm that the _extract(epoch from timestamptz_value)_ result is unaffected by the session's _timeZone_ setting. This is, of course, what your intuition tells you to expect.
- The output confirms that the display of a _timestamptz_ value (formatted as _text_) is sensitive to the session's _timeZone_ setting and that the _text_ display of a plain _timestamp_ value is not sensitive in this way.
- The timezone-sensitive formatting of the displayed _timestamptz_ values lines up consistently with what the examples in the previous sections on this page (and on other pages in this overall _date-time_ section) show: informally (as was stated above) that _(10 + 1)_ and _(14 - 3)_ both represent the same value.
- This is the careful statement of the rule, supported by the fact that all the _assert_ statements succeeded:

  ```output
  [timestamptz-value] display ◄—
    [internal-timestamp-value + UTC-offset-value-from-session-timezone] display
    annotated with
    [UTC-offset-value-from-session-timezone] display
  ```

- This rule statement lines up with what meeting partners living in the US Pacific coastal region and London, in the winter, understand:

  - The _UTC offset_ for the US Pacific coastal region, in the winter, is _minus eight hours_.
  - The _UTC offset_ for London, in the winter, is _zero hours_—in other words, London at that time of year uses the _UTC Time Standard_.
  - When the local time is, say, 10:00 in the US Pacific coastal region, it's 18:00 in London.
  - _Eighteen_ is _ten minus (minus eight)_.
