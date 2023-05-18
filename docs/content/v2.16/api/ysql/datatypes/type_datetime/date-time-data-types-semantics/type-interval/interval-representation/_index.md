---
title: The internal representation of an interval value [YSQL]
headerTitle: How does YSQL represent an interval value?
linkTitle: Interval representation
description: Explains how interval value is represented internally as three fields (months, days, and seconds). [YSQL]
image: /images/section_icons/api/subsection.png
menu:
  v2.16:
    identifier: interval-representation
    parent: type-interval
    weight: 10
type: indexpage
---
{{< tip title="Download and install the date-time utilities code." >}}
The code on this page and on its child, [Modeling the internal representation and comparing the model with the actual implementation](./internal-representation-model/), depends on the code presented in the section [User-defined _interval_ utility functions](../interval-utilities/). This is included in the larger [code kit](../../../download-date-time-utilities/) that includes all of the reusable code that the overall _[date-time](../../../../type_datetime/)_ section describes and uses.
{{< /tip >}}

The PostgreSQL documentation, under the table [Interval Input](https://www.postgresql.org/docs/11/datatype-datetime.html#DATATYPE-INTERVAL-INPUT-EXAMPLES), says this:

> Internally, _interval_ values are stored as months, days, and seconds. This is done because the number of days in a month varies, and a day can have 23 or 25 hours if a Daylight Savings Time adjustment is involved. The months and days fields are integers while the seconds field can store fractions. Because intervals are usually created from constant strings or timestamp subtraction, this storage method works well in most cases, but can cause unexpected results.

Inspection of the C code of the implementation shows that the _mm_ and _dd_ fields of the _[mm, dd, ss]_ internal implementation tuple are four-byte integers. The _ss_ field is an eight-byte integer that records the value in microseconds.

The reference to Daylight Savings Time is a nod to the critical distinction between [_clock-time-semantics_](../../../conceptual-background/#clock-time) and [_calendar-time-semantics_](../../../conceptual-background/#calendar-time). Notice the use of "unexpected". It is better to say that your ability confidently to predict the outcome of _interval_ arithmetic rests on a relatively elaborate mental model. This model has two complementary parts:

- How the values of the three fields of the _[mm, dd, ss]_ representation of an _interval_ value are computed when an _interval_ value is created. The present _"How does YSQL represent an interval value?"_ section addresses this.

- The different semantics of these three fields when an _interval_ value is added or subtracted to/from a _timestamptz_ value, a _timestamp_ value, or a _time_ value or when an _interval_ value is created by subtracting one moment (typically a plain _timestamp_ value or a _timestamptz_value_) from another. This is addressed in the section [_Interval_ arithmetic](../interval-arithmetic/).

As long as you have a robust mental model, then your results will not be unexpected. This section explains the mental model for _interval_ value creation. It enables you to predict what values for _months_, _days_, and _seconds_ will be represented internally when you specify an _interval_ value using values for _years_, _months_, _days_, _hours_, _minutes_, and _seconds_. And it enables you to predict what values for _years_, _months_, _days_, _hours_, _minutes_, and _seconds_ you will read back from an _interval_ value whose _months_, _days_, and _seconds_ values you have managed to predict.

The value recorded by each of the three fields of the representation can be arbitrarily large with respect to the conventions that say, for example, that _25 hours_ is _1 day_ and _1 hour_. For example, this tuple is allowed: _99 months 700 days 926351.522816 seconds_. (Of course, the physical internal representation does impose some limits. See the section [_interval_ value limits](../interval-limits/).)

**Note:** The internal sixteen-byte format of the internal _[mm, dd, ss]_ representation of an _interval_ value determines the theoretical upper limits on the values of each of the three fields. Other factors determine the actual limits. This is explained in the section [_interval_ value limits](../interval-limits/).

## Ad hoc examples

There are no built-in functions or operators that let you display the _months_, _days_, and _seconds_ "as is" from the internal representation. Rather, you can display only canonically derived values for _years_, _months_, _days_, _hours_, _minutes_, and _seconds_. The rule for extracting these values from the internal representation is simple and intuitive. It is presented as executable PL/pgSQL in the implementation of the function [_parameterization (interval_mm_dd_ss_t)_](../interval-utilities/#function-parameterization-interval-mm-dd-ss-t-returns-interval-parameterization-t) in the section [User-defined _interval_ utility functions](../interval-utilities/). Briefly, the internal integral _months_ value is displayed as integral _years_ and integral _months_ by taking one _year_ to be 12 _months_; the internal integral _days_ value is displayed "as is"; and the real number internal _seconds_ is displayed as integral _hours_, integral _minutes_, and real number _seconds_ by taking one _hour_ to be _60 minutes_ and _one minute_ to be _60 seconds_.

The section [Ad hoc examples of defining _interval_ values](./ad-hoc-examples/) provides six examples that give a flavor of the complexity of the rules.

## Modeling the internal representation and comparing the model with the actual implementation

The best way to express a statement of the rules that are consistent with the outcomes of the six [Ad hoc examples of defining _interval_ values](./ad-hoc-examples/), and any number of other examples that you might try, is to implement an executable simulation and to compare its outputs with the outputs that the actual PostgreSQL, and therefore YSQL, implementations produce.

**Note:** If you follow the recommendations made below, you can simply skip attempting to understand these tricky rules without sacrificing any useful functionality.

The section [Modeling the internal representation and comparing the model with the actual implementation](./internal-representation-model/) presents this. Here is the algorithm, copied from the body of [function interval_mm_dd_ss (interval_parameterization_t)](./internal-representation-model/#function-interval-mm-dd-ss-interval-parameterization-t-returns-interval-mm-dd-ss-t):

```output
-- The input values are "p.yy", "p.mm", "p.dd", "p.hh", "p.mi", and "p.ss" — i.e. the
-- conventional parameterization of an "interval" value used by the "::interval" typecast
-- and the "make_interval()" approaches.

-- The output values are "mm_out", "dd_out", and "ss_out" — i.e. the fields of the internal
-- representation tuple.

-- "mm_per_yy", "dd_per_mm", "ss_per_dd", "ss_per_hh", and "ss_per_mi" are constants
-- with the meanings that the mnemonics suggest: the number of months in a year,
-- and so on.
```

```output
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
```

{{< tip title="The algorithm is too hard to remember and produces unhelpful outcomes." >}}
Yugabyte staff members have carefully considered the rules that this algorithm expresses. They have the property that when non-integral values are used in the _::interval_ typecast approach, even a literal that specifies, for example, only months can result in an internal _[mm, dd, ss]_ tuple where each of the fields is non-zero. Try this:

```plpgsql
select interval_mm_dd_ss('11.674523 months '::interval)::text;
```

(The function [interval_mm_dd_ss (interval)](../interval-utilities/#function-interval-mm-dd-ss-interval-returns-interval-mm-dd-ss-t) is defined in the section [User-defined _interval_ utility functions](../interval-utilities/). This is the result:

```output
 (11,20,20363.616)
```

The section [Interval arithmetic](../interval-arithmetic/) explains that the semantics is critically different for each of the internal representation's fields. It recommends that you use only _interval_ values where just one of the three fields is non-zero. The section [Custom domain types for specializing the native _interval_ functionality](../custom-interval-domains/) shows how to impose this discipline programmatically.
{{< /tip >}}

## Possible upcoming implementation change

{{< tip title="Heads up." >}}
There has been some discussion on the [pgsql-general](mailto:pgsql-general@lists.postgresql.org) and [pgsql-hackers](mailto:pgsql-hackers@lists.postgresql.org) mail lists about the algorithm whose implementation that the function _interval()_ documents. As a result, a patch has been developed for a future version of the PostgreSQL system that makes some subtle changes to the "spill-down" behavior in response to real number input values for _years_, _months_, _days_, _hours_, and _minutes_ when you use the _::interval_ typecast approach to construct an _interval_ value. When YugabyteDB adopts this patch, the implementation of the function [interval_mm_dd_ss (interval_parameterization_t)](./internal-representation-model/#function-interval-mm-dd-ss-interval-parameterization-t-returns-interval-mm-dd-ss-t) will be changed accordingly.

If you follow Yugabyte's recommendation to construct _interval_ values using only integral values for _years_, _months_, _days_, _hours_, and _minutes_ (or, equivalently, always to use the _make_interval()_ SQL built-in function rather than the _::interval_ typecast approach), then your application code will not see a behavior change when you move to a version of YugabyteDB that implements this patch. As mentioned above, the section [Custom domain types for specializing the native _interval_ functionality](../custom-interval-domains/) shows how to impose this discipline programmatically.
{{< /tip >}}
