---
title: Ad hoc examples of defining interval values [YSQL]
headerTitle: Ad hoc examples of defining interval values
linkTitle: Ad hoc examples
description: Provides six ad-hoc-examples of defining interval values. [YSQL]
menu:
  v2.20:
    identifier: ad-hoc-examples
    parent: interval-representation
    weight: 10
type: docs
---

The general way to specify an _interval_ value is by giving values for each of _years_, _months_, _days_, _hours_, _minutes_, and _seconds_ as real numbers. The algorithm for computing the internal _months_, _days_, and _seconds_ is complex—so much so that a precise statement in prose would be tortuous and hard to comprehend. Rather, the rules are given in the section [Modeling the internal representation and comparing the model with the actual implementation](../internal-representation-model/) as PL/pgSQL code.

The following examples give a flavor of the complexity of the rules.

## First example

This specifies only values for the fields that the internal representation uses. And it deliberately uses only integral values for _months_ and _days_.

```plpgsql
select (
    '
            99        months,
           700        days,
      83987851.522816 seconds
    '::interval
  )::text as i;
```

This is the result:

```output
8 years 3 mons 700 days 23329:57:31.522816
```

Notice that: 99 _months_ is displayed as 8 _years_ 3 _months_; 700 _days_ is displayed "as is", even though this duration is longer than two years; and 83987851.522816 _seconds_ is displayed as 23329 _hours_ 57 _minutes_ and 31.522816 _seconds_ even though this duration is longer than two years (and therefore much longer than a month or a day). In other words, neither the _days_ nor the _seconds_ input "spills upwards" in the larger unit direction.

## Second example

This specifies only a value for _years_. But it deliberately specifies this as a real number.

```plpgsql
select (
    '
      3.853467 years
    '::interval
  )::text as i;
```

This is the result:

```output
 3 years 10 mons
```

Notice that _3.853467 years_ is _3 years_ plus _10.241604 months_.  This explains the _10 mons_ in the result. But the _0.241604 months_ remainder did _not_ spill down into _days_.

## Third example

This specifies only a value for _months_. But it deliberately specifies this as a real number.

```plpgsql
select (
    '
      11.674523 months
    '::interval
  )::text as i;
```

This is the result:

```output
 11 mons 20 days 05:39:23.616
```

The fractional portion of the _months_ input _has_ spilled down as _days_—in contrast to the outcome of the second example. Try this continuation of the example:

```plpgsql
-- Notice that 0.674523 months = 0.674523*30 days = 20.235690 days.
-- Use only the fractional part now:
select (
    '
      0.235690 days
    '::interval
  )::text as i;
```

This is the result

```output
 05:39:23.616
```

This _[hours, minutes, seconds]_ tuple is identical to the trailing part of the initial result.

## Fourth example

This specifies only a value for _days_. But it deliberately specifies this as a real number.

```plpgsql
select (
    '
      700.546798 days
    '::interval
  )::text as i;
```

This is the result:

```output
 700 days 13:07:23.3472
```

The fractional portion of the _days_ input has spilled down as seconds. This spill-down is reported in _hours_, _minutes_, and _seconds_, as this continuation of the example confirms:

```plpgsql
-- Notice that 0.546798 days = 0.546798*24*60*60 seconds = 47243.347200 seconds
select (
    '
      47243.347200 seconds
    '::interval
  )::text as i;
```

This means that _minutes_, _hours_, and _seconds_ are reported. This is the result:

```output
 13:07:23.3472
```

This _[hours, minutes, seconds]_ tuple is identical to the trailing part of the result above.

The behavior shown in the [second example](#second-example), on the one hand, and the [third](#third-example) and [fourth](#fourth-example) examples, on the other hand, is remarkable:

- If a real number value for _years_ has, after multiplication by _12_ to convert it into _months_, a remainder after extracting the integral months, then this remainder does _not_ spill down to days.
- But if you specify a real number value for _months_ explicitly, then the remainder after extracting the integral months, _does_ spill down to days.

It's impossible to retrofit a common-sense requirements statement that this asymmetrical behavior meets. You can conclude only that it is simply an emergent behavior of an implementation.

## Fifth example

This example highlights another emergent quirk of the implementation.

```plpgsql
select
  '-0.54 months 17.4 days'::interval as i1,
  '-0.55 months 17.4 days'::interval as i2;
```

This is the result:

```output
       i1       |       i2
----------------+-----------------
 1 day 04:48:00 | 1 day -02:24:00
```

This query helps you visualize what happened:

```plpgsql
select
  17.4 - 0.54*30 as "17.4 - 0.54*30",
  17.4 - 0.55*30 as "17.4 - 0.55*30";
```

This is the result:

```output
 17.4 - 0.54*30 | 17.4 - 0.55*30
----------------+----------------
           1.20 |           0.90
```

Though _0.9 days_ is less than _1 day_, it has been coerced to an integral value using _round()_ semantics, rather than _trunc()_ semantics, leaving a negative remainder to spill down as _hours_. Contrast that outcome with this:

```plpgsql
select
  '1.2 days'::interval as i1,
  '0.9 days'::interval as i2;
```

This is the result:

```output
       i1       |    i2
----------------+----------
 1 day 04:48:00 | 21:36:00
```

In this case, _0.9 days_ _has_ been coerced to an integral value using _trunc()_ semantics, leaving a positive remainder to spill down as _hours_. Try this:

```plpgsql
select (
    '1 day -02:24:00'::interval
    =
    '21:36:00'::interval)
  ::text as "are they equal?";
```

This is the result:

```output
 are they equal?
-----------------
 true
```

You might think that, because the two _interval_ values test as equal, the very strange asymmetry that this fifth example shows has no ultimate consequence. However, the section [Interval arithmetic](../../interval-arithmetic/) shows you that the two differently-spelled _interval_ values that test as equal actually have different semantics—so arguably the outcome of the equality test shows a bug in the PostgreSQL code that YSQL inherits.

The section [Custom domain types for specializing the native _interval_ functionality](../../custom-interval-domains/) shows you that, by following the approach that it describes, you will side-step the quirks that these _ad hoc_ examples have revealed without, in fact, sacrificing any useful functionality.

## Sixth example

Finally try this. It creates the maximum opportunities for spillage. It also adds complexity by specifying negative values for the _years_, _days_, and _minutes_.


```plpgsql
select (
    '
        -9.123456 years,
       18.123456 months,
     -700.123456 days,
       97.123456 hours,
      -86.123456 minutes,
       75.123456 seconds
    '::interval
  )::text as i;
```

This is the result:

```output
 -7 years -8 mons -697 days +109:38:03.511296
```

## Summary

The outcomes of these six _ad hoc_ tests might seem to be inscrutable. (This doubtless explains the use of "unexpected" in the PostgreSQL documentation.) But they most certainly follow well-defined rules, as the section [Modeling the internal representation and comparing the model with the actual implementation](../internal-representation-model) shows. You can predict the results of the _ad hoc_ examples shown above—and, indeed, any example that you might care to invent.
