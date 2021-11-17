---
title: Date and time data types—table of contents [YSQL]
headerTitle: Date and time data types—table of contents
linkTitle: Date and time
description: YSQL supports the date, time, timestamp, and interval data types together with interval arithmetic.
image: /images/section_icons/api/ysql.png
menu:
  v2.6:
    identifier: api-ysql-datatypes-datetime
    parent: api-ysql-datatypes
isTocNested: true
showAsideToc: true
---

Many users of all kinds of SQL databases have reported that they find everything about the _date-time_ story complex and confusing. This explains why this overall section is rather big and why the hierarchy of pages and child pages is both wide and deep. The order presented in the table of contents was designed so that the pages can be read just like the sections and subsections in a printed text book. (The order of the items in the navigation side-bar reflects this reading order too.) The overall pedagogy was designed with this reading order in mind. It is highly recommended, therefore, that you (at least once) read the whole story from start to finish in this order.

If you have to maintain extant application code, you'll probably need to understand everything that this overall section explains. This is likely to be especially the case when the legacy code is old and has, therefore, been migrated from PostgreSQL to YugabyteDB. However, if your purpose is only to write brand-new application code, and if you're happy simply to accept Yugabyte's various recommendations without studying the reasoning that supports these, then you'll need to read only a small part of the overall section. This is what you need:

- **Conceptual background [►](./conceptual-background/)**
- **Real timezones that observe Daylight Savings Time [►](./timezones/extended-timezone-names/canonical-real-country-with-dst/)**
- **Real timezones that do not observe Daylight Savings Time [►](./timezones/extended-timezone-names/canonical-real-country-no-dst/)**
- **The plain timestamp and timestamptz data types [►](./date-time-data-types-semantics/type-timestamp/)**
- **The sensitivity of the conversion between timestamptz and plain timestamp to the UTC offset [►](./timezones/timezone-sensitive-operations/timestamptz-plain-timestamp-conversion/)**
- **The sensitivity of timestamptz-interval arithmetic to the current timezone [►](./timezones/timezone-sensitive-operations/timestamptz-interval-day-arithmetic/)**
- **Recommended practice for specifying the UTC offset [►](./timezones/recommendation/)**
- **Defining and using custom domain types to specialize the native interval functionality [►](./date-time-data-types-semantics/type-interval/custom-interval-domains/)**

## Introduction [►](./intro/)

This page explains how to download the _.zip_ file to create the reusable code that this overall major section describes. And it lists the five _date-time_ data types whose use is recommended. (Yugabyte, following the PostgreSQL documentation, recommends against using the _time with time zone_ data type.)

## Conceptual background [►](./conceptual-background/)

