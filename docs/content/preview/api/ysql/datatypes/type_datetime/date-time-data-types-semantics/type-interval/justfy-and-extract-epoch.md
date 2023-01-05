---
title: The justify() and extract(epoch ...) functions for intervals [YSQL]
headerTitle: The justify() and extract(epoch ...) functions for interval values
linkTitle: Justify() and extract(epoch...)
description: Describes the functions justify_hours(interval), justify_days(interval), justify_interval(interval), and extract(epoch from interval_value). [YSQL]
menu:
  preview:
    identifier: justfy-and-extract-epoch
    parent: type-interval
    weight: 35
type: docs
---

{{< tip title="Download and install the date-time utilities code." >}}
The code on this page depends on the code presented in the section [User-defined _interval_ utility functions](../interval-utilities/). This is included in the larger [code kit](../../../download-date-time-utilities/) that includes all of the reusable code that the overall _[date-time](../../../../type_datetime/)_ section describes and uses.
{{< /tip >}}

## The justify_hours(), justify_days(), and justify_interval() built-in functions

Consider these _interval_ values:

```output
i1: '5 days    1 hour  '::interval
i2: '4 days   25 hours' ::interval
i3: '5 months  1 day'   ::interval
i4: '4 months 31 days ' ::interval
```

The values _i2_ and _i4_ are noteworthy:

- because _25 hours_ is more than the number of hours in at least a typical _1 day_ period (but notice that _1 day_ will be _23 hours_ or _25 hours_ at the Daylight Savings Time "spring forward" and "fall back" transitions);
- and because _31 days_ is more than the number of days in at least some _1 month_ durations.

The _justify_ built-in functions have a single _interval_ formal _in_ parameter and return an _interval_ value. They normalize the fields of the _[\[mm, dd, ss\]](../interval-representation/)_ internal representation of the input by decreasing the value of a finer-granularity time unit field and correspondingly increasing the value of its greater granularity neighbor.

- _justify_hours()_ returns an _interval_ whose _ss_ value doesn't exceed the number of seconds in one day _(24\*60\*60)_.
- _justify_days()_ returns an _interval_ whose _dd_ value doesn't exceed the nominal number of days in one month _(30)_.
- _justify_interval()_ returns an _interval_ _both_ whose _ss_ value doesn't exceed the number of seconds in one day _and_ whose _dd_ value doesn't exceed the nominal number of days in one month.

In general, justifying an _interval_ value changes the semantics of adding or subtracting the result to a plain _timestamp_ value or a _timestamptz_ value with respect to using the input, unjustified, value. In general, too, justifying a _pure interval_ value will produce a _hybrid interval_ value. (A _pure_ value is one where only one of the three fields of the _[\[mm, dd, ss\]](../interval-representation/)_ internal representation is non-zero; And a _hybrid_ value has two or more of these fields non-zero.)

### justify_hours()

The _justify_hours()_ built-in function "normalizes" the value of the _ss_ field of the internal _[\[mm, dd, ss\]](../interval-representation/)_ representation by subtracting an appropriate integral number of _24 hour_ periods so that the resulting _ss_ value is less than _24 hours_ (but not less than zero). The subtracted _24 hour_ periods are converted to _days_, using the rule that one _24 hour_ period is always the same as _1 day_, and added to the value of the _dd_ field. (Daylight Savings Time regimes are ignored by the implementation of this rule of thumb.) Try this:

```plpgsql
select justify_hours('4 days 25 hours'::interval);
```

This is the result:

```output
 5 days 01:00:00
```

In general, _justify_hours()_ changes the semantics of the _interval-timestamptz_ overloads of the `+` and `-` operators. Try this:

```plpgsql
set timezone = 'America/Los_Angeles';
with
  c as (
    select
      '2021-03-13 19:00:00 America/Los_Angeles'::timestamptz as d,
      '25 hours'::interval                                   as i)
select
  d +               i  as "d + i",
  d + justify_hours(i) as "d + justify_hours(i)"
from c;
```

