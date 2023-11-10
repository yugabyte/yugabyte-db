---
title: Test the date-time subtraction overloads [YSQL]
headerTitle: Test the date-time subtraction overloads
linkTitle: Test subtraction overloads
description: Presents code that tests the date-time subtraction overloads. [YSQL]
menu:
  stable:
    identifier: test-date-time-subtraction-overloads
    parent: date-time-operators
    weight: 30
type: docs
---

The [start page](../../../type_datetime/) of the overall _date-time_ section explains why _timetz_ is not covered. So there are five _date-time_ data types to consider here and therefore twenty-five overloads to test. The following code implements all of the tests. The design of the code (it tests the legal subtractions and the illegal subtractions separately) was informed by _ad hoc_ tests during its development.

Try this:

```plpgsql
drop function if exists type_from_date_time_subtraction_overload(text) cascade;

create function type_from_date_time_subtraction_overload(i in text)
  returns regtype
  language plpgsql
as $body$
declare
  d1     constant date        not null := '01-01-2020';
  d2     constant date        not null := '02-01-2020';
  t1     constant time        not null := '12:00:00';
  t2     constant time        not null := '13:00:00';
  ts1    constant timestamp   not null := '01-01-2020 12:00:00';
  ts2    constant timestamp   not null := '02-01-2020 12:00:00';
  tstz1  constant timestamptz not null := '01-01-2020 12:00:00 UTC';
  tstz2  constant timestamptz not null := '02-01-2020 12:00:00 UTC';
  i1     constant interval    not null := '12 hours';
  i2     constant interval    not null := '13 hours';

  t               regtype     not null := pg_typeof(d1);
begin
  case i
    -- "date" row.
    when 'date-date' then
      t := pg_typeof(d1 - d2);
    when 'date-time' then
      t := pg_typeof(d1 - t2);
    when 'date-ts' then
      t := pg_typeof(d1 - ts2);
    when 'date-tstz' then
      t := pg_typeof(d1 - tstz2);
    when 'date-interval' then
      t := pg_typeof(d1 - i2);

    -- "time" row.
    when 'time-date' then
      t := pg_typeof(t1 - d2);
    when 'time-time' then
      t := pg_typeof(t1 - t2);
    when 'time-ts' then
      t := pg_typeof(t1 - ts2);
    when 'time-tstz' then
      t := pg_typeof(t1 - tstz2);
    when 'time-interval' then
      t := pg_typeof(t1 - i2);

    -- Plain "timestamp" row.
    when 'ts-date' then
      t := pg_typeof(ts1 - d2);
    when 'ts-time' then
      t := pg_typeof(ts1 - t2);
    when 'ts-ts' then
      t := pg_typeof(ts1 - ts2);
    when 'ts-tstz' then
      t := pg_typeof(ts1 - tstz2);
    when 'ts-interval' then
      t := pg_typeof(ts1 - i2);

    -- "timestamptz" row.
    when 'tstz-date' then
      t := pg_typeof(tstz1 - d2);
    when 'tstz-time' then
      t := pg_typeof(tstz1 - t2);
    when 'tstz-ts' then
      t := pg_typeof(tstz1 - ts2);
    when 'tstz-tstz' then
      t := pg_typeof(tstz1 - tstz2);
    when 'tstz-interval' then
      t := pg_typeof(tstz1 - i2);

    -- "interval" row.
    when 'interval-date' then
      t := pg_typeof(i1 - d2);
    when 'interval-time' then
      t := pg_typeof(i1 - t2);
    when 'interval-ts' then
      t := pg_typeof(i1 - ts2);
    when 'interval-tstz' then
      t := pg_typeof(i1 - tstz2);
    when 'interval-interval' then
      t := pg_typeof(i1 - i2);
  end case;

  return t;
end;
$body$;

drop procedure if exists confirm_expected_42883(text) cascade;

create procedure confirm_expected_42883(i in text)
  language plpgsql
as $body$
declare
  t regtype;
begin
  t := type_from_date_time_subtraction_overload(i);
  assert false, 'Unexpected';

-- 42883: operator does not exist...
exception when undefined_function then
  null;
end;
$body$;

drop procedure if exists confirm_expected_42725(text) cascade;

create procedure confirm_expected_42725(i in text)
  language plpgsql
as $body$
declare
  t regtype;
begin
  t := type_from_date_time_subtraction_overload(i);
  assert false, 'Unexpected';

-- 42725: operator is not unique...
exception when ambiguous_function then
  null;
end;
$body$;

drop function if exists date_time_subtraction_overloads_report() cascade;

create function date_time_subtraction_overloads_report()
  returns table(z text)
  language plpgsql
as $body$
begin
  -- 19 legal subtractions in all.
  z := 'date-date:         '||type_from_date_time_subtraction_overload('date-date')::text;           return next;
  z := 'date-time:         '||type_from_date_time_subtraction_overload('date-time')::text;           return next;
  z := 'date-ts:           '||type_from_date_time_subtraction_overload('date-ts')::text;             return next;
  z := 'date-tstz:         '||type_from_date_time_subtraction_overload('date-tstz')::text;           return next;
  z := 'date-interval:     '||type_from_date_time_subtraction_overload('date-interval')::text;       return next;
  z := '';                                                                                           return next;

  z := 'time-time:         '||type_from_date_time_subtraction_overload('time-time')::text;           return next;
  z := 'time-interval:     '||type_from_date_time_subtraction_overload('time-interval')::text;       return next;
  z := '';                                                                                           return next;

  z := 'ts-date:           '||type_from_date_time_subtraction_overload('ts-date')::text;             return next;
  z := 'ts-time:           '||type_from_date_time_subtraction_overload('ts-time')::text;             return next;
  z := 'ts-ts:             '||type_from_date_time_subtraction_overload('ts-ts')::text;               return next;
  z := 'ts-tstz:           '||type_from_date_time_subtraction_overload('ts-tstz')::text;             return next;
  z := 'ts-interval:       '||type_from_date_time_subtraction_overload('ts-interval')::text;         return next;
  z := '';                                                                                           return next;

  z := 'tstz-date:         '||type_from_date_time_subtraction_overload('tstz-date')::text;           return next;
  z := 'tstz-time:         '||type_from_date_time_subtraction_overload('tstz-time')::text;           return next;
  z := 'tstz-ts:           '||type_from_date_time_subtraction_overload('tstz-ts')::text;             return next;
  z := 'tstz-tstz:         '||type_from_date_time_subtraction_overload('tstz-tstz')::text;           return next;
  z := 'tstz-interval:     '||type_from_date_time_subtraction_overload('tstz-interval')::text;       return next;
  z := '';                                                                                           return next;

  z := 'interval-time:     '||type_from_date_time_subtraction_overload('interval-time')::text;       return next;
  z := 'interval-interval: '||type_from_date_time_subtraction_overload('interval-interval')::text;   return next;

  -- 6 illegal subtractions in all.
  call confirm_expected_42883('time-date');
  call confirm_expected_42883('time-ts');
  call confirm_expected_42883('time-tstz');

  call confirm_expected_42883('interval-date');
  call confirm_expected_42883('interval-ts');
  call confirm_expected_42883('interval-tstz');
end;
$body$;

select z from date_time_subtraction_overloads_report();
```

This is the result:

```output
 date-date:         integer
 date-time:         timestamp without time zone
 date-ts:           interval
 date-tstz:         interval
 date-interval:     timestamp without time zone

 time-time:         interval
 time-interval:     time without time zone

 ts-date:           interval
 ts-time:           timestamp without time zone
 ts-ts:             interval
 ts-tstz:           interval
 ts-interval:       timestamp without time zone

 tstz-date:         interval
 tstz-time:         timestamp with time zone
 tstz-ts:           interval
 tstz-tstz:         interval
 tstz-interval:     timestamp with time zone

 interval-time:     interval
 interval-interval: interval
```
