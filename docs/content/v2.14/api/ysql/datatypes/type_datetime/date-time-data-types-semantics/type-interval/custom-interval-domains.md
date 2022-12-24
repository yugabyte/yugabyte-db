---
title: Defining and using custom interval domains [YSQL]
headerTitle: Custom domain types for specializing the native interval functionality
linkTitle: Custom interval domains
description: Explains how to define and use custom interval domain types to specialize the native interval functionality. [YSQL]
menu:
  v2.14:
    identifier: custom-interval-domains
    parent: type-interval
    weight: 90
type: docs
---

{{< tip title="Download the kit to create the custom interval domains." >}}
The code that this page presents is included in a larger set of useful reusable _date-time_ code. In particular, it also installs the [User-defined _interval_ utility functions](../interval-utilities/). The custom _interval_ domains code depends on some of these utilities. See the section [Download the _.zip_ file to create the reusable code that this overall major section describes](../../../download-date-time-utilities/).
{{< /tip >}}

Each of the sections [The moment-moment overloads of the "-" operator](../interval-arithmetic/moment-moment-overloads-of-minus/) and [The moment-_interval overloads_ of the "+" and "-" operators](../interval-arithmetic/moment-interval-overloads-of-plus-and-minus/) makes the point that hybrid _interval_ arithmetic is dangerous and recommends that you should ensure that you create and use only _"pure months"_,  _"pure seconds"_, or _"pure days"_ _interval_ values. And they recommend the adoption of the approach that this section describes so that your good practice will be ensured by using its APIs rather than the native _interval_ functionality.

The basic idea is to create a user-defined domain for each of the three kinds of _"pure"_ _interval_, defining each with a constraint function that ensures the purity and checks that the _mm_, _dd_, or _ss_ component of the _[\[mm, dd, ss\]](../interval-representation/)_ internal representation lies within a meaningful range (see the section [Interval value limits](../interval-limits/)). Domain-specific functions create a new value of the domain by each of these methods:

- From a parameterization that uses values, respectively: for _years_ and _months_; for _days_; or for _hours_, _minutes_, and _seconds_.
- By subtracting one _timestamptz_ value from another.
- By multiplying an existing value of the domain by a real number.

## Create the three domains

The design of the code is the same for each of the three domains. The code for the second and third domains is trivially derived, using copy and massage, from the code for the first domain. Each of the functions _interval_X_ok()_ (where X is one of _months_, _days_, or _seconds_) can raise the _23514_ pre-defined error (mapped to _check_violation_ in PL/pgSQL) with one of these error texts:

```output
value for domain interval_months_t violates check constraint "interval_months_ok".
```

or:

```output
value for domain interval_days_t violates check constraint "interval_days_ok".
```

or:

```output
value for domain interval_seconds_t violates check constraint "interval_seconds_ok".
```

The hint is function-specific and reflects the detail of how the constraint is violated.

### The "interval_months_t" domain

The constraint function _interval_months_ok()_ checks that only the _mm_ component of the _[mm, dd, ss]_ tuple is non-zero and that it lies within a sensible range. It uses the value _3,587,867_ for the constant _max_mm_ for the range check. Create the _interval_months_t_ domain thus:

```plpgsql
drop domain if exists interval_months_t cascade;
drop function if exists interval_months_ok(interval) cascade;
drop function if exists mm_value_ok(int) cascade;

create function mm_value_ok(mm in int)
  returns text
  language plpgsql
as $body$
declare
  max_mm constant bigint not null := 3587867;
begin
  return
    case abs(mm) > max_mm
      when true then 'Bad mm: '||mm::text||'. Must be in [-'||max_mm||', '||max_mm||'].'
      else           ''
    end;
end;
$body$;

create function interval_months_ok(i in interval)
  returns boolean
  language plpgsql
as $body$
begin
  if i is null then
    return true;
  else
    declare
      mm_dd_ss       constant interval_mm_dd_ss_t not null := interval_mm_dd_ss(i);
      mm             constant int                 not null := mm_dd_ss.mm;
      dd             constant int                 not null := mm_dd_ss.dd;
      ss             constant double precision    not null := mm_dd_ss.ss;
      chk_violation  constant text                not null := '23514';
      msg            constant text                not null :=
                       'value for domain interval_months_t violates check constraint "interval_months_ok".';
    begin
      if dd <> 0 or ss <> 0.0 then
        begin
          raise exception using
            errcode = chk_violation,
            message = msg,
            hint    = case
                        when dd <> 0 and ss <> 0.0 then  'dd = '||dd::text||'. ss = '||ss::text||'. Both must be zero'
                        when dd <> 0               then  'dd = '||dd::text||'. Both dd and ss must be zero'
                        when             ss <> 0.0 then  'ss = '||ss::text||'. Both dd and ss must be zero'
                      end;
        end;
      end if;

      declare
        hint constant text not null := mm_value_ok(mm);
      begin
        if hint <> '' then
          raise exception using
            errcode = chk_violation,
            message = msg,
            hint    = hint;
        end if;
      end;

      return true;
    end;
  end if;
end;
$body$;

create domain interval_months_t as interval
constraint interval_months_ok check(interval_months_ok(value));
```

Do these four basic sanity tests in order:

```plpgsql
select ('1 month 1 day'::interval)::interval_months_t;
select ('1 month 0.000001 sec'::interval)::interval_months_t;
select ('1 month 1 day 0.000001 sec'::interval)::interval_months_t;
select ('3587868 month'::interval)::interval_months_t;
```

They cause, respectively, these expected errors:

```output
ERROR:  23514: value for domain interval_months_t violates check constraint "interval_months_ok".
HINT:  dd = 1. Both dd and ss must be zero
```

```output
ERROR:  23514: value for domain interval_months_t violates check constraint "interval_months_ok".
HINT:  ss = 1e-06. Both dd and ss must be zero
```

```output
ERROR:  23514: value for domain interval_months_t violates check constraint "interval_months_ok".
HINT:  dd = 1. ss = 1e-06. Both must be zero
```

```output
ERROR:  23514: value for domain interval_months_t violates check constraint "interval_months_ok".
HINT:  Bad mm: 3587868. Must be in [-3587867, 3587867].
```

### The "interval_days_t" domain

The constraint function _interval_days_ok()_ checks that only the _dd_ component of the _[mm, dd, ss]_ tuple is non-zero and that it lies within a sensible range. It uses the value _109,203,489_ for the constant _max_dd_ for the range check. Create the _interval_days_t_ domain thus:

