---
title: Scenarios that are sensitive to the timezone/UTC offset [YSQL]
headerTitle: Scenarios that are sensitive to the UTC offset or explicitly to the timezone
linkTitle: Offset/timezone-sensitive operations
description: Explains the scenarios that are sensitive to the UTC offset and possibly, additionally, to the timezone. [YSQL]
image: /images/section_icons/api/subsection.png
menu:
  v2.20:
    identifier: timezone-sensitive-operations
    parent: timezones
    weight: 30
type: indexpage
showRightNav: true
---

All possible operations are inevitably executed in the context of a specified _UTC offset_ because the default scheme for the _TimeZone_ session setting ensures that this is never a zero-length _text_ value or _null_. (See the section [Specify the _UTC offset_ using the session environment parameter _TimeZone_](../syntax-contexts-to-spec-offset/#specify-the-utc-offset-using-the-session-environment-parameter-timezone).) The _TimeZone_ setting might specify the _UTC offset_ directly as an _interval_ value or it might specify it indirectly by identifying the timezone.

However, only _three_ operations are sensitive to the setting:

- The conversion of a plain _timestamp_ value to a _timestamptz_ value.
- The conversion of a _timestamptz_ value to a plain _timestamp_ value.
- Adding or subtracting an _interval_ value to/from a _timestamptz_ value.

## Converting between plain timestamp values and timestamptz values

The detail is explained in the section [Sensitivity of converting between _timestamptz_ and plain _timestamp_ to the _UTC offset_](./timestamptz-plain-timestamp-conversion/). That section defines the semantics of the conversions.

- Other conversions where the source or target data type is _timestamptz_ exhibit sensitivity to the _UTC offset_; but this can always be understood as a transitive sensitivity to the fundamental _timestamptz_ to/from plain _timestamp_ conversions. The section [Typecasting between values of different date-time data types](../../typecasting-between-date-time-values/) calls out all of these cases. Here is an example:

    ```output
    timestamptz_value::date = (timestamptz_value::timestamp)::date
    ```

- You can convert between a _timestamptz_ value and a plain _timestamp_ value using either the _::timestamp_ typecast or the _at time zone_ operator. The former approach is sensitive to the current _TimeZone_ session setting. And the latter approach, because the _UTC offset_ is specified explicitly (maybe directly as an _interval_ value or indirectly via an identified timezone) is insensitive to the current _TimeZone_ session setting.
- The built-in function overloads _timezone(timestamp, text)_ and _timezone(interval, text)_ have identical semantics to the _at time zone_ operator and it can be advantageous to prefer these. See the section [Recommended practice for specifying the _UTC offset_](../recommendation/).

- You can create a _timestamptz_ value explicitly using either the _make_timestamptz()_ built-in or a _text_ literal. In each case, you can identify the timezone, or supply an _interval_ value, directly in the syntax; or you can elide this information and let it be taken from the current _TimeZone_ session setting. The full explanations are given in the section [Specify the _UTC offset_ explicitly within the text of a timestamptz literal or for make_interval()'s 'timezone' parameter](../syntax-contexts-to-spec-offset/#specify-the-utc-offset-explicitly-within-the-text-of-a-timestamptz-literal-or-for-make-interval-s-timezone-parameter) and in the [_text_ to _timestamptz_](../../typecasting-between-date-time-values/#text-to-timestamptz) subsection on the [Typecasting between values of different date-time data types](../../typecasting-between-date-time-values/) page.

## Adding or subtracting an interval value to/from a timestamptz value

The section [The sensitivity of _timestamptz-interval_ arithmetic to the current timezone](./timestamptz-interval-day-arithmetic/) defines the semantics. Briefly, the outcome of the operation is sensitive as follows:

- The sensitivity is specific to adding or subtracting an _interval_ value to or from exactly and only a _timestamptz_ value.

- The sensitivity is specific to only the _dd_ value of the _[\[mm, dd, ss\]](../../date-time-data-types-semantics/type-interval/interval-representation/)_ internal representation of an _interval_ value.

- Only the session's _TimeZone_ setting matters. There is no explicit syntax, analogous to the _at time zone_ operator or its equivalent _timezone()_ function overloads to let you override the session's setting.

- The potential sensitivity requires that the session's _TimeZone_ setting identifies a timezone name, and not an explicit _UTC offset_.

- The identified timezone must specify a Daylight Savings regime.

- The range between the starting _timestamptz_ value and the ending _timestamptz_ value that the _interval_ specifies must span either the "spring forward" moment or the "fall back" moment.
