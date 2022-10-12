---
title: Upper and lower limits for interval values [YSQL]
headerTitle: Understanding and discovering the upper and lower limits for interval values
linkTitle: interval value limits
description: Explains how the Upper and lower limits for interval values must be understood and specified in terms of the three fields of the internal representation. [YSQL]
menu:
  v2.6:
    identifier: interval-limits
    parent: type-interval
    weight: 20
type: docs
---

The design of the code that this section presents, and the interpretation of the results that it produces, depend on the explanations given in the section [How does YSQL represent an _interval_ value?](../interval-representation/). This is the essential point:

- An _interval_ value is stored as a _[mm, dd, ss]_ tuple. The _mm_ and _dd_ fields are recorded "as is" as four-byte integers; and the _ss_ field is recorded in microseconds as an eight-byte integer.

## Summary

The code examples below illustrate and explain the origin of the limits that this summary sets out. You can regard studying it as optional.

You should appreciate the significance of these limits in the context of the minimum and maximum legal values for the plain _timestamp_ and _timestamptz_ data types: _[4713 BC, 294000 AD]_.

- The maximum useful _interval_ value is therefore _298712 years_. (There is no year _zero_.) This is _3584544 months_.
- This maximum useful _interval_ is about _109102317 days_.

**Limits for the mm field:** _[-2147483648, 2147483647]_. These lower and upper values are about _600_ times bigger than are needed to express the maximum useful _interval_ value using _years_ or _months_—in other words, the limits for the _mm_ field have no practical consequence.

**Limits for the dd field:** _[-2147483648, 2147483647]_. These lower and upper values are about _20_ times bigger than are needed to express the maximum useful _interval_ value in _days_—in other words, the limits for the _dd_ field, too, have no practical consequence.

**Limits for the ss field:** _[-7730941136399, 7730941132799]_. These lower and upper limit values correspond to about _244983 years_. Though similar in magnitude to the maximum useful _interval_ value of _298712 years_, this is nevertheless _53729 years_ smaller. The _ss_ limit does, therefore, have a nominal practical consequence in that a carelessly implemented subtraction of _timestamp_ values can cause an error. Try this:

```plpgsql
select (
  '294276-01-01 00:00:00 AD'::timestamp -
    '4713-01-01 00:00:00 BC'::timestamp);
```

This is the result:

```output
-104300858 days -08:01:49.551616
```

The fact that the result is negative is clearly wrong. And a silent wrong results error is the most dangerous kind. The section [_Interval_ arithmetic](../interval-arithmetic/) explains how the rules that govern adding or subtracting an _interval_ value to or from a _timestamp_ or _timestamptz_ value are different for the _mm_, _dd_, and _ss_ fields. When you understand the rules, you'll see that striving for _seconds_ arithmetic semantics when the duration that the _interval_ value represents is as much, even, as _100 years_ is arguably meaningless. This means that the actual limitation that the legal _ss_ range imposes has no consequence when you design application code sensibly. However, you must always design your code so that you maximally reduce the chance that a local careless programming error brings a silent wrong results bug. The section [Defining and using custom domain types to specialize the native _interval_ functionality](../custom-interval-domains/) recommends a regime that enforces proper practice to this end.

## Limits for the mm and dd fields of the internal implementation

The internal representation implies that the legal values for the _mm_ and _dd_ fields are both expressed by this inclusive range:

```output
[-2^31, (2^31 - 1)]   i.e.   [-2147483648, 2147483647]
```

The following trivial tests show that the legal ranges for _mm_ and _dd_ are both exactly what you'd expect. First, try the positive test:

```plpgsql
select
  '-2147483648 months' ::interval as "lower months limit",
  ' 2147483647 months' ::interval as "upper months limit",
  '-2147483648 days'   ::interval as "lower days limit",
  ' 2147483647 days'   ::interval as "upper days limit";
```

This is the result:

```output
    lower months limit    |   upper months limit   | lower days limit | upper days limit
--------------------------+------------------------+------------------+------------------
 -178956970 years -8 mons | 178956970 years 7 mons | -2147483648 days | 2147483647 days
```

Next, for the negative test, try decreasing either of the lower limit values by _1_, or increasing either of the upper limit values by _1_, like this:

```plpgsql
drop procedure if exists p() cascade;
create procedure p()
  language plpgsql
as $body$
declare
  i interval not null := make_interval();
begin
  begin
    i := '-2147483649 months';
  exception when interval_field_overflow then
    raise info '"interval_field_overflow" for "-2147483649 months"';
  end;

  begin
    i := '2147483648 months';
  exception when interval_field_overflow then
    raise info '"interval_field_overflow" for " 2147483648 months"';
  end;

  begin
    i := '-2147483649 days';
  exception when interval_field_overflow then
    raise info '"interval_field_overflow" for "-2147483649 days"';
  end;

  begin
    i := '2147483648 days';
  exception when interval_field_overflow then
    raise info '"interval_field_overflow" for " 2147483648 days"';
  end;
end;
$body$;

call p();
```

This is the result (after stripping out the _INFO:_ leading text):

```output
"interval_field_overflow" for "-2147483649 months"
"interval_field_overflow" for " 2147483648 months"
"interval_field_overflow" for "-2147483649 days"
"interval_field_overflow" for " 2147483648 days"
```

