---
title: TheFunction extract() | date_part() returns double precision [YSQL]
headerTitle: Function extract() | date_part() returns double precision
linkTitle: Function extract() | date_part()
description: The semantics of The functions extract() returns double precision, and its alternative formulation date_part() [YSQL]
menu:
  preview:
    identifier: extract
    parent: miscellaneous
    weight: 20
type: docs
---

The function _extract()_, and the alternative syntax that the function _date_part()_ supports for the same semantics, return a _double precision_ value corresponding to a nominated so-called _field_, like _year_ or _second_, from the input _date-time_ value.

The two functions, _extract()_ and _date_part()_, have identical semantics. They differ not just in name but also in syntax:

```output
extract(<field> from date_time_value) == date_part(field_text_value, date_time_value)
```

Notice that _extract()_ requires that _<field>_ is a _keyword_ while _date_part()_ requires that _field_text_value_ is a _text_ value (expression).

Try this:

```plpgsql
set timezone = 'America/Los_Angeles';
with
  c1 as (
    select '2021-09-22 13:17:53.123456 Europe/Helsinki'::timestamptz as t),
  c2 as (
  select
    extract(week   from t) as ew,
    extract(hour   from t) as eh,
    extract(second from t) as es,

    date_part('week',   t) as dw,
    date_part('hour',   t) as dh,
    date_part('second', t) as ds
  from c1)
select
  ew::text as "week",
  eh::text as "hour",
  es::text as "second",

  (ew = dw)::text as "(ew = dw)",
  (eh = dh)::text as "(eh = dh)",
  (es = ds)::text as "(es = ds)"
from c2;
```

This is the result:

```output
 week | hour |  second   | (ew = dw) | (eh = dh) | (es = ds)
------+------+-----------+-----------+-----------+-----------
 38   | 3    | 53.123456 | true      | true      | true
```

The _extract()_ function is specified in the SQL Standard and seems to be supported in all SQL database systems. The list of keywords, though, is database-system-specific. (For example, Oracle Database supports only about ten of these while PostgreSQL, and therefore YSQL, support about twenty.) In contrast, the _date_part()_ function is specific to PostgreSQL (and any system like YSQL that aims to support the identical syntax and semantics).

The \\_df_ meta-command produces output for _date_part()_ in the normal way; but it produces no output for _extract()_. Here is the interesting part of the output from \\_df date_part()_:

```output
 Result data type |        Argument data types
------------------+-----------------------------------
 double precision | text, date

 double precision | text, time without time zone

 double precision | text, timestamp without time zone
 double precision | text, timestamp with time zone

 double precision | text, interval
```

Three rows were removed manually:

