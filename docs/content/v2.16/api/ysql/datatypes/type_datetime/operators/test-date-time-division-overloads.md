---
title: Test the date-time division overloads [YSQL]
headerTitle: Test the date-time division overloads
linkTitle: Test division overloads
description: Presents code that tests the date-time division overloads. [YSQL]
menu:
  v2.16:
    identifier: test-date-time-division-overloads
    parent: date-time-operators
    weight: 50
type: docs
---

Try this:

```plpgsql
drop procedure if exists test_date_time_division_overloads(text) cascade;

create procedure test_date_time_division_overloads(i in text)
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
begin
  case i
    -- "date" row.
    when 'date-date' then
      if d2 / d1 then null; end if;
    when 'date-time' then
      if d2 / t1 then null; end if;
    when 'date-ts' then
      if d2 / ts1 then null; end if;
    when 'date-tstz' then
      if d2 / tstz1 then null; end if;
    when 'date-interval' then
      if d2 / i1 then null; end if;

    -- "time" row.
    when 'time-date' then
      if t2 / d1 then null; end if;
    when 'time-time' then
      if t2 / t1 then null; end if;
    when 'time-ts' then
      if t2 / ts1 then null; end if;
    when 'time-tstz' then
      if t2 / tstz1 then null; end if;
    when 'time-interval' then
      if t2 / i1 then null; end if;

    -- Plain "timestamp" row.
    when 'ts-date' then
      if ts2 / d1 then null; end if;
    when 'ts-time' then
      if ts2 / t1 then null; end if;
    when 'ts-ts' then
      if ts2 / ts1 then null; end if;
    when 'ts-tstz' then
      if ts2 / tstz1 then null; end if;
    when 'ts-interval' then
      if ts2 / i1 then null; end if;

    -- "timestamptz" row.
    when 'tstz-date' then
      if tstz2 / d1 then null; end if;
    when 'tstz-time' then
      if tstz2 / t1 then null; end if;
    when 'tstz-ts' then
      if tstz2 / ts1 then null; end if;
    when 'tstz-tstz' then
      if tstz2 / tstz1 then null; end if;
    when 'tstz-interval' then
      if tstz2 / i1 then null; end if;

    -- "interval" row.
    when 'interval-date' then
      if i2 / d1 then null; end if;
    when 'interval-time' then
      if i2 / t1 then null; end if;
    when 'interval-ts' then
      if i2 / ts1 then null; end if;
    when 'interval-tstz' then
      if i2 / tstz1 then null; end if;
    when 'interval-interval' then
      if i2 / i1 then null; end if;
  end case;
end;
$body$;

drop procedure if exists confirm_expected_42883(text) cascade;

create procedure confirm_expected_42883(i in text)
  language plpgsql
as $body$
begin
  call test_date_time_division_overloads(i);
  assert true, 'Unexpected';

-- 42883: operator does not exist...
exception when undefined_function then
  null;
end;
$body$;

do $body$
begin
  call confirm_expected_42883('date-date');
  call confirm_expected_42883('date-time');
  call confirm_expected_42883('date-ts');
  call confirm_expected_42883('date-tstz');
  call confirm_expected_42883('date-interval');

  call confirm_expected_42883('time-date');
  call confirm_expected_42883('time-time');
  call confirm_expected_42883('time-ts');
  call confirm_expected_42883('time-tstz');
  call confirm_expected_42883('time-interval');

  call confirm_expected_42883('ts-date');
  call confirm_expected_42883('ts-time');
  call confirm_expected_42883('ts-ts');
  call confirm_expected_42883('ts-tstz');
  call confirm_expected_42883('ts-interval');

  call confirm_expected_42883('tstz-date');
  call confirm_expected_42883('tstz-time');
  call confirm_expected_42883('tstz-ts');
  call confirm_expected_42883('tstz-tstz');
  call confirm_expected_42883('tstz-interval');

  call confirm_expected_42883('interval-date');
  call confirm_expected_42883('interval-time');
  call confirm_expected_42883('interval-ts');
  call confirm_expected_42883('interval-tstz');
  call confirm_expected_42883('interval-interval');
end;
$body$;
```

The final anonymous block finishes silently, confirming that division is not supported between a pair of _date-time_ data types.
