---
title: Interval-number multiplication [YSQL]
headerTitle: Multiplying or dividing an interval value by a number
linkTitle: Interval-number multiplication
description: Explains the semantics of multiplying or dividing an interval value by a real or integral number. [YSQL]
menu:
  preview:
    identifier: interval-number-multiplication
    parent: interval-arithmetic
    weight: 30
type: docs
---

{{< tip title="Download and install the date-time utilities code." >}}
The code on this page depends on the code presented in the section [User-defined _interval_ utility functions](../../interval-utilities/). This is included in the larger [code kit](../../../../download-date-time-utilities/) that includes all of the reusable code that the overall _[date-time](../../../../../type_datetime/)_ section describes and uses.
{{< /tip >}}

This section presents a PL/pgSQL implementation of the model that explains how multiplying or dividing an _interval_ value by a number works. Make sure that you have read the section [Adding or subtracting a pair of _interval_ values](../interval-interval-addition/) before reading this section.

Create a table function to display the model in action:

```plpgsql
drop function if exists interval_multiplication_result(interval, double precision) cascade;

create function interval_multiplication_result(i in interval, f in double precision)
  returns table(z text)
  language plpgsql
as $body$
declare
  mm_dd_ss_in      constant interval_mm_dd_ss_t not null := interval_mm_dd_ss(i);

  mm_model         constant double precision    not null := mm_dd_ss_in.mm*f;
  dd_model         constant double precision    not null := mm_dd_ss_in.dd*f;
  ss_model         constant double precision    not null := mm_dd_ss_in.ss*f;
  i_model          constant interval            not null :=
                     interval_value(
                       interval_parameterization(
                         mm=>mm_model, dd=>dd_model, ss=>ss_model));

  mm_dd_ss_model   constant interval_mm_dd_ss_t not null := interval_mm_dd_ss(i_model);

  i_actual         constant interval            not null := i*f;
  mm_dd_ss_actual  constant interval_mm_dd_ss_t not null := interval_mm_dd_ss(i_actual);
begin
  z := 'input mm_dd_ss:              '||
    to_char(mm_dd_ss_in.mm,     '999999990.9999')||' months' ||
    to_char(mm_dd_ss_in.dd,     '999999990.9999')||' days'   ||
    to_char(mm_dd_ss_in.ss,     '999999990.9999')||' seconds' ; return next;

  z := 'intermediate model mm_dd_ss: '||
    to_char(mm_model,           '999999990.9999')||' months' ||
    to_char(dd_model,           '999999990.9999')||' days'   ||
    to_char(ss_model,           '999999990.9999')||' seconds' ; return next;

  z := 'ultimate model mm_dd_ss:     '||
    to_char(mm_dd_ss_model.mm,  '999999990.9999')||' months' ||
    to_char(mm_dd_ss_model.dd,  '999999990.9999')||' days'   ||
    to_char(mm_dd_ss_model.ss,  '999999990.9999')||' seconds' ; return next;

  z := 'actual mm_dd_ss:             '||
    to_char(mm_dd_ss_actual.mm, '999999990.9999')||' months' ||
    to_char(mm_dd_ss_actual.dd, '999999990.9999')||' days'   ||
    to_char(mm_dd_ss_actual.ss, '999999990.9999')||' seconds' ; return next;

  z := '';                                                      return next;
  z := 'ultimate model result: '||i_model::text;                return next;
end;
$body$;
```

Test it first for multiplication and then for division:

```plpgsql
select z from interval_multiplication_result('2 months'::interval, 1.2345678);
select z from interval_multiplication_result('2 months'::interval, 0.9876543);
```

This is the multiplication result:

```output
 input mm_dd_ss:                       2.0000 months         0.0000 days         0.0000 seconds
 intermediate model mm_dd_ss:          2.4691 months         0.0000 days         0.0000 seconds
 ultimate model mm_dd_ss:              2.0000 months        14.0000 days      6399.4752 seconds
 actual mm_dd_ss:                      2.0000 months        14.0000 days      6399.4752 seconds

 ultimate model result: 2 mons 14 days 01:46:39.4752
```

And this is the division result:

```output
 input mm_dd_ss:                       2.0000 months         0.0000 days         0.0000 seconds
 intermediate model mm_dd_ss:          1.9753 months         0.0000 days         0.0000 seconds
 ultimate model mm_dd_ss:              1.0000 months        29.0000 days     22399.8912 seconds
 actual mm_dd_ss:                      1.0000 months        29.0000 days     22399.8912 seconds

 ultimate model result: 1 mon 29 days 06:13:19.8912
```

