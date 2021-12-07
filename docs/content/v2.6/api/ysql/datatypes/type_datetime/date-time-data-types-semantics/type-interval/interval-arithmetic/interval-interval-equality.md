---
title: Interval equality [YSQL]
headerTitle: Comparing two interval values for equality
linkTitle: interval-interval equality
description: Explains the semantics of comparing two interval values for equality. [YSQL]
menu:
  v2.6:
    identifier: interval-interval-equality
    parent: interval-arithmetic
    weight: 10
isTocNested: true
showAsideToc: true
---

{{< note title="Inequality comparisons." >}}
The account can be straightforwardly generalized to cover _inequality_ comparisons. The discussion, and some code examples, will be added by future revision of this page.
{{< /note >}}

The semantics of the _interval-interval_ overload of the `=` can be understood in terms of the semantics of the _justify_interval()_ built-in function.

## The justify_hours(), justify_days(), and justify_interval() built-in functions

Consider these interval values from the code example in the [parent section](../../interval-arithmetic#interval-interval-equality):

```output
i1: '5 days    1 hour  '::interval
i2: '4 days   25 hours' ::interval
i3: '5 months  1 day'   ::interval
i4: '4 months 31 days ' ::interval
```

The values _i2_ and _i4_ are noteworthy because: (1) _25 hours_ is more than the number of hours in at least a typical _1 day_ period (but notice that _1 day_ will be _23 hours_ or _25 hours_ at the Daylight Savings Time "spring forward" and "fall back" transitions); (2) and _31 days_ is more than the number of days in at least some _1 month_ durations.

The _justify_hours()_ built-in function "normalizes" the value of the _ss_ field of the internal _[[mm, dd, ss]](../../interval-representation/)_ representation by subtracting an appropriate integral number of _24 hour_ periods so that the resulting _ss_ value is less than _24 hours_. The subtracted _24 hour_ periods are converted to _days_, using the rule that one _24 hour_ period is the same as _1 day_, and added to the value of the _dd_ field. The section [Adding or subtracting a pair of interval values](../interval-interval-addition/) explains that this, in general, changes the semantics of the _interval-timestamptz_ overloads of the _+_ and _-_ operators. Try this:

```plpgsql
select justify_hours('4 days 25 hours'::interval);
```

This is the result:

```output
 5 days 03:00:00
```

In a corresponding way, the _justify_days()_ built-in function "normalizes" the value of the _dd_ field of the internal _[[mm, dd, ss]](../../interval-representation/)_ representation by subtracting an appropriate integral number of _30 day_ periods so that the resulting _dd_ value is less than _30 days_. The subtracted _30 day_ period are converted to _months_, using the rule that one _30 day_ period is the same as _1 month_, and added to the value of the _mm_ field. This changes the semantics of the _interval-timestamptz_ overloads of the _+_ and _-_ operators in the same way that _justify_hours()_ does. Try this:

```plpgsql
select justify_days('4 months 31 days'::interval);
```

This is the result:

```output
 5 mons 1 day
```

The _justify_interval()_ built-in function simply applies first the _justify_hours()_ function and then the _justify_days()_ function to produce what the YSQL documentation refers to as a "normalized _interval_ value". (The PostgreSQL documentation doesn't define such a term.) A normalized _interval_ value is one where the extracted _hours_ value is less than _24_ and the  extracted _days_ value is less than _30_.

Try this:

```plpgsql
select justify_interval('4 months 31 days 25 hours'::interval);
```

This is the result:

```output
 5 mons 2 days 01:00:00
```

## Modeling the interval-interval "=" operator in PL/pgsql

Implement the model as the _function equal(interval, interval)_ thus. It depends upon the function _[interval_mm_dd_ss (interval) returns interval_mm_dd_ss_t](../../interval-utilities/#function-interval-mm-dd-ss-interval-returns-interval-mm-dd-ss-t)_, defined in the section [User-defined interval utility functions](../../interval-utilities/).

```plpgsql
drop function if exists equals(interval, interval) cascade;

create function equals(i1 in interval, i2 in interval)
  returns boolean
  language plpgsql
as $body$
declare
  mm_dd_ss_1 constant interval_mm_dd_ss_t := interval_mm_dd_ss(justify_interval(i1));
  mm_dd_ss_2 constant interval_mm_dd_ss_t := interval_mm_dd_ss(justify_interval(i2));
  modeled_equals constant boolean := mm_dd_ss_1 is not distinct from mm_dd_ss_2;

  actual_equals  constant boolean := i1 = i2;
begin
  assert modeled_equals = actual_equals, 'assert failed';

  return mm_dd_ss_1 = mm_dd_ss_2;
end;
$body$;
```

Notice the use of the _assert_ statement to self-test the model. test it by rewriting the code example in the [parent section](../../interval-arithmetic#interval-interval-equality):

```plpgsql
select
  equals(
    '5 days  1 hours'::interval,
    '4 days 25 hours'::interval
  )::text as e1,
  equals(
    '5 months  1 day' ::interval,
    '4 months 31 days'::interval
  )::text as e2;
```

It finishes silently, showing that the _assert_ holds, and produces this result:

```
  e1  |  e2  
------+------
 true | true
```

## The "strict equals" interval-interval "==" operator

See [The user-defined "strict equals" _interval-interval_ `==` operator](../../interval-utilities/#the-user-defined-strict-equals-interval-interval-operator) in the [User-defined _interval_ utility functions](../../interval-utilities/) section.

(By all means use the native _interval-interval_ `=`  if you're sure that it's the right choice for your use-case.)

Create this function to compare the native `=` and the strict `==` behavior:

```plpgsql
drop function if exists native_vs_strict_equals(interval, interval) cascade;

create function native_vs_strict_equals(i1 in interval, i2 in interval)
  returns text
  language plpgsql
as $body$
declare
  -- "strict" is a reserved word in PL/pgSQL.
  native   boolean := i1 =  i2;
  strict_  boolean := i1 == i2;

  native_txt  text := coalesce(native  ::text, 'null');
  strict_txt  text := coalesce(strict_ ::text, 'null');
begin
  return 'native: ' ||native_txt  ||' | strict: ' ||strict_txt;
end;
$body$;
```

Test it with the following queries. First

```plpgsql
select native_vs_strict_equals(
    '24 months 120 minutes' ::interval,
    ' 2  years   2 hours'   ::interval
  );
```

This is the result:

```output
 native: true | strict: true
```

The first result shows that, though the spellings of the two literals that define the _interval_ values to be compared are different, they both produce the same _mm, dd, ss]_ internal representations.

Second:

```plpgsql
select native_vs_strict_equals(
    '1 mon 30 days 126360 seconds'::interval,
    '1 mon 31 days  39960 seconds'::interval
  );
```

This is the result:

```output
 native: true | strict: false
```

The second result shows the value of the "strict" approach.

Finally:

```plpgsql
select native_vs_strict_equals(
    null::interval,
    null::interval
  );
```

This is the result:

```output
 native: null | strict: null
```

This last result simply shows that the user-defined `==` operator handles `null` arguments correctly.

The section [The moment-interval overloads of the "+" and "-" operators for _timestamptz_, _timestamp_, and _time_](../moment-interval-overloads-of-plus-and-minus/) demonstrates the value of the _interval-interval_ `==` operator.
