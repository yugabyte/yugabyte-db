---
title: Interval arithmetic [YSQL]
headerTitle: Interval arithmetic
linkTitle: Interval arithmetic
description: Explains the semantics of timestamp-interval arithmetic and interval-only arithmetic. [YSQL]
image: /images/section_icons/api/subsection.png
menu:
  stable:
    identifier: interval-arithmetic
    parent: type-interval
    weight: 40
type: indexpage
showRightNav: true
---

This section uses the term "moment" as an umbrella for a _timestamptz_ value, a _timestamp_ value, or a _time_ value. (In a broader scenario, a _date_ value is also a moment. But you get an _integer_ value when you subtract one _date_ value from another. And you cannot add or subtract an _interval_ value to/from a _date_ value.) The term "_interval_ arithmetic" is used somewhat loosely to denote these three distinct scenarios:

- **[The interval-interval overload of the "=" operator](#the-interval-interval-overload-of-the-operator)**—_interval-interval_ equality. This is what the term implies.

- **[Interval-only addition/subtraction and multiplication/division](#interval-only-addition-subtraction-and-multiplication-division):** This has two subtopics:

  - _First_ [The _interval-interval_ overloads of the "+" and "-" operators](#the-interval-interval-overloads-of-the-and-operators) to produce a new _interval_ value.

  - And _second_ [The _interval_-number overloads of the "*" and "/" operators](#the-interval-number-overloads-of-the-and-operators) to produce a new _interval_ value.

- **[moment-interval arithmetic](#moment-interval-arithmetic):** This has two subtopics:

  - _First_ [The moment-moment overloads of the "-" operator](#the-moment-moment-overloads-of-the-operator) to produce an _interval_ value. (Addition of two moments is meaningless and is therefore illegal. So is multiplication or division of a moment by a number.)

  - And _second_ [The moment-interval overloads of the "+" and "-" operators](#the-moment-interval-overloads-of-the-and-operators) to produce a new moment of the same data type.

You need to understand the notions that the section [Two ways of conceiving of time: calendar-time and clock-time](../../../conceptual-background/#two-ways-of-conceiving-of-time-calendar-time-and-clock-time) addresses in order to understand the code and the explanations in this page's child page [The moment-_interval_ overloads of the "+" and "-" operators for _timestamptz_, _timestamp_, and _time_](./moment-interval-overloads-of-plus-and-minus/). The notions help you understand how the semantic rules of the native [moment-moment overloads of the "-" operator for timestamptz, timestamp, and time](./moment-moment-overloads-of-minus/) are ultimately confusing and therefore unhelpful—and why you should therefore adopt the practices that the section [Custom domain types for specializing the native _interval_ functionality](../custom-interval-domains/) explains. The distinction between _clock-time-semantics_ and _calendar-time-semantics_ is only implicitly relevant for the notions that the sections [The interval-interval overload of the "=" operator](#the-interval-interval-overload-of-the-operator) and [Interval-only addition/subtraction and multiplication/division](#interval-only-addition-subtraction-and-multiplication-division) explain.

The PostgreSQL documentation does not carefully specify the semantics of _interval_ arithmetic. This page and its children aim to specify the operations in terms of the individual fields of the [internal representation](../interval-representation/).

{{< note title="The results of 'interval' arithmetic are, in general, sensitive to the session's timezone." >}}
More carefully stated, the results of some moment-_interval_ arithmetic operations (the moment-moment overloads of the `-` operator and the moment-_interval_ overloads of the `+` and `-` operators) are sensitive to the session's _TimeZone_ setting when the moments are _timestamptz_ values.
{{< /note >}}

## The interval-interval overload of the "=" operator

Try this:

```plpgsql
select
  (
    '5 days  1 hours'::interval =
    '4 days 25 hours'::interval
  )::text as "'1 day' is defined to be equal to '24 hours'",
  (
    '5 months  1 day' ::interval =
    '4 months 31 days'::interval
  )::text as "'1 month' is defined to be equal to '30 days'";
```

The result for each equality expression is _true_. This is a strange definition of equality because there are _29 days_ in February in a leap year and otherwise _28 days_, there are four _30 day_ months, and there are seven _31 day_ months. Further, _1 day_ is only _usually_ _24 hours_. (The _usually_ caveat acknowledges the consequences of Daylight Savings Time changes.) All this crucially effects the semantics—see [_interval_-moment arithmetic](#moment-interval-arithmetic) below.

The section [Comparing two _interval_ values](./interval-interval-comparison/) explains the model that the _interval_ overload of the `=` operator uses and tests it with a PL/pgSQL implementation. It also shows how to implement a [user-defined _interval-interval_ `==` operator](../interval-utilities#the-user-defined-strict-equals-interval-interval-operator) that implements _strict equality_. The criterion for this is that each field of the LHS and RHS _[\[mm, dd, ss\]](../interval-representation/)_ internal representations must be pairwise equal.

## Interval-only addition/subtraction and multiplication/division

Empirical tests show the following:

- The `+` operator and the `-` operator are overloaded to allow the addition and subtraction of two _interval_ values. Here, the outcome _can_ be understood in terms of pairwise field-by-field addition or subtraction of the two _[mm, dd, ss]_ tuples.

- The `*` operator and the `/` operator are overloaded to allow multiplication or division of an _interval_ value by a real or integer number. Here, the outcome can be _mainly_ understood in terms of multiplying, or dividing, the _[mm, dd, ss]_ tuple, field-by-field, using the same factor. Notice the caveat _mainly_. In some rare corner cases, the model holds only when the forgiving built-in _interval-interval_ `=` operator is used to compare the outcome of the model with that of the actual functionality. When the [user-defined _strict equality_ _interval-interval_ `==`operator](../interval-utilities#the-user-defined-strict-equals-interval-interval-operator) is used, the tests show that, in these corner cases, the outcome of the model does _not_ agree with that of the actual functionality.

In all cases of addition/subtraction and multiplication/division, the model assumes that a new intermediate _[mm, dd, ss]_ tuple is produced and that each of the _mm_ or _dd_ fields might well be real numbers. It must be assumed that this intermediate value is then coerced into the required _[integer, integer, real number]_ format using the same algorithm (see the section [Modeling the internal representation and comparing the model with the actual implementation](../interval-representation/internal-representation-model/)) that is used when such a tuple is provided in the _::interval_ typecast approach.

### The interval-interval overloads of the "+" and "-" operators

The operation acts separately on the three individual fields of the [internal representation](../interval-representation/)  adding or subtracting them pairwise:

- _[mm1, dd1, ss1] ± [mm2, dd2, ss2] = [(mm1 ± mm2), (dd1 ± dd2), (ss1 ± ss2)]_

The section [Adding or subtracting a pair of _interval_ values](./interval-interval-addition/) simulates and tests the model for how this works in PL/pgSQL code.

Try this simple test:

```plpgsql
select '2 months'::interval + '2 days'::interval;
```

This is the result:

```output
 2 mons 2 days
```

This is consistent with the assumed model. And it shows that a practice that the user might adopt to use only _interval_ values that have just a single non-zero internal representation field can easily be thwarted by _interval-interval_ addition or subtraction.

### The interval-number overloads of the "*" and "/" operators

The operation is assumed to be intended to act separately on each of the three individual fields:

- _[mm, dd, ss]\*x = [mm\*x, dd\*x, ss*x]_

When _x_ is equal to _f_, where _f > 1_, the effect is multiplication by _f_. And when _x_ is equal to _1/f_, where _f > 1_, the effect is division by _f_. Therefore a single mental model explains both operations.

Try this positive test:

```plpgsql
select
  '2 months 2 days'::interval*0.9                    as "result 1",
  '2 months'::interval*0.9 + '2 days'::interval*0.9  as "result 2";
```

This is the result:

```output
        result 1        |        result 2
------------------------+------------------------
 1 mon 25 days 19:12:00 | 1 mon 25 days 19:12:00
```

It _is_ consistent with the assumed model.

Now try this negative test:

```plpgsql
select
  '2 months 2 days'::interval*0.97                     as "result 1",
  '2 months'::interval*0.97 + '2 days'::interval*0.97  as "result 1";
```

This is the result:

```output
        result 1        |        result 1
------------------------+------------------------
 1 mon 30 days 03:21:36 | 1 mon 29 days 27:21:36
```

It is _not_ consistent with the assumed model. But the only difference between the positive test and the negative test is that the former uses the factor _0.9_ and the latter uses the factor _0.97_.

Compare the apparently different results using the forgiving native _interval-interval_ '=' operator like this:

```plpgsql
select ('1 mon 30 days 03:21:36'::interval = '1 mon 29 days 27:21:36'::interval)::text;
```

The result is _true_. The section [Multiplying or dividing an _interval_ value by a number](./interval-number-multiplication/) simulates and tests the model for how this works in PL/pgSQL code, and examines this unexpected outcome closely.

One thing, at least, is clear: a practice that the user might adopt to use only _interval_ values that have just a single non-zero internal representation field can easily be thwarted by _interval-number_ multiplication or division. Moreover, the semantics of these operations is not documented and cannot be reliably determined by empirical investigation. The outcomes must, therefore, be considered to be unpredictable.

## Recommendation

{{< tip title="Avoid native 'interval'-'interval' addition/subtraction and 'interval'-number multiplication/division." >}}
Yugabyte recommends that you avoid performing operations whose results can easily thwart an adopted principle for good practice and especially that you avoid operations whose outcomes  must be considered to be unpredictable. It recommends that instead you adopt the practice that the section [Defining and using custom domain types to specialize the native _interval_ functionality](../custom-interval-domains/) explains. Doing this will let you perform the addition, subtraction, multiplication, and division operations that are unsafe with native _interval_ values in a controlled fashion that brings safety.
{{< /tip >}}

## Moment-interval arithmetic

The `-` operator has a set of moment-moment overloads and a set of moment-_interval_ overloads. The `+` operator has a set of -_interval_-moment overloads. The `+` operator has no moment-moment overloads. (This operation would make no sense.)

### The moment-moment overloads of the "-" operator

The `-` operator has an overload for each pair of operands of the _timestamptz_, _timestamp_, and _time_ data types. The result of subtracting two _date_ values has data type _integer_. Try this:

```plpgsql
drop function if exists f() cascade;
create function f()
  returns table(t text)
  language plpgsql
as $body$
declare
  d1 constant date           := '2021-01-13';
  d2 constant date           := '2021-02-17';

  t1 constant time           := '13:23:17';
  t2 constant time           := '15:37:43';

  ts1 constant timestamp     := '2021-01-13 13:23:17';
  ts2 constant timestamp     := '2021-02-17 15:37:43';

  tstz1 constant timestamptz := '2021-01-13 13:23:17 +04:00';
  tstz2 constant timestamptz := '2021-02-17 15:37:43 -01:00';

begin
  t := 'date:        '||(pg_typeof(d2    - d1   ))::text; return next;
  t := 'time:        '||(pg_typeof(t2    - t1   ))::text; return next;
  t := 'timestamp:   '||(pg_typeof(ts2   - ts1  ))::text; return next;
  t := 'timestamptz: '||(pg_typeof(tstz2 - tstz1))::text; return next;
end;
$body$;

select t from f();
```

This is the result:

```output
 date:        integer
 time:        interval
 timestamp:   interval
 timestamptz: interval
```

The _interval_ value that results from subtracting one moment from another (for the _timestamptz_, _timestamp_, or _time_ data types) has, in general, a non-zero value for each of the _dd_ and _ss_ fields of the internal _[\[mm, dd, ss\]](../interval-representation/)_ representation. The value of the _mm_ field is _always_ zero. The section [The moment-moment overloads of the "-" operator for _timestamptz_, _timestamp_, and _time_](./moment-moment-overloads-of-minus/) explains the algorithm that produces the value and shows that, because it has two fields that have different rules for the semantics of the _interval_-moment overloads of the `+` and `-` operators, this approach for producing an _interval_ value should be avoided. See the section [Custom domain types for specializing the native _interval_ functionality](../custom-interval-domains/) for the recommended alternative approach.

### The moment-interval overloads of the "+" and "-" operators

The  `+` and `-` operators have overloads for each pair of operands of each of the _timestamptz_, _timestamp_, and _time_ data types with an _interval_ operand. The notions that the section [Two ways of conceiving of time: calendar-time and clock-time](../../../conceptual-background/#two-ways-of-conceiving-of-time-calendar-time-and-clock-time) addresses are critical for the understanding of this functionality. The topic is explained carefully in the child page [The moment-_interval_ overloads of the "+" and "-" operators for _timestamptz_, _timestamp_, and _time_](./moment-interval-overloads-of-plus-and-minus/). This test is copied from that page:

```plpgsql
select (
    '30 days '::interval = '720 hours'::interval and
    ' 1 month'::interval = ' 30 days '::interval and
    ' 1 month'::interval = '720 hours'::interval
  )::text;
```

The result is _true_, showing that (at least in an inexact sense), the three spellings _'720 hours'_, _'30 days'_, and '_1 month_' all denote the same _interval_ value. Critically, though, _'720 hours'_ is a _clock-time-semantics_ notion while _'30 days'_ and '_1 month_' are each _calendar-time-semantics_ notions, though in subtly different ways. This explains the outcome of this test—again, copied from (but in a simplified form) the child page:

```plpgsql
drop table if exists t;
create table t(
   t0               timestamptz primary key,
  "t0 + 720 hours"  timestamptz,
  "t0 + 30 days"    timestamptz,
  "t0 + 1 month"    timestamptz);

insert into t(t0) values ('2021-02-19 12:00:00 America/Los_Angeles');

set timezone = 'America/Los_Angeles';

update t set
  "t0 + 720 hours" = t0 + '720 hours' ::interval,
  "t0 + 30 days"   = t0 + '30 days'   ::interval,
  "t0 + 1 month"   = t0 + '1 month'   ::interval;

select
  t0,
  "t0 + 720 hours",
  "t0 + 30 days",
  "t0 + 1 month"
from t;
```

This is the result:

```output
           t0           |     t0 + 720 hours     |      t0 + 30 days      |      t0 + 1 month
------------------------+------------------------+------------------------+------------------------
 2021-02-19 12:00:00-08 | 2021-03-21 13:00:00-07 | 2021-03-21 12:00:00-07 | 2021-03-19 12:00:00-07
```

The fact that adding the (inexactly) "same" value produces three different results motivates the careful, and rather tricky, discussion of _clock-time_ and the two sub-flavors of _calendar-time_ (days versus months and years).
