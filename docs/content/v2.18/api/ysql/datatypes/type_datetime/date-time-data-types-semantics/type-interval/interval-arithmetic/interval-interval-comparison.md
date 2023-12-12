---
title: Interval comparison [YSQL]
headerTitle: Comparing two interval values
linkTitle: Interval-interval comparison
description: Explains the semantics of comparing two interval values. [YSQL]
menu:
  v2.18:
    identifier: interval-interval-comparison
    parent: interval-arithmetic
    weight: 10
type: docs
---

{{< tip title="Download and install the date-time utilities code." >}}
The code on this page depends on the code presented in the section [User-defined _interval_ utility functions](../../interval-utilities/). This is included in the larger [code kit](../../../../download-date-time-utilities/) that includes all of the reusable code that the overall _[date-time](../../../../../type_datetime/)_ section describes and uses.
{{< /tip >}}

The semantics of the _interval-interval_ overloads of these comparison operators can be understood in terms of the semantics of the _[justify_interval()](../../justfy-and-extract-epoch/#the-justify-hours-justify-days-and-justify-interval-built-in-functions)_ built-in function:

```output
<  <=  = >=  >  !=
```

## Modeling the interval-interval comparison test

Implement the model as _function modeled_inequality_comparison()_ thus. It depends upon these two functions, defined in the _"User-defined interval utility functions"_ section:

- _[interval_mm_dd_ss (interval)](../../interval-utilities/#function-interval-mm-dd-ss-interval-returns-interval-mm-dd-ss-t)_

- _[justified_seconds(interval)](../../interval-utilities/#function-justified-seconds-interval-returns-double-precision)_

```plpgsql
drop function if exists modeled_inequality_comparison(interval, interval) cascade;

create function modeled_inequality_comparison(i1 in interval, i2 in interval)
  returns text
  language plpgsql
as $body$
declare
  gt constant text not null := 'i1 > i2';
  eq constant text not null := 'i1 = i2';
  lt constant text not null := 'i1 < i2';

  -- This implicity tests that neither "i1" nor "i2" is NULL.
  actual_ineq   constant text             not null := case
                                                        when i1 > i2 then gt
                                                        when i1 = i2 then eq
                                                        when i1 < i2 then lt
                                                      end;

  s1            constant double precision not null := justified_seconds(i1);
  s2            constant double precision not null := justified_seconds(i2);
  modeled_ineq  constant text             not null := case
                                                        when s1 > s2 then gt
                                                        when s1 = s2 then eq
                                                        when s1 < s2 then lt
                                                      end;
begin
  assert modeled_ineq = actual_ineq, 'Assert #1 failed';

  -- Test an alternative model for equality.
  if actual_ineq = eq then
    declare
      r1 constant interval_mm_dd_ss_t not null := interval_mm_dd_ss(justify_interval(i1));
      r2 constant interval_mm_dd_ss_t not null := interval_mm_dd_ss(justify_interval(i2));
    begin
      assert r1 = r2, 'Assert #2 failed';
    end;
  end if;

  return actual_ineq;
end;
$body$;
```

Notice the use of the _assert_ statement to self-test an alternative definition of the model that works only for the `=` comparison.

Sanity-check the model by rewriting the code example in the [parent section](../../interval-arithmetic#the-interval-interval-overload-of-the-operator):

```plpgsql
select
  modeled_inequality_comparison(
    '5 days  1 hours'::interval,
    '4 days 25 hours'::interval
  )::text as e1,
  modeled_inequality_comparison(
    '5 months  1 day' ::interval,
    '4 months 31 days'::interval
  )::text as e2;
```

It finishes silently, showing that the _assert_ holds, and produces this result:

```output
   e1    |   e2
---------+---------
 i1 = i2 | i1 = i2
```

Here is a more thorough test:

```plpgsql
drop procedure if exists test_modeled_inequality_comparison() cascade;

create procedure test_modeled_inequality_comparison()
  language plpgsql
as $body$
declare
  gt constant text not null := 'i1 > i2';
  eq constant text not null := 'i1 = i2';
  lt constant text not null := 'i1 < i2';
begin
  ----------------------------------------------------------
  assert
    modeled_inequality_comparison(
        '1 day  1 second'::interval,
        '24 hours       '::interval
      ) = gt,
    'Assert #1 failed';

  assert
    modeled_inequality_comparison(
        '1 day          '::interval,
        '24 hours       '::interval
      ) = eq,
    'Assert #2 failed';

  assert
    modeled_inequality_comparison(
        '1 day -1 second'::interval,
        '24 hours       '::interval
      ) = lt,
    'Assert #3 failed';
  ----------------------------------------------------------

  assert
    modeled_inequality_comparison(
        '1 day                         '::interval,
        '23 hours 59 minutes 59 seconds'::interval
      ) = gt,
    'Assert #4 failed';

  assert
    modeled_inequality_comparison(
        '1 day                         '::interval,
        '24 hours                      '::interval
      ) = eq,
    'Assert #5 failed';

  assert
    modeled_inequality_comparison(
        '1 day                         '::interval,
        '24 hours  1 minutes  1 seconds'::interval
      ) = lt,
    'Assert #6 failed';
  ----------------------------------------------------------

  assert
    modeled_inequality_comparison(
        '10000 years  1 second'::interval,
        '3600000 days         '::interval
      ) = gt,
    'Assert #7 failed';

  assert
    modeled_inequality_comparison(
        '10000 years          '::interval,
        '3600000 days         '::interval
      ) = eq,
    'Assert #8 failed';

  assert
    modeled_inequality_comparison(
        '10000 years -1 second'::interval,
        '3600000 days         '::interval
      ) = lt,
    'Assert #9 failed';
  ----------------------------------------------------------

  assert
    modeled_inequality_comparison(
        '1004 years 2 mons 11 days 04:51:31'::interval,
         make_interval(secs=>31234567890)
        ) = gt,
    'Assert #10 failed';

  assert
    modeled_inequality_comparison(
        '1004 years 2 mons 11 days 04:51:31'::interval,
         make_interval(secs=>31234567891)
      ) = eq,
    'Assert #11 failed';

  assert
    modeled_inequality_comparison(
        '1004 years 2 mons 11 days 04:51:31'::interval,
         make_interval(secs=>31234567892)
      ) = lt,
    'Assert #12 failed';
end;
$body$;

call test_modeled_inequality_comparison();
```

The procedure finishes without error, showing that each of the assertions holds. In summary, the semantics of all of the _interval-interval_ comparisons can be understood in terms of the semantics of these operators for comparing to real numbers. Each of the to-be-compared _interval_ values is mapped to a real number using the function _[justified_seconds()](../../interval-utilities/#function-justified-seconds-interval-returns-double-precision)_.


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

The second result shows the benefit brought by the "strict" approach.

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

The section [The moment-_interval_ overloads of the "+" and "-" operators for _timestamptz_, _timestamp_, and _time_](../moment-interval-overloads-of-plus-and-minus/) demonstrates the benefit brought by the _interval-interval_ `==` operator.