- The one for the _timetz_ argument data type. (See the [recommendation to avoid using _timetz_](../../../../type_datetime#avoid-timetz) on the overall _date-time_ section's main page.)
- The two for the _abstime_ and _reltim_ data types. (See the recommendation below.)

The remaining rows were re-ordered, and blank lines were added, to improve the readability.

{{< tip title="Avoid using the 'abstime' and 'reltime' fields." >}}
The [PostgreSQL documentation](https://www.postgresql.org/docs/11/datatype-datetime.html#DATATYPE-DATETIME-TABLE) says this:

> The data types _abstime_ and _reltime_ are lower precision types which are used internally. Don't use these types in applications; these internal types might disappear in a future release.
{{< /tip >}}

Because _date_part()_ uses a _text_ value to specify the to-be-extracted field rather than a keyword, it allows noticeably more flexible programmability. Here's a simple example:

```plpgsql
drop function if exists fields_report(timestamptz) cascade;

create function fields_report(tstz in timestamptz)
  returns table(z text)
  language plpgsql
as $body$
declare
  fields constant text[] not null := array ['year', 'month', 'day', 'hour', 'minute', 'second'];
  f               text   not null := '';
begin
  foreach f in array fields loop
    assert pg_typeof(date_part(f, tstz))::text = 'double precision';
    z := rpad(f||':', 8)||date_part(f, tstz)::text; return next;
  end loop;
end;
$body$;
```

Test it first like this:

```plpgsql
set timezone = 'UTC';
select z from fields_report('2017-05-17 11:42:13 UTC');
```

This is the result:

```output
 year:   2017
 month:  5
 day:    17
 hour:   11
 minute: 42
 second: 13
```

Now test it like this:

```plpgsql
set timezone = 'America/Los_Angeles';
select z from fields_report('2021-09-22 13:17:53.123456 Europe/Helsinki');
```

This is the result:

```output
 year:   2021
 month:  9
 day:    22
 hour:   3
 minute: 17
 second: 53.123456
```

You can see that, in the normal way, the extracted value for the _months_ field from a _timestamptz_ value is sensitive both to the reigning timezone when the value is set and the reigning timezone when it is observed.

## List of keywords

| Keyword | Description |
| ------- | ----- |
| _millennium_ | The millennium. |
| _century_ | The century. |
| _decade_ | The year field divided by 10. |
| _year_ | The year field. Remember that there is no year _zero_, so subtracting BC years from AD years should be done with care. (Add one to the result.) |
| _quarter_ | The quarter of the year (1..4) that the date is in. |
| _month_ | For _timestamp[tz]_ values, the number of the month within the year (1..12); for _interval_ values, the number of months, modulo 12 (0..11). |
| _day_ | For _timestamp[tz]_ values, the day (of the month) field (1..31); for _interval_ values, the number of days. |
| _hour_ | The hour field (0..23). |
| _minute_ | The minutes field (0..59). |
| _second_ | The seconds field, including fractional parts (0..59.999999). |
| _milliseconds_ | The seconds field, including fractional parts, multiplied by 1,000. Note that this includes full seconds. |
| _microseconds_ | The seconds field, including fractional parts, multiplied by 1,000,000. Note that this includes full seconds. |
| _timezone_hour_ | The hour component of the time zone offset. |
| _timezone_minute_ | The minute component of the time zone offset. |
| _timezone_ | The time zone offset from UTC, measured in seconds. Positive values correspond to time zones east of UTC, negative values to zones west of UTC. |
| _doy_ | The day of the year (1..365/366). |
| _dow_ | The day of the week as Sunday (0) to Saturday (6). |
| _isodow_ | The day of the week as Monday (1) to Sunday (7). |
| _week_ | The number of the week of the year using the ISO 8601 week-numbering scheme. By definition, ISO weeks start on Mondays and the first week of a year contains 4-January of that year. In other words, the first Thursday of a year is in week 1 of that year. In the ISO week-numbering system, it is possible for early-January dates to be part of the 52nd or 53rd week of the previous year, and for late-December dates to be part of the first week of the next year. For example, _2005-01-01_ is part of the 53rd week of _2004_, and _2006-01-01_ is part of the 52nd week of _2005_, while _2012-12-31_ is part of the first week of _2013_. Use the _isoyear_ field together with the _week_ field to get consistent results. See the example immediately after this table. |
| _isoyear_ | The ISO 8601 week-numbering year that the date falls in (not applicable to intervals). |
| _epoch_ | For _timestamptz_ values, the result is the number of seconds since _1970-01-01 00:00:00 UTC_ (negative for values before that moment); and for _date_ and plain _timestamp_ values, the result is the number of seconds since _1970-01-01 00:00:00_. See the [demonstration](../../../date-time-data-types-semantics/type-timestamp/#the-demonstration) subsection and the discussion the parcedes it on the [plain _timestamp_ and _timestamptz_ data types](../../../date-time-data-types-semantics/type-timestamp/) page. For interval values, the result is the total number of seconds in the interval. See [The extract(epoch from interval_value) built-in function](../../../date-time-data-types-semantics/type-interval/justfy-and-extract-epoch/#the-extract-epoch-from-interval-value-built-in-function). Notice that the result of _extract(epoch from ...)_ is never sensitive to the session's timezone setting. |
| _julian_ | The Julian Date corresponding to the _date_ or _timestamp[tz]_ value (not supported for _interval_ values). _timestamp[tz]_ values that are not local midnight result in a fractional value. See [Appendix B.7. Julian Dates](https://www.postgresql.org/docs/11/datetime-julian-dates.html) in the PostgreSQL documentation. |

Demonstrate the recommended joint use of _isoyear_ and _week_ thus:

```plpgsql
drop function if exists f(date) cascade;
create function f(d in date)
  returns text
  language plpgsql
as $body$
begin
  return
    d::text||' -> '||
    ltrim(to_char(extract(isoyear from d), '9999999'))||':'||
    ltrim(to_char(extract(week    from d),      '09'));
end;
$body$;

select
  f('2005-01-01') as "y:d 1",
  f('2006-01-01') as "y:d 2",
  f('2012-12-31') as "y:d 3";
```

This is the result:

```output
         y:d 1         |         y:d 2         |         y:d 3
-----------------------+-----------------------+-----------------------
 2005-01-01 -> 2004:53 | 2006-01-01 -> 2005:52 | 2012-12-31 -> 2013:01
```

## Which fields can you extract from values of which data type?

Obviously, it makes sense to ask about the value of the timezone only for an observed _timestamptz_ value. And because an _interval_ value measures a duration and isn't anchored to any particular moment, it makes no sense to ask which millennium it falls in. The following table function attempts to extract, in turn, each of the fields with legal names (there are twenty-two in all) from values of each of the five relevant _date-time_ data types, _date_, plain _time_, plain _timestamp_, _timestamptz_, and _interval_. (_timetz_ values are excluded from the attempt following the recommendation, [here](../../../../type_datetime/#avoid-timetz), to avoid this data type. A table function is used so that the results can be presented in a nicely comprehensible fashion as a five-by-twenty-two matrix with the filed name on the vertical axis and the data type on the horizontal axis. If the requested extraction is legal, then the _text_ typecast of resulting _double precision_ value is shown in the cell. And if the operation is illegal, then the cell is left blank.

Create and execute the table function like this:

```plpgsql
drop function if exists field_versus_data_type_extractability() cascade;

create function field_versus_data_type_extractability()
  returns table(z text)
  language plpgsql
as $body$
declare
  d       constant date        not null := '2016-09-18';
  t       constant time        not null := '13:17:53.123456';
  ts      constant timestamp   not null := '2016-09-18 13:17:53.123456';
  tstz    constant timestamptz not null := '2016-09-18 13:17:53.123456 Europe/Helsinki';
  i                interval    not null := make_interval(months=>14653, days=>99, secs=>5767.123456);

  pad     constant int         not null := 18;
  f                text        not null := '';
  v_d              text        not null := 0;
  v_t              text        not null := 0;
  v_ts             text        not null := 0;
  v_tstz           text        not null := 0;
  v_i              text        not null := 0;
  fields  constant text[]           not null := array [
    'millennium',
    'century',
    'decade',
    'year',
    'quarter',
    'month',
    'day',
    'hour',
    'minute',
    'second',
    'milliseconds',
    'microseconds',
    'timezone_hour',
    'timezone_minute',
    'timezone',

    'doy',
    'dow',
    'isodow',
    'week',
    'isoyear',

    'epoch',
    'julian'
  ];
begin
  z := rpad('date:',     pad)||d::text;                                                 return next;
  z := rpad('time:',     pad)||t::text;                                                 return next;
  z := rpad('ts:',       pad)||ts::text;                                                return next;
  z := rpad('tstz:',     pad)||tstz::text;                                              return next;
  z := rpad('interval:', pad)||i::text;                                                 return next;
  z:= '';                                                                               return next;

  z := lpad(' ', pad)||lpad('date',     pad)||
                       lpad('time',     pad)||
                       lpad('ts',       pad)||
                       lpad('tstz',     pad)||
                       lpad('interval', pad);                                           return next;

  z := lpad(' ', pad)||'  '||lpad('-', (pad-2), '-')||
                       '  '||lpad('-', (pad-2), '-')||
                       '  '||lpad('-', (pad-2), '-')||
                       '  '||lpad('-', (pad-2), '-')||
                       '  '||lpad('-', (pad-2), '-');                                   return next;


  foreach f in array fields loop

    -- "feature_not_supported"   is "0A000"
    -- "invalid_parameter_value" is "22023"
    begin
      v_d := date_part(f, d)::text;
    exception
      when feature_not_supported then
        v_d := '';
    end;

    begin
      v_t := date_part(f, t)::text;
    exception
      when feature_not_supported or invalid_parameter_value then
        v_t := '';
    end;

    begin
      v_ts := date_part(f, ts)::text;
    exception
      when feature_not_supported or invalid_parameter_value then
        v_ts := '';
    end;

    v_tstz := date_part(f, tstz)::text;

    begin
      v_i := date_part(f, i)::text;
    exception
      when feature_not_supported or invalid_parameter_value then
        v_i := '';
    end;

    z := rpad(f||':', pad)||lpad(v_d,    pad)||
                           lpad(v_t,    pad)||
                           lpad(v_ts,   pad)||
                           lpad(v_tstz, pad)||
                           lpad(v_i,    pad);                                           return next;
  end loop;
end;
$body$;

select z from field_versus_data_type_extractability();
```

This is the result:

```output
 date:             2016-09-18
 time:             13:17:53.123456
 ts:               2016-09-18 13:17:53.123456
 tstz:             2016-09-18 03:17:53.123456-07
 interval:         1221 years 1 mon 99 days 01:36:07.123456

                                 date              time                ts              tstz          interval
                     ----------------  ----------------  ----------------  ----------------  ----------------
 millennium:                        3                                   3                 3                 1
 century:                          21                                  21                21                12
 decade:                          201                                 201               201               122
 year:                           2016                                2016              2016              1221
 quarter:                           3                                   3                 3                 1
 month:                             9                                   9                 9                 1
 day:                              18                                  18                18                99
 hour:                              0                13                13                 3                 1
 minute:                            0                17                17                17                36
 second:                            0         53.123456         53.123456         53.123456          7.123456
 milliseconds:                      0         53123.456         53123.456         53123.456          7123.456
 microseconds:                      0          53123456          53123456          53123456           7123456
 timezone_hour:                                                                          -7
 timezone_minute:                                                                         0
 timezone:                                                                           -25200
 doy:                             262                                 262               262
 dow:                               0                                   0                 0
 isodow:                            7                                   7                 7
 week:                             37                                  37                37
 isoyear:                        2016                                2016              2016
 epoch:                    1474156800      47873.123456  1474204673.12346  1474193873.12346  38542980967.1235
 julian:                      2457650                    2457650.55408708  2457650.13742041
```

Notice that an illegal extraction attempt with all data types apart from _time_ causes the _0A000_ error (_feature_not_supported_). But an illegal extraction attempt on a _time_ value might cause the _22023_ error (_invalid_parameter_value_) with a message like _"time" units "millennium" not recognized_.

The rules that let you predict the extracted field values from an _interval_ value rely on understanding how interval values are stored as a _[mm, dd, ss]_ tuple. This is explained in the section [How does YSQL represent an _interval_ value?](../../../date-time-data-types-semantics/type-interval/interval-representation/) Further, the algorithm that _extract()_ uses on an _interval_ value depends on some arbitrarily asserted rules of thumb, explained [here](../../../date-time-data-types-semantics/type-interval/justfy-and-extract-epoch/#the-extract-epoch-from-interval-value-built-in-function).
