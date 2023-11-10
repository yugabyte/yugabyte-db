---
title: Interval value limits [YSQL]
headerTitle: Interval value limits
linkTitle: Interval value limits
description: Explains how the upper and lower limits for interval values must be understood and specified in terms of the three fields of the internal representation. [YSQL]
menu:
  v2.18:
    identifier: interval-limits
    parent: type-interval
    weight: 20
type: docs
---

{{< tip title="Download and install the date-time utilities code." >}}
The code on this page depends on the code presented in the section [User-defined _interval_ utility functions](../interval-utilities/). This is included in the larger [code kit](../../../download-date-time-utilities/) that includes all of the reusable code that the overall _[date-time](../../)_ section describes and uses.
{{< /tip >}}

The design of the code that this section presents, and the interpretation of the results that it produces, depend on the explanations given in the section [How does YSQL represent an _interval_ value?](../interval-representation/). This is the essential point:

- An _interval_ value is stored as a _[mm, dd, ss]_ tuple. The _mm_ and _dd_ fields are recorded "as is" as four-byte integers; and the _ss_ field is recorded in microseconds as an eight-byte integer.

## Summary

The code examples below illustrate and explain the origin of the limits that this summary sets out. You can regard studying it as optional.

You should appreciate the significance of these limits in the context of the minimum and maximum legal values for the plain _timestamp_ and _timestamptz_ data types: _['4713-01-01 00:00:00 UTC BC', '294276-12-31 23:59:59 UTC AD']_. See the note [Maximum and minimum supported values](../../../#maximum-and-minimum-supported-values) on the _date-time_ major section's top page.

### Practical limit for the mm field — ±3,587,867

The actual limit is, of course, set by the range that a four-byte integer can represent. But this range is very much bigger than is needed to express the maximum useful _interval_ value using _years_ or _months_—in other words, the limits for the _mm_ field have no practical consequence. The practical limit is set by the largest _mm_ value, for an _interval_ value with the interval representation _[mm, 0, 0]_, that you can add to the lower limit for _timestamp[tz]_ values, _'4713-01-01 00:00:00 BC'_, without error. This turns out to be _3,587,867_ months.

### Practical limit for the dd field — ±109,203,489

The actual limit, here too, is set by the range that a four-byte integer can represent. But this range, here too, is very much bigger than is needed to express the maximum useful _interval_ value in _days_—in other words, the limits for the _dd_ field, too, have no practical consequence. The practical limit is set by the largest _dd_ value, for an _interval_ value with the interval representation _[0, dd, 0]_, that you can add to the lower limit for _timestamp[tz]_ values, _'4713-01-01 00:00:00 BC'_, without error. This turns out to be _109,203,489_ days.

### Practical limit for the ss field —  ±7,730,941,132,799

This limit corresponds to about _244,983 years_. This is less than the number of seconds between the minimum and the maximum legal _timestamp[tz]_ values because it's limited by some emergent properties of the implementation. (This is demonstrated in the subsection [Limit for the ss field of the internal implementation](#limit-for-the-ss-field-of-the-internal-implementation) below.)

This does, therefore, have a nominal practical consequence in that a carelessly implemented subtraction of _timestamp_ values can cause an error. Try this:

```plpgsql
select (
  '294276-01-01 00:00:00 AD'::timestamp -
    '4713-01-01 00:00:00 BC'::timestamp);
```

This is the result:

```output
-104300858 days -08:01:49.551616
```

The fact that the result is negative is clearly wrong. And a silent wrong results error is the most dangerous kind. The section [_Interval_ arithmetic](../interval-arithmetic/) explains how the rules that govern adding or subtracting an _interval_ value to or from a _timestamp_ or _timestamptz_ value are different for the _mm_, _dd_, and _ss_ fields. When you understand the rules, you'll see that striving for _seconds_ arithmetic semantics when the duration that the _interval_ value represents is as much, even, as _100 years_ is arguably meaningless. This means that the actual limitation that the legal _ss_ range imposes has no consequence when you design application code sensibly. However, you must always design your code so that you maximally reduce the chance that a local careless programming error brings a silent wrong results bug. The section [Custom domain types for specializing the native _interval_ functionality](../custom-interval-domains/) recommends a regime that enforces proper practice to this end.

## Limits for the mm and dd fields of the internal implementation

Create and execute the _mm_and_dd_limits()_ table function to discover the useful practical limits for the _mm_ and _dd_ fields:

```plpgsql
-- The domain "ts_t" is a convenient single point of maintenance to allow
-- choosing between "plain timestamp" and "timestamptz" for the test.
drop domain if exists ts_t cascade;
create domain ts_t as timestamptz;

drop function if exists mm_and_dd_limits() cascade;
create function mm_and_dd_limits()
  returns table(z text)
  language plpgsql
as $body$
declare
  t0               constant ts_t     not null := '4713-01-01 00:00:00 UTC BC';
  t                         ts_t     not null := t0;

  mm_limit         constant int      not null := 3587867;
  dd_limit         constant int      not null := 109203489;

  max_mm_interval  constant interval not null := make_interval(months=>mm_limit);
  max_dd_interval  constant interval not null := make_interval(days=>dd_limit);

  one_month        constant interval not null := make_interval(months=>1);
  one_day          constant interval not null := make_interval(days=>1);
begin
  t := t0 + max_mm_interval;
  z := 'max_mm_interval:                    '||rpad(max_mm_interval::text, 22)||
                                               interval_mm_dd_ss(max_mm_interval)::text;     return next;
  z := 't0 + max_mm_interval:               '||t::text;                                      return next;

  begin
    t := t0 + (max_mm_interval + one_month);
  exception when datetime_field_overflow
    -- 22008: timestamp out of range
    then
      z := 't0 + (max_mm_interval + one_month): causes 22008 error';                         return next;
  end;

  z := '';                                                                                   return next;

  t := t0 + max_dd_interval;
  z := 'max_dd_interval:                    '||rpad(max_dd_interval::text, 22)||
                                               interval_mm_dd_ss(max_dd_interval)::text;     return next;
  z := 't0 + max_dd_interval:               '||t::text;                                      return next;

  begin
    t := t0 + (max_dd_interval + one_day);
  exception when datetime_field_overflow
    -- 22008: timestamp out of range
    then
      z := 't0 + (max_dd_interval + one_day):   causes 22008 error';    return next;
  end;
end;
$body$;

set timezone = 'UTC';
select z from mm_and_dd_limits();
```
This is the result:

```output
 max_mm_interval:                    298988 years 11 mons  (3587867,0,0)
 t0 + max_mm_interval:               294276-12-01 00:00:00+00
 t0 + (max_mm_interval + one_month): causes 22008 error

 max_dd_interval:                    109203489 days        (0,109203489,0)
 t0 + max_dd_interval:               294276-12-31 00:00:00+00
 t0 + (max_dd_interval + one_day):   causes 22008 error
```

You see the same limits both when you define _ts_t_ as _timestamptz_ and when you define it as plain _timestamp_.

## Limit for the ss field of the internal implementation

The practical limits must be established by empirical testing.

The maximum useful limits are bounded by the duration, in seconds, between the minimum and maximum legal _timestamp[tz]_ values. Try this:

```plpgsql
drop function if exists max_seconds() cascade;
create function max_seconds()
  returns table(z text)
  language plpgsql
as $body$
declare
  secs constant double precision not null :=
    extract(epoch from '294276-12-31 23:59:59 UTC AD'::timestamptz) -
    extract(epoch from   '4713-01-01 00:00:00 UTC BC'::timestamptz);
begin
  z := to_char(secs, '9,999,999,999,999');                return next;

  declare
    i   constant interval         not null := make_interval(secs=>secs);
    t            text             not null := '';
    s            double precision not null := 0;
  begin
    begin
      t := i::text;
    exception when datetime_field_overflow then
      z := '22008: "interval out of range" caught.';        return next;
    end;
    begin
      s := extract(seconds from i);
    exception when datetime_field_overflow then
      z := '22008: "interval out of range" caught.';        return next;
    end;
  end;
end;
$body$;

select z from max_seconds();
```

this is the result:

```output
  9,435,181,535,999
 22008: "interval out of range" caught.
 22008: "interval out of range" caught.
```

Anomalously, the evaluation of _"i := make_interval(secs=>secs)"_ (where _secs_ is _9,435,181,535,999_) silently succeeds. But the attempts to use it with, for example, _i::text_ or _extract(seconds from i)_ both cause the _"interval out of range"_ error.

The actual limit, in microseconds, is, of course, set by the range that an eight-byte integer can represent. However, empirical tests show that the actual legal range for _ss_, in seconds, is a lot less than what the representation implies. This is the legal _ss_ range (in seconds):

```output
[-((2^31)*60*60 + 59*60 + 59),  ((2^31 - 1)*60*60 + 59*60 + 59)]  i.e.  [-7730941136399, 7730941132799]
```

This limiting range was discovered by guesswork and testing. It's presumably brought by the decision made by the PostgreSQL designers that the legal _interval_ values must allow representation as text using the familiar _hh:mi:ss_ notation and that you must be able to use the values for _hours_, _minutes_, and _seconds_ that you see in such a representation as inputs to _make_interval()_. Try this positive test:

```plpgsql
select
  interval_mm_dd_ss(make_interval(secs => -7730941136399))::text as "lower limit",
  interval_mm_dd_ss(make_interval(secs =>  7730941132799))::text as "upper limit";
```

This is the result:

```output
     lower limit      |     upper limit
----------------------+---------------------
 (0,0,-7730941136399) | (0,0,7730941132799)
```

Go below the lower limit:

```plpgsql
select make_interval(secs => -7730941136400)::text;
```

This causes the _"22008: interval out of range"_ error. Now go above the upper limit:

```plpgsql
select make_interval(secs => 7730941132800)::text;
```

This causes the same _"22008"_ error.

{{< note title="There appears to be a bug in the '::text' typecast of the resulting 'interval' value when the legal lower limit, 'make_interval(secs => -7730941136399') is used." >}}

Try this:

```plpgsql
select
  make_interval(secs => -7730941136399)::text as "lower limit",
  make_interval(secs =>  7730941132799)::text as "upper limit";
```

This is the result:

```output
        lower limit        |       upper limit
---------------------------+-------------------------
 --2147483648:59:58.999552 | 2147483647:59:58.999552
```

The value for _"lower limit"_, with two leading minus signs, is nonsense. (The value for _"upper limit"_ suffers from a tiny rounding error.) This is why the limits were first demonstrated using the user-defined function _interval_mm_dd_ss()_, written specially to help the pedagogy in the overall section [The _interval_ data type and its variants](../../type-interval/). Look at its implementation in the section [function interval_mm_dd_ss (interval) returns interval_mm_dd_ss_t](../interval-utilities/#function-interval-mm-dd-ss-interval-returns-interval-mm-dd-ss-t). You'll see that it uses the _extract_ function to create the _interval_mm_dd_ss_t_ instance that it returns. This apparently doesn't suffer from the bug that the _::text_ typecast suffers from.

Do this to find the threshold value below which this bug on doing a  _::text_ typecast of an _interval_ value kicks in:

```plpgsql
select
  make_interval(secs => -7730941132800)::text as "one below practical lower limit",
  make_interval(secs => -7730941132799)::text as "practical lower limit";
```

This is the result:

```output
 one below practical lower limit |  practical lower limit
---------------------------------+--------------------------
 --2147483648:00:00              | -2147483647:59:58.999552
```
{{< /note >}}

**Yugabyte therefore recommends that you regard this as the practical range for the _ss_ field of the internal representation:**

```output
[-7,730,941,132,799, 7,730,941,132,799]
```

Confirm that this is sensible like this:

```plpgsql
select
  make_interval(secs => -7730941132799)::text as "practical lower limit",
  make_interval(secs =>  7730941132799)::text as "upper limit";
```

This is the result:

```output
  practical lower limit   |       upper limit
--------------------------+-------------------------
 -2147483647:59:58.999552 | 2147483647:59:58.999552
```

Finally, try this:

```plpgsql
select '7730941132799 seconds'::interval;
```

It causes the error _"22015: interval field value out of range"_. This is a spurious limitation that the _make_interval()_ approach doesn't suffer from. (In fact, any number that you use in the _::interval_ typecast approach is limited to the four-byte integer range _[-2147483648, 2147483647]_.) This explains why these three statements cause the _"22015: interval field value out of range"_ error:

```plpgsql
select '2147483648 months'  ::interval;
select '2147483648 days'    ::interval;
select '2147483648 seconds' ::interval;
```

and why these this statement runs without error:

```plpgsql
select
  '2147483647 months'  ::interval,
  '2147483647 days'    ::interval,
  '2147483647 seconds' ::interval;
```

{{< tip title="Avoid using the '::interval' typecast approach for constructing an 'interval' value." >}}
Yugabyte recommends that you avoid using the _::interval_ typecast approach to construct an _interval_ value in application code.

Notice, though, that if you follow Yugabyte's recommendation to use only the _months_, _days_, and _seconds_ user-defined domains in application code (see the section [Custom domain types for specializing the native _interval_ functionality](../custom-interval-domains/)), and never to use "raw" _interval_ values, then you'll always use the value-constructor functions for these domains and therefore never face the choice between using _make_interval()_ or the _::text_ typecast of an _interval_ literal.

(By all means, use either of these approaches in _ad hoc_ statements at the _ysqlsh_ prompt.)
{{< /tip >}}

It might seem that you lose functionality by following this recommendation because the _::interval_ typecast approach allows you to specify real number values for _years_, _months_, _days_, _hours_, and _minutes_ while the _make_interval()_ approach allows only integral values for these parameters. However, the ability to specify non-integral values for parameters other than _seconds_ brings only confusion.

- The section [Modeling the internal representation and comparing the model with the actual implementation](../interval-representation/internal-representation-model/) shows that the algorithm for computing the _[mm, dd, ss]_ internal representation from the _[yy, mm, dd, hh, mi, ss]_ parameterization, when you typecast the _text_ literal of an _interval_ value,  is so complex (and arbitrary) when non-integral values are provided for the first five of the six parameterization values that you are very unlikely to be able usefully to predict what final _interval_ value you'll get.

- The section [_Interval_ arithmetic](../interval-arithmetic/) shows that significantly different semantics governs how each of the fields of the _[mm, dd, ss]_ internal representation is used. Ensure, therefore, that the _interval_ values you create and use have a non-zero value for only one of the three _[mm, dd, ss]_ fields. You can't ensure this if you allow non-integral values for any of the _years_, _months_, _days_, _hours_, and _minutes_ fields in an _interval_ literal. The section [Custom domain types for specializing the native _interval_ functionality](../custom-interval-domains/) shows how to enforce the recommended approach.
