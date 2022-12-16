---
title: Date and time formatting functions [YSQL]
headerTitle: Date and time formatting functions
linkTitle: Formatting functions
description: Describes the date and time formatting functions. [YSQL]
image: /images/section_icons/api/subsection.png
menu:
  v2.14:
    identifier: date-time-formatting-functions
    parent: api-ysql-datatypes-datetime
    weight: 100
type: docs
---

This page describes all of the _date-time_ formatting functions, both in the direction _date-time_ value to _text_ value and in the direction _text_ value to _date-time_ value. The functions use a so-called _template_ to determine, in the to _text_ value direction, how the _date-time_ value will be rendered as a _text_ value and, in the to _date-time_ value direction, how the to-be-converted _text_ value is to be interpreted. The template, in turn, is made up of a mixture of pre-defined so-called _template patterns_ and free text, intermingled in a user-defined order. See the section [Date-time template patterns](#date-time-template-patterns). The effects of these template patterns, again in turn, can be modified. See the section [Date-time template pattern modifiers](#date-time-template-pattern-modifiers).

Here's a simple example. It uses all of the relevant functions.

```plpgsql
set timezone = 'Asia/Kathmandu';
select
  to_char('2021-05-17 15:00:00'::timestamp, 'hh24:mi "on" dd-Mon-yyyy')::text as "timestamp to text",
  to_timestamp('15:00 17-May-2021',         'hh24:mi dd-Mon-yyyy'     )::text as "text to timestamptz",
  to_date('17-May-2021',                    'dd-Mon-yyyy'             )::text as "text to date";
```

This is the result:

```output
  timestamp to text   |    text to timestamptz    | text to date
----------------------+---------------------------+--------------
 15:00 on 17-May-2021 | 2021-05-17 15:00:00+05:45 | 2021-05-17
```

The general-purpose _date-time_ functions are described in the separate dedicated [General-purpose functions](../functions/) section.

## From date-time to text

Free text that you might include in a template has a specific effect only in the to _text_ direction. But you might need to use dummy free text when you convert in the to _date-time_ value direction and the to-be-converted _text_ value contains uninterpretable substrings. See the subsection [From _text_ to _date-time_](#from-text-to-date-time).

There are very many template patterns, and these are interpreted in preference to the free text. Whitespace that might surround such pattern characters makes no difference to how they are detected. For example, the single letter _I_ calls for the last digit of the ISO 8601 week-numbering year. This means that the phrase _In the kitchen sink_, rendered bare in the template, would not be interpreted as you're likely to want. You can escape an individual free text character by surrounding it with double quotes: _"I"n the k"i"tchen s"i"nk_. But the usual approach is simply to surround every free text string, no matter what its length is, with double quotes: _"In the kitchen sink"_.

Try this:

```plpgsql
\x on
with c as (select '2019-04-22 09:00:00'::timestamp as t)
select
  to_char(t, 'Started at Yugabyte in yyyy at HH AM on Dy ddth Mon')          as "proving the point",
  to_char(t, 'Starte"d" at "Y"ugab"y"te "i"n yyyy at HH AM on Dy ddth Mon')  as "cumbersome",
  to_char(t, '"Started at Yugabyte in" yyyy "at" HH AM "on" Dy ddth Mon')    as "conventional"
from c;
\x off
```

This is the result:

```output
proving the point | Starte2 at 9ugab9te 9n 2019 at 09 AM on Mon 22nd Apr
cumbersome        | Started at Yugabyte in 2019 at 09 AM on Mon 22nd Apr
conventional      | Started at Yugabyte in 2019 at 09 AM on Mon 22nd Apr
```

If you want to output the double quote character within the free text, then you must escape it with a backslash. The same applies, by extension, if you want to output a backslash. The backslash has no effect when it precedes any other characters. Try this:

```plpgsql
select
  to_char('2021-05-17'::timestamp, '"Here is the \"year\" c\o\m\p\o\n\e\n\t of a date\\time value:" yyyy');
```

This is the result:

```output
 Here is the "year" component of a date\time value: 2021
```

There is only one function for this conversion direction, _to_char()_. Here is the interesting part of the output from \\_df to_char()_:

```output
  Name   | Result data type |        Argument data types
---------+------------------+-----------------------------------
 to_char | text             | interval, text
 to_char | text             | timestamp with time zone, text
 to_char | text             | timestamp without time zone, text
```

(There are also overloads to convert a _bigint_, _double precision_, _integer_, _numeric_, or _real_ value to a _text_ value. But these are outside the scope of the present _date-time_ major section.)

Notice that there are no overloads to convert a _date_ value or a _time_ value to a _text_ value. But this has no practical consequence because the overload resolution rules look after this. Try this:

```plpgsql
drop function if exists f(interval) cascade;
drop function if exists f(timestamp) cascade;
drop function if exists f(timestamptz) cascade;

create function f(t in interval)
  returns text
  language plpgsql
as $body$
begin
  return '"interval" overload: '||t::text;
end;
$body$;

create function f(t in timestamp)
  returns text
  language plpgsql
as $body$
begin
  return 'plain "timestamp" overload: '||t::text;
end;
$body$;

create function f(t in timestamptz)
  returns text
  language plpgsql
as $body$
begin
  return '"timestamptz" overload: '||t::text;
end;
$body$;

set timezone = 'UTC';
select f('2021-03-15'::date);
```

This is the result:

```output
 "timestamptz" overload: 2021-03-15 00:00:00+00
```

Now try this:

```plpgsql
select f('15:00:00'::time);
```

```output
"interval" overload: 15:00:00
```

(You can see the rules that bring these outcomes by querying _pg_type_ and _pg_cast_.)

A _date_ value simply cannot be converted to an _interval_ value. (The attempt causes the _42846_ error.)  So the _interval_ overload of _to_char()_ can have no effect on the overload selection when a _date_ actual argument is used. Therefore, because _to_char()_ has two moment overloads, one with a plain _timestamp_ formal parameter and the other with a _timestamptz_ formal parameter, if you invoke it with a _date_ actual argument, then the _timestamptz_ overload is selected. As it happens, the overload selection makes no difference to the outcome when you use a natural template for a _date_ value. Try this:

```plpgsql
with c as (
  select
    '2021-06-15'::date as d,
    'dd-Mon-yyyy'      as fmt)
select
  to_char(d,              fmt) as "implicit cast to timestamptz",
  to_char(d::timestamptz, fmt) as "explicit cast to timestamptz",
  to_char(d::timestamp,   fmt) as "explicit cast to plain timestamp"
from c;
```

This is the result:

```output
 implicit cast to timestamptz | explicit cast to timestamptz | explicit cast to plain timestamp
------------------------------+------------------------------+----------------------------------
 15-Jun-2021                  | 15-Jun-2021                  | 15-Jun-2021
```

Now try this:

```plpgsql
with c as (
  select
    '15:00:00'::time as t,
    'hh12-mi-ss AM'  as fmt)
select
  to_char(t,           fmt) as "implicit cast to interval",
  to_char(t::interval, fmt) as "explicit cast to interval"
from c;
```

This is the result:

```output
 implicit cast to interval | explicit cast to interval
---------------------------+---------------------------
 03-00-00 PM               | 03-00-00 PM
```

You might think that it's strange that an _interval_ value can be rendered with _AM_ or _PM_. But it is what it is.

```plpgsql
select to_char(make_interval(hours=>15), 'AM');
```

This is the result:

```output
 PM
```

{{< tip title="Prefer uncluttered code to explicit typecasting with 'to_char()' for 'date' and 'time' values." >}}

Usually, users avoid writing code that brings an implicit typecast and prefer, instead, to write the typecast that they want explicitly. The tests above show that in the case that _to_char()_ is to be used to render a _date_ value or a _time_ value, the result is the same when the implicit typecast (respectively to _timestamptz_ or to _interval_) is allowed and when the explicit typecast is written. More carefully stated, writing the explicit typecast of a _date_ value either to a plain _timestamp_ value or to a _timestamptz_ value has the same effect as not writing a typecast; and writing the explicit typecast of a _time_ value to an _interval_ value has the same effect as not writing the typecast.

Yugabyte recommends that, for the special case of invoking _to_char()_ with a _date_ or _time_ value, simply to allow the implicit typecast to have its innocent effect. In other words, you should prefer uncluttered code, here, to fastidious adherence to the normal rule of good practice.
{{< /tip >}}

## From text to date-time

There are just two functions for this conversion direction, _to_date()_ and _to_timestamp()_. Here is the interesting part of the output from \\_df to_date()_:

```output
 Result data type | Argument data types
------------------+---------------------
 date             | text, text
```

Try this:

```plpgsql
select ( to_date('22nd Apr, 2019', 'ddth Mon, yyyy') )::text;
```

This is the result:

```output
 2019-04-22
```

Here is the interesting part of the output from \\_df to_timestamp()_:

```output
     Result data type     | Argument data types
--------------------------+---------------------
 timestamp with time zone | double precision
 timestamp with time zone | text, text
```

The _double precision_ overload is not a formatting function in the sense that this section uses the term. That overload is described in this [dedicated subsection](../functions/creating-date-time-values/#function-to-timestamp-returns-timestamptz) within the [General-purpose date and time functions](../functions/) section. Arguably, this functionality would have been better implemented as overloads of _make_timestamp()_ and _make_timestamptz()_, each with a single _double precision_ parameter. But this, simply, is not how it was done.

Notice that there's no built-in function to convert a _text_ value directly to a plain timestamp value. If you want this conversion, then you have to code it explicitly:

```plpgsql
set timezone = 'Europe/Helsinki';
with c as (select to_timestamp('15:00 17-May-2021', 'hh24:mi dd-Mon-yyyy')::timestamp as t)
select
  pg_typeof(t)::text, t::text
from c;
```

This is the result:

```output
          pg_typeof          |          t
-----------------------------+---------------------
 timestamp without time zone | 2021-05-17 15:00:00
```

See the subsection [_timestamptz_ to plain _timestamp_](../typecasting-between-date-time-values/#timestamptz-to-plain-timestamp) on the [Typecasting between values of different _date-time_ datatypes](../typecasting-between-date-time-values/) page. It explains that, in the exact scenario that's shown here where a _text_ value is converted first to a _timestamptz_ value and then, in the same statement, typecast to a plain _timestamp_ value, the result is insensitive to the value of the session timezone.

In _to_date()_ and _to_timestamp()_, free text asks simply to skip as many characters as it contains before resuming parsing for actual template patterns in the normal way. The characters that are used to specify the free text, in the to _date-time_ direction are insignificant as long as the first and last are not whitespace. It's easy to show this by a test. First, do this, in the to _text_ direction:

```plpgsql
with c as (select '"The date is:"FMDay, DD, FMMonth, Y,YYY'::text as fmt)
select
  to_char('2021, 06-15'::date, fmt) as d1,
  to_char('2021, 09-18'::date, fmt) as d2
from c;
```

The _FM_ modifier is explained in the subsection [Date-time template pattern modifiers](#date-time-template-pattern-modifiers)

This is the result:

```output
                  d1                  |                     d2
--------------------------------------+--------------------------------------------
 The date is:Tuesday, 15, June, 2,021 | The date is:Saturday, 18, September, 2,021
```

Now do it in reverse, in the to _date_ direction:

```plpgsql
drop function if exists f(text) cascade;
create function f(d in text)
  returns text
  language plpgsql
as $body$
begin
  --                  The date is:
  return to_date(d, '"1234567890ab"Day, DD, Month, Y,YYY')::text;
end;
$body$;

select
  f('The date is:Tuesday, 15, June, 2,021'      ) as d1,
  f('The date is:Saturday, 18, September, 2,021') as d2;
```

This is the result:

```output
     d1     |     d2
------------+------------
 2021-06-15 | 2021-09-18
```

Now do this exhaustive test:

```plpgsql
drop function if exists f(text) cascade;

create function f(d in text)
  returns text
  language plpgsql
as $body$
declare msg constant text not null := rpad(chr(187)||d||chr(171), 65, '.');
begin
  assert
    to_date(d, '"1234567890ab"Day, DD, Month, Y,YYY') = '2021-06-15'::date;
  return msg||' OK';
exception when invalid_datetime_format then
  return msg||' failed';
end;
$body$;

drop function if exists test_results(int) cascade;

create function test_results(pad_len in int)
  returns table(z text)
  language plpgsql
as $body$
declare
  pad      constant text not null := rpad(' ', pad_len, ' ');
  payload  constant text not null := pad||'Tuesday   ,   15   ,   June   ,   2,021   ';
begin
  z := f('   1234567890ab'  ||payload);         return next;
  z := f('   1           '  ||payload);         return next;


  z := '';                                      return next;
  z := 'Too long.';                             return next;
  z := f('   1234567890abc' ||payload);         return next;
  z := f('   1           c' ||payload);         return next;

  z := '';                                      return next;
  z := 'Too short.';                            return next;
  z := f('   1234567890a'   ||payload);         return next;
  z := f('   1          '   ||payload);         return next;
end;
$body$;

select z from test_results(0);
```

This is the result:

```output
 »   1234567890abTuesday   ,   15   ,   June   ,   2,021   «...... OK
 »   1           Tuesday   ,   15   ,   June   ,   2,021   «...... OK

 Too long.
 »   1234567890abcTuesday   ,   15   ,   June   ,   2,021   «..... failed
 »   1           cTuesday   ,   15   ,   June   ,   2,021   «..... failed

 Too short.
 »   1234567890aTuesday   ,   15   ,   June   ,   2,021   «....... failed
 »   1          Tuesday   ,   15   ,   June   ,   2,021   «....... failed
```

You can see three things immediately:

- A run of zero or more spaces at the start of the to-be-interpreted _text_ value has no effect.
- The parsing of the payload starts immediately following the length of the defined free text, counting from its first non-space character.
- The payload may start with a run of zero or more spaces before its first non-space character.

You can confirm this with this new invocation of the test:

```plpgsql
select z from test_results(1);
```

This is the new result:

```outout
 »   1234567890ab Tuesday   ,   15   ,   June   ,   2,021   «..... OK
 »   1            Tuesday   ,   15   ,   June   ,   2,021   «..... OK

 Too long.
 »   1234567890abc Tuesday   ,   15   ,   June   ,   2,021   «.... failed
 »   1           c Tuesday   ,   15   ,   June   ,   2,021   «.... failed

 Too short.
 »   1234567890a Tuesday   ,   15   ,   June   ,   2,021   «...... OK
 »   1           Tuesday   ,   15   ,   June   ,   2,021   «...... OK
```

The _"Too long"_ tests still fail. but the _"Too short"_ tests now succeed.

{{< tip title="Avoid using 'years' substring values less than one to specify BC in 'to_date()' and 'to_timestamp()'." >}}

The section "Usage notes for date/time formatting" on the page "9.8. Data Type Formatting Functions" just under [Table 9.25. Template Pattern Modifiers for Date/Time Formatting](https://www.postgresql.org/docs/11/functions-formatting.html#FUNCTIONS-FORMATTING-DATETIMEMOD-TABLE) says this:

> In to_timestamp and to_date, negative years are treated as signifying BC. If you write both a negative year and an explicit BC field, you get AD again. An input of year zero is treated as 1 BC.

This seems to be a suspect solution looking for a problem for these reasons:

- Nobody ever talks about "year zero" in the Gregorian calendar because there's no such year. So code that exploits the feature that _"zero is taken to mean 1 BC"_ will be obscure and, at the very least, will require extensive comments to explain choosing this approach.

- Nobody ever talks about dates by saying "the year minus 42". They always say "42 BC". So, code that exploits the feature that _"year -X means X BC"_ is, here too, going to need extensive commenting.

- If you invoke _make_timestamp()_ or _make_timestamptz()_ with a negative (or zero) argument for "year", then you get the _22008_ error. (This is the case in PostgreSQL Version 11.2 (the earliest version of interest for users of YugabyteDB). It remains the case through PostgreSQL Version 13. But Version 14 newly allows a negative argument for "year" but still causes the _22008_ error with a zero argument for "year".

- The _text_-to-_date_ typecast _'-2021-06-15'::date_ gets the _22009_ error, as it does with _-0000_ for the year. This holds through PostgreSQL Version 14.

- The unary _minus_ operator is shorthand for _subtract the operand from zero_ — i.e. _-x_ means _0 - x_. But there is no year zero. And anyway, the difference between two _date_ values is an _integer_ value; and the difference between two _timestamp_ or _timestamptz_ values is an _interval_ value.

- Finally, the implementation is buggy. This is also the case in PostgreSQL through Version 14—but in subtly different ways on crossing some version boundaries. Try this:

\x on
select
  to_date( '15/06/-2021',    'DD/MM/YYYY'    ) as a1,
  to_date( '15/06/-2021 BC', 'DD/MM/YYYY/AD' ) as a2,
  ''                                           as "-",
  to_date( '15 06 -2021',    'DD MM YYYY'    ) as b1,
  to_date( '15 06 -2021 BC', 'DD MM YYYY AD' ) as b2;
\x off

Notice that the difference between the first two expressions (that produce the values _a1_ and _a2_) and the second two expressions (that produce the values _b1_ and _b2_) is how the to-be-converted substrings for _DD_, _MM_, and _YYYY_ are separated. Otherwise, they express the same intention. So _b1_ should be the same as _a1_ and _b2_ should be the same as _a2_. This is the result:

```outout
a1 | 2022-06-15 BC
a2 | 2022-06-15 BC
-  |
b1 | 2021-06-15
b2 | 2021-06-15 BC
```

_b1_ differs from _a1_ and _b2_ differs from _a2_.

Yugabyte therefore recommends that you simply avoid using "years" substring values less than _one_ to specify _BC_ in _to_date()_ and _to_timestamp()_.
{{< /tip >}}

## Date-time template patterns

This table lists about fifty distinct template patterns. They are ordered in rough order of increasingly coarse granularity.

| Pattern                  | Description                                                                                      |
| ------------------------ | ------------------------------------------------------------------------------------------------ |
| _US_                     | Microsecond (000000-999999). See [The effect of the MS and US template patterns](#the-effect-of-the-ms-and-us-template-patterns). |
| _MS_                     | Millisecond (000-999). See [The effect of the MS and US template patterns](#the-effect-of-the-ms-and-us-template-patterns). |
| _SS_                     | Second (00-59).                                                                                   |
| _SSSS_                   | Seconds past midnight (0-86399).                                                                  |
| _MI_                     | Minute (00-59).                                                                                   |
| _HH, HH12_               | Hour of day (01-12).                                                                              |
| _HH24_                   | Hour of day (00-23).                                                                              |
| _AM, am, PM, pm_         | meridiem indicator (without periods).                                                             |
| _A.M., a.m., P.M., p.m._ | Meridiem indicator (with periods).                                                                |
| _DD_                     | Day of month (01-31).                                                                             |
| _DY_                     | Abbreviated upper case day name (3 chars in English, localized lengths vary).                     |
| _Dy_                     | Abbreviated capitalized day name (3 chars in English, localized lengths vary).                    |
| _dy_                     | Abbreviated lower case day name (3 chars in English, localized lengths vary).                     |
| _DAY_                    | Full upper case day name (blank-padded to 9 chars).                                               |
| _Day_                    | Full capitalized day name (blank-padded to 9 chars).                                              |
| _day_                    | Full lower case day name (blank-padded to 9 chars).                                               |
| _D_                      | Day of the week, Sunday (1) to Saturday (7).                                                      |
| _ID_                     | ISO 8601 day of the week, Monday (1) to Sunday (7).                                               |
| _DDD_                    | Day of year (001-366).                                                                            |
| _IDDD_                   | Day of ISO 8601 week-numbering year (001-371; day 1 of the year is Monday of the first ISO week). |
| _W_                      | Week of month (1-5) (the first week starts on the first day of the month).                        |
| _WW_                     | Week number of year (1-53) (the first week starts on the first day of the year).                  |
| _IW_                     | Week number of ISO 8601 week-numbering year (01-53; the first Thursday of the year is in week 1). |
| _MM_                     | Month number (01-12).                                                                             |
| _MON_                    | Abbreviated upper case month name (3 chars in English, localized lengths vary).                   |
| _Mon_                    | Abbreviated capitalized month name (3 chars in English, localized lengths vary).                  |
| _mon_                    | Abbreviated lower case month name (3 chars in English, localized lengths vary).                   |
| _MONTH_                  | Full upper case month name (blank-padded to 9 chars).                                             |
| _Month_                  | Full capitalized month name (blank-padded to 9 chars).                                            |
| _month_                  | Full lower case month name (blank-padded to 9 chars).                                             |
| _RM_                     | Month in upper case Roman numerals (I-XII; I=January).                                            |
| _rm_                     | Month in lower case Roman numerals (i-xii; i=January).                                            |
| _Q_                      | Quarter.                                                                                          |
| _YYYY_                   | Year (4 or more digits).                                                                          |
| _Y,YYY_                  | Year (4 or more digits) with commas.                                                              |
| _IYYY_                   | ISO 8601 week-numbering year (4 or more digits).                                                  |
| _YYY_                    | Last 3 digits of year.                                                                            |
| _IYY_                    | Last 3 digits of ISO 8601 week-numbering year.                                                    |
| _YY_                     | Last 2 digits of year.                                                                            |
| _IY_                     | Last 2 digits of ISO 8601 week-numbering year.                                                    |
| _Y_                      | Last digit of year.                                                                               |
| _I_                      | Last digit of ISO 8601 week-numbering year.                                                       |
| _BC, bc, AD, ad_         | Era indicator (without periods).                                                                  |
| _B.C., b.c., A.D., a.d._ | Era indicator (with periods).                                                                     |
| _CC_                     | Century (2 digits) (the twenty-first century starts on 2001-01-01). See [The effect of the CC template pattern](#the-effect-of-the-cc-template-pattern). |
| _TZ_                     | Upper case time-zone abbreviation—supported only in _to_char()_.                                  |
| _tz_                     | Lower case time-zone abbreviation—supported only in _to_char()_.                                  |
| _TZH_                    | Time-zone hours.                                                                                  |
| _TZM_                    | Time-zone minutes.                                                                                |
| _OF_                     | Time-zone offset from UTC—supported only in _to_char()_.                                          |
| _J_                      | Julian Date (integer days since November 24, 4714 BC at local midnight; see [B.7. Julian Dates](https://www.postgresql.org/docs/11/datetime-julian-dates.html) in Appendix B. Date/Time Support of the PostgreSQL documentation). |

Create and execute the _template_pattern_results()_ table function to demonstrate the effect of almost all of the template patterns. When a few different template patterns simply produce different upper/lower case mixtures of their resulting text, only the one that produces the init-cap variant is used. The function uses three overloads of a formatting function _f()_. This is a simple technique to reduce repetition and clutter in the _template_pattern_results()_ function itself.

Do this:

```plpgsql
drop function if exists template_pattern_results() cascade;
drop function if exists f(text) cascade;
drop function if exists f(timestamp,   text) cascade;
drop function if exists f(timestamptz, text) cascade;

create function f(str in text)
  returns text
  language plpgsql
as $body$
begin
  return rpad(str||':', 20);
end;
$body$;

create function f(t in timestamp, tmplt in text)
  returns text
  language plpgsql
as $body$
begin
  return rpad(to_char(t, tmplt), 9);
end;
$body$;

create function f(t in timestamptz, tmplt in text)
  returns text
  language plpgsql
as $body$
begin
  return rpad(to_char(t, tmplt), 9);
end;
$body$;

create function template_pattern_results()
  returns table(z text)
  language plpgsql
as $body$
declare
  t             constant text             not null := '2021-09-17 15:47:41.123456';
  ts            constant timestamp        not null := t;
  tstz          constant timestamptz      not null := t||' UTC';

  set_timezone  constant text             not null := $$set timezone = '%s'$$;
  tz_on_entry   constant text             not null := current_setting('timezone');
begin
  execute format(set_timezone, 'Asia/Kathmandu');

  z := f('Seconds')                    ||
         '  US:    '||f(ts,   'US')    ||
       ' |  MS:    '||f(ts,   'MS')    ||
       ' |  SS:    '||f(ts,   'SS')    ||
       ' |  SSSS:  '||f(ts,   'SSSS')  ||
       ' |'                            ;                  return next;

  z := f('Minutes, hours')             ||
         '  MI:    '||f(ts,   'MI')    ||
       ' |  HH:    '||f(ts,   'HH')    ||
       ' |  HH12:  '||f(ts,   'HH12')  ||
       ' |  HH24:  '||f(ts,   'HH24')  ||
       ' |  AM:    '||f(ts,   'AM')    ||
       ' |  SSSS:  '||f(ts,   'SSSS')  ||
       ' |'                            ;                  return next;

  z := f('Day')                        ||
         '  DD:    '||f(ts,   'DD')    ||
       ' |  Dy:    '||f(ts,   'Dy')    ||
       ' |  Day:   '||f(ts,   'Day')   ||
       ' |  D:     '||f(ts,   'D')     ||
       ' |  ID:    '||f(ts,   'ID')    ||
       ' |  DDD:   '||f(ts,   'DDD')   ||
       ' |  IDDD:  '||f(ts,   'IDDD')  ||
       ' |'                            ;                  return next;

  z := f('Week')                       ||
         '  W:     '||f(ts,   'W')     ||
       ' |  WW:    '||f(ts,   'WW')    ||
       ' |  IW:    '||f(ts,   'IW')    ||
       ' |'                            ;                  return next;

  z := f('Month and quarter')          ||
         '  MM:    '||f(ts,   'MM')    ||
       ' |  Mon:   '||f(ts,   'Mon')   ||
       ' |  Month: '||f(ts,   'Month') ||
       ' |  RM:    '||f(ts,   'RM')    ||
       ' |  Q:     '||f(ts,   'Q')     ||
       ' |'                            ;                  return next;

  z := f('Year and century')           ||
         '  Y,YYY: '||f(ts,   'Y,YYY') ||
       ' |  IYYY:  '||f(ts,   'IYYY')  ||
       ' |  YYY:   '||f(ts,   'YYY')   ||
       ' |  YY:    '||f(ts,   'YY')    ||
       ' |  Y:     '||f(ts,   'Y')     ||
       ' |  BC:    '||f(ts,   'BC')    ||
       ' |  CC:    '||f(ts,   'CC')    ||
       ' |'                            ;                  return next;

  z := f('Timezone')                   ||
         '  TZ:    '||f(tstz, 'TZ')    ||
       ' |  TZH:   '||f(tstz, 'TZH')   ||
       ' |  TZM:   '||f(tstz, 'TZM')   ||
       ' |  OF:    '||f(tstz, 'OF')    ||
       ' |'                            ;                  return next;

  z := f('Julian Date')                ||
         '  J:     '||f(tstz, 'J')     ||
       ' |'                            ;                  return next;

  execute format(set_timezone, tz_on_entry);
end;
$body$;

select z from template_pattern_results();
```

This is the result:

```output
 Seconds:              US:    123456    |  MS:    123       |  SS:    41        |  SSSS:  56861     |
 Minutes, hours:       MI:    47        |  HH:    03        |  HH12:  03        |  HH24:  15        |  AM:    PM        |  SSSS:  56861     |
 Day:                  DD:    17        |  Dy:    Fri       |  Day:   Friday    |  D:     6         |  ID:    5         |  DDD:   260       |  IDDD:  257       |
 Week:                 W:     3         |  WW:    38        |  IW:    37        |
 Month and quarter:    MM:    09        |  Mon:   Sep       |  Month: September |  RM:    IX        |  Q:     3         |
 Year and century:     Y,YYY: 2,021     |  IYYY:  2021      |  YYY:   021       |  YY:    21        |  Y:     1         |  BC:    AD        |  CC:    21        |
 Timezone:             TZ:    +0545     |  TZH:   +05       |  TZM:   45        |  OF:    +05:45    |
 Julian Date:          J:     2459475   |
```

If you use the timezone template patterns with a plain _timestamp_ value, then they silently show a zero offset. Try this:

```plpgsql
deallocate all;
prepare stmt as
select
  current_setting('timezone')                                                 as "session timezone",
  to_char('2021-06-15 12:00:00'::timestamp, 'dd-Mon-yyyy hh24:mi:ss TZH:TZM') as "ts";

set timezone = 'America/Los_Angeles';
execute stmt;

set timezone = 'Europe/Helsinki';
execute stmt;
```

Both executions report _+00:00_ for _TZH:TZM_.

## Date-time template pattern modifiers

| Modifier  | Description                                                             | Example          |
| --------- | ----------------------------------------------------------------------- | ---------------- |
| FM prefix | fill mode (suppress leading zeroes and padding blanks)                  | FMMonth          |
| TM prefix | translation mode (print localized day and month names based on lc_time) | TMMonth          |
| TH suffix | upper case ordinal number suffix                                        | DDTH, e.g., 12TH |
| th suffix | lower case ordinal number suffix (always in Emglish form)               | DDth, e.g., 12th |
| FX prefix | fixed format global option (see [usage notes](#usage-notes))            | FX Month DD Day  |

For more information on _lc_time_, see the section [Typecasting between date-time values and text values](../typecasting-between-date-time-and-text/). Briefly, the list of acceptable values is sensitive to your operating system environment. On Unix-like systems, this command:

```output
locale -a
```

lists the values that are defined there.

## Usage notes

This section explains clarifying detail for the use of those template patterns and template pattern modifiers whose effect isn't self-evident.

### The FM modifier

_FM_ suppresses leading zeroes and trailing blanks that would otherwise be added to make the output of a pattern be fixed-width. Try this:

```plpgsql
with c as (select '0020-05-03 BC'::timestamp as t)
  select
    to_char(t, 'MMth "month ("Month")", DDth "day", YYYY AD')          as "plain",
    to_char(t, 'FMMMth "month ("FMMonth")", FMDDth "day", FMYYYY AD')  as "using FM"
from c;
```

This is the result:

```output
              plain                   |            using FM
-------------------------------------------+---------------------------------
05th month (May      ), 03rd day, 0020 BC | 5th month (May), 3rd day, 20 BC
```

### The TM modifier

The _TM_ modifier affects only the rendering of the full and abbreviated day and month names, in the to _text_ direction using _to_char()_. Because these fields don't affect the resulting value in the to _date-time_ direction, _TM_ is ignored by _to_date()_ and _to_timestamp()_. Not only does _TM_ determine the national language that is used for day and month names and abbreviations, but it also has the side effect of suppressing trailing blanks. Notice that _TM_ has no effect on how the ordinal number suffix is rendered. This is inevitably the English rendition (_-st_, _-nd_, or _-th_). Try this:

```plpgsql
deallocate all;
prepare stmt as
select
  to_char('2021-02-01'::timestamp, 'Day, ddth Month, y,yyy') as "plain",
  to_char('2021-02-01'::timestamp, 'TMDay, ddth TMMonth, y,yyy') as "with TM";

set lc_time = 'en_US';
execute stmt;

set lc_time = 'it_IT';
execute stmt;

set lc_time = 'fr_FR';
execute stmt;

set lc_time = 'fi_FI';
execute stmt;
```

Here are the results:

```output
  Monday   , 01st February , 2,021 | Monday, 01st February, 2,021
  Monday   , 01st February , 2,021 | Lunedì, 01st Febbraio, 2,021
  Monday   , 01st February , 2,021 | Lundi, 01st Février, 2,021
  Monday   , 01st February , 2,021 | Maanantai, 01st Helmikuu, 2,021
```

### The FX modifier

The _to_date()_ and _to_timestamp()_ functions treat runs of spaces as a single space in the to-be-converted _text_ value unless the _FX_ modifier is used. Try this:

```plpgsql
set timezone = 'UTC';
select to_timestamp('2000    JUN', 'YYYY MON')::text;
```

It runs without error and produces the expected result:

```output
  2000-06-01 00:00:00+00
```

Now try this:

```plpgsql
set timezone = 'UTC';
select to_timestamp('2000    JUN', 'FXYYYY MON')::text;
```

It causes the _22007_ error because _to_timestamp()_ expects one space only:

```output
invalid value "   " for "MON"
```

### Template patterns with just YYY, YY, or Y

With _to_date()_ and _to_timestamp()_, if the putative "years" substring in the to-be-converted _text_ value is fewer than four digits and the template pattern is _YYY_, _YY_, or _Y_, then the "years" substring is left-padded with digits to produce the four-digit string that, among all possible choices, is closest to 2020. In the case of the tie between 1970 and 2070, 1970 is deemed to be closest.

Try this:

```plpgsql
with c as (
  select '15-Jun-100'::text as t1, '15-Jun-999'::text as t2)
select
  to_date(t1, 'dd-Mon-YYYY') ::text  as "t1 using YYYY",
  to_date(t2, 'dd-Mon-YYYY') ::text  as "t2 using YYYY",

  to_date(t1, 'dd-Mon-YYY')  ::text  as "t1 using YYY",
  to_date(t2, 'dd-Mon-YYY')  ::text  as "t2 using YYY"
from c;
```

This is the result:

```output
 t1 using YYYY | t2 using YYYY | t1 using YYY | t2 using YYY
---------------+---------------+--------------+--------------
 0100-06-15    | 0999-06-15    | 2100-06-15   | 1999-06-15
```

Among the candidate values for 100, 2100 is closer to 2020 than is 1100 or 3100. And among the candidate values for 999, 1999 is closer to 2020 than is 0999 or 2999.

The result is the same when either _YY_ or _Y_ is used in place of _YYY_.

Notice that this brings a challenge if you want to interpret _'6781123'::text_ as _'0678-11-23'::date_. Try this:

```plpgsql
select
  to_date('6781123',   'YYYYMMDD')   as "1st try",
  to_date('6781123',   'YYYMMDD')    as "2nd try",
  to_date('678-11-23', 'YYYY-MM-DD') as "workaround";
```

This is the result:

```output
  1st try   |  2nd try   | workaround
------------+------------+------------
 6781-12-03 | 1678-11-23 | 0678-11-23
```

Neither the first nor the second attempt brings what you want. Your only option is to require (or to get this by pre-parsing yourself) that the input has separators between the substrings that are to be interpreted as _YYYY_, _MM_, and _DD_, like the workaround shows.

### Interpreting text values where the "years" substring has more than four digits

You _can_ use _to_date()_ and _to_timestamp()_ to convert a _text_ value like _'21234-1123'_ with the template _'YYYY-MMDD'_ without error. And you _can_ also use _to_date()_ and _to_timestamp()_ to convert a _text_ value like _'21231123'_ with the template _'YYYYMMDD'_ without error. Try this:

```plpgsql
select
  to_date('21234-1123', 'YYYY-MMDD') ::text as d1,
  to_date('21231123', 'YYYYMMDD')    ::text as d2;
```

This is the result:

```output
      d1      |     d2
-------------+------------
  21234-11-23 | 2123-11-23
```

But you simply _cannot_ write a template that lets

you use _to_date()_ and _to_timestamp()_ to convert a _text_ value like _'212341123'_ . Try this:

```plpgsql
select to_date('212341123', 'YYYYMMDD');
```

It causes the _22008_ error with this unhelpful message:

```output
date/time field value out of range: "212341123"
```

And it doesn't help if you use an extra _Y_&nbsp;&nbsp;to change _'YYYYMMDD'_&nbsp;&nbsp;to _'YYYYYMMDD'_. Now it complains about the _template_ with the _22007_ thus:

```output
conflicting values for "Y" field in formatting string
This value contradicts a previous setting for the same field type.
```

Your only option is to ensure that the "years" substring in the to-be-converted _text_ value is terminated with a non-numeric character and that the _template_ matches this—like in the previous code example that used _to_date('21234-1123', 'YYYY-MMDD')_.

### The effect of the CC template pattern

With _to_date()_ and _to_timestamp()_, the _CC_ template pattern is accepted but ignored if there is a _YYY_, _YYYY_ or _Y,YYY_ pattern. If _CC_ is used with either of the _YY_ or the _Y_ patterns, then the result is computed as that year in the specified century. If the century is specified but the year is not, the first year of the century is assumed.

Try this:

```plpgsql
select
  to_date('19 2021', 'CC YYYY') as "result 1",
  to_date('19 21', 'CC YY')     as "result 2",
  to_date('19', 'CC')           as "result 3";
```

This is the result:

```output
  result 1  |  result 2  |  result 3
------------+------------+------------
 2021-01-01 | 1821-01-01 | 1801-01-01
```

### The templates used in to_timestamp() and to_date() allow mutually contradictory overspecification

Try this:

```plpgsql
with
  c1 as (
    select
      'DD-Mon-YYYY'         as input_tmplt_1,
      'Day, DD-Mon-YYYY'    as input_tmplt_2,
      'FMDay, DD-Mon-YYYY'  as output_tmplt),
  c2 as (
    select
      to_date(           '20-May-2019', input_tmplt_1) as d1,
      to_date('Wednesday, 20-May-2019', input_tmplt_2) as d2,
      output_tmplt
    from c1)
select
  to_char(d1, output_tmplt)  "result date",
  (d1 = d2)::text            as "d1 = d2"
from c2;
```

This is the result:

```output
Monday, 20-May-2019     result date     | d1 = d2
---------------------+---------
 Monday, 20-May-2019 | true
```

Notice that the input _text_ value has _Wednesday_ but _20-May-2019_ is actually a Monday— a mutually contradictory overspecification. Further, the input _Wednesday_ has _nine_ characters but the template pattern _Day_ prescribes values of a variable length from the shortest, _Monday_ (_six_ characters) to the longest, _Wednesday_. Nevertheless, the input is accepted without error.

The rule that mutually contradictory overspecification is acceptable holds for all such substrings whose values are determined by the date itself (like _Day_, _D_, _ID_, _Mon_, _Month_, and _Q_). You will need to rely on this tolerance, and on  the character count tolerance, if you're ingesting text that happens to include, for example full day names or full month names because these substrings have a variable length and so simply using fixed-length dummy free text like _"?????"_ won't work. Tty this:

```plpgsql
with c as (select 'DD-Month-YYYY' as templt)
select
  to_date('27-May-2021',       templt)::text as d1,
  to_date('18-September-2021', templt)::text as d2
from c;
```

This is the result:

```output
     d1     |     d2
------------+------------
 2021-05-27 | 2021-09-18
```

### Avoid mixing "ISO 8601 week-numbering" patterns and "Gregorian date" patterns in the same template

With _to_date()_ and _to_timestamp()_, an ISO 8601 week-numbering date (as distinct from a Gregorian date) can be specified in one of two ways:

- You can specify the ISO 8601 week-numbering year, the week number of the ISO 8601 week-numbering year, and the ISO 8601 day of the week. Try this:

  ```plpgsql
  select to_char(to_date('2006-42-4', 'IYYY-IW-ID'), 'Dy dd-Mon-YYYY');
  ```

  </br>This is the result:

  ```output
   Thu 19-Oct-2006
  ```

  </br>If you omit the weekday it is assumed to be 1 (Monday). Try this:

  ```plpgsql
  select to_char(to_date('2006-42', 'IYYY-IW'), 'Dy dd-Mon-YYYY');
  ```

  </br>This is the result:

  ```output
   Mon 16-Oct-2006
  ```

- Specifying just the ISO 8601 week-numbering year and the day of the ISO 8601 week-numbering year is also sufficient to determine the resulting _date_ value.

  </br>Try this:

  ```plpgsql
  select to_char(to_date('2006-291', 'IYYY-IDDD'), 'Dy dd-Mon-YYYY');
  ```

  </br>This is the result:

  ```output
   Thu 19-Oct-2006
  ```

The attempt to interpret a _text_ value as a _date-time_ value using a mixture of ISO 8601 week-numbering patterns and Gregorian date patterns is nonsensical. Try this:

```plpgsql
-- ISO "2006-291" is the same as Gregorian "2006-10-19"
select to_date('2006-291 2006-10-19', 'IYYY-IDDD YYYY-MM-DD');
```

It causes the _22007_ error:

```output
invalid combination of date conventions
Do not mix Gregorian and ISO week date conventions in a formatting template.
```

In the context of the ISO 8601 year-, week- and day-numbering scheme, the concept of a "month" or a "day of the month" has no meaning; and in the context of the Gregorian calendar, the ISO scheme has no meaning.

### The effect of the MS and US template patterns in the to date-time direction

Create a table function to demonstrate the effect of the _MS_ and _US_ template patterns:

```plpgsql
-- Trivial wrapper for "rpad(t, 30)" to reduce clutter in the main code.
drop function if exists w(text) cascade;
create function w(t in text)
  returns text
  language plpgsql
as $body$
begin
  return rpad(t, 30);
end;
$body$;

drop function if exists effect_of_ms_and_us() cascade;
create function effect_of_ms_and_us()
  returns table(z text)
  language plpgsql
as $body$
declare
  t0    constant text not null := '2021-06-15 13:17:47';
  t1    constant text not null := t0||'.1';
  t2    constant text not null := t0||'.12';
  t3    constant text not null := t0||'.123';
  t4    constant text not null := t0||'.1234';
  t5    constant text not null := t0||'.12345';
  t6    constant text not null := t0||'.123456';

  x     constant text not null := 'YYYY-MM-DD HH24:MI:SS';
  x_0   constant text not null := x;
  x_ms  constant text not null := x||'.MS';
  x_us  constant text not null := x||'.US';
begin
  z := 'Input "text" value            Resulting "date" value';           return next;
  z := '--------------------------    -----------------------------';    return next;
  z := w(t0)||to_timestamp(t0, x_0);                                     return next;
  z := w(t1)||to_timestamp(t1, x_ms);                                    return next;
  z := w(t2)||to_timestamp(t2, x_ms);                                    return next;
  z := w(t3)||to_timestamp(t3, x_ms);                                    return next;
  z := w(t4)||to_timestamp(t4, x_us);                                    return next;
  z := w(t5)||to_timestamp(t5, x_us);                                    return next;
  z := w(t6)||to_timestamp(t6, x_us);                                    return next;
end;
$body$;
```

Execute it thus:

```plpgsql
set timezone = 'UTC';
select z from effect_of_ms_and_us();
```

This is the result:

```output
 Input "text" value            Resulting "date" value
 --------------------------    -----------------------------
 2021-06-15 13:17:47           2021-06-15 13:17:47+00
 2021-06-15 13:17:47.1         2021-06-15 13:17:47.1+00
 2021-06-15 13:17:47.12        2021-06-15 13:17:47.12+00
 2021-06-15 13:17:47.123       2021-06-15 13:17:47.123+00
 2021-06-15 13:17:47.1234      2021-06-15 13:17:47.1234+00
 2021-06-15 13:17:47.12345     2021-06-15 13:17:47.12345+00
 2021-06-15 13:17:47.123456    2021-06-15 13:17:47.123456+00
```

This certainly honors the input _text_ values.

Now edit the source text of _effect_of_ms_and_us()_ to change the definitions if _x_0_ and _x_ms_ to make all three definitions identical thus:

```plpgsql
  x_0   constant text not null := x||'.US';
  x_ms  constant text not null := x||'.US';
  x_us  constant text not null := x||'.US';
```

and re-execute the table function. The result is unchanged.

You can simply use the same template for both the _text_ value to _date-time_ value conversion and for the _date-time_ value to _text_ value conversion. It's just that the conversion from _text_ value needs more explanation than the conversion to _text_ value. Try this:

```plpgsql
with c as (
  select
    'YYYY-MM-DD HH24:MI:SS.US' as tmplt)
select
  to_char(to_timestamp('2021-06-15 13:17:47.123456', tmplt), tmplt)
from c;
```

This is the result:

```output
 2021-06-15 13:17:47.123456
```

The input _text_ value has been exactly recreated in the output.

{{< tip title="Always use 'SS.US' in to_timestamp()." >}}
Yugabyte recommends that you Always use _SS.US_ in to _timestamp()_ when your input _text_ value has a "seconds" substring. The demonstration above shows that specifying _US_ (for microseconds) has no harmful effect over all range of possible trailing digits after the decimal point, from _zero_ through the maximum supported precision of _six_.

If you use _SS.MS_ throughout, then input values that have _four_, _five_, or _six_ digits after the decimal point for the "seconds" substring will cause the _22008_ error:

```output
date/time field value out of range: ...
```

And if you use plain _SS_ throughout, then all values for the "seconds" substring will be accepted; but the digits after the decimal point will be ignored.

It's very hard to see how using anything other than _SS.US_ can be useful. But if you're convinced that your use-case needs this, then you should document your reasons carefully.

Notice that, with contrived input _text_ values, you _can_ use both _MS_ and _US_ in the same template. Try this:

```plpgsql
select to_timestamp('2021-06-15 13:17:47:123:123456', 'YYYY-MM-DD HH24:MI:SS:MS:US')::text;
```

This is the result:

```output
2021-06-15 13:17:47.246456+00
```

The milliseconds substring, _123_, and the microseconds substring, _123346_, have simply been added together to produce _246456_ microseconds. It's harder still to see how using both _MS_ and _US_ in the same template might be useful.
{{< /tip >}}

### D in to_char() is a very similar idea to dow in extract() but they're out of step by one day

Notice the descriptions of the _D_ and _ID_ patterns from the table in the subsection [Date-time template patterns](#date-time-template-patterns) above:

| Pattern | Description                                         |
| ------- | --------------------------------------------------- |
| _D_     | Day of the week, Sunday (1) to Saturday (7).        |
| _ID_    | ISO 8601 day of the week, Monday (1) to Sunday (7). |

And notice the descriptions of the _dow_ and _isodow_ keywords from the table in the subsection [List of keywords](../functions/miscellaneous/extract/#list-of-keywords) on the [Function extract() | date_part() returns double precision](../functions/miscellaneous/extract/) page:

| Keyword  | Description                                        |
| -------- | -------------------------------------------------- |
| _dow_    | The day of the week as Sunday (0) to Saturday (6). |
| _isodow_ | The day of the week as Monday (1) to Sunday (7).   |

The template pattern _D_ for the built-in functions _to_char()_, _to_date()_ and _to_timestamp()_, and the keyword _dow_ for the built-in function _extract()_ and its alternative _date_part()_ syntax, express the same idea. But they differ in convention. In the former, the range is _[1, 7]_ where Sunday is day number _1_; and in the latter, the range is _[0, 6]_, and Sunday is day number _0_. You have no choice but to know this and to beware.

In contrast, the template pattern _ID_ and the keyword _isodow_ express the same idea and agree in convention.

Try this:

```plpgsql
with c as (select to_date('19-Oct-2006', 'dd-Mon-yyyy') as d)
select
  to_char(d, 'D')                as "'D' value",
  date_part('dow', d)::text      as "'dow' value",
  to_char(d, 'ID')               as "'ID' value",
  date_part('isodow', d)::text   as "'isodow' value"
from c;
```

This is the result:

```output
 'D' value | 'dow' value | 'ID' value | 'isodow' value
-----------+-------------+------------+----------------
 5         | 4           | 4          | 4
```

### Don't use HH or HH12 in to_char() with an interval value

The _ss_ field of the [internal representation of an _interval_ value](../date-time-data-types-semantics/type-interval/interval-representation/) can be very big.

Try this:

```plpgsql
with c as (
  select
    make_interval(hours=>10000) as i1,
    make_interval(hours=>-15)   as i2)
select
  to_char(i1, 'HH24:MI:SS') as "Natural use of HH24",
  to_char(i1, 'HH12:MI:SS') as "Strange use of HH12",
  to_char(i2, 'HH24:MI:SS') as "Natural use of HH24",
  to_char(i2, 'HH12:MI:SS') as "Strange use of HH12"
from c;
```

This is the result:

```output
 Natural use of HH24 | Strange use of HH12 | Natural use of HH24 | Strange use of HH12
---------------------+---------------------+---------------------+---------------------
 10000:00:00         | 04:00:00            | -15:00:00           | -03:00:00
```

Now try this:

```plpgsql
with c as (
  select
    '00:00:00.000001'::interval  as delta,
    '01:00:00.000000'::interval  as one,
    '12:59:59.999999'::interval  as almost_thirteen,
    '13:00:00.000000'::interval  as thirteen)
select
  to_char((one - delta),   'HH12:MI:SS.US') as "one - delta",
  to_char(one,             'HH12:MI:SS.US') as one,
  to_char(almost_thirteen, 'HH12:MI:SS.US') as almost_thirteen,
  to_char(thirteen,        'HH12:MI:SS.US') as thirteen
from c;
```

You can see that using _HH12_ constrains the absolute value of the rendered "hours", _h_ to satisfy this rule:

```output
1 ≤ h < 13
```

This is the closed-open range _[1, 13)_. Such is the convention of the twelve-hour clock!

In other words, it's very hard to see how the _HH12_ template pattern (and its synonym _HH_ pattern) can be useful for rendering an _interval_ value. In contrast, the _HH24_ template pattern is exactly what you need.