This section explains the background for the accounts of the five _date-time_ data types that the [table](./intro/#table-of-five) shown in the _"Introduction"_ section lists. In particular, it explains the notions that underly the sensitivity to the reigning timezone of these operations:

- [Converting between _timestamptz_ and plain _timestamp_ values](./timezones/timezone-sensitive-operations/timestamptz-plain-timestamp-conversion/).
- [Adding or subtracting an _interval_ value to/from a _timestamptz_ or plain _timestamp_ value](./date-time-data-types-semantics/type-interval/interval-arithmetic/moment-interval-overloads-of-plus-and-minus/).

## Timezones and UTC offsets [►](./timezones/)

This section explains: the purpose and significance of the _set timezone_ SQL statement; the _at time zone_ operator for plain _timestamp_ and _timestamptz_ expressions; the various other ways that, ultimately, the intended _UTC offset_ is specified; and which operations are sensitive to the specified _UTC offset_. It has these child pages:

- **Catalog views for timezone information—pg_timezone_names and pg_timezone_abbrevs [►](./timezones/catalog-views/)**
- **The extended_timezone_names view [►](./timezones/extended-timezone-names/)**
  - **extended_timezone_names—unrestricted full projection [►](./timezones/extended-timezone-names/unrestricted-full-projection/)**
  - **Real timezones that observe Daylight Savings Time [►](./timezones/extended-timezone-names/canonical-real-country-with-dst/)**
  - **Real timezones that do not observe Daylight Savings Time [►](./timezones/extended-timezone-names/canonical-real-country-no-dst/)**
  - **Synthetic timezones (do not observe Daylight Savings Time) [►](./timezones/extended-timezone-names/canonical-no-country-no-dst/)**
- **Scenarios that are sensitive to the UTC offset and possibly, additionally, to the timezone [►](./timezones/timezone-sensitive-operations/)**
  - **The sensitivity of the conversion between timestamptz and plain timestamp to the UTC offset [►](./timezones/timezone-sensitive-operations/timestamptz-plain-timestamp-conversion/)**
  - **The sensitivity of timestamptz-interval arithmetic to the current timezone [►](./timezones/timezone-sensitive-operations/timestamptz-interval-day-arithmetic/)**
- **Four ways to specify the UTC offset [►](./timezones/ways-to-spec-offset/)**
  - **Rules for resolving a string that's intended to identify a UTC offset [►](./timezones/ways-to-spec-offset/name-res-rules/)**
    - **Rule 1 [►](./timezones/ways-to-spec-offset/name-res-rules/rule-1/)** — It's resolved case-insensitively.
    - **Rule 2 [►](./timezones/ways-to-spec-offset/name-res-rules/rule-2/)** — It's never resolved in _pg_timezone_names.abbrev_.
    - **Rule 3 [►](./timezones/ways-to-spec-offset/name-res-rules/rule-3/)** — It's never resolved in _pg_timezone_abbrevs.abbrev_ as the argument of set timezone but is resolved there as the argument of _at time zone_ (and, equivalently, in _timezone()_) and as the argument of _make_timestamptz()_ (and equivalently within a text literal for a _timestamptz_ value).
    - **Rule 4 [►](./timezones/ways-to-spec-offset/name-res-rules/rule-4/)** — It's is resolved first in _pg_timezone_abbrevs.abbrev_ and, only if this fails, then in _pg_timezone_names.name_. This applies only in those syntax contexts where _pg_timezone_abbrevs.abbrev_ is a candidate for the resolution—so not for _set timezone_, which looks only in _pg_timezone_names.name_.
    - **Helper functions [►](./timezones/ways-to-spec-offset/name-res-rules/helper-functions/)**
- **Three syntax contexts that use the specification of a UTC offset [►](./timezones/syntax-contexts-to-spec-offset/)**
- **Recommended practice for specifying the UTC offset [►](./timezones/recommendation/)**

## Typecasting between date-time values and text values [►](./typecasting-between-date-time-and-text/)

Many of the code examples rely on typecasting—especially from/to _text_ values to/from plain _timestamp_ and _timestamptz_ values. It's unlikely that you'll use such typecasting in actual application code. (Rather, you'll use dedicated built-in functions for the conversions.) But you'll rely heavily on typecasting for _ad hoc_ tests while you develop such code.

## The semantics of the date-time data types] [►](./date-time-data-types-semantics/)

This section defines the semantics of the _date_ data type, the _time_ data type, the plain _timestamp_ and _timestamptz_ data types, and the _interval_ data type. _Interval_ arithmetic is rather tricky. This explains the size of the subsection that's devoted to this data type. The section has these child pages:

- **The date data type [►](./date-time-data-types-semantics/type-date/)**
- **The time data type [►](./date-time-data-types-semantics/type-time/)**
- **The plain timestamp and timestamptz data types [►](./date-time-data-types-semantics/type-timestamp/)**
- **The interval data type and its variants [►](./date-time-data-types-semantics/type-interval/)**
  - **How does YSQL represent an interval value? [►](./date-time-data-types-semantics/type-interval/interval-representation/)**
    - **Ad hoc examples of defining interval values [►](./date-time-data-types-semantics/type-interval/interval-representation/ad-hoc-examples/)**
    - **Modeling the internal representation and comparing the model with the actual implementation [►](./date-time-data-types-semantics/type-interval/interval-representation/internal-representation-model/)**
  - **Understanding and discovering the upper and lower limits for interval values [►](./date-time-data-types-semantics/type-interval/interval-limits/)**
  - **Declaring intervals [►](./date-time-data-types-semantics/type-interval/declaring-intervals/)**
  - **Interval arithmetic [►](./date-time-data-types-semantics/type-interval/interval-arithmetic/)**
    - **Comparing two interval values for equality [►](./date-time-data-types-semantics/type-interval/interval-arithmetic/interval-interval-equality/)**
    - **Adding or subtracting a pair of interval values [►](./date-time-data-types-semantics/type-interval/interval-arithmetic/interval-interval-addition/)**
    - **Multiplying or dividing an interval value by a real or integral number [►](./date-time-data-types-semantics/type-interval/interval-arithmetic/interval-number-multiplication/)**
    - **The moment-moment overloads of the "-" operator for timestamptz, timestamp, and time [►](./date-time-data-types-semantics/type-interval/interval-arithmetic/moment-moment-overloads-of-minus/)**
    - **The moment-interval overloads of the "+" and "-" operators for timestamptz, timestamp, and time [►](./date-time-data-types-semantics/type-interval/interval-arithmetic/moment-interval-overloads-of-plus-and-minus/)**
  - **Defining and using custom domain types to specialize the native interval functionality [►](./date-time-data-types-semantics/type-interval/custom-interval-domains/)**
  - **User-defined interval utility functions [►](./date-time-data-types-semantics/type-interval/interval-utilities/)**

## Typecasting between values of different date-time datatypes [►](./typecasting-between-date-time-values/)

This section presents the five-by-five matrix of all possible conversions between values of the _date-time_ datatypes. Many of the cells are empty because they correspond to operations that aren't supported (or, because the cell is on the diagonal representing the conversion between values of the same data type, it's tautologically uninteresting). This still leaves *twenty* typecasts whose semantics you need to understand. However, many can be understood as combinations of others, and this leaves only a few that demand careful study. The critical conversions are between plain _timestamp_ and _timestamptz_ values in each direction.