```plpgsql
drop domain if exists interval_days_t cascade;
drop function if exists interval_days_ok(interval) cascade;
drop function if exists dd_value_ok(int) cascade;

create function dd_value_ok(dd in int)
  returns text
  language plpgsql
as $body$
declare
  max_dd constant bigint not null := 109203489;
begin
  return
    case abs(dd) > max_dd
      when true then 'Bad dd: '||dd::text||'. Must be in [-'||max_dd||', '||max_dd||'].'
      else           ''
    end;
end;
$body$;

create function interval_days_ok(i in interval)
  returns boolean
  language plpgsql
as $body$
begin
  if i is null then
    return true;
  else
    declare
      mm_dd_ss       constant interval_mm_dd_ss_t not null := interval_mm_dd_ss(i);
      mm             constant int                              not null := mm_dd_ss.mm;
      dd             constant int                              not null := mm_dd_ss.dd;
      ss             constant double precision                 not null := mm_dd_ss.ss;
      chk_violation  constant text                             not null := '23514';
      msg            constant text                             not null :=
                       'value for domain interval_days_t violates check constraint "interval_days_ok".';
    begin
      if mm <> 0 or ss <> 0.0 then
        begin
          raise exception using
            errcode = chk_violation,
            message = msg,
            hint    = case
                        when mm <> 0 and ss <> 0.0 then  'mm = '||mm::text||'. ss = '||ss::text||'. Both must be zero'
                        when mm <> 0               then  'mm = '||mm::text||'. Both mm and ss must be zero'
                        when             ss <> 0.0 then  'ss = '||ss::text||'. Both mm and ss must be zero'
                      end;
        end;
      end if;

      declare
        hint constant text not null := dd_value_ok(dd);
      begin
        if hint <> '' then
          raise exception using
            errcode = chk_violation,
            message = msg,
            hint    = hint;
        end if;
      end;

      return true;
    end;
  end if;
end;
$body$;

create domain interval_days_t as interval
constraint interval_days_ok check(interval_days_ok(value));
```

Do these four basic sanity tests in order:

```plpgsql
select ('1 month 1 day'::interval)::interval_days_t;
select ('1 day 0.000001 sec'::interval)::interval_days_t;
select ('1 month 1 day 0.000001 sec'::interval)::interval_days_t;
select ('109203490 day'::interval)::interval_days_t;
```

They cause, respectively, these expected errors:

```output
ERROR:  23514: value for domain interval_days_t violates check constraint "interval_days_ok".
HINT:  mm = 1. Both mm and ss must be zero
```

```output
ERROR:  23514: value for domain interval_days_t violates check constraint "interval_days_ok".
HINT:  ss = 1e-06. Both mm and ss must be zero
```

```output
ERROR:  23514: value for domain interval_days_t violates check constraint "interval_days_ok".
HINT:  mm = 1. ss = 1e-06. Both must be zero
```

```output
ERROR:  23514: value for domain interval_days_t violates check constraint "interval_days_ok".
HINT:  Bad dd: 109203490. Must be in [-109203489, 109203489].
```

### The "interval_seconds_t" domain

The constraint function _interval_seconds_ok()_ checks that only the _ss_ component of the _[mm, dd, ss]_ tuple is non-zero and that it lies within a sensible range. It uses the value _7,730,941,132,799_ for the constant _max_ss_ for the range check. Create the _interval_seconds_t_ domain thus:

```plpgsql
drop domain if exists interval_seconds_t cascade;
drop function if exists interval_seconds_ok(interval) cascade;
drop function if exists ss_value_ok(double precision) cascade;

create function ss_value_ok(ss in double precision)
  returns text
  language plpgsql
as $body$
declare
  max_ss constant double precision not null := 7730941132799.0;
begin
  return
    case abs(ss) > max_ss
      when true then 'Bad ss: '||ss::text||'. Must be in [-'||max_ss||', '||max_ss||'].'
      else           ''
    end;
end;
$body$;

create function interval_seconds_ok(i in interval)
  returns boolean
  language plpgsql
as $body$
begin
  if i is null then
    return true;
  else
    declare
      mm_dd_ss       constant interval_mm_dd_ss_t not null := interval_mm_dd_ss(i);
      mm             constant int                 not null := mm_dd_ss.mm;
      dd             constant int                 not null := mm_dd_ss.dd;
      ss             constant double precision    not null := mm_dd_ss.ss;
      chk_violation  constant text                not null := '23514';
      msg            constant text                not null :=
                       'value for domain interval_seconds_t violates check constraint "interval_seconds_ok".';
    begin
      if mm <> 0 or dd <> 0 then
        begin
          raise exception using
            errcode = chk_violation,
            message = msg,
            hint    = case
                        when mm <> 0 and dd <> 0 then  'mm = '||mm::text||'. dd = '||dd::text||'. Both must be zero'
                        when mm <> 0             then  'mm = '||mm::text||'. Both mm and dd must be zero'
                        when             dd <> 0 then  'dd = '||dd::text||'. Both mm and dd must be zero'
                      end;
        end;
      end if;

      declare
        hint constant text not null := ss_value_ok(ss);
      begin
        if hint <> '' then
          raise exception using
            errcode = chk_violation,
            message = msg,
            hint    = hint;
        end if;
      end;

      return true;
    end;
  end if;
end;
$body$;

create domain interval_seconds_t as interval
constraint interval_seconds_ok check(interval_seconds_ok(value));
```

Do these four basic sanity tests in order:

```plpgsql
select ('1 month 1 sec'::interval)::interval_seconds_t;
select ('1 day 1 sec'::interval)::interval_seconds_t;
select ('1 month 1 day 1 sec'::interval)::interval_seconds_t;
select make_interval(secs=>7730941132799.01)::interval_seconds_t;
```

They cause, respectively, these expected errors:

```output
ERROR:  23514: value for domain interval_seconds_t violates check constraint "interval_seconds_ok".
HINT:  mm = 1. Both mm and dd must be zero
```

```output
ERROR:  23514: value for domain interval_seconds_t violates check constraint "interval_seconds_ok".
HINT:  dd = 1. Both mm and dd must be zero
```

```output
ERROR:  23514: value for domain interval_seconds_t violates check constraint "interval_seconds_ok".
HINT:  mm = 1. dd = 1. Both must be zero
```

```output
ERROR:  23514: value for domain interval_seconds_t violates check constraint "interval_seconds_ok".
HINT:  Bad ss: 7730941132799.01. Must be in [-7730941132799, 7730941132799].
```

### Implement the domain-specific functionality

The high-level design of the code is the same for each of the three domains. The detail is domain-specific. The variation is greatest for the function that subtracts one _timestamptz_ value from another.

