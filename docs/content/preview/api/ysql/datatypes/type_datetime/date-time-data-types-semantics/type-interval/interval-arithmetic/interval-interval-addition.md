---
title: Interval addition and subtraction [YSQL]
headerTitle: Adding or subtracting a pair of interval values
linkTitle: Interval-interval addition and subtraction
description: Explains the semantics of adding or subtracting a pair of interval values. [YSQL]
menu:
  preview:
    identifier: interval-interval-addition
    parent: interval-arithmetic
    weight: 20
type: docs
---

{{< tip title="Download and install the date-time utilities code." >}}
The code on this page depends on the code presented in the section [User-defined _interval_ utility functions](../../interval-utilities/). This is included in the larger [code kit](../../../../download-date-time-utilities/) that includes all of the reusable code that the overall _[date-time](../../../../../type_datetime/)_ section describes and uses.
{{< /tip >}}

This section presents a PL/pgSQL implementation of the model that explains how adding two _interval_ values or subtracting one _interval_ value from another works.

Create a table function to display the model in action:

```plpgsql
drop function if exists interval_interval_addition_result(interval, interval);

create function interval_interval_addition_result(i1 in interval, i2 in interval)
  returns table(x text)
  language plpgsql
as $body$
declare
  mm_dd_ss_1       constant interval_mm_dd_ss_t not null := interval_mm_dd_ss(i1);
  mm_dd_ss_2       constant interval_mm_dd_ss_t not null := interval_mm_dd_ss(i2);

  mm_model         constant int                 not null := mm_dd_ss_1.mm + mm_dd_ss_2.mm;
  dd_model         constant int                 not null := mm_dd_ss_1.dd + mm_dd_ss_2.dd;
  ss_model         constant double precision    not null := mm_dd_ss_1.ss + mm_dd_ss_2.ss;

  mm_dd_ss_model   constant interval_mm_dd_ss_t not null := (mm_model, dd_model, ss_model)::interval_mm_dd_ss_t;
  i_model          constant interval            not null := interval_value(mm_dd_ss_model);

  i_actual         constant interval            not null := i1 + i2;
  mm_dd_ss_actual  constant interval_mm_dd_ss_t not null := interval_mm_dd_ss(i_actual);
begin
  x := 'input 1 mm_dd_ss:            '||
    to_char(mm_dd_ss_1.mm,      '999999990.9999')||' months' ||
    to_char(mm_dd_ss_1.dd,      '999999990.9999')||' days'   ||
    to_char(mm_dd_ss_1.ss,      '999999990.9999')||' seconds' ; return next;

  x := 'input 2 mm_dd_ss:            '||
    to_char(mm_dd_ss_2.mm,      '999999990.9999')||' months' ||
    to_char(mm_dd_ss_2.dd,      '999999990.9999')||' days'   ||
    to_char(mm_dd_ss_2.ss,      '999999990.9999')||' seconds' ; return next;

  x := 'intermediate model mm_dd_ss: '||
    to_char(mm_model,           '999999990.9999')||' months' ||
    to_char(dd_model,           '999999990.9999')||' days'   ||
    to_char(ss_model,           '999999990.9999')||' seconds' ; return next;

  x := 'ultimate model mm_dd_ss:     '||
    to_char(mm_dd_ss_model.mm,  '999999990.9999')||' months' ||
    to_char(mm_dd_ss_model.dd,  '999999990.9999')||' days'   ||
    to_char(mm_dd_ss_model.ss,  '999999990.9999')||' seconds' ; return next;

  x := 'actual mm_dd_ss:             '||
    to_char(mm_dd_ss_actual.mm, '999999990.9999')||' months' ||
    to_char(mm_dd_ss_actual.dd, '999999990.9999')||' days'   ||
    to_char(mm_dd_ss_actual.ss, '999999990.9999')||' seconds' ; return next;

  x := '';                                                      return next;
  x := 'ultimate model result: '||i_model::text;                return next;
end;
$body$;
```

Test the function with values that are easy to understand:

```plpgsql
select x from interval_interval_addition_result(
  '6 months'::interval,
  '2 days'::interval);
```

This is the result:

```output
 input 1 mm_dd_ss:                     6.0000 months         0.0000 days         0.0000 seconds
 input 2 mm_dd_ss:                     0.0000 months         2.0000 days         0.0000 seconds
 intermediate model mm_dd_ss:          6.0000 months         2.0000 days         0.0000 seconds
 ultimate model mm_dd_ss:              6.0000 months         2.0000 days         0.0000 seconds
 actual mm_dd_ss:                      6.0000 months         2.0000 days         0.0000 seconds

 ultimate model result: 6 mons 2 days
```

