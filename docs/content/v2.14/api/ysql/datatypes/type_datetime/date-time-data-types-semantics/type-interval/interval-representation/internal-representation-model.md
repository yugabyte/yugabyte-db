---
title: Modeling the internal representation of an interval value [YSQL]
headerTitle: Modeling the internal representation and comparing the model with the actual implementation
linkTitle: Representation model
description: Demonstrates a model of the internal representation of an interval value using PL/pgSQL. [YSQL]
menu:
  v2.14:
    identifier: internal-representation-model
    parent: interval-representation
    weight: 10
type: docs
---

{{< tip title="Download and install the date-time utilities code." >}}
The code on this page depends on the code presented in the section [User-defined _interval_ utility functions](../../interval-utilities/). This is included in the larger [code kit](../../../../download-date-time-utilities/) that includes all of the reusable code that the overall _[date-time](../../../../../type_datetime/)_ section describes and uses.
{{< /tip >}}

Each of the [ad hoc examples](../ad-hoc-examples/) creates an _interval_ value by specifying any subset of its parameterization using _years_, _months_, _days_, _hours_, _minutes_, and _seconds_ from at least one of these through at most all six of them. (Omitting any of these six has the same effect as specifying _zero_ for that parameter.)

This section explains the algorithm that derives the fields of the _[mm, dd, ss]_ representation tuple from the six _years_, _months_, _days_, _hours_, _minutes_, and _seconds_ input values by modeling it using PL/pgSQL.

## Function to create an instance of the modeled internal representation of an interval value

The function _interval_mm_dd_ss(interval_parameterization_t)_ accepts values for the subset of interest of the _years_, _months_, _days_, _hours_, _minutes_, and _seconds_ parameterization that each of the _::interval_ typecast construction method and the _make_interval()_ built-in function construction method uses. It returns an _interval_mm_dd_ss_t_ instance.

The implementation of the function therefore acts as the promised documentation of the algorithm.

### function interval_mm_dd_ss (interval_parameterization_t) returns interval_mm_dd_ss_t

```plpgsql
drop function if exists interval_mm_dd_ss(interval_parameterization_t) cascade;

create function interval_mm_dd_ss(p in interval_parameterization_t)
  returns interval_mm_dd_ss_t
  language plpgsql
as $body$
declare
  mm_per_yy               constant double precision not null := 12.0;
  dd_per_mm               constant double precision not null := 30.0;
  ss_per_dd               constant double precision not null := 24.0*60.0*60.0;
  ss_per_hh               constant double precision not null := 60.0*60.0;
  ss_per_mi               constant double precision not null := 60.0;

  mm_trunc                constant int              not null := trunc(p.mm);
  mm_remainder            constant double precision not null := p.mm - mm_trunc::double precision;

  -- This is a quirk.
  mm_out                  constant int              not null := trunc(p.yy*mm_per_yy) + mm_trunc;

  dd_real_from_mm         constant double precision not null := mm_remainder*dd_per_mm;

  dd_int_from_mm          constant int              not null := trunc(dd_real_from_mm);
  dd_remainder_from_mm    constant double precision not null := dd_real_from_mm - dd_int_from_mm::double precision;

  dd_int_from_user        constant int              not null := trunc(p.dd);
  dd_remainder_from_user  constant double precision not null := p.dd - dd_int_from_user::double precision;

  dd_out                  constant int              not null := dd_int_from_mm + dd_int_from_user;

  d_remainder             constant double precision not null := dd_remainder_from_mm + dd_remainder_from_user;

  ss_out                  constant double precision not null := d_remainder*ss_per_dd +
                                                                p.hh*ss_per_hh +
                                                                p.mi*ss_per_mi +
                                                                p.ss;
begin
  return (mm_out, dd_out, ss_out)::interval_mm_dd_ss_t;
end;
$body$;
```

Notice how the quirks that the [ad hoc examples](../ad-hoc-examples/) highlight are modeled.