{{< note title="Only 'timestamptz' overloads are presented here." >}}
If you need to do _interval_ arithmetic with values of the plain _timestamp_ data type, then you'll need to implement overloads for that data type that correspond directly to the overloads for the _timestamptz_ data type that are shown here. You can derive these more-or-less mechanically, with just a little thought.

If you need to do _interval_ arithmetic with values of the _time_ data type, then you'll need only to implement overloads to provide functionality for the _interval_seconds_t_ domain because subtracting one _time_ value from another cannot produce a result as long as _one day_ and because it's meaningless to add _days_, _months_, or _years_ to a pure time of day.

Both of these exercises are left to the reader.

Notice that if you follow Yugabyte's advice and persist only _timestamptz_ values, then it's very unlikely that you will need to do _interval_ arithmetic on values of the other moment data types.
{{< /note >}}

Each of the three domains, _interval_months_t_, _interval_days_t_, and _interval_seconds_t_, is provided with a matching set of three functions. Each of these constructs a value of the domain: _either_ using an explicit parameterization; _or_ by subtracting one _timestamptz_ value from another; _or_ by multiplying an existing domain value by a number.

The _interval_months_t_ domain has these functions:

- [function interval_months (years in int default 0, months in int default 0) returns interval_months_t](#function-interval-months-years-in-int-default-0-months-in-int-default-0-returns-interval-months-t)
- [function interval_months (t_finish in timestamptz, t_start in timestamptz) returns interval_months_t](#function-interval-months-t-finish-in-timestamptz-t-start-in-timestamptz-returns-interval-months-t)
- [function interval_months (i in interval_months_t, f in double precision) returns interval_months_t](#function-interval-months-i-in-interval-months-t-f-in-double-precision-returns-interval-months-t)

The _interval_days_t_ domain has these functions:

- [function interval_days (days in int default 0) returns interval_days_t](#function-interval-days-days-in-int-default-0-returns-interval-days-t)
- [function interval_days (t_finish in timestamptz, t_start in timestamptz) returns interval_days_t](#function-interval-days-t-finish-in-timestamptz-t-start-in-timestamptz-returns-interval-days-t)
- [function interval_days (i in interval_days_t, f in double precision) returns interval_days_t](#function-interval-days-i-in-interval-days-t-f-in-double-precision-returns-interval-days-t)

The _interval_seconds_t_ domain has these functions:

- [function interval_seconds (hours in int default 0, mins in int default 0, secs in double precision default 0.0) returns interval_seconds_t](#function-interval-seconds-hours-in-int-default-0-mins-in-int-default-0-secs-in-double-precision-default-0-0-returns-interval-seconds-t)
- [function interval_seconds (t_finish in timestamptz, t_start in timestamptz) returns interval_seconds_t](#function-interval-seconds-t-finish-in-timestamptz-t-start-in-timestamptz-returns-interval-seconds-t)
- [function interval_seconds (i in interval_seconds_t, f in double precision) returns interval_seconds_t](#function-interval-seconds-i-in-interval-seconds-t-f-in-double-precision-returns-interval-seconds-t)

### The "interval_months_t" domain's functionality

##### function interval_months (years in int default 0, months in int default 0)  returns interval_months_t

This function is parameterized so that you can produce only a _"pure months_ _interval_ value.

```plpgsql
drop function if exists interval_months(int, int) cascade;
create function interval_months(years in int default 0, months in int default 0)
  returns interval_months_t
  language plpgsql
as $body$
declare
  mm             constant int  not null := years*12 + months;
  hint           constant text not null := mm_value_ok(mm);
  chk_violation  constant text not null := '23514';
  msg            constant text not null :=
                   'value for domain interval_months_t violates check constraint "interval_months_ok".';
begin
  if hint <> '' then
    raise exception using
      errcode = chk_violation,
      message = msg,
      hint    = hint;
  end if;
  return make_interval(years=>years, months=>months);
end;
$body$;
```

##### function interval_months (t_finish in timestamptz, t_start in timestamptz) returns interval_months_t

This function provides critically useful functionality that is simply missing in the native implementation. There is no way, by directly subtracting one _timestamptz_ value from another, to produce a _"pure months"_ _interval_ value (or even a hybrid _interval_ value whose internal _mm_ component is non-zero). You have to write your own implementation.

When a _months_ _interval_ is added to a starting moment, the finish moment always has the same day-number (in whatever is the finish month) as the start moment has. (If this is not possible, because the target day-number doesn't exist in the target month, then the target day-number is set to that month's biggest day-number.) And it has the same time of day.

When one moment is subtracted from another, the day-number and the time of day of each moment are likely to differ. This implies that the _interval_ result from subtracting one moment from another cannot necessarily produce the finish moment when added back to the start moment.

The specification for this function therefore must depend on asserting a plausible rule. This implementation simply ignores the day number and the time of day of each of the two input moments.

```plpgsql
drop function if exists interval_months(timestamptz, timestamptz) cascade;
create function interval_months(t_finish in timestamptz, t_start in timestamptz)
  returns interval_months_t
  language plpgsql
as $body$
declare
  finish_year   constant int     not null := extract(year  from t_finish);
  finish_month  constant int     not null := extract(month from t_finish);
  finish_AD_BC  constant text    not null := to_char(t_finish, 'BC');
  finish_is_BC  constant boolean not null :=
    case
      when finish_AD_BC = 'BC' then true
      when finish_AD_BC = 'AD' then false
    end;

  start_year   constant int not null := extract(year  from t_start);
  start_month  constant int not null := extract(month from t_start);
  start_AD_BC  constant text    not null := to_char(t_start, 'BC');
  start_is_BC  constant boolean not null :=
    case
      when start_AD_BC = 'BC' then true
      when start_AD_BC = 'AD' then false
    end;

  -- There is no "year zero". Therefore, when the two input moments straddle
  -- the AD/BC boundary, we must subtract 12 months to the computed months difference
  diff_as_months constant int not null :=
    (
      (finish_year*12 + finish_month)
      -
      (start_year*12  + start_month)
    )
    - case (finish_is_BC = start_is_BC)
        when true then 0
        else           12
      end;

  hint           constant text not null := mm_value_ok(diff_as_months);
  chk_violation  constant text not null := '23514';
  msg            constant text not null :=
                   'value for domain interval_months_t violates check constraint "interval_months_ok".';
begin
  -- You can reason that "interval_months(largest_legal_timestamptz_value, smallest_legal_timestamptz_value)"
  -- give mm = 3587867 and that because mm_value_ok() tests if this value is exceded, "hint" will always be
  -- the empty string and that the following test is unnecessary. It's done for symmetry and completeness.
  if hint <> '' then
    raise exception using
      errcode = chk_violation,
      message = msg,
      hint    = hint;
  end if;
  return interval_months(months=>diff_as_months);
end;
$body$;
```

Test it like this:

```plpgsql
select interval_months('2020-06-07 13:47:19 UTC'::timestamptz, '2013-10-23 17:42:09 UTC'::timestamptz);
```

This is the result:

```output
 6 years 8 mons
```

##### function interval_months (i in interval_months_t, f in double precision) returns interval_months_t

The logic of this function is trivial. But it's essential in order to maintain the status of the _interval_months_t_ value as _"pure months"_ under multiplication or division. See the section [Multiplying or dividing an _interval_ value by a number](../interval-arithmetic/interval-number-multiplication/). If you multiply a native _interval_ value by a real number (or divide it), then it's more than likely that fractional months will spill down to days and beyond. Try this:

```plpgsq
select make_interval(years=>3, months=>99)*0.5378;
```

This is the result:

```output
 6 years 18 days 02:09:36
```

Try the native `*` operator on the corresponding _interval_months_t_ value instead:

```plpgsql
select (interval_months(years=>3, months=>99)*0.5378)::interval_months_t;
```

The attempt causes this error:

```output
ERROR:  23514: value for domain interval_months_t violates check constraint "interval_months_ok".
HINT:  dd = 18. ss = 7776. Both must be zero
```

The function _interval\_months(interval\_months\_t, double precision)_ fixes this. Create it thus:

```plpgsql
drop function if exists interval_months(interval_months_t, double precision) cascade;
create function interval_months(i in interval_months_t, f in double precision)
  returns interval_months_t
  language plpgsql
as $body$
declare
  mm             constant double precision  not null := (interval_mm_dd_ss(i)).mm;
  mm_x_f         constant int               not null := round(mm*f);
  hint           constant text              not null := mm_value_ok(mm_x_f);
  chk_violation  constant text              not null := '23514';
  msg            constant text              not null :=
                   'value for domain interval_months_t violates check constraint "interval_months_ok".';
begin
  if hint <> '' then
    raise exception using
      errcode = chk_violation,
      message = msg,
      hint    = hint;
  end if;
  return interval_months(months=>mm_x_f);
end;
$body$;
```

Test it like this:

```plpgsql
select interval_months(interval_months(years=>3, months=>99), 0.5378);
```

This is the result:

```output
6 years 1 mon
```

Compare this with the result (above) that the native `*` operator produces with a native _interval_ value:

```output
6 years 18 days 02:09:36
```

The "impure" part, _18 days 02:09:36_, is more than half way through the month, so the approximation is what you'd want. More significantly, you must maintain the purity of the _interval_ value in order to bring understandable semantics under subsequent operations like adding the value to a _timestamptz_ value.

### The "interval_days_t" domain's functionality

##### function interval_days (days in int default 0)  returns interval_days_t

This function is parameterized so that you can produce only a _"pure days_ _interval_ value.

```plpgsql
drop function if exists interval_days(int) cascade;
create function interval_days(days in int default 0)
  returns interval_days_t
  language plpgsql
as $body$
declare
  hint           constant text not null := dd_value_ok(days);
  chk_violation  constant text not null := '23514';
  msg            constant text not null :=
                   'value for domain interval_days_t violates check constraint "interval_days_ok".';
begin
  if hint <> '' then
    raise exception using
      errcode = chk_violation,
      message = msg,
      hint    = hint;
  end if;
  return make_interval(days=>days);
end;
$body$;
```

##### function interval_days (t_finish in timestamptz, t_start in timestamptz) returns interval_days_t

This function provides critically useful functionality that is simply missing in the native implementation. There is no way, by directly subtracting one _timestamptz_ value from another, to guarantee that you produce a _"pure days"_ _interval_ value. You have to write your own implementation.

When a _days_ _interval_ is added to a starting moment, the finish moment always has the same time of day. But when one moment is subtracted from another, the times of day of each moment are likely to differ.

This implies that the _interval_ result from subtracting one moment from another cannot necessarily produce the finish moment when added back to the start moment.

The specification for this function therefore must depend on asserting a plausible rule. This implementation simply ignores the time of day of each of the two input moments.

```plpgsql
drop function if exists interval_days(timestamptz, timestamptz) cascade;
create function interval_days(t_finish in timestamptz, t_start in timestamptz)
  returns interval_days_t
  language plpgsql
as $body$
declare
  d_finish       constant date not null := t_finish::date;
  d_start        constant date not null := t_start::date;
  dd             constant int  not null := d_finish - d_start;
  hint           constant text not null := dd_value_ok(dd);
  chk_violation  constant text not null := '23514';
  msg            constant text not null :=
                   'value for domain interval_days_t violates check constraint "interval_days_ok".';
begin
  -- You can reason that "interval_days(largest_legal_timestamptz_value, smallest_legal_timestamptz_value)"
  -- give dd = 109203489 and that because dd_value_ok() tests if this value is exceded, "hint" will always be
  -- the empty string and that the following test is unnecessary. It's done for symmetry and completeness.
  if hint <> '' then
    raise exception using
      errcode = chk_violation,
      message = msg,
      hint    = hint;
  end if;
  return interval_days(days=>dd);
end;
$body$;
```

Test it like this:

```plpgsql
select interval_days('2020-06-07 13:47:19 UTC'::timestamptz, '2013-10-23 17:42:09 UTC'::timestamptz);
```

This is the result:

```output
 2419 days
```

Notice that the two _timestamptz_ actual arguments for this test are the same as those that were used for the _interval_months(timestamptz, timestamptz)_ test. That produced the result _6 years 8 mons_. But _(6\*12 + 8)\*30_ is _2400_. The disagreement between _2419 days_ for the _interval_days()_ test and the effective _2400_ for the _interval_months()_ test reflects the critical difference between _"days interval"_ arithmetic semantics and _"months interval"_ arithmetic semantics.

##### function interval_days (i in interval_days_t, f in double precision) returns interval_days_t

The logic of this function is trivial. But it's essential in order to maintain the status of the _interval_days_t value_ as _"pure days"_ under multiplication or division. See the section [Multiplying or dividing an _interval_ value by a number](../interval-arithmetic/interval-number-multiplication/). If you multiply a native _interval_ value by a real number (or divide it), then it's more than likely that fractional days will spill down to hours and beyond. Try this:

```plpgsq
select make_interval(days=>99)*7.5378;
```

This is the result:

```output
 746 days 05:48:46.08
```

Try the native `*` operator on the corresponding _interval_days_t_ value instead:

```plpgsql
select (interval_days(days=>99)*7.5378)::interval_days_t;
```

The attempt causes this error:

```output
ERROR:  23514: value for domain interval_days_t violates check constraint "interval_days_ok".
HINT:  ss = 20926.08. Both mm and ss must be zero
```

The function _interval\_days(interval\_days\_t, double precision)_ fixes this. Create it thus:

```plpgsql
drop function if exists interval_days(interval_days_t, double precision) cascade;
create function interval_days(i in interval_days_t, f in double precision)
  returns interval_days_t
  language plpgsql
as $body$
declare
  dd             constant double precision not null := (interval_mm_dd_ss(i)).dd;
  dd_x_f         constant int              not null := round(dd*f);
  hint           constant text             not null := dd_value_ok(dd_x_f);
  chk_violation  constant text             not null := '23514';
  msg            constant text             not null :=
                   'value for domain interval_days_t violates check constraint "interval_days_ok".';
begin
  if hint <> '' then
    raise exception using
      errcode = chk_violation,
      message = msg,
      hint    = hint;
  end if;
  return interval_days(days=>dd_x_f);
end;
$body$;
```

Test it like this:

```plpgsql
select interval_days(interval_days(days=>99), 7.5378);
```

This is the result:

```output
 746 days
```

Compare this with the result (above) that the native `*` operator produces with a native _interval_ value:

```output
 746 days 05:48:46.08
```

You must accept the approximation in order to maintain the purity of the _interval_ value. This is the only way to bring understandable semantics under subsequent operations like adding the value to a _timestamptz_ value.

### The "interval_seconds_t" domain's functionality

##### function interval_seconds (hours in int default 0, mins in int default 0, secs in double precision default 0.0)  returns interval_seconds_t

{{< note title="it's important to work around the delayed manifestation of the 22008 error" >}}

Try this:

```plpgsql
do $body$
declare
  i constant interval not null := make_interval(secs=>7730941132800);
begin
  raise info 'Hello';
end;
$body$;
```

Notice _7730941132800_ is greater than the limit of _7730941132799_ that the subsection [Limit for the _ss_ field of the internal implementation](../interval-limits/#limit-for-the-ss-field-of-the-internal-implementation) gives. Yet it runs without error and reports "Hello". The error manifests only when you try to use the _interval_ value _i_. Try this:

```plpgsql
do $body$
declare
  i constant interval not null := make_interval(secs=>7730941132800);
  t          text     not null := '';
begin
  t := i::text;
  raise info 'i: %', t;
end;
$body$;
```

This causes the error "22008: interval out of range". It's reported for the assignment _t := i::text_. This is a different example of the effect that the "Limit for the _ss_ field of the internal implementation" subsection describes thus:

> Anomalously, the evaluation of _"i := make_interval(secs=>secs)"_ (where secs is 9,435,181,535,999) silently succeeds. But the attempts to use it with, for example, _extract(seconds from i)_ or _i::text_ both cause the "interval out of range" error.

Without working around this effect, The invocation _interval_seconds(secs=>7730941132800)_ would report this confusing error:

```output
ERROR:  22008: interval out of range
CONTEXT: ...while casting return value to function's return type
```

{{< /note >}}

The function _interval_seconds(int, int, double precision)_ is parameterized so that you can produce only a _"pure seconds"_ _interval_ value. Create it thus:

```plpgsql
drop function if exists interval_seconds(int, int, double precision) cascade;
create function interval_seconds(hours in int default 0, mins in int default 0, secs in double precision default 0.0)
  returns interval_seconds_t
  language plpgsql
as $body$
declare
  ss             constant double
                          precision not null := (hours::double precision)*60*60 + (mins::double precision)*60 + secs;
  hint           constant text not null := ss_value_ok(ss);
  chk_violation  constant text not null := '23514';
  msg            constant text not null :=
                   'value for domain interval_seconds_t violates check constraint "interval_seconds_ok".';
begin
  if hint <> '' then
    raise exception using
      errcode = chk_violation,
      message = msg,
      hint    = hint;
  end if;
  return make_interval(hours=>hours, mins=>mins, secs=>secs);
end;
$body$;
```

Do this basic sanity test:

```plpgsql
select interval_seconds(secs=>7730941132800);
```

This is the result:

```output
ERROR:  23514: value for domain interval_seconds_t violates check constraint "interval_seconds_ok".
HINT:  Bad ss: 7730941132800. Must be in [-7730941132799, 7730941132799].
```

##### function interval_seconds (t_finish in timestamptz, t_start in timestamptz) returns interval_seconds_t

This function provides critically useful functionality that is simply missing in the native implementation. There is no way to guarantee that you produce a _"pure seconds"_ _interval_ value unless you write your own implementation. (For anything bigger than _24 hours_, using the native functionality, you get a hybrid _"days-seconds"_ _interval_ value.)

```plpgsql
drop function if exists interval_seconds(timestamptz, timestamptz) cascade;
create function interval_seconds(t_finish in timestamptz, t_start in timestamptz)
  returns interval_seconds_t
  language plpgsql
as $body$
declare
  s_finish       constant double precision not null := extract(epoch from t_finish);
  s_start        constant double precision not null := extract(epoch from t_start);
  ss             constant double precision not null := s_finish - s_start;
  hint           constant text not null := ss_value_ok(ss);
  chk_violation  constant text not null := '23514';
  msg            constant text not null :=
                   'value for domain interval_seconds_t violates check constraint "interval_seconds_ok".';
begin
  if hint <> '' then
    raise exception using
      errcode = chk_violation,
      message = msg,
      hint    = hint;
  end if;
  return interval_seconds(secs=>ss);
end;
$body$;
```

Try this positive test:

```plpgsql
with c as (
  select
    interval_seconds(
      '247003-10-10 19:59:59 UTC'::timestamptz,
        '2020-01-01:12:00:00 UTC'::timestamptz)
    as i)
select
  interval_mm_dd_ss(i),
  i::text
from c;
```

This is the result:

```output
  interval_mm_dd_ss  |            i
---------------------+-------------------------
 (0,0,7730941132799) | 2147483647:59:58.999552
```

Notice that the result suffers from a tiny rounding error. It seems to be inconceivable that an application would need clock time semantics when the to-be-differenced _timestamptz_ values are about two hundred and fifty millennia apart—and so this rounding error won't matter.

Try this negative test:

```plpgsql
select
  interval_seconds(
    '247003-10-10 20:00:00 UTC'::timestamptz,
      '2020-01-01:12:00:00 UTC'::timestamptz);
```

As expected, it causes this error:

```output
ERROR:  23514: value for domain interval_seconds_t violates check constraint "interval_seconds_ok".
HINT:  Bad ss: 7730941132800. Must be in [-7730941132799, 7730941132799].
```

##### function interval_seconds (i in interval_seconds_t, f in double precision) returns interval_seconds_t

The logic of this function is trivial. Moreover, it isn't essential because the _ss_ field of the internal _[\[mm, dd, ss\]](../interval-representation/)_ tuple is a real number with microseconds precision and there is no "spill up" possibility from the _ss_ field to the _dd_, or _mm_, fields. Try this:

```plpgsq
select make_interval(hours=>99)*3.6297;
```

This is the result:

```output
 359:20:25.08
```

The native `*` operator on the corresponding _interval_seconds_t_ value also runs without error:

```plpgsql
select (interval_seconds(hours=>99)*3.6297)::interval_seconds_t;
```

It brings the same result as when you use the native _interval_.

The function _interval\_seconds(interval\_seconds\_t, double precision)_ is provided just in the interests of symmetry. Create it thus:

```plpgsql
drop function if exists interval_seconds(interval_seconds_t, double precision) cascade;
create function interval_seconds(i in interval_seconds_t, f in double precision)
  returns interval_seconds_t
  language plpgsql
as $body$
declare
  ss             constant double precision not null := (interval_mm_dd_ss(i)).ss;
  ss_x_f         constant double precision not null := ss*f;
  hint           constant text             not null := ss_value_ok(ss_x_f);
  chk_violation  constant text             not null := '23514';
  msg            constant text             not null :=
                   'value for domain interval_seconds_t violates check constraint "interval_seconds_ok".';
begin
  if hint <> '' then
    raise exception using
      errcode = chk_violation,
      message = msg,
      hint    = hint;
  end if;
  return interval_seconds(secs=>ss_x_f);
end;
$body$;
```

Test it like this:

```plpgsql
select interval_seconds(interval_seconds(hours=>99), 3.6297);
```

Once again, It brings the same result as when you use the native _interval_. Now push it beyond the limit with a huge multiplier:

```plpgsql
select interval_seconds(interval_seconds(hours=>99), 100000000);
```

It causes this error:

```output
ERROR:  23514: value for domain interval_seconds_t violates check constraint "interval_seconds_ok".
HINT:  Bad ss: 35640000000000. Must be in [-7730941132799, 7730941132799]
```

## Basic demonstration using one month expressed as a months interval, a days, interval, and a seconds interval.

This test uses the rules of thumb that one month is always thirty days and one day is always twenty-four hours. Create the table function _test_results()_ thus:

```plpgsql
drop function if exists test_results() cascade;
create function test_results()
  returns table(z text)
  language plpgsql
as $body$
declare
  t0                    constant timestamptz         not null := '2021-03-13 20:00 America/Los_Angeles';

  i_months              constant interval_months_t   not null := interval_months (months => 1);
  i_days                constant interval_days_t     not null := interval_days   (days   => 30);
  i_seconds             constant interval_seconds_t  not null := interval_seconds(hours  => 30*24);

  t0_plus_i_months      constant timestamptz         not null := t0 + i_months;
  t0_plus_i_days        constant timestamptz         not null := t0 + i_days;
  t0_plus_i_seconds     constant timestamptz         not null := t0 + i_seconds;

  calculated_i_months   constant interval_months_t   not null := interval_months (t0_plus_i_months,  t0);
  calculated_i_days     constant interval_days_t     not null := interval_days   (t0_plus_i_days,    t0);
  calculated_i_seconds  constant interval_seconds_t  not null := interval_seconds(t0_plus_i_seconds, t0);
begin
  assert calculated_i_months  = i_months,  'calculated_i_months  <> i_months';
  assert calculated_i_days    = i_days,    'calculated_i_days    <> i_days';
  assert calculated_i_seconds = i_seconds, 'calculated_i_seconds <> i_seconds';

  z := 't0'||rpad(' ', 40)||t0::text;                                         return next;

  z := 'i_months:  '||rpad(interval_mm_dd_ss(i_months)::text,  15)||
       't0 + i_months:  '||t0_plus_i_months::text;                            return next;

  z := 'i_days:    '||rpad(interval_mm_dd_ss(i_days)::text,    15)||
       't0 + i_days:    '||t0_plus_i_days::text;                              return next;

  z := 'i_seconds: '||rpad(interval_mm_dd_ss(i_seconds)::text, 15)||
       't0 + i_seconds: '||t0_plus_i_seconds::text;                           return next;
end;
$body$;
```

Execute it using a timezone where the interval values cross the spring-forward moment:

```plpgsql
set timezone = 'America/Los_Angeles';
select z from test_results();
```

This is the result:

```output
 t0                                        2021-03-13 20:00:00-08
 i_months:  (1,0,0)        t0 + i_months:  2021-04-13 20:00:00-07
 i_days:    (0,30,0)       t0 + i_days:    2021-04-12 20:00:00-07
 i_seconds: (0,0,2592000)  t0 + i_seconds: 2021-04-12 21:00:00-07
```

Each test result is different from the other two and is consistent, respectively, with the semantic definitions of _months_ calendar time durations, _days_ calendar time durations, and _seconds_ clock time durations:

- The test that uses the _interval_months_t_ domain advances the month by one while keeping the day number the same, even though it starts from a date in March which has thirty-one days. And it keeps the local time the same even though the timezone offset has sprung forward from _minus eight hours_ to _minus seven hours_.

- The test that uses the _interval_days_t_ domain advances the day by thirty days. Because it starts from the thirteenth of March, which has thirty-one days, it finishes on the twelfth of April. It keeps the local time the same even though the timezone offset has sprung forward from _minus eight hours_ to _minus seven hours_.

- The test that uses the _interval_seconds_t_ domain advances the day by thirty days to finish on the twelfth of April. It started at _20:00_ local time. But because it has crossed the spring forward moment, it finishes at _21:00_ local time.

## Thoroughly test the whole apparatus

Create and execute the following procedure to assert that all the expected outcomes hold:

```plpgsql
drop procedure if exists do_tests() cascade;

create procedure do_tests()
  language plpgsql
as $body$
declare
  -- Define all timestamptz values using a zero tz offset.
  -- Fair interpretation of "max legal value is 294276 AD"
  -- and "min legal value is 4713 BC".
  ts_max  constant timestamptz not null := '294276-01-01 00:00:00 UTC AD';
  ts_min  constant timestamptz not null :=   '4713-01-01 00:00:00 UTC BC';

  ts_1    constant timestamptz not null :=   '2021-01-01 00:00:00 UTC AD';
  ts_2    constant timestamptz not null :=   '2000-01-01 00:00:13 UTC AD';
  ts_3    constant timestamptz not null := '294275-06-01 00:00:00 UTC AD';
  ts_4    constant timestamptz not null := '294275-06-01 00:00:13 UTC AD';

  ts_5    constant timestamptz not null := '240271-10-10 07:59:59 UTC AD';
begin
  -- Do all tests using session tz 'UTC'
  set timezone = 'UTC';

  <<"interval_months_t tests">>
  begin
    <<"Test #1">>
    -- Check that given "i = ts_max - ts_min", then "ts_min + i = ts_max".
    declare
      i      constant interval_months_t not null := interval_months(ts_max, ts_min);
      ts_new constant timestamptz       not null := ts_min + i;
    begin
      assert (ts_new = ts_max), 'Test #1 failure';
    end "Test #1";

    <<"Test #2">>
    -- Check that when ts_2 and ts_1 differ in their dd, hh, mi, or ss values,
    -- given "i = ts_1 - ts_2", then "ts_2 + i <> ts_1".
    declare
      i       constant interval_months_t not null := interval_months(ts_1, ts_2);
      ts_new  constant timestamptz       not null := ts_2 + i;
    begin
      assert (ts_new <> ts_1), 'Test #2 failure';
    end "Test #2";
  end "interval_months_t tests";

  <<"interval_days_t tests">>
  begin
    <<"Test #3">>
    -- Check that given "i = ts_max - ts_min", then "ts_min + i = ts_max"
    -- for the full "ts_max, ts_min" range,
    declare
      i      constant interval_days_t not null := interval_days(ts_max, ts_min);
      ts_new constant timestamptz     not null := ts_min + i;
    begin
      assert (ts_new = ts_max), 'Test #3 failure';
    end "Test #3";

    <<"Test #4">>
    -- Check that given "i = ts_3 - ts_min", then "ts_min + i = ts_3"
    -- where ts_3 and ts_min differ by their day number but have their hh:mi:ss the same.
    declare
      i       constant interval_days_t not null := interval_days(ts_3, ts_min);
      ts_new  constant timestamptz     not null := ts_min + i;
    begin
      assert (ts_new = ts_3), 'Test #4 failure';
    end "Test #4";

    <<"Test #5">>
    -- Check that when ts_2 and ts_1 differ in their hh, mi, or ss values,
    -- given "i = ts_4 - ts_min", then "ts_min + i <> ts_4".
    declare
      i       constant interval_days_t not null := interval_days(ts_4, ts_min);
      ts_new  constant timestamptz     not null := ts_min + i;
    begin
      assert (ts_new <> ts_4), 'Test #5 failure';
    end "Test #5";
  end "interval_days_t tests";

  <<"interval_seconds_t tests">>
  begin
    <<"Test #6">>
    -- Check that given "i = ts_5 - ts_min", then "ts_min + i = ts_5"
    -- for the full "ts_5, ts_min" range,
    declare
      i       constant interval_seconds_t not null := interval_seconds(ts_5, ts_min);
      ts_new  constant timestamptz        not null := ts_min + i;
      ts_tol  constant double precision   not null := 0.0005;
    begin
      -- date_trunc('milliseconds', t) is too blunt an instrument.
      assert
        (abs(extract(epoch from ts_new) - extract(epoch from ts_5)) < ts_tol),
        'Test #6 failure';
    end "Test #6";
  end "interval_seconds_t tests";

  <<"Test #7">>
  -- Outcomes from interval multiplication/division.
  declare
    months_result   constant interval_months_t  not null := interval_months (years=>6, months=>1);
    days_result     constant interval_days_t    not null := interval_days   (days=>746);
    seconds_result  constant interval_seconds_t not null := interval_seconds(hours=>359, mins=>20, secs=>25.08);
  begin
    assert (
      -- Notice the use of the "strict equals" operator.
      interval_months(interval_months(years=>3, months=>99), 0.5378) == months_result  and
      interval_days(interval_days(days=>99), 7.5378)                 == days_result    and
      interval_seconds(interval_seconds(hours=>99), 3.6297)          == seconds_result
      ), 'Test #7 failure';
  end "Test #7";

  <<"Test #8">>
  -- Months to days ratio.
  declare
    m      constant interval_months_t not null := interval_months(ts_max, ts_min);
    mm     constant double precision  not null := (interval_mm_dd_ss(m)).mm;
    ym     constant double precision  not null := mm/12.0;

    d      constant interval_days_t   not null := interval_days  (ts_max, ts_min);
    dd     constant double precision  not null := (interval_mm_dd_ss(d)).dd;

    yd     constant double precision  not null := dd/365.2425;

    ratio  constant double precision  not null := abs(ym -yd)/greatest(ym, yd);
  begin
    assert ratio < 0.000001, 'Test #8 failure';
  end "Test #8";

end;
$body$;

call do_tests();
```

It finishes silently, showing that all the assertions hold.

## Comparing the results of interval_seconds(), interval_days(), and interval_months() for the same timestamptz pair

The table function _seconds_days_months_comparison()_ creates a report thus:

- It uses the _secs_ actual argument value to create the _interval_seconds_t_ value _i_secs_.
- It initializes the _timestamptz_ value _t0_ to the earliest moment that PostgreSQL, and therefore YSQL, support.
- It initializes the _timestamptz_ value _t1_ to the sum of _t0_ and _i_secs_.
- It initializes _i_days_ using _interval_days(t1, t0)_.
- It initializes _i_months_ using _interval_months(t1, t0)_.
- It evaluates _interval_mm_dd_ss()_ for each of these _interval_ domain values and reports the _ss_ value that _i_secs_ represents, the _dd_ value that _i_days_ represents, and the _mm_ value that _i_months_ represents.
- It converts each of the values _ss_, _dd_, and _mm_ to a real number of _years_ using these facts: the _fixed_ number of seconds per day is _24\*60\*60_ and the _fixed_ number of months per year is _12_; and the _average_ number of days per year is _365.2425_ (see the Wikipedia article [Year](https://en.wikipedia.org/wiki/Year)).

- It reports the values that it has calculated.

{{< note title="365.2425 or 365.25 for the average number of days per year?" >}}
The Wikipedia article [Year](https://en.wikipedia.org/wiki/Year) gives both _365.2425_ days and _365.25_ days as the average number of days per year. The first figure (used in the code below) is the average according to the Gregorian scheme. And the second figure is the average according to the Julian scheme. The [_extract(epoch from interval_value)_ built-in function](../justfy-and-extract-epoch/#the-extract-epoch-from-interval-value-built-in-function) section presents a PL/pgSQL model for this function. This uses _365.25_ days as the average number of days per year in order to produce the same result as does the native implementation that it models. (The designers of PostgreSQL might well have chosen to use _365.2425_ days—but they happened not to. The choice is arbitrary.) However, the nominal durations of the three kinds of _interval_ in the test below are closer to each other when _365.2425_ days is used.
{{< /note >}}

Create the table function thus:

```plpgsql
drop function if exists seconds_days_months_comparison(double precision) cascade;

create function seconds_days_months_comparison(secs in double precision)
  returns table(x text)
  language plpgsql
as $body$
declare
  err                            text             not null := '';
  msg                            text             not null := '';
  hint                           text             not null := '';
  seconds_per_day       constant double precision not null := 24*60*60;
  avg_days_per_year     constant double precision not null := 365.2425;
  avg_seconds_per_year  constant double precision not null := seconds_per_day*avg_days_per_year;
  months_per_year       constant double precision not null := 12;
begin
  x := 'secs input:  '||secs;                                                   return next;

  set timezone = 'UTC';
  declare
    i_secs  constant interval_seconds_t not null := interval_seconds(secs=>secs);
    t0      constant timestamptz        not null := '4713-01-01 00:00:00 UTC BC';
    t1      constant timestamptz        not null := t0 + i_secs;
  begin
    declare
      i_days       constant interval_days_t   not null := interval_days  (t1, t0);
      i_months     constant interval_months_t not null := interval_months(t1, t0);

      ss           constant double precision  not null := (interval_mm_dd_ss(i_secs  )).ss;
      dd           constant int               not null := (interval_mm_dd_ss(i_days  )).dd;
      mm           constant int               not null := (interval_mm_dd_ss(i_months)).mm;

      yrs_from_ss  constant numeric           not null := round((ss/avg_seconds_per_year)::numeric, 3);
      yrs_from_dd  constant numeric           not null := round((dd/avg_days_per_year   )::numeric, 3);
      yrs_from_mm  constant numeric           not null := round((mm/months_per_year     )::numeric, 3);
    begin
      x := 't0:          '||lpad(to_char(t0, 'yyyy-mm-dd hh24:mi:ss BC'), 25);  return next;
      x := 't1:          '||lpad(to_char(t1, 'yyyy-mm-dd hh24:mi:ss BC'), 25);  return next;
      x := '';                                                                  return next;
      x := 'i_secs:      '||i_secs::text;                                       return next;
      x := 'i_days:      '||i_days::text;                                       return next;
      x := 'i_months:    '||i_months::text;                                     return next;
      x := '';                                                                  return next;
      x := 'yrs_from_ss  '||yrs_from_ss;                                        return next;
      x := 'yrs_from_dd: '||yrs_from_dd;                                        return next;
      x := 'yrs_from_mm: '||yrs_from_mm;                                        return next;
    end;
  end;
exception when check_violation then
  get stacked diagnostics
    err = returned_sqlstate,
    msg = message_text,
    hint = pg_exception_hint;

  x := '';                                                                      return next;
  x := 'ERROR: '||err;                                                          return next;
  x := msg;                                                                     return next;
  x := hint;                                                                    return next;
end;
$body$;
```

Invoke the function like this, using the biggest legal _interval_seconds_t_ value:

```plpgsql
set timezone = 'UTC';
select x from seconds_days_months_comparison(7730941132799);
```

This is the result:

```output
 secs input:  7730941132799
 t0:             4713-01-01 00:00:00 BC
 t1:           240271-10-10 07:59:58 AD

 i_secs:      2147483647:59:58.999552
 i_days:      89478485 days
 i_months:    244983 years 9 mons

 yrs_from_ss  244983.772
 yrs_from_dd: 244983.771
 yrs_from_mm: 244983.750
```

Notice that the real numbers of years calculated from each of the _"pure seconds"_, _"pure days"_, and _"pure months"_ _interval_ values are in very close agreement. The fact that they are so close, yet do differ from each other, reflects these facts:

- The _interval_months()_ implementation uses _calendar-time-semantics_ and disregards the time of day and the day number in the month.
- The _interval_days()_ implementation uses _calendar-time-semantics_ and disregards the time of day.
- The _interval_seconds()_ implementation uses _clock-time-semantics_ and is exact to a microsecond precision.
- The duration of about 250 millennia is so big that the rounding errors brought by _calendar-time-semantics_ show up only as tens of milliseconds.

Now invoke the function using much smaller durations. First, like this:

```plpgsql
set timezone = 'UTC';
select x from seconds_days_months_comparison(200000000000);
```

This is the result:

```output
 secs input:  200000000000
 t0:             4713-01-01 00:00:00 BC
 t1:             1625-09-30 19:33:20 AD

 i_secs:      55555555:33:20
 i_days:      2314814 days
 i_months:    6337 years 8 mons

 yrs_from_ss  6337.748
 yrs_from_dd: 6337.745
 yrs_from_mm: 6337.667
```

And secondly like this:

```plpgsql
set timezone = 'UTC';
select x from seconds_days_months_comparison(8854000);
```

This is the result:

```output
 secs input:  8854000
 t0:             4713-01-01 00:00:00 BC
 t1:             4713-04-12 11:26:40 BC

 i_secs:      2459:26:40
 i_days:      102 days
 i_months:    3 mons

 yrs_from_ss  0.281
 yrs_from_dd: 0.279
 yrs_from_mm: 0.250
```

The input number of seconds was chosen by trial and error so that: the time of day component of _t1_ is about half way through the day; and the day number in the month is about half way through the month. Here, of course, the real numbers of years calculated from each of the _"pure seconds"_, _"pure days"_, and _"pure months"_ _interval_ values are in rather poor agreement.

Finally, test the error behavior with an input number of seconds that exceeds the maximum value that the _interval_seconds()_ constructor function allows by one second:

```plpgsql
select x from seconds_days_months_comparison(7730941132800);
```

This is the result, as expected:

```output
 secs input:  7730941132800

 ERROR: 23514
 value for domain interval_seconds_t violates check constraint "interval_seconds_ok".
 Bad ss: 7730941132800. Must be in [-7730941132799, 7730941132799].
```