Notice that because the internal representation for the input has a non-zero value for only the _mm_ field. Then, after both multiplication and division, the intermediate result, of course, still has a non-zero value for only the _mm_ field. However, this non-zero value is now a real number and not an integer. This means that when the ultimate result is constructed, the hard-to-grasp spill-down rules kick in. See the section [Modeling the internal representation and comparing the model with the actual implementation](../../interval-representation/internal-representation-model/). This explains the fact that the ultimate result has non-zero values for the _dd_ and _ss_ fields as well as for the _mm_ field.

This shows that a practice that the user might adopt to use only _interval_ values that have just a single non-zero internal representation field can easily be thwarted by _interval_ multiplication or division. The section [Custom domain types for specializing the native _interval_ functionality](../../custom-interval-domains/) shows how you can guarantee that you avoid this problem in a way that is consistent with what you can reasonably expect _interval_ multiplication and division to mean.

The procedure _assert_interval_multiplication_model_ok()_ is a mechanical re-write of the function _interval_multiplication_result()_ that produces no output. Instead, it uses an _assert_ statement to check that the model produces the same result as the native implementation.

This makes it suitable for testing the model with a wide range of input values. Create it thus:

```plpgsql
drop procedure if exists assert_interval_multiplication_model_ok(interval, double precision);

create procedure assert_interval_multiplication_model_ok(i in interval, f in double precision)
  language plpgsql
as $body$
declare
  mm_dd_ss_in  constant interval_mm_dd_ss_t not null := interval_mm_dd_ss(i);

  mm_model     constant double precision    not null := mm_dd_ss_in.mm*f;
  dd_model     constant double precision    not null := mm_dd_ss_in.dd*f;
  ss_model     constant double precision    not null := mm_dd_ss_in.ss*f;
  i_model      constant interval            not null :=
                 interval_value(
                   interval_parameterization(
                     mm=>mm_model, dd=>dd_model, ss=>ss_model));

  i_actual     constant interval            not null := i*f;
begin
  -- Notice the use of the user-defined "strict equals" operator.
  assert i_actual == i_model, 'assert failed';
end;
$body$;
```

