---
title: Date and time operators [YSQL]
headerTitle: Date and time operators
linkTitle: Operators
description: Describes the date and time operators. [YSQL]
image: /images/section_icons/api/subsection.png
menu:
  stable:
    identifier: date-time-operators
    parent: api-ysql-datatypes-datetime
    weight: 80
type: indexpage
---

Each of the comparison operators, `<`, `<=`, `=`, `>=`, `>`, and `<>`, each of the arithmetic operators, `+`, `-`, `*`, and `/`, and, of course, the `::` typecast operator has one or several overloads whose two operands are among the _date_, _time_, plain _timestamp_, _timestamptz_, and _interval_ data types. The [parent section](../../type_datetime/) explains why _timetz_ is not covered in this overall _date-time_ section. This is why there are _five_ interesting _date-time_ data types.

The section [Typecasting between values of different date-time datatypes](../typecasting-between-date-time-values/) shows which of the twenty potential typecasts between pairs of _different_ interesting date-time data types are legal. (It's meaningless to think about typecasting between a pair of values of the _same_ data type.) And for each of those _ten_ that is legal, the section describes its semantics. Further, the typecast operator, `::`, has an overload from/to each of the five listed interesting _date-time_ data types to/from _text_. In other words, there are _ten_ typecasts from/to the interesting _date-time_ data types to/from _text_. The section [Typecasting between date-time values and text values](../typecasting-between-date-time-and-text/) describes these.

The following tables show, for the comparison operators jointly, for each of the addition and subtraction operators separately, and for the multiplication and division operators jointly, which of the twenty-five nominally definable operand pairs are legal. Links are given to the sections that describe the semantics.

{{< tip title="Always write the typecast explicitly." >}}
Some of the expressions that use the binary operators that this section covers are legal where you might expect them not to be. The reason is that, as the section [Typecasting between values of different date-time datatypes](../typecasting-between-date-time-values/) shows, typecasts, and corresponding implicit conversions, are defined between the data types of the operand pairs that you might not expect. Yugabyte recommends that you consider very carefully what your intention is when you take advantage of such conversions between values of different data type.

There are two reasons for writing the typecast explicitly:

- You advertise to the reader that typecasting is being done and that they need to understand the typecast semantics.

- You specify explicitly whether the typecast is to be done on the left operand's value to the right operand's data type, or _vice versa_, rather than having you, and other readers of your code, rely on remembering what the default behavior is.
{{< /tip >}}

## Overloads of the comparison operators

All of the comparison operators, `<`, `<=`, `=`, `>=`, `>`, and `<>` are  legal for pairs of values of the same _date-time_ data type. The semantics of comparing two moment values is straightforward because a moment value is a scalar. The section [How does YSQL represent an interval value?](../date-time-data-types-semantics/type-interval/interval-representation/) explains that an interval value is actually a three-component _[mm, dd, ss]_ tuple. The semantics of _interval-interval_ comparison, therefore, needs careful definition—and the section [Comparing two interval values](../date-time-data-types-semantics/type-interval/interval-arithmetic/interval-interval-comparison/) does this. Yugabyte recommends that you avoid the complexity that the non-scalar nature of _interval_ values brings by adopting the approach that the section [Custom domain types for specializing the native interval functionality](../date-time-data-types-semantics/type-interval/custom-interval-domains/) describes. (It shows you how two define three _interval_ flavors, pure months, pure days, and pure hours, so that their values are effectively scalars.)

When the data types differ, the comparison is sometimes simply illegal. The attempt then causes this error:

```output
42883: operator does not exist...
```

Otherwise, even though the data types differ, the comparison is legal. The section [Test the date-time comparison overloads](./test-date-time-comparison-overloads/) presents code that tests all of the comparison overloads whose syntax you can write. This table summarizes the outcomes. An empty cell means that the overload is illegal.

|                              |          |               |                     |                 |              |
| ---------------------------- | ---------| --------------| --------------------| ----------------| -------------|
| _left operand\right operand_ | **DATE** | **TIME**      | **PLAIN TIMESTAMP** | **TIMESTAMPTZ** | **INTERVAL** |
| **DATE**                     | **ok**   |               | ok                  | ok              |              |
| **TIME**                     |          | **ok**        |                     |                 | ok           |
| **PLAIN TIMESTAMP**          | ok       | [Note](#note) | **ok**              | ok              |              |
| **TIMESTAMPTZ**              | ok       | [Note](#note) | ok                  | **ok**          |              |
| **INTERVAL**                 |          | ok            |                     |                 | **ok**       |

If a comparison is legal between values of two different data types, then (with the caveat that the immediately following note states) it's legal _both_ when values of the two data types are used, respectively, as the left and right operands _and_ when they're used as the right and left operands. In all of these cases, the mutual typecast between values of the pair of data types is legal in each direction.

<a name="note"></a>**Note:** In just _two_ cases, the comparison is _illegal_ between values of a pair of data types where the typecast _is_ legal. The table calls out these cases. In both these cases, the typecast operator is legal _from_ the left operand _to_ the right operand, but _not vice versa_.

If you think that it makes sense, then you can execute the comparison by writing the explicit typecast. Try this:

```plpgsql
set timezone = 'UTC';
with c as (
  select
    '13:00:00'                ::time        as t,
    '02-01-2020 12:30:00'     ::timestamp   as ts,
    '02-01-2020 13:30:00 UTC' ::timestamptz as tstz
  )
select
  (ts  ::time > t)::text as "ts > t",
  (tstz::time > t)::text as "tstz > t"
from c;
```

It runs without error and produces these two values:

```output
 ts > t | tstz > t
--------+----------
 false  | true
```

The subsections [plain _timestamp_ to _time_](../typecasting-between-date-time-values#plain-timestamp-to-time) and [timestamptz to time](../typecasting-between-date-time-values#timestamptz-to-time), in the section [Typecasting between values of different date-time datatypes](../typecasting-between-date-time-values/), explain the semantics of these two typecasts. The outcomes here, _false_ and _true_, are consistent with those explanations.

In summary, comparison makes obvious sense only when the data types of the left and right operands are the same. These comparisons correspond to the on-diagonal cells. If you have worked out that it makes sense to use any of the comparison operations that the table above shows with _ok_ in an off-diagonal cell, then, for clarity, you should write the explicit typecast that you intend.

## Overloads of the addition and subtraction operators

The model for the intended, and therefore useful, functionality is clear and simple:

- Subtraction between a pair of _date_ values produces an _integer_ value. Correspondingly, adding or subtracting an _integer_ value to/from a _date_ value produces a _date_ value. (Early in PostgreSQL's history, _date_ was the only _date-time_ data type. Because _interval_ was not yet available, it was natural that the difference between two _date_ values would be an _integer_ value.)
- Subtraction between a pair of values of the newer data types _time_, plain _timestamp_, or _timestamptz_, produces an _interval_ value. Correspondingly, adding or subtracting an _interval_ value to/from a _time_, plain _timestamp_, or _timestamptz_ value produces, respectively, a _time_, plain _timestamp_, or _timestamptz_ value.
- Adding two _interval_ values or subtracting one _interval_ value from another produces an _interval_ value.

The data types that these useful operations produce are shown in **bold** in the cells in the tables in the sections [Overloads of the addition operator](#overloads-of-the-addition-operator) and [Overloads of the subtraction operator](#overloads-of-the-subtraction-operator). The resulting data types in any other non-empty cells in these tables are shown in regular font—and Yugabyte recommends that you avoid using the operations that they denote in application code. (If you are sure that an operation denoted by a regular font cell makes sense in your present use case, then you should write the implied typecast explicitly.)

- The section [The moment-interval overloads of the "+" and "-" operators for _timestamptz_, _timestamp_, and _time_](../date-time-data-types-semantics/type-interval/interval-arithmetic/moment-interval-overloads-of-plus-and-minus/) explains the semantics here. Because an _interval_ value is a (non-scalar) _[\[mm, dd, ss\]](../date-time-data-types-semantics/type-interval/interval-representation/)_ tuple, the rules are quite complicated.
- The section [Adding or subtracting a pair of _interval_ values](../date-time-data-types-semantics/type-interval/interval-arithmetic/interval-interval-addition/) explains the semantics here. The rules here, too, are subtle—again, because an _interval_ value is an  _[\[mm, dd, ss\]](../date-time-data-types-semantics/type-interval/interval-representation/)_ tuple.

### Overloads of the addition operator

The section [Test the date-time addition overloads](./test-date-time-addition-overloads/) presents code that tests all of the addition operator overloads whose syntax you can write. This table summarizes the outcomes. An empty cell means that the overload is illegal.

|                              |                 |                 |                     |                 |                     |
| ---------------------------- | ----------------| ----------------| --------------------| ----------------| --------------------|
| _left operand\right operand_ | **DATE**        | **TIME**        | **PLAIN TIMESTAMP** | **TIMESTAMPTZ** | **INTERVAL**        |
| **DATE**                     |                 | plain timestamp |                     |                 | plain timestamp     |
| **TIME**                     | plain timestamp |                 | plain timestamp     | timestamptz     | **time**            |
| **PLAIN TIMESTAMP**          |                 | plain timestamp |                     |                 | **plain timestamp** |
| **TIMESTAMPTZ**              |                 | timestamptz     |                     |                 | **timestamptz**     |
| **INTERVAL**                 | plain timestamp | **time**        | **plain timestamp** | **timestamptz** | **interval**        |

Notice that the table is symmetrical about the top-left to bottom-right diagonal. This is to be expected because addition is commutative.

When the resulting data type is rendered in **bold** font, this indicates that the operation makes intrinsic sense. In three of these cases, the operation has a moment value as one of the arguments and an _interval_ value as the other. (The Yugabyte documentation refers to values of the _date_, _time_, plain _timestamp_ or _timestamptz_ data types as _moments_.) In the fourth case, both arguments are _interval_ values.

The _date-time_ data types are unusual with respect to addition, at least with respect to the conceptual proposition, in that you can't add two values of the same moment data type. The on-diagonal cell for each of these four data types is empty. (As mentioned, you _can_ add a pair of _interval_ values.)

The outcomes for the _"date_value + interval_value"_ cell, and the converse _"interval_value + date_value"_ cell, might surprise you. This is explained by the fact that, uniquely for subtraction between moment values, subtracting one _date_ value from another produces an _integer_ value. Try this:

```plpgsql
select
  pg_typeof('2020-01-06'::date - '2020-01-01'::date)  as "data type",
            '2020-01-06'::date - '2020-01-01'::date   as "result";
```

This is the result:

```output
 data type | result
-----------+--------
 integer   |      5
```

The inverse of this, then, is that adding an _integer_ value to a _date_ value produces a _date_ value. Try this:

```plpgsql
select
  pg_typeof('2020-01-01'::date + 5::integer)  as "data type",
            '2020-01-01'::date + 5::integer   as "result";
```

This is the result:

```output
 data type |   result
-----------+------------
 date      | 2020-01-06
```

As mentioned above, this departure, by _date_, from the pattern that the other moment data types follow reflects PostgreSQL's history. Adding a sixth column and a sixth row for _integer_ to the table would clutter it unnecessarily because only the _date-integer_ and _integer-date_ cells in the new column and the new row would be non-empty.

The other outcomes are intuitively clear. What could it mean to add three o'clock to five o'clock? However, the clear conceptual proposition is compromised because, as the sections [time to interval](../typecasting-between-date-time-values/#time-to-interval) and [interval to time](../typecasting-between-date-time-values/#interval-to-time) (on the [Typecasting between values of different _date-time_ datatypes](../typecasting-between-date-time-values/) page) show, _time_ values can be implicitly converted to _interval_ values, and vice-versa.

This explains the non-empty cells in the _time_ row and the _time_ column that are rendered in normal font.

Try this:

```plpgsql
do $body$
declare
  t0  constant time     not null := '12:00:00';
  i0  constant interval not null := '12:00:00';

  t   constant time     not null := i0;
  i   constant interval not null := t0;
begin
  assert t = t0, 'interval > time failed.';
  assert i = i0, 'time > interval failed.';
end;
$body$;
```

The block finishes silently, showing that both assertions hold. Notice the practice recommendation above. If you work out that you _can_ make use of the semantics of these conversions, you should write the typecasts explicitly rather than rely on implicit conversion.

**Note**: you might argue that you _can_ give a meaning to the strange notion of adding a pair of _timestamptz_ values like this:

```plpgsql
drop function if exists sum(timestamptz, timestamptz) cascade;

create function sum(t1 in timestamptz, t2 in timestamptz)
  returns timestamptz
  stable
  language plpgsql
as $body$
declare
  e1      constant double precision not null := extract(epoch from t1);
  e2      constant double precision not null := extract(epoch from t2);
  result  constant timestamptz      not null := to_timestamp(e1 + e2);
begin
  return result;
end;
$body$;
```

Test it like this:

```plpgsql
set timezone = 'UTC';
select sum('1970-01-01 01:00:00 UTC', '1970-01-01 02:00:00 UTC')::text;
```

This is the result:

```output
 1970-01-01 03:00:00+00
```

It's exactly what the semantics of _[extract(epoch from timestamptz_value)](../date-time-data-types-semantics/type-timestamp/)_ and _[to_timestamp(double_precision_value)](../formatting-functions/#from-text-to-date-time)_ tell you to expect. You could even create a user-defined operator `+` based on the function _sum(timestamptz_value, timestamptz_value)_ as defined here. But it's hard to see how the semantics brought by this might be generally useful.

### Overloads of the subtraction operator

The section [Test the date-time subtraction overloads](./test-date-time-subtraction-overloads/) presents code that tests all of the subtraction addition operator overloads whose syntax you can write. This table summarizes the outcomes. An empty cell means that the overload is illegal.

|                              |                 |                 |                     |                 |                     |
| ---------------------------- | ----------------| ----------------| --------------------| ----------------| --------------------|
| _left operand\right operand_ | **DATE**        | **TIME**        | **PLAIN TIMESTAMP** | **TIMESTAMPTZ** | **INTERVAL**        |
| **DATE**                     |                 | plain timestamp | interval            | interval        | plain timestamp     |
| **TIME**                     |                 | **interval**    |                     |                 | **time**            |
| **PLAIN TIMESTAMP**          | interval        | plain timestamp | **interval**        | interval        | **plain timestamp** |
| **TIMESTAMPTZ**              | interval        | timestamptz     | interval            | **interval**    | **timestamptz**     |
| **INTERVAL**                 |                 | interval        |                     |                 | **interval**        |

Notice that the table is symmetrical about the top-left to bottom-right diagonal. This is to be expected because subtraction is commutative in this sense:

```output
(a - b) = -(b - a)
```

When the resulting data type is rendered in **bold** font, this indicates that the operation makes intrinsic sense. You should avoid the operations that the non-empty cells in regular font denote unless you are convinced that a particular one of these makes sense in your present use case. Then, as recommended above, you should write the implied typecast operator explicitly.

Notice that the number of non-empty cells (_seven_ in all) in **bold** font in this table for the overloads of the subtraction operator is the same as the corresponding number in the table for the overloads of the addition operator. This reflects the complementary relationship between addition and subtraction.

However, there are more non-empty cells in regular font (_eleven_ in all) in this table for the overloads of the subtraction operator than there are in the table for the overloads of the addition operator (_eight_ in all). You might think that this is odd. This outcome reflects the complex rules for when implicit typecasting may be invoked.

## Overloads of the multiplication and division operators

The multiplication and division operators are illegal between all possible pairs of values of the _date-time_ data types. This is consistent with the common-sense expectation: what could it mean, for example, to multiply or divide one _timestamp_ value by another? For completeness, the sections [Test the date-time multiplication overloads](./test-date-time-multiplication-overloads/) and [Test the date-time division overloads](./test-date-time-division-overloads/) present code that confirms that this is the case—in other words, that there are no implicit data type conversions, here, to confuse the simple statement of rule.

Multiplication and division are legal only when you multiply or divide an _interval_ value by a real or integral number. (Again, what could it mean, for example, to multiply or divide a _timestamp_ value by a number?) Moreover, division needs no specific discussion because dividing the _interval_ value _i_ by the number _n_ is the same as multiplying _i_ by _1/n_. The section [Multiplying or dividing an interval value by a number](../date-time-data-types-semantics/type-interval/interval-arithmetic/interval-number-multiplication/) explains the semantics. This needs careful definition because of the [non-scalar nature of _interval_ values](../date-time-data-types-semantics/type-interval/interval-representation/).

Yugabyte recommends that you avoid the complexity that the non-scalar nature of _interval_ values brings by adopting the approach that the section [Custom domain types for specializing the native interval functionality](../date-time-data-types-semantics/type-interval/custom-interval-domains/) describes.