This is the result:

```output
         d + i          |  d + justify_hours(i)
------------------------+------------------------
 2021-03-14 21:00:00-07 | 2021-03-14 20:00:00-07
```

Notice that the result of adding the _interval_ value _i_ "as is" is changed (it becomes one hour earlier) when _justify_hours(i)_ is used.

See the section [Sensitivity of timestamptz-interval arithmetic to the current timezone](../../../timezones/timezone-sensitive-operations/timestamptz-interval-day-arithmetic/#interpretation-and-statement-of-the-rules).

### justify_days()

In a corresponding way, the _justify_days()_ built-in function "normalizes" the value of the _dd_ field of the internal _[\[mm, dd, ss\]](../interval-representation/)_ representation by subtracting an appropriate integral number of _30 day_ periods so that the resulting _dd_ value is less than _30 days_ (but not less than zero). The subtracted _30 day_ periods are converted to _months_, using the rule that one _30 day_ period is the same as _1 month_, and added to the value of the _mm_ field. Try this:

```plpgsql
select justify_days('4 months 31 days'::interval);
```

This is the result:

```output
 5 mons 1 day
```

In general, _justify_days()_ changes the semantics of the _interval-timestamp_ and the _interval-timestamptz_ overloads of the `+` and `-` operators. This has nothing to do with Daylight Savings Time and how the reigning timezone specifies the relevant rules. Rather, it has simply to do with the _[calendar-time](../../../conceptual-background/#calendar-time)_ convention for the meaning of adding one month: it aims to take you to the same date in the next month. Try this:

```plpgsql
-- Here, the effect is seen even with plain timestamp (of course).
with
  c as (
    select
      '2021-02-20 12:00:00'::timestamp as d,
      '33 days'::interval              as i)
select
  d +              i  as "d + i",
  d + justify_days(i) as "d + justify_days(i)"
from c;
```

This is the result:

```output
        d + i        | d + justify_days(i)
---------------------+---------------------
 2021-03-25 12:00:00 | 2021-03-23 12:00:00
```

Notice that the result of adding the _interval_ value _i_ "as is" is changed (it becomes two days earlier) when _justify_days(i)_ is used. The rules are explained in the [moment-_interval_ overloads of the "+" and "-" operators for _timestamptz_, _timestamp_, and _time_](../interval-arithmetic/moment-interval-overloads-of-plus-and-minus/) section.

### justify_interval()

The _justify_interval()_ built-in function simply applies first the _justify_hours()_ function and then the _justify_days()_ function to produce what the YSQL documentation refers to as a "normalized _interval_ value". (The PostgreSQL documentation doesn't define such a term.) A normalized _interval_ value is one where the extracted _hours_ value is less than _24_ (but not less than zero) and the extracted _days_ value is less than _30_ (but not less than zero).

Try this:

```plpgsql
select justify_interval('4 months 31 days 25 hours'::interval);
```

This is the result:

```output
 5 mons 2 days 01:00:00
```

Of course, _justify_interval()_ affects the semantics of moment-_interval_ arithmetic too by combining the semantic effects of both _justify_hours()_ and _justify_days()_.

### Modeling the implementations of justify_hours(), justify_days(), and justify_interval()

First, _[function justify_outcomes()](#function-justify-outcomes)_ tests these properties of the _justify_ functions:

```output
justify_days(justify_hours(i)) == justify_interval(i) # user-defined "strict" equals operator
```

and:

```output
     justify_hours(justify_days (i)) =  justify_interval(i)  # natve equals operator

not (justify_hours(justify_days (i)) == justify_interval(i)) # "strict" equals operator
```

Then _[procedure test_justify_model()](#procedure-test-justify-model)_ implements the algorithms that the three _justify_ functions use (as described in prose above) and tests that each produces the same result as the native implementation that it models.

The helper _[function i()](#helper-function-i)_ defines an _interval_ value so that this can be used in both the implementation of _[function justify_outcomes()](#function-justify-outcomes)_ and to invoke _[procedure test_justify_model()](#procedure-test-justify-model)_ without cluttering the interesting code.

#### Helper function i()

Create _function i()_ and inspect the value that it returns:

```plpgsql
drop function if exists i() cascade;

create function i()
  returns interval
  language plpgsql
as $body$
begin
  return make_interval(
    years  => 516,
    months => 317,
    days   => 977,
    hours  => 473,
    mins   => 853,
    secs   => 417.5);
end;
$body$;

select i();
```

This is the result:

```output
 542 years 5 mons 977 days 487:19:57.5
```

#### function justify_outcomes()

Demonstrate the outcomes of _justify_hours()_, _justify_days()_, and _justify_interval()_. And test the fact the effect of _justify_interval()_ is the same as the effect of _justify_hours()_ followed by _justify_days()_—and different from the effect of _justify_days()_ followed by _justify_hours()_.

```plpgsql
drop function if exists justify_outcomes() cascade;

create function justify_outcomes()
  returns table(z text)
  language plpgsql
as $body$
declare
  i  constant interval not null := i();
  i1 constant interval not null := justify_hours(justify_days (i));
  i2 constant interval not null := justify_days (justify_hours(i));
  i3 constant interval not null := justify_interval(i);
begin
  -- Native equality test.
  assert     i2 =   i1,  'Assert #1 failed';

  -- Strict equality tests.
  assert not(i2 ==  i1), 'Assert #2 failed';
  assert     i3 ==  i2,  'Assert #3 failed';

  z := 'i:  '||i ::text;  return next;
  z := 'i1: '||i1::text;  return next;
  z := 'i2: '||i2::text;  return next;
end;
$body$;

select z from justify_outcomes();
```

Notice that the function _justify_outcomes()_ uses both the native _interval-interval_ equality operator, `=`, and the user-defined so-called _strict_ _interval-interval_ equality operator, `==`. See the section [Comparing two _interval_ values](../interval-arithmetic/interval-interval-comparison/). Briefly, the native `=` operator uses a loose definition that judges two _interval_ values to be equal if their internal representations, after applying _justify_interval()_ to each, are identical; but the strict `==` operator judges two _interval_ values to be equal only if the internal representations of the two "as is" values are identical.

This is the result:

```output
 i:  542 years  5 mons  977 days  487:19:57.5
 i1: 545 years  1 mon    37 days   07:19:57.5
 i2: 545 years  2 mons    7 days   07:19:57.5
```

(Whitespace was added manually to improve the readability by vertically align the corresponding fields.)

#### procedure test_justify_model()

```plpgsql
drop procedure if exists test_justify_model(interval) cascade;

create procedure test_justify_model(i in interval)
  language plpgsql
as $body$
declare
  secs_pr_day    constant double precision not null := 24*60*60;
  days_pr_month  constant int              not null := 30;
begin
  -- justify_hours()
  declare
    r_native  constant interval_mm_dd_ss_t not null := interval_mm_dd_ss(justify_hours(i));

    r         constant interval_mm_dd_ss_t not null := interval_mm_dd_ss(i);
    ss        constant double precision    not null := mod(r.ss::numeric, secs_pr_day::numeric);
    dd        constant int                 not null := r.dd + trunc(r.ss/secs_pr_day);
    r_model   constant interval_mm_dd_ss_t not null := (r.mm, dd, ss);
  begin
    assert r_model = r_native, 'Assert #1 failed';
  end;

  -- justify_days()
  declare
    r_native  constant interval_mm_dd_ss_t not null := interval_mm_dd_ss(justify_days(i));

    r         constant interval_mm_dd_ss_t not null := interval_mm_dd_ss(i);
    dd        constant int                 not null := mod(r.dd, days_pr_month);
    mm        constant int                 not null := r.mm + trunc(r.dd/days_pr_month);
    r_model   constant interval_mm_dd_ss_t not null := (mm, dd, r.ss);
  begin
    assert r_model = r_native, 'Assert #2 failed';
  end;

  -- justify_interval()
  declare
    r_native  constant interval_mm_dd_ss_t not null := interval_mm_dd_ss(justify_interval(i));

    r         constant interval_mm_dd_ss_t not null := interval_mm_dd_ss(i);
    ss        constant double precision    not null := mod(r.ss::numeric, secs_pr_day::numeric);
    dd1       constant int                 not null := r.dd + trunc(r.ss/secs_pr_day);

    dd        constant int                 not null := mod(dd1, days_pr_month);
    mm        constant int                 not null := r.mm + trunc(dd1/days_pr_month);

    r_model   constant interval_mm_dd_ss_t not null := (mm, dd, ss);
  begin
    assert r_model = r_native, 'Assert #3 failed';
  end;
end;
$body$;
```

Test it thus:

```plpgsql
call test_justify_model(i());
```

The procedure finishes without error showing that the conditions tested by the _assert_ statements hold.

## The justified_seconds() user-defined function

The section [Comparing two _interval_ values](../interval-arithmetic/interval-interval-comparison/#modeling-the-interval-interval-comparison-test) relies on this function to model the implementation of the comparison algorithm. It's therefore included [here](../interval-utilities/#function-justified-seconds-interval-returns-double-precision) in the [User-defined _interval_ utility functions](../interval-utilities/) section.

Here's a simple test:

```plpgsql
drop function if exists justified_seconds_demo() cascade;

create function justified_seconds_demo()
  returns table(z text)
  language plpgsql
as $body$
begin
  z := 'secs  =>       86000: '||justified_seconds(make_interval(secs  =>       86000)); return next;
  z := 'secs  => 31234567891: '||justified_seconds(make_interval(secs  => 31234567891)); return next;
  z := 'hours =>  2147483647: '||justified_seconds(make_interval(hours =>  2147483647)); return next;
  z := 'days  =>  2147483647: '||justified_seconds(make_interval(days  =>  2147483647)); return next;
end;
$body$;

select z from justified_seconds_demo();
```

The function finishes without error, showing that, for the _interval_ values used for the test, the assertion holds. This is the result:

```output
 secs  =>       86000: 86000
 secs  => 31234567891: 31234567891
 hours =>  2147483647: 7730941129200
 days  =>  2147483647: 185542587100800
```

## The extract(epoch from interval_value) built-in function

The [PostgreSQL documentation](https://www.postgresql.org/docs/11/functions-datetime.html#FUNCTIONS-DATETIME-EXTRACT) specifies the semantics of _extract(epoch from interval_value)_ thus:

- [the function returns] the total number of seconds in the _interval_ [value].

However, it does not explain what this means.

The discussion and demonstration of the user-defined _[justified_seconds()](#the-justified-seconds-user-defined-function)_ function shows that the notion of the total number of seconds in an _interval_ value can be defined only by using a rule of thumb. Try this:

```plpgsql
drop function if exists seconds_in_two_years() cascade;

create function seconds_in_two_years()
  returns table(z text)
  language plpgsql
as $body$
declare
  e_1999 constant double precision not null := extract(epoch from '1999-07-15 12:00:00'::timestamptz);
  e_2001 constant double precision not null := extract(epoch from '2001-07-15 12:00:00'::timestamptz);
  e_2003 constant double precision not null := extract(epoch from '2003-07-15 12:00:00'::timestamptz);

  s1     constant double precision not null := e_2001 - e_1999;
  s2     constant double precision not null := e_2003 - e_2001;
  s3     constant double precision not null := justified_seconds(make_interval(years=>2));
  s4     constant double precision not null := extract(epoch from make_interval(years=>2));
begin
  z := 's1: '||s1::text;  return next;
  z := 's2: '||s2::text;  return next;
  z := 's3: '||s3::text;  return next;
  z := 's4: '||s4::text;  return next;
end;
$body$;

select z from seconds_in_two_years();
```

The number of seconds in a nominal two year period is defined in four different ways:

- s1 ::= from noon on Midsummer's Day 1999 to noon on Midsummer's Day 2001.
- s2 ::= from noon on Midsummer's Day 2001 to noon on Midsummer's Day 2003.
- s3 ::= _justified_seconds('2 years'::interval)_.
- s4 ::= the total number of seconds in _'2 years'::interval_ (quoting the text from the PostgreSQL documentation)

This is the result:

```output
 s1: 63158400
 s2: 63072000
 s3: 62208000
 s4: 63115200
```

Notice that each reported number of seconds differs from the others.

- It's unremarkable that the duration from Midsummer 1999 to Midsummer 2001 is longer than that from Midsummer 2001 to Midsummer 2003 because the former includes a leap year and the latter does not. The difference _(63158400 - 63072000)_ is exactly equal to the number of seconds in one day _(24\*60\*60)_.
- It's easy to see why _justified_seconds('2 years'::interval)_ is less than both _63158400_ and _63072000_: it's because it simply uses the rule that twelve months is _(12\*30)_ days—five days less than a non-leap year. So _(63072000 - 62208000)_ is equal to _(2\*5\*24\*60\*60)_.
- But why is the fourth result, from _extract(epoch from interval_value)_ different from the third result?

It turns out that the result from _extract(epoch from interval_value)_ aims to give a sensible number of seconds for durations of many years. So it uses the (semantics of the) _trunc()_ and _mod()_ built-in functions to transform the value of the _mm_ field of the _interval_ representation to years, _yy_, and a months remainder, _mm_. Then the _yy_ value is multiplied by the number of days in a _Julian year_. This is greater than _12\*30_.

{{< note title="How many days are there in a year?" >}}
Internet search quickly finds lots of articles on this topic—for example, [Gregorian year](https://en.wikipedia.org/wiki/Gregorian_calendar) in Wikipedia. Two subtly different answers are in common use.

- **The _Julian year_ is defined to be _365.25_ days**. This is calculated using the fact that over a four year period, there are _usually_ three normal years and one leap year—so the average number of days per year is _365.25_. The [International Astronomical Union](https://en.wikipedia.org/wiki/International_Astronomical_Union) uses the Julian year to define the size of the [light year](https://en.wikipedia.org/wiki/Light-year).

- **The _Gregorian year_ is defined to be _365.2425_ days**—just a little shorter than the Julian year. This is because the Julian calendar assumed incorrectly that the average solar year is exactly 365.25 days long—an overestimate of a little under one day per century. The Gregorian reform shortened the average (calendar) year by 0.0075 days to stop the drift of the calendar with respect to the equinoxes. It uses the following rule to say when leap years occur in order to produce the required shorter average over a sufficiently long period: a year that is evenly divisible by _100_ is a leap year _only_ if it is _also_ evenly divisible by _400_.
{{< /note >}}

### function modeled_extract_epoch_from_interval()

This function models the rule described in prose above. And it uses an _assert_statement to confirm that the model produces the same result as the native built-in function. Notice that it uses these scaling factors:

- _secs_pr_day_
- _secs_pr_month_
- _secs_pr_year_ (calculated as _secs_pr_day\*avg_days_pr_year_ where _avg_days_pr_year_ is the length of the Julian year)

Create it thus.

```plpgsql
drop function if exists modeled_extract_epoch_from_interval(interval) cascade;

create function modeled_extract_epoch_from_interval(i in interval)
  returns double precision
  language plpgsql
as $body$
declare
  e                 constant double precision    not null := extract(epoch from i);

  -- Scaling factors
  secs_pr_day       constant double precision    not null := 24*60*60;
  secs_pr_month     constant double precision    not null := secs_pr_day*30;
  avg_days_pr_year  constant double precision    not null := 365.25;
  secs_pr_year      constant double precision    not null := secs_pr_day*avg_days_pr_year;

  r                 constant interval_mm_dd_ss_t not null := interval_mm_dd_ss(i);
  mm                constant double precision    not null := mod(r.mm, 12::int);
  yy                constant double precision    not null := trunc(r.mm::numeric/12::int);

  modeled_e         constant double precision    not null := r.ss +
                                                             r.dd*secs_pr_day +
                                                             mm*secs_pr_month +
                                                             yy*secs_pr_year;
begin
  assert
    (extract(epoch from make_interval(days  =>1)) = secs_pr_day) and
    (extract(epoch from make_interval(months=>1)) = secs_pr_month) and
    (extract(epoch from make_interval(years =>1)) = secs_pr_year)
    ,
    'Assert scale factors OK failed';

  assert
    e = modeled_e, 'Assert "e = modeled_e" failed';

  return e;
end;
$body$;
```

### function modeled_extract_epoch_from_interval_demo()

This function invokes _modeled_extract_epoch_from_interval()_ using the same set of input _interval_ values as was used (above) to demonstrate the _[justified_seconds()](#the-justified-seconds-user-defined-function)_ user-defined function.

```plpgsql
drop function if exists modeled_extract_epoch_from_interval_demo() cascade;

create function modeled_extract_epoch_from_interval_demo()
  returns table(z text)
  language plpgsql
as $body$
begin
  z := 'secs  =>       86000: '||modeled_extract_epoch_from_interval(make_interval(secs  =>       86000)); return next;
  z := 'secs  => 31234567891: '||modeled_extract_epoch_from_interval(make_interval(secs  => 31234567891)); return next;
  z := 'hours =>  2147483647: '||modeled_extract_epoch_from_interval(make_interval(hours =>  2147483647)); return next;
  z := 'days  =>  2147483647: '||modeled_extract_epoch_from_interval(make_interval(days  =>  2147483647)); return next;
end;
$body$;

select z from modeled_extract_epoch_from_interval_demo();
```

This is the result:

```output
 secs  =>       86000: 86000
 secs  => 31234567891: 31234567891
 hours =>  2147483647: 7730941129200
 days  =>  2147483647: 185542587100800
```

Compare this with the results from invoking the  _justified_seconds_demo()_ function (above):

```output
 secs  =>       86000: 86000
 secs  => 31234567891: 31234567891
 hours =>  2147483647: 7730941129200
 days  =>  2147483647: 185542587100800
```

They happen to be identical. But this is just a fortuitous outcome due to how the input values happen to be defined as pure seconds and pure days _interval_ values. Try a test that uses a _hybrid interval_ value:

```plpgsql
with c as (
  select make_interval(months=>987, days=>876, hours=>765) as i)
select
  justified_seconds(i)                   as "justified_seconds()",
  modeled_extract_epoch_from_interval(i) as "extracted epoch"
from c;
```

This is the result:

```output
 justified_seconds() | extracted epoch
---------------------+-----------------
          2636744400 |      2673939600
```

Now the results from  _justified_seconds()_ and _extract(epoch ...)_ are different. This is the rule for the behavior difference:

- The results from _justified_seconds()_ and _extract(epoch ...)_ are the same for a _pure interval_ value—i.e. one where only one of the three fields of the _[\[mm, dd, ss\]](../interval-representation/)_ internal representation is non-zero. (The section [Custom domain types for specializing the native interval functionality](../custom-interval-domains/) shows how to ensure declaratively that an _interval_ value is _pure_ in this way.)
- The results from the two approaches will differ for a _hybrid interval_  value when the value's duration is long enough that the scaling of years to seconds, in _extract(epoch ...)_, uses the Julian year definition.

As a consequence, the total number of seconds in two _interval_ values _i1_ and _i2_,  (using the PostgreSQL documentation's wording) will differ when:

```output
(i1 = i2) and not (i1 == i2)
```

In other words, the extracted seconds will differ when the native equality comparison shows _i1_ and _i2_ to be the same but the user-defined strict equality comparison shows _i1_ and _i2_ to be different.

Of course, therefore, _extract(epoch from interval_value)_ is no help for understanding the semantics of the native _interval_ value equality and inequality comparisons.