Notice the use of the [user-defined _"strict equals"_ interval-interval `==` operator](../../interval-utilities#the-user-defined-strict-equals-interval-interval-operator). It's essential to use this, and not the native `=`, because two _interval_ values that compare as _true_ with the native `=` operator but as _false_ with the strict `==` operator can produce different results when added to a _timestamptz_ value—see the section [The moment-_interval_ overloads of the "+" and "-" operators.](../moment-interval-overloads-of-plus-and-minus/).

Use the _assert_ procedure thus:

```plpgsql
call assert_interval_multiplication_model_ok('2 months'::interval, 1.2345678);
call assert_interval_multiplication_model_ok('2 months'::interval, 0.9876543);

call assert_interval_multiplication_model_ok('2 months 2 days 2 hours'::interval, 3);
call assert_interval_multiplication_model_ok('2 months 2 days 2 hours'::interval, 0.5);

call assert_interval_multiplication_model_ok('2 months 2 days 2 hours'::interval, 3.5);
call assert_interval_multiplication_model_ok('2 months 2 days 2 hours'::interval, 0.25);

call assert_interval_multiplication_model_ok('2 months 2 days 2 hours'::interval, 1.1);
call assert_interval_multiplication_model_ok('2 months 2 days 2 hours'::interval, 0.9);

call assert_interval_multiplication_model_ok('2 months 2 days 2 hours'::interval, 1.11);

/*
call assert_interval_multiplication_model_ok('2 months 2 days 2 hours'::interval, 0.99);
*/;

call assert_interval_multiplication_model_ok(
  '
      -9.123456 years,
      18.123456 months,
    -700.123456 days,
      97.123456 hours,
    -86.123456 minutes,
      75.123456 seconds
  '::interval,
  1.2345);

/*
call assert_interval_multiplication_model_ok(
  '
      -9.123456 years,
      18.123456 months,
    -700.123456 days,
      97.123456 hours,
    -86.123456 minutes,
      75.123456 seconds
  '::interval,
  0.0009);
*/;
```

Notice that two of the tests are commented out. The remaining tests finish silently. So the hypothesised model has been disproved. It's easy to see why if the two failing tests are re-cast to use the _interval_multiplication_result()_ function. Try this first:

```plpgsql
select z from interval_multiplication_result('2 months 2 days 2 hours'::interval, 0.96);
```

This is the result:

```output
 input mm_dd_ss:                       2.0000 months         2.0000 days      7200.0000 seconds
 intermediate model mm_dd_ss:          1.9200 months         1.9200 days      6912.0000 seconds
 ultimate model mm_dd_ss:              1.0000 months        28.0000 days    138240.0000 seconds
 actual mm_dd_ss:                      1.0000 months        29.0000 days     51840.0000 seconds

 ultimate model result: 1 mon 28 days 38:24:00
```

Clearly, the result the model differs in the strict sense from the actual result. But compare the results with the native `=` operator:

```plpgsql
select (
    '1 month 28 days 138240 seconds'::interval =
    '1 month 29 days  51840 seconds'::interval
  )::text;
```

This result is _true_. (Of course, with the user-defined strict `==` operator, the result is _false_.) The explanation is just the same with the other failing test. You look at more failing cases most effectively by using a stripped re-write of the function _interval_multiplication_result()_ that simply returns the _interval_ result without trace output:

```postgresql
drop function if exists interval_multiplication_result(interval, double precision) cascade;

create function interval_multiplication_result(i in interval, f in double precision)
  returns interval
  language plpgsql
as $body$
declare
  mm_dd_ss_in  constant interval_mm_dd_ss_t not null := interval_mm_dd_ss(i);

  mm_model     constant double precision    not null := mm_dd_ss_in.mm*f;
  dd_model     constant double precision    not null := mm_dd_ss_in.dd*f;
  ss_model     constant double precision    not null := mm_dd_ss_in.ss*f;
  i_model      constant interval            not null :=
                 interval_value(
                   interval_parameterization(
                     mm=>mm_model, dd=>dd_model, ss=>ss_model));
begin
  return i_model;
end;
$body$;

drop operator if exists ** (interval, double precision) cascade;

create operator ** (
    leftarg   = interval,
    rightarg  = double precision,
    procedure = interval_multiplication_result
);
```

The user-defined operator allows the test to be terser and therefore easier to read.

Now try two simple tests. First, a test where the model result agrees with the actual result:

```plpgsql
select
  '2 months 2 days'::interval *  0.95  as "actual",
  '2 months 2 days'::interval ** 0.95  as "model";
```

This is the result:

```output
         actual         |         model
------------------------+------------------------
 1 mon 28 days 21:36:00 | 1 mon 28 days 21:36:00
```

And next, a test where the model result _disagrees_ with the actual result. (The only change from the previous test is that _0.95_ is replaced by _0.96_.)

```plpgsql
select
  '2 months 2 days'::interval *  0.96  as "actual",
  '2 months 2 days'::interval ** 0.96  as "model";
```

This is the result:

```output
         actual         |         model
------------------------+------------------------
 1 mon 29 days 12:28:48 | 1 mon 28 days 36:28:48
```

This prompts the obvious question: which of the two alternatives, PostgreSQL's C native code or the PL/pgSQL function _interval_multiplication_result()_ implements the more sensible requirements specification? Here's a good way to choose the winner. Consider this ordinary principle of algebra and arithmetic:

```output
(a + b)*x = a*x +b*x
```

Test whether the native _interval-number_ `*` overload satisfies this rule:

```plpgsql
select
  ('2 months'::interval          +    '2 days'::interval) *  0.96   as "add then multiply",
  ('2 months'::interval *  0.96) +   ('2 days'::interval  *  0.96)  as "multiply then add";
```

This is the result:

```output
   add then multiply    |   multiply then add
------------------------+------------------------
 1 mon 29 days 12:28:48 | 1 mon 28 days 36:28:48
```

So the native _interval-number_ `*` overload fails to satisfy the basic rule. Repeat the test using the user-defined `**` operator instead of the native `*` operator:

```plpgsql
select
  ('2 months'::interval          +    '2 days'::interval) ** 0.96   as "add then multiply",
  ('2 months'::interval ** 0.96) +   ('2 days'::interval  ** 0.96)  as "multiply then add";
```

This is the result:

```output
   add then multiply    |   multiply then add
------------------------+------------------------
 1 mon 28 days 36:28:48 | 1 mon 28 days 36:28:48
```

So the user-defined _interval-number_ `**` overload _does_ satisfy the basic rule.

The conclusion is inescapable: the native PostgreSQL implementation of _interval-number_ multiplication is buggy to the extent that, when edge-case input values are presented, the result is not _strictly_ equal to what a sensible specification of the operation's semantics requires. (It's impossible to imagine a specification that _would_ require the observed behavior.) However, when the native, _non-strict_ implementation of the _interval-interval_ overload of the `=` operator is used to judge the fidelity of the native _interval-number_ multiplication behavior, the results _do_ accord with what the assumed specification requires.

{{< tip title="Avoid the problems of native 'interval' multiplacaton by using custom 'interval' domain types." >}}
Yugabyte recommends that you simply program defensively to avoid the quirks of the native implementation of _interval_ multiplication by following the approach that the section [Custom domain types for specializing the native _interval_ functionality](../../custom-interval-domains/) describes.
{{< /tip >}}
