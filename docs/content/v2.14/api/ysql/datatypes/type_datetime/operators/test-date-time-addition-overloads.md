---
title: Test the date-time addition overloads [YSQL]
headerTitle: Test the date-time addition overloads
linkTitle: Test addition overloads
description: Presents code that tests the date-time addition overloads. [YSQL]
menu:
  v2.14:
    identifier: test-date-time-addition-overloads
    parent: date-time-operators
    weight: 20
type: docs
---

The [start page](../../../type_datetime/) of the overall _date-time_ section explains why _timetz_ is not covered. So there are five _date-time_ data types to consider here and therefore twenty-five overloads to test. The following code implements all of the tests. The design of the code (it tests the legal additions and the illegal additions separately) was informed by _ad hoc_ tests during its development.

Try this:

```plpgsql
drop function if exists type_from_date_time_addition_overload(text) cascade;

create function type_from_date_time_addition_overload(i in text)
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
      t := pg_typeof(d1 + d2);
    when 'date-time' then
      t := pg_typeof(d1 + t2);
    when 'date-ts' then
      t := pg_typeof(d1 + ts2);
    when 'date-tstz' then
      t := pg_typeof(d1 + tstz2);
    when 'date-interval' then
      t := pg_typeof(d1 + i2);

    -- "time" row.
    when 'time-date' then
      t := pg_typeof(t1 + d2);
    when 'time-time' then
      t := pg_typeof(t1 + t2);
    when 'time-ts' then
      t := pg_typeof(t1 + ts2);
    when 'time-tstz' then
      t := pg_typeof(t1 + tstz2);
    when 'time-interval' then
      t := pg_typeof(t1 + i2);

    -- Plain "timestamp" row.
    when 'ts-date' then
      t := pg_typeof(ts1 + d2);
    when 'ts-time' then
      t := pg_typeof(ts1 + t2);
    when 'ts-ts' then
      t := pg_typeof(ts1 + ts2);
    when 'ts-tstz' then
      t := pg_typeof(ts1 + tstz2);
    when 'ts-interval' then
      t := pg_typeof(ts1 + i2);

    -- "timestamptz" row.
    when 'tstz-date' then
      t := pg_typeof(tstz1 + d2);
    when 'tstz-time' then
      t := pg_typeof(tstz1 + t2);
    when 'tstz-ts' then
      t := pg_typeof(tstz1 + ts2);
    when 'tstz-tstz' then
      t := pg_typeof(tstz1 + tstz2);
    when 'tstz-interval' then
      t := pg_typeof(tstz1 + i2);

    -- "interval" row.
    when 'interval-date' then
      t := pg_typeof(i1 + d2);
    when 'interval-time' then
      t := pg_typeof(i1 + t2);
    when 'interval-ts' then
      t := pg_typeof(i1 + ts2);
    when 'interval-tstz' then
      t := pg_typeof(i1 + tstz2);
    when 'interval-interval' then
      t := pg_typeof(i1 + i2);
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
  t := type_from_date_time_addition_overload(i);
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
  t := type_from_date_time_addition_overload(i);
  assert false, 'Unexpected';

-- 42725: operator is not unique...
exception when ambiguous_function then
  null;
end;
$body$;

drop function if exists date_time_addition_overloads_report() cascade;

create function date_time_addition_overloads_report()
  returns table(z text)
  language plpgsql
as $body$
begin
  -- 15 legal additions in all
  z := 'date-time:         '||type_from_date_time_addition_overload('date-time')::text;           return next;
  z := 'date-interval:     '||type_from_date_time_addition_overload('date-interval')::text;       return next;
  z := '';                                                                                        return next;

  z := 'time-date:         '||type_from_date_time_addition_overload('time-date')::text;           return next;
  z := 'time-ts:           '||type_from_date_time_addition_overload('time-ts')::text;             return next;
  z := 'time-tstz:         '||type_from_date_time_addition_overload('time-tstz')::text;           return next;
  z := 'time-interval:     '||type_from_date_time_addition_overload('time-interval')::text;       return next;
  z := '';                                                                                        return next;

  z := 'ts-time:           '||type_from_date_time_addition_overload('ts-time')::text;             return next;
  z := 'ts-interval:       '||type_from_date_time_addition_overload('ts-interval')::text;         return next;
  z := '';                                                                                        return next;

  z := 'tstz-time:         '||type_from_date_time_addition_overload('tstz-time')::text;           return next;
  z := 'tstz-interval:     '||type_from_date_time_addition_overload('tstz-interval')::text;       return next;
  z := '';                                                                                        return next;

  z := 'interval-date:     '||type_from_date_time_addition_overload('interval-date')::text;       return next;
  z := 'interval-time:     '||type_from_date_time_addition_overload('interval-time')::text;       return next;
  z := 'interval-ts:       '||type_from_date_time_addition_overload('interval-ts')::text;         return next;
  z := 'interval-tstz:     '||type_from_date_time_addition_overload('interval-tstz')::text;       return next;
  z := 'interval-interval: '||type_from_date_time_addition_overload('interval-interval')::text;   return next;

  -- 10 illegal additions in all
  call confirm_expected_42883('date-date');
  call confirm_expected_42883('date-ts');
  call confirm_expected_42883('date-tstz');

  call confirm_expected_42725('time-time');

  call confirm_expected_42883('ts-date');
  call confirm_expected_42883('ts-ts');
  call confirm_expected_42883('ts-tstz');

  call confirm_expected_42883('tstz-date');
  call confirm_expected_42883('tstz-ts');
  call confirm_expected_42883('tstz-tstz');
end;
$body$;

select z from date_time_addition_overloads_report();
```

This is the result:

```output
 date-time:         timestamp without time zone
 date-interval:     timestamp without time zone

 time-date:         timestamp without time zone
 time-ts:           timestamp without time zone
 time-tstz:         timestamp with time zone
 time-interval:     time without time zone

 ts-time:           timestamp without time zone
 ts-interval:       timestamp without time zone

 tstz-time:         timestamp with time zone
 tstz-interval:     timestamp with time zone

 interval-date:     timestamp without time zone
 interval-time:     time without time zone
 interval-ts:       timestamp without time zone
 interval-tstz:     timestamp with time zone
 interval-interval: interval
```