You could reimplement the procedure _p()_ using the _make_interval()_ approach rather than the _::interval_ typecast approach. This would, though, be a feeble test because the _make_interval()_ procedure is declared with _int_ formal parameters for _months_ and _days_. But the presented arguments are detected as _bigint_ values. The test, then, would cause formal errors like _"42883: function make_interval(months => bigint) does not exist"_. Such errors don't illuminate the question of interest: "What limitations are brought by the internal representation format?"

## Limit for the ss field of the internal implementation

If you haven't already done so, then install the code presented in the section [User-defined interval utility functions](../interval-utilities/).

The representation implies that the legal values for _ss_ (in microseconds) are certainly constrained by this inclusive range:

```output
[-2^63, 2^63 - 1]
```

However, the actual legal range for _ss_ is, in fact, _less_ than what you'd expect. This is the legal _ss_ range (in seconds):

```output
[-((2^31)*60*60 + 59*60 + 59),  ((2^31 - 1)*60*60 + 59*60 + 59)]

i.e.

[-7730941136399, 7730941132799]
```

This limiting range is brought by the decision made by the PostgreSQL designers that the legal _interval_ values must allow representation as text using the familiar _hh:mi:ss_ notation and that you must be able to use the values for _hours_, _minutes_, and _seconds_ that you see in such a representation as inputs to _make_interval()_. Try this positive test:

```plpgsql
select
  interval_mm_dd_ss(make_interval(secs => -7730941136399)) as "lower limit",
  interval_mm_dd_ss(make_interval(secs =>  7730941132799)) as "upper limit";
```

This is the result:

```output
         lower limit         |        upper limit
-----------------------------+----------------------------
 (0,0,-7730941136399.000000) | (0,0,7730941132799.000000)
```

Go below the lower limit:

```plpgsql
select interval_mm_dd_ss(make_interval(secs => -7730941136400));
```

This causes the _"22008: interval out of range"_ error. Now go above the upper limit:

```plpgsql
select interval_mm_dd_ss(make_interval(secs => 7730941132800));
```

This causes the same _"22008"_ error.

{{< note title="There appears to be a bug in the ::text typecast when 'secs => -7730941136400' is used." >}}
Try this

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

The value for _"lower limit"_ is nonsense. (The value for _"upper limit"_ suffers from a tiny rounding error.) This is why the limits were first demonstrated using the user-defined function _interval_mm_dd_ss()_, written specially to help the pedagogy in the overall section [The _interval_ data type and its variants](../../type-interval/). Look at its implementation in the section [function interval_mm_dd_ss (interval) returns interval_mm_dd_ss_t](../interval-utilities/#function-interval-mm-dd-ss-interval-returns-interval-mm-dd-ss-t). You'll see that it uses the _extract_ function to create the _interval_mm_dd_ss_t_ instance that it returns. This apparently doesn't suffer from the bug that the _::text_ typecast suffers from.
{{< /note >}}

**Yugabyte therefore recommends that you regard this as the practical range for the _ss_ field of the internal representation:**

```output
[-7730941132799, 7730941132799]
```

Confirm that this is sensible like this:

```plpgsql
select
  make_interval(secs => -7730941132799)::text as "lower limit",
  make_interval(secs =>  7730941132799)::text as "upper limit";
```

This is the result:

```output
       lower limit        |       upper limit
--------------------------+-------------------------
 -2147483647:59:58.999552 | 2147483647:59:58.999552
```

Finally, try this:

```plpgsql
select '7730941132799 seconds'::interval;
```

It causes the error _"22015: interval field value out of range"_. This is a spurious limitation that the _make_interval()_ approach doesn't suffer from. (In fact, any parameter that you use in the _::interval_ typecast approach is limited to the range _[-2147483648, 2147483647]_.) This explains why these three statements cause the _"22015: interval field value out of range"_ error:

```plpgsql
select '2147483648 months'  ::interval;
select '2147483648 days'    ::interval;
select '2147483648 seconds' ::interval;
```

and why these two statements run without error:

```plpgsql
select '2147483647 hours'   ::interval;
select '2147483647 minutes' ::interval;
```

and why this statement causes the _"22008: interval out of range"_ error:

```plpgsql
select '2147483647 years' ::interval;
```

{{< tip title="Use only 'make_interval()' to construct an 'interval' value." >}}
Yugabyte recommends that you avoid using the _::interval_ typecast approach to construct an _interval_ value in application code. (By all means, use it in _ad hoc_ statements at the _ysqlsh_ prompt.) Use only _make_interval()_ in application code to construct an _interval_ value.
{{< /tip >}}

It might seem that you lose functionality by following this recommendation because the _::interval_ typecast approach allows you to specify real number values for _years_, _months_, _days_, _hours_, and _minutes_ while the _make_interval()_ approach allows only integral values for these parameters. However, the ability to specify real number values for parameters other than _seconds_ brings only confusion.

- The section [Modeling the internal representation and comparing the model with the actual implementation](../interval-representation/internal-representation-model/) shows that the algorithm for computing the _[mm, dd, ss]_ internal representation from the _[yy, mm, dd, hh, mi, ss]_ parameterization is so complex (and arbitrary) when real number values are provided for the first five of the six parameterization values that you are very unlikely to be able usefully to predict what final _interval_ value you'll get.

- The section [_Interval_ arithmetic](../interval-arithmetic/) shows that the semantics of how each of the fields of the _mm, dd, ss]_ representation is used are so significantly different that it's best to arrange that the _interval_ values that you create and use have only one of the three internal field values non-zero. You can't ensure this if you allow real number values for any of the first five parameters. The section [Defining and using custom domain types to specialize the native _interval_ functionality](../custom-interval-domains/) shows how to enforce the recommended approach.