Test it with values that lead to remainder "spill-down" (in the [internal representation](../../interval-representation)) from the _mm_ field to the _dd_ field and from the _dd_ field to the _ss_ field:

```plpgsql
select x from interval_interval_addition_result(
  '6.6 months 7.8 days 8 hours'::interval,
  '2.9 months 4.3 days 5 hours'::interval);
```

This is the result:

```output
 input 1 mm_dd_ss:                     6.0000 months        25.0000 days     97920.0000 seconds
 input 2 mm_dd_ss:                     2.0000 months        31.0000 days     43920.0000 seconds
 intermediate model mm_dd_ss:          8.0000 months        56.0000 days    141840.0000 seconds
 ultimate model mm_dd_ss:              8.0000 months        56.0000 days    141840.0000 seconds
 actual mm_dd_ss:                      8.0000 months        56.0000 days    141840.0000 seconds

 ultimate model result: 8 mons 56 days 39:24:00
```

Notice that the "spill-down" is an explicit consequence of the design of the algorithm that transforms an _interval_ specification that uses values for each of _years_, _months_, _days_, _hours_, _minutes_, and _seconds_ to the target _[mm, dd, ss]_ internal representation. (See the function _[interval_mm_dd_ss (interval_parameterization_t)](../../interval-representation/internal-representation-model/#function-interval-mm-dd-ss-interval-parameterization-t-returns-interval-mm-dd-ss-t)_.) So it affects the calculation of the values _mm_dd_ss_1_ and _mm_dd_ss_2_. But thereafter, addition or subtraction of the already integral _mm_ and _dd_ fields can only produce integral totals; and so no further "spill-down" can take place. This is why the _"intermediate model mm_dd_ss"_ row and the _"ultimate model mm_dd_ss"_ row in the output are identical.  (Multiplication and division of an _interval_ value by a real number are critically different in this respect.)

This result shows that a practice that the user might adopt to use only _interval_ values that have just a single non-zero _mm_, _dd_, or _ss_ value in the internal representation can easily be thwarted by _interval-interval_ addition or subtraction. The section [Custom domain types for specializing the native _interval_ functionality](../../custom-interval-domains/) shows how you can guarantee that you avoid this problem.

The procedure _assert_interval_interval_addition_model_ok()_ is a mechanical re-write of the function _interval_interval_addition_result()_ that produces no output. Instead, it uses an _assert_ statement to check that the model produces the same result as the native implementation.

This makes it suitable for testing the model with a wide range of input values. Create it thus:

```plpgsql
drop procedure if exists assert_interval_interval_addition_model_ok(interval, interval);

create procedure assert_interval_interval_addition_model_ok(i1 in interval, i2 in interval)
  language plpgsql
as $body$
declare
  mm_dd_ss_1      constant interval_mm_dd_ss_t not null := interval_mm_dd_ss(i1);
  mm_dd_ss_2      constant interval_mm_dd_ss_t not null := interval_mm_dd_ss(i2);

  mm_model        constant int                 not null := mm_dd_ss_1.mm + mm_dd_ss_2.mm;
  dd_model        constant int                 not null := mm_dd_ss_1.dd + mm_dd_ss_2.dd;
  ss_model        constant double precision    not null := mm_dd_ss_1.ss + mm_dd_ss_2.ss;

  mm_dd_ss_model  constant interval_mm_dd_ss_t not null := (mm_model, dd_model, ss_model)::interval_mm_dd_ss_t;
  i_model         constant interval            not null := interval_value(mm_dd_ss_model);

  i_actual        constant interval            not null := i1 + i2;
begin
  assert i_actual == i_model, 'assert failed';
end;
$body$;
```

Notice the use of the [user-defined "strict equals" operator](../../interval-utilities/#the-user-defined-strict-equals-interval-interval-operator), `==`. It's essential to use this, and not the native `=`, because two _interval_ values that compare as _true_ with the native `=` operator but as _false_ with the strict `==` operator can produce different results when added to a _timestamptz_ value—see the section [The moment-_interval_ overloads of the "+" and "-" operators](../moment-interval-overloads-of-plus-and-minus/). Use the _assert_ procedure thus:

```plpgsql
call assert_interval_interval_addition_model_ok(
  '6 months 7 days 8 hours'::interval,
  '2 months 4 days 5 hours'::interval);

call assert_interval_interval_addition_model_ok(
   '6 months 4 days 8 hours'::interval,
  -'2 months 7 days 5 hours'::interval);

call assert_interval_interval_addition_model_ok(
  '6 months 4 days 8 hours'::interval,
  '
      -9.123456 years,
      18.123456 months,
    -700.123456 days,
      97.123456 hours,
    -86.123456 minutes,
      75.123456 seconds
  '::interval);
```

The tests finish silently, showing that the hypothesised model has not been disproved.
