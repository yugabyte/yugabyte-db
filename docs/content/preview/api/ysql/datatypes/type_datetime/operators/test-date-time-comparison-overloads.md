---
title: Test the date-time comparison overloads [YSQL]
headerTitle: Test the date-time comparison overloads
linkTitle: Test comparison overloads
description: Presents code that tests the date-time comparison overloads. [YSQL]
menu:
  preview:
    identifier: test-date-time-comparison-overloads
    parent: date-time-operators
    weight: 10
type: docs
---

Tests (not shown here) confirm that if the `>` operator is legal between a particular pair of different _date-time_ data types, then all of the other comparison operators, `<=`, `=`, `>=`, `>`, and `<>`, are also legal. Consider these two overloads of some operator, _A_:

```output
data_type_1_value A data_type_2_value
```

and:

```output
data_type_2_value A data_type_1_value
```

The fact that one is legal does not imply that the other is. The legality of each is informed by its own mental model; and each requires its own test.

The [start page](../../../type_datetime/) of the overall _date-time_ section explains why _timetz_ is not covered. So there are five _date-time_ data types to consider here and therefore twenty-five overloads to test. (You can't assume that you can always compare values of the same data type. For example, if the data type represents the latitude and longitude of a location, then you can't ask which location is greater than the other.)

The following code implements all of the tests. The design of the code (it tests the legal comparisons and the illegal comparisons separately) was informed by _ad hoc_ tests during its development.

Try this:

```plpgsql
drop procedure if exists test_date_time_comparison_overloads(text) cascade;

create procedure test_date_time_comparison_overloads(i in text)
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
      if d2 > d1 then null; end if;
    when 'date-time' then
      if d2 > t1 then null; end if;
    when 'date-ts' then
      if d2 > ts1 then null; end if;
    when 'date-tstz' then
      if d2 > tstz1 then null; end if;
    when 'date-interval' then
      if d2 > i1 then null; end if;

    -- "time" row.
    when 'time-date' then
      if t2 > d1 then null; end if;
    when 'time-time' then
      if t2 > t1 then null; end if;
    when 'time-ts' then
      if t2 > ts1 then null; end if;
    when 'time-tstz' then
      if t2 > tstz1 then null; end if;
    when 'time-interval' then
      if t2 > i1 then null; end if;

    -- Plain "timestamp" row.
    when 'ts-date' then
      if ts2 > d1 then null; end if;
    when 'ts-time' then
      if ts2 > t1 then null; end if;
    when 'ts-ts' then
      if ts2 > ts1 then null; end if;
    when 'ts-tstz' then
      if ts2 > tstz1 then null; end if;
    when 'ts-interval' then
      if ts2 > i1 then null; end if;

    -- "timestamptz" row.
    when 'tstz-date' then
      if tstz2 > d1 then null; end if;
    when 'tstz-time' then
      if tstz2 > t1 then null; end if;
    when 'tstz-ts' then
      if tstz2 > ts1 then null; end if;
    when 'tstz-tstz' then
      if tstz2 > tstz1 then null; end if;
    when 'tstz-interval' then
      if tstz2 > i1 then null; end if;

    -- "interval" row.
    when 'interval-date' then
      if i2 > d1 then null; end if;
    when 'interval-time' then
      if i2 > t1 then null; end if;
    when 'interval-ts' then
      if i2 > ts1 then null; end if;
    when 'interval-tstz' then
      if i2 > tstz1 then null; end if;
    when 'interval-interval' then
      if i2 > i1 then null; end if;
  end case;
end;
$body$;

drop procedure if exists confirm_expected_42883(text) cascade;

create procedure confirm_expected_42883(i in text)
  language plpgsql
as $body$
begin
  call test_date_time_comparison_overloads(i);
  assert true, 'Unexpected';

-- 42883: operator does not exist...
exception when undefined_function then
  null;
end;
$body$;

do $body$
begin
  -- 13 legal comparisons in all
  call test_date_time_comparison_overloads('date-date');
  call test_date_time_comparison_overloads('date-ts');
  call test_date_time_comparison_overloads('date-tstz');

  call test_date_time_comparison_overloads('time-time');
  call test_date_time_comparison_overloads('time-interval');

  call test_date_time_comparison_overloads('ts-date');
  call test_date_time_comparison_overloads('ts-ts');
  call test_date_time_comparison_overloads('ts-tstz');

  call test_date_time_comparison_overloads('tstz-date');
  call test_date_time_comparison_overloads('tstz-ts');
  call test_date_time_comparison_overloads('tstz-tstz');

  call test_date_time_comparison_overloads('interval-time');
  call test_date_time_comparison_overloads('interval-interval');

  -- 12 illegal comparisons in all
  call confirm_expected_42883('date-time');
  call confirm_expected_42883('date-interval');

  call confirm_expected_42883('time-date');
  call confirm_expected_42883('time-ts');
  call confirm_expected_42883('time-tstz');

  call confirm_expected_42883('ts-time');
  call confirm_expected_42883('ts-interval');

  call confirm_expected_42883('tstz-time');
  call confirm_expected_42883('tstz-interval');

  call confirm_expected_42883('interval-date');
  call confirm_expected_42883('interval-ts');
  call confirm_expected_42883('interval-tstz');
end;
$body$;
```

The final anonymous block finishes silently, confirming the legality of the first thirteen comparisons and the illegality of the second twelve comparisons.