- A fractional _years_ part, after subtracting the integral part, carries "right" as INTEGRAL _months_. But, when there's a remainder after dividing this by _12_, this does _not_ carry further to _days_.
- A fractional _months_ part, after subtracting the integral part, _does_ carry "right" as REAL _days_.
- A fractional  _days_ part, after subtracting the integral part, carries "right" as REAL _hours_.
- The INTEGRAL _days_ value is calculated by _first_ truncating (a) the carry-over from _months_ and (b) the truncation of the user-supplied value and then adding these. And the carry-over to _hours_ is computed by adding the individually calculated remainders from steps (a) and (b).

All this seems to be counter-intuitive. But the need to do the calculation this way is pinpointed by the [second](../ad-hoc-examples/#second-example) and [fifth](../ad-hoc-examples/#fifth-example) _ad hoc_ tests.

The algorithm that this function implements was designed by humanly analyzing very many _ad hoc_ observations like those shown above. The hypothesis that the algorithm embodies was tested as described below.

## Test that the hypothesized rules are consistent with the observations that reflect the rules of the actual implementation

Create a procedure to encapsulate the assertion that the rules that govern the simulation are the same as the rules that govern the actual implementation and a procedure to call this basic _assert_ encapsulation with a range of parameterizxations.

### procedure assert_model_ok(interval_parameterization_t)

```plpgsql
drop procedure if exists assert_model_ok(interval_parameterization_t) cascade;

create procedure assert_model_ok(p in interval_parameterization_t)
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
```

Now test it on a set of input values that includes those used for the _ad hoc_ tests shown above.

### procedure test_internal_interval_representation_model()

```plpgsql
drop procedure if exists test_internal_interval_representation_model() cascade;

create procedure test_internal_interval_representation_model()
  language plpgsql
as $body$
begin
  call assert_model_ok(interval_parameterization());

  call assert_model_ok(interval_parameterization(
    mm =>       99,
    dd =>      700,
    ss => 83987851.522816));

  call assert_model_ok(interval_parameterization(
    yy => 3.853467));

  call assert_model_ok(interval_parameterization(
    mm => 11.674523));

  call assert_model_ok(interval_parameterization(
    dd => 0.235690));

  call assert_model_ok(interval_parameterization(
    dd => 700.546798));

  call assert_model_ok(interval_parameterization(
    ss => 47243.347200));

  call assert_model_ok(interval_parameterization(
    mm => -0.54,
    dd => 17.4));

  call assert_model_ok(interval_parameterization(
    mm => -0.55,
    dd => 17.4));

  call assert_model_ok(interval_parameterization(
    mm =>  0.11,
    dd => -1));

  call assert_model_ok(interval_parameterization(
    mm =>  0.12,
    dd => -1));

  call assert_model_ok(interval_parameterization(
    dd => 1.2));

  call assert_model_ok(interval_parameterization(
    dd => 0.9));

  call assert_model_ok(interval_parameterization(
    dd => 1,
    hh => -2,
    mi => 24,
    ss => 0));

  call assert_model_ok(interval_parameterization(
    dd => 0,
    hh => 21,
    mi => 36,
    ss => 0));

  call assert_model_ok(interval_parameterization(
    yy =>  19,
    mm => -1,
    dd =>  17,
    hh => -100,
    mi =>  87,
    ss => -76));

  call assert_model_ok(interval_parameterization(
    yy =>  9.7,
    mm => -1.55,
    dd =>  17.4,
    hh => -99.7,
    mi =>  86.7,
    ss => -75.7));

  call assert_model_ok(interval_parameterization(
    yy => -9.7,
    mm =>  1.55,
    dd => -17.4,
    hh =>  99.7,
    mi => -86.7,
    ss =>  75.7));
  end;
$body$;
```

Invoke it like this:

```plpgsql
call test_internal_interval_representation_model();
```

Each call of _assert_model_ok()_ finishes silently showing that the hypothesized rules have so far been shown always to agree with the rules of the actual implementation. You are challenged to disprove the hypothesis by inventing more tests. If any of your tests causes _assert_model_ok()_ to finish with an assert failure, then [raise a GitHub issue](https://github.com/yugabyte/yugabyte-db/issues/new) against this documentation section.

If you do find such a counter-example, you can compare the results from using the actual implementation and the modeled implementation with this re-write of the logic of the _assert_model_ok()_ procedure. Instead of using _assert_, it shows you the outputs for visual comparison:

### function model_vs_actual_comparison (interval_parameterization_t) returns table(x text)

```plgsql
drop function if exists model_vs_actual_comparison(interval_parameterization_t) cascade;

create function model_vs_actual_comparison(p in interval_parameterization_t)
  returns table(x text)
  language plpgsql
as $body$
declare
  i_modeled        constant interval_mm_dd_ss_t         not null := interval_mm_dd_ss(p);
  i_actual         constant interval                    not null := interval_value(p);

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
  ss =>  75.7));
```

This is the result:

```output
 modeled:   -9 yy,   -7 mm,   -1 dd,  100 hh,   40 mi,  33.700000 ss
 actual:    -9 yy,   -7 mm,   -1 dd,  100 hh,   40 mi,  33.700000 ss
```

## Alternative approach using make_interval()

The _make_interval()_ built-in procedure has _int_ input formal parameters for _years_, _months_, _days_, _hours_, and _minutes_; and it has a _double precision_ input formal parameter for _seconds_. It also, as a bonus, has an _int_ input formal parameter for _weeks_ (where this is treated simply as _7 days_). This parameterization suggests that the PostgreSQL designers saw no use-case for supplying the quantities that use _int_ input formal parameters as real numbers and that it's an unintended consequence of the _::interval_ typecast approach to construct an _interval_ value that it allows a parameterization that provides real numbers for these values. Nevertheless, the implementation supports this and therefore must be documented, as the page has done.

You can substitute the following alternative implementation of the _interval_value()_ function for the implementation given above:

### function interval_value(interval_parameterization_t) returns interval — replacement implementation

```plpgsql
drop function if exists interval_value(interval_parameterization_t) cascade;

create function interval_value(p in interval_parameterization_t)
  returns interval
  language plpgsql
as $body$
declare
  yy int not null := p.yy::int;
  mm int not null := p.mm::int;
  dd int not null := p.dd::int;
  hh int not null := p.hh::int;
  mi int not null := p.mi::int;
begin
  return make_interval(
    years  => yy,
    months => mm,
    days   => dd,
    hours  => hh,
    mins   => mi,
    secs   => p.ss);
end;
$body$;
```

Of course, to test it you must supply only integral values for _years_, _months_, _days_, _hours_, and _minutes_ like this:

```plpgsql
call assert_model_ok(interval_parameterization());

call assert_model_ok(interval_parameterization(
  mm =>       99,
  dd =>      700,
  ss => 83987851.522816
));

call assert_model_ok(interval_parameterization(
  yy => 3,
  mm => 8));

call assert_model_ok(interval_parameterization(
  mm => 11,
  dd => 17));

call assert_model_ok(interval_parameterization(
  hh => 123456));

call assert_model_ok(interval_parameterization(
  dd => 123456));

call assert_model_ok(interval_parameterization(
  ss => 47243.347200));

call assert_model_ok(interval_parameterization(
  mm => -1,
  dd => 17));

call assert_model_ok(interval_parameterization(
  mm =>  11,
  dd => -1));

call assert_model_ok(interval_parameterization(
  dd => 1,
  hh => -2,
  mi => 24,
  ss => 0));

call assert_model_ok(interval_parameterization(
  dd => 0,
  hh => 21,
  mi => 36,
  ss => 0));

call assert_model_ok(interval_parameterization(
  yy =>  19,
  mm => -1,
  dd =>  17,
  hh => -100,
  mi =>  87,
  ss => -76));
```

Each of the calls to _assert_model_ok()_ finishes silently, showing that the assertions that it tests hold. Many more, and more varied, tests than are shown here were run while this example was being developed. The tested assertions have never been seen to fail.
