---
title: Declaring intervals [YSQL]
headerTitle: Declaring intervals
linkTitle: Declaring intervals
description: Explains that the nominally fourteen distinct interval declaration syntaxes have just six distinct semantics. [YSQL]
menu:
  v2.20:
    identifier: declaring-intervals
    parent: type-interval
    weight: 30
type: docs
---

{{< tip title="Download and [re]install the date-time utilities code." >}}
The code on this page depends on the code presented in the section [User-defined _interval_ utility functions](../interval-utilities/) and on the [function interval_mm_dd_ss (interval_parameterization_t)](../interval-representation/internal-representation-model/#function-interval-mm-dd-ss-interval-parameterization-t-returns-interval-mm-dd-ss-t), explained in the section [Modeling the internal representation and comparing the model with the actual implementation](../interval-representation/internal-representation-model/). This is all included in the larger [code kit](../../../download-date-time-utilities/) that includes all of the reusable code that the overall _[date-time](../../../../type_datetime/)_ section describes and uses.

Even if you have already installed the code kit, install it again now. Do this because a code example in the section [How does YSQL represent an _interval_ value?](../interval-representation/) redefines one of the _interval_ utility functions.
{{< /tip >}}

There are over one hundred different spellings of the declaration of an _interval_. This might seem daunting. However, when you understand the degrees of freedom that the variations exploit, the mental model will seem straightforward. By analogy, when you consider the optional annotations of a bare _numeric_ declaration to specify the _scale_ and _precision_, you realize that multiplying the numbers of possible spellings for these two annotations gives a vast number of distinct possible spellings. But you need only to understand the _concepts_ of _scale_ and _precision_. The syntax variants for _interval_ declarations express analogous concepts. This page explains it all. And it also points out that the variations in the syntax spellings can be grouped to reflect the fact that, within each group, the variants all express the same semantics.

However, if you follow the approach described in the section [Custom domain types for specializing the native _interval_ functionality](../custom-interval-domains/), then you will not need to understand what this page explains.

## Summary

The explanations and the supporting code below address the semantics of the different spellings of _interval_ declarations—in other words, how these different spellings differently constrain the _interval_ values that can be represented. The code and the explanations presented below show that though there are several syntax spellings for each, there are in fact just six kinds of _interval_ declaration. It's convenient to call these kinds _year_, _month_, _day_, _hour_, _minute_, and _second_. The constraints are applied whenever a new _interval_ value is created, just before recording it in the [internal _\[mm, dd, ss\]_ tuple format](../interval-representation/). The effects of each of the six constraints are conveniently described by this PL/pgSQL code:

```output
-- Here with [mm, dd, ss] computed with, so far, no constraints.
case mode
  when 'bare' then
    null;
  when 'second' then
    null;
  when 'minute' then
    ss := trunc(ss/60.0)*60.0;
  when 'hour' then
    ss := trunc(ss/(60.0*60.0))*60.0*60.0;
  when 'day' then
    ss := 0.0;
  when 'month' then
    ss := 0.0;
    dd := 0;
  when 'year' then
    ss := 0.0;
    dd := 0;
    mm := (mm/12)*12; -- integer division uses "trunc()" semantics
end case;
```

{{< tip title="Yugabyte recommends using only the bare 'interval' declaration." >}}
The term of art "bare _interval_" is to be taken literally: no trailing key words, and no _(p)_ precision specifier.

Yugabyte staff members have carefully considered the practical value that these various constraints bring and have concluded that none captures the intent of the SQL Standard or brings useful functionality. The section [Custom domain types for specializing the native _interval_ functionality](../custom-interval-domains/) shows how you can usefully constrain _interval_ values, declared using the bare syntax, to allow the internal _interval_ representation to record only:

- **either** the _years_ and _months_ fields (corresponding to the _mm_ field in the internal representation).
- **or** the _days_ field (corresponding to the _dd_ field in the internal representation).
- **or** the _hours_, _minutes_, and _seconds_ fields  (corresponding to the _ss_ field in the internal representation).

If you follow this recommendation, then you don't need to study the remainder of this section.
{{< /tip >}}

The SQL Standard (cosmetically reworded) says this:

> There are two classes of intervals. One class, called _year-month intervals_, has an express or implied _date-time_ precision that includes no fields other than YEAR and MONTH, though not both are required. The other class, called _day-time intervals_, has an express or implied _interval_ value precision that includes no fields other than DAY, HOUR, MINUTE, and SECOND, though not all are required.

Briefly, _day-time intervals_ implement _clock-time-semantics_ and _year-month intervals_ implement _calendar-time-semantics_. See the section [Two ways of conceiving of time: calendar-time and clock-time](../../../conceptual-background/#two-ways-of-conceiving-of-time-calendar-time-and-clock-time).

The PostgreSQL design that YSQL inherits allows, for example, the declaration spelling _day to second_ but, as the tests below show, the _day to_ part of the phrase has no effect and so all six fields (_years_, _months_, _days_, _hours_, _minutes_ , and _seconds_) are allowed. This is not useful—in particular because the sharp distinction that the SQL Standard intends between _clock-time-semantics_ and _calendar-time-semantics_ is blurred: hybrid semantics emerge.

However, the regime that the [custom domains approach](../custom-interval-domains/) defines _is_ useful because the semantics of _interval_ arithmetic for the values of each of the constrained domains is different—and honors the spirit of the SQL Standard. See the section [_Interval_ arithmetic](../interval-arithmetic/). Moreover, the fact that there are _three_ variants, in contrast to the SQL Standard's _two_ variants, brings beneficial extra functionality.

## Interval declaration syntax variants

The [PostgreSQL documentation](https://www.postgresql.org/docs/11/datatype-datetime.html) specifies that this is the general syntax for an _interval_ declaration:

```output
interval [ fields ] [ (p) ]
```

And it states that the _fields_ element restricts the set of stored fields by writing one of these listed phrases (or, as the syntax diagram says, by writing nothing):

```output
            year

            month
year to     month

day

            hour
day to      hour

            minute
day to      minute
hour to     minute

            second
day to      second
hour to     second
minute to   second
<"fields" specification omitted>
```

This list shows no fewer than _fourteen_ distinct syntax spellings.

The optional _(p)_ element specifies the precision, in microseconds, with which the _seconds_ field of the internal representation records the value. It is legal only after the _"second"_ keyword or after the bare _interval_ declaration. The allowed values are in _0..6_. Omitting the _(p)_ element has the same effect as specifying _(6)_. So with the fourteen spellings listed above, together with the eight precision designations (_0_, _1_, _2_, _3_, _4_, _5_, _6_, or omitting this element) it multiplies up to _112_ distinct ways to declare an _interval_ as a table column, in PL/pgSQL code, and so on.

**Note:** No pair chosen from the fourteen different _interval_ declarations can be used to distinguish procedure or function overloads. The same applies to the variations in the specification of the _(p)_ element.

The account below shows that the different declaration syntaxes produce only _six_ distinct semantic outcomes. The blank lines in the list above, and in the code, reflect this by grouping the syntaxes according to their semantic outcome.

## Syntax variants within each of just six groups have the same effect

The anonymous block below demonstrates the syntax groupings. It uses integral values for each of the inputs, except for the _seconds_, to make the results maximally understandable. The expected outcomes, used on the right hand sides of the putative equalities that the asserts test, are consistent with the explanations that the section [How does YSQL represent an _interval_ value?](../interval-representation/) gave of how a parameterization that specifies values using a _[yy, mm, dd, hh, mi, ss]_ tuple is encoded as the internal representation that uses a _[mm, dd, ss]_ tuple with integral _mm_ and _dd_ fields and a real number _ss_ field.

```plpgsql
do $body$
declare
  i_bare      constant  interval :=

    make_interval(years=>9, months=>18, days=>700, hours=>97, mins=>86, secs=>75.123456);

  i_year      constant  interval              year    := i_bare;

  i_month_1   constant  interval              month   := i_bare;
  i_month_2   constant  interval  year   to   month   := i_bare;

  i_day       constant  interval              day     := i_bare;

  i_hour_1    constant  interval              hour    := i_bare;
  i_hour_2    constant  interval  day     to  hour    := i_bare;

  i_minute_1  constant  interval              minute  := i_bare;
  i_minute_2  constant  interval  day     to  minute  := i_bare;
  i_minute_3  constant  interval  hour    to  minute  := i_bare;

  i_second_1  constant  interval              second  := i_bare;
  i_second_2  constant  interval  day     to  second  := i_bare;
  i_second_3  constant  interval  hour    to  second  := i_bare;
  i_second_4  constant  interval  minute  to  second  := i_bare;

  r_year    constant text := '10 years';
  r_month   constant text := '10 years 6 mons';
  r_day     constant text := '10 years 6 mons 700 days';
  r_hour    constant text := '10 years 6 mons 700 days 98:00:00';
  r_minute  constant text := '10 years 6 mons 700 days 98:27:00';
  r_second  constant text := '10 years 6 mons 700 days 98:27:15.123456';
begin
  -- "Year" group
  assert i_year::text = r_year, 'i_year = r_year failed';

  -- "Month" group
  assert i_month_1::text = r_month, 'i_month_1 = r_month failed';
  assert i_month_2::text = r_month, 'i_month_2 = r_month failed';

  -- "Day" group
  assert i_day::text = r_day, 'i_day = r_day failed';

  -- "Hour" group
  assert i_hour_1::text = r_hour, 'i_hour_1 = r_hour failed';
  assert i_hour_2::text = r_hour, 'i_hour_2 = r_hour failed';

  -- "Minute" group
  assert i_minute_1::text = r_minute, 'i_minute_1 = r_minute failed';
  assert i_minute_2::text = r_minute, 'i_minute_2 = r_minute failed';
  assert i_minute_3::text = r_minute, 'i_minute_3 = r_minute failed';

  -- "Second" group
  assert i_bare    ::text = r_second, 'i_bare     = r_second failed';
  assert i_second_1::text = r_second, 'i_second_1 = r_second failed';
  assert i_second_2::text = r_second, 'i_second_2 = r_second failed';
  assert i_second_3::text = r_second, 'i_second_3 = r_second failed';
  assert i_second_4::text = r_second, 'i_second_4 = r_second failed';
end;
$body$;
```

The block finishes silently, showing that each assertion holds.

The declarations are grouped according to the six possible choices for the trailing keyword: _year_, _month_, _day_, _hour_, _minute_, or _second_. The assertions show that the syntax variants within each group have the same effect—in other words that the optional leading phrases, _year to_, _day to_, _hour to_, and _minute to_ have no semantic effect. Only the trailing keyword  is semantically significant. Omitting this keyword (i.e. the bare declaration) has the same semantic effect has writing _second_, and so it belongs in that group.

**Each group has different resolution semantics: the choice of trailing keyword determines the least granular unit (_years_, _months_, _days_, _hours_, _minutes_, or _seconds_) that is respected.**

## The effect of the optional (p) element

Try this:

```plpgsql
do $body$
declare
  i_bare constant interval :=

    '9 years 18 months 700 days 97 hours 86 minutes 75.123456 seconds';

  i6 constant interval(6) not null := i_bare;
  i5 constant interval(5) not null := i_bare;
  i4 constant interval(4) not null := i_bare;
  i3 constant interval(3) not null := i_bare;
  i2 constant interval(2) not null := i_bare;
  i1 constant interval(1) not null := i_bare;
  i0 constant interval(0) not null := i_bare;

  r6 constant text := '10 years 6 mons 700 days 98:27:15.123456';
  r5 constant text := '10 years 6 mons 700 days 98:27:15.12346';

-- This shows the "round()" semantics.
  r4 constant text := '10 years 6 mons 700 days 98:27:15.1235';

  r3 constant text := '10 years 6 mons 700 days 98:27:15.123';
  r2 constant text := '10 years 6 mons 700 days 98:27:15.12';
  r1 constant text := '10 years 6 mons 700 days 98:27:15.1';
  r0 constant text := '10 years 6 mons 700 days 98:27:15';
begin
  -- Notice that i_bare, declared as bare "interval",
  -- and i6, declared as "interval(6)" are the same.
  assert i_bare ::text = r6, 'i_bare  = r6 failed';
  assert i6     ::text = r6, 'i6 = r6 failed';

  assert i5     ::text = r5, 'i5 = r5 failed';
  assert i4     ::text = r4, 'i4 = r4 failed';
  assert i3     ::text = r3, 'i3 = r3 failed';
  assert i2     ::text = r2, 'i2 = r2 failed';
  assert i1     ::text = r1, 'i1 = r1 failed';
  assert i0     ::text = r0, 'i0 = r0 failed';
end;
$body$;
```

The block finishes silently showing that all the assertions hold. This confirms that the _(p)_ element determines the precision, in microseconds, with which the seconds field of the internal representation records the value. Repeat the test after globally replacing _interval_ with _interval second_ in the declarations. The outcome is identical.

## Modeling the implementation

Make sure that you've read the section [How does YSQL represent an _interval_ value?](../interval-representation/) before reading this section.

The assumption that informs the following test is that any operation that produces an _interval_ value first computes the _[mm, dd, ss]_ internal representation and only then applies the rules that the _ad hoc_ test above illustrates. The rule, expressed in PL/pgSQL code, is shown in the [Summary](#summary) above.

The test provides new overloads for these two functions:

- [function interval_mm_dd_ss (interval_parameterization_t)](../interval-representation/internal-representation-model/#function-interval-mm-dd-ss-interval-parameterization-t-returns-interval-mm-dd-ss-t)

- [function interval_value (interval_parameterization_t)](../interval-utilities/#function-interval-value-interval-parameterization-t-returns-interval)

that each adds a _mode text_ formal parameter to express the class name of the _interval_ declaration as one of _'bare'_, _'year'_, _'month'_, _'day'_, _'hour'_, _'minute'_, or _'second'_, thus:

```plpgsql
drop function if exists interval_mm_dd_ss(interval_parameterization_t, text) cascade;

create function interval_mm_dd_ss(p in interval_parameterization_t, mode in text)
  returns interval_mm_dd_ss_t
  language plpgsql
as $body$
declare
  -- Use the single-parameter overload
  mm_dd_ss  constant interval_mm_dd_ss_t not null := interval_mm_dd_ss(p);

  mm                 int                 not null := mm_dd_ss.mm;
  dd                 int                 not null := mm_dd_ss.dd;
  ss                 double precision    not null := mm_dd_ss.ss;
begin
  case mode
    when 'bare' then
      null;
    when 'second' then
      null;
    when 'minute' then
      ss := trunc(ss/60.0)*60.0;
    when 'hour' then
      ss := trunc(ss/(60.0*60.0))*60.0*60.0;
    when 'day' then
      ss := 0.0;
    when 'month' then
      ss := 0.0;
      dd := 0;
    when 'year' then
      ss := 0.0;
      dd := 0;
      mm := (mm/12)*12; -- integer division uses "trunc()" semantics
  end case;
  return (mm, dd, ss)::interval_mm_dd_ss_t;
end;
$body$;
```

and:


```plpgsql
drop function if exists interval_value(interval_parameterization_t, text) cascade;

create function interval_value(p in interval_parameterization_t, mode in text)
  returns interval
  language plpgsql
as $body$
declare
  -- Use the single-parameter overload
  i_bare   constant interval        not null := interval_value(p);

  i_year   constant interval year   not null := i_bare;
  i_month  constant interval month  not null := i_bare;
  i_day    constant interval day    not null := i_bare;
  i_hour   constant interval hour   not null := i_bare;
  i_minute constant interval minute not null := i_bare;
  i_second constant interval second not null := i_bare;
begin
  return
    case mode
      when 'bare'   then i_bare
      when 'year'   then i_year
      when 'month'  then i_month
      when 'day'    then i_day
      when 'hour'   then i_hour
      when 'minute' then i_minute
      when 'second' then i_second
    end;
end;
$body$;
```

Test the modeled implementation of the constraints in the same way that the unconstrained model was tested in the section [Modeling the internal representation and comparing the model with the actual implementation](../interval-representation/internal-representation-model/). The procedure _assert_model_ok_worker()_ has the identical implementation to procedure _[assert_model_ok()](../interval-representation/internal-representation-model/#procedure-assert-model-ok-interval-parameterization-t)_ in that section.

```plpgsql
drop procedure if exists assert_model_ok_worker(interval_parameterization_t, text) cascade;

create procedure assert_model_ok_worker(p in interval_parameterization_t, mode in text)

  language plpgsql
as $body$
declare
  i_modeled        constant interval_mm_dd_ss_t not null := interval_mm_dd_ss(p);
  i_from_modeled   constant interval            not null := interval_value(i_modeled);
  i_actual         constant interval            not null := interval_value(p);
  mm_dd_ss_actual  constant interval_mm_dd_ss_t not null := interval_mm_dd_ss(i_actual);

  p_modeled  constant interval_parameterization_t not null := parameterization(i_modeled);
  p_actual   constant interval_parameterization_t not null := parameterization(i_actual);
begin
  -- Belt-and-braces check for mutual consistency among the "interval" utilities.
  assert (i_modeled      ~= mm_dd_ss_actual), 'assert #1 failed';
  assert (p_modeled      ~= p_actual       ), 'assert #2 failed';
  assert (i_from_modeled == i_actual       ), 'assert #3 failed';
end;
$body$;

drop procedure if exists assert_model_ok(interval_parameterization_t) cascade;

create procedure assert_model_ok(p in interval_parameterization_t)
  language plpgsql
as $body$
begin
  call assert_model_ok_worker(p, 'bare');
  call assert_model_ok_worker(p, 'year');
  call assert_model_ok_worker(p, 'month');
  call assert_model_ok_worker(p, 'day');
  call assert_model_ok_worker(p, 'hour');
  call assert_model_ok_worker(p, 'minute');
  call assert_model_ok_worker(p, 'second');
end;
$body$;
```

Execute the tests using the same _[procedure test_internal_interval_representation_model()](../interval-representation/internal-representation-model/#procedure-test-internal-interval-representation-model)_ that was defined and used to test the unconstrained model. (This is also included in the code kit.)

```plpgsql
call test_internal_interval_representation_model();
```

Each of the tests finishes silently showing that the rules explained above have so far been shown always to agree with the rules of the actual implementation. You are challenged to disprove the hypothesis by inventing more tests. If any of your tests causes _assert_model_ok()_ to finish with an assert failure, then [raise a GitHub issue](https://github.com/yugabyte/yugabyte-db/issues/new) against this documentation section.

If you do find such a counter-example, you can compare the results from using the actual implementation and the modeled implementation with this re-write of the logic of the _assert_model_ok()_ procedure. Instead of using _assert_, it shows you the outputs for visual comparison. It has an almost identical implementation to the function _[model_vs_actual_comparison()](../interval-representation/internal-representation-model/#function-model-vs-actual-comparison-interval-parameterization-t-returns-table-x-text)_ in the section [Modeling the internal representation and comparing the model with the actual implementation](../interval-representation/internal-representation-model/). The difference is that the following implentation adds the formal input parameter _mode_ and carries this through to the new overload of _interval_mm_dd_ss()_.


```plpgsql
drop function if exists model_vs_actual_comparison(interval_parameterization_t, text) cascade;

create function model_vs_actual_comparison(p in interval_parameterization_t, mode in text)
  returns table(x text)
  language plpgsql
as $body$
declare
  i_modeled        constant interval_mm_dd_ss_t         not null := interval_mm_dd_ss(p, mode);
  i_actual         constant interval                    not null := interval_value(p, mode);

  p_modeled        constant interval_parameterization_t not null := parameterization(i_modeled);
  p_actual         constant interval_parameterization_t not null := parameterization(i_actual);

  ss_modeled_text  constant text                        not null := ltrim(to_char(p_modeled.ss, '9999999999990.999999'));
  ss_actual_text   constant text                        not null := ltrim(to_char(p_actual.ss,  '9999999999990.999999'));
begin
  x := 'modeled: '||
    lpad(p_modeled.yy ::text,  4)||' yy, '||
    lpad(p_modeled.mm ::text,  4)||' mm, '||
    lpad(p_modeled.dd ::text,  4)||' dd, '||
    lpad(p_modeled.hh ::text,  4)||' hh, '||
    lpad(p_modeled.mi ::text,  4)||' mi, '||
    lpad(ss_modeled_text,     10)||' ss';                           return next;

  x := 'actual:  '||
    lpad(p_actual.yy  ::text,  4)||' yy, '||
    lpad(p_actual.mm  ::text,  4)||' mm, '||
    lpad(p_actual.dd  ::text,  4)||' dd, '||
    lpad(p_actual.hh  ::text,  4)||' hh, '||
    lpad(p_actual.mi  ::text,  4)||' mi, '||
    lpad(ss_actual_text,      10)||' ss';                           return next;
end;
$body$;
```

Use it like this:

```plpgsql
select x from model_vs_actual_comparison(interval_parameterization(
  yy => -9.7,
  mm =>  1.55,
  dd => -17.4,
  hh =>  99.7,
  mi => -86.7,
  ss =>  75.7),
  'day');
```

This is the result:

```output
 modeled:   -9 yy,   -7 mm,   -1 dd,    0 hh,    0 mi,   0.000000 ss
 actual:    -9 yy,   -7 mm,   -1 dd,    0 hh,    0 mi,   0.000000 ss
```
