---
title: Date-time types ToC [YSQL]
headerTitle: Table of contents for the date-time data types section
linkTitle: Section contents
description: Overview and content map of the date-time types section.
menu:
  v2.14:
    identifier: toc
    parent: api-ysql-datatypes-datetime
    weight: 31
type: docs
---

## [The "Date and time data types and functionality" parent page](../../type_datetime/)

This section is the top of the entire _date-time_ documentation subtree. Its siblings are the top-of-subtree pages for other data types like [JSON data types and functionality](../../type_json/) and [Array data types and functionality](../../type_array/).

It presents a table that summarizes the properties of the _date-time_ data types, and that links to the dedicated sections for each of these. It recommends avoiding the _timetz_ datatype and states that it will not, therefore, be treated in the _data-time_ major section.

For completeness, it presents a table of the special _date-time_ manifest constants and recommends that you avoid using all of these except for _'infinity'_ and _'-infinity'_.

Finally, it lists the _date-time_ subsections that cover just those topics that you will need to understand if your purpose is only to write brand-new application code.

## [Conceptual background](../conceptual-background/)

This section explains the background for the accounts of the _date-time_ data types. In particular, it explains the notions that underly the sensitivity to the reigning timezone of these operations:

- [Converting between _timestamptz_ and plain _timestamp_ values](../timezones/timezone-sensitive-operations/timestamptz-plain-timestamp-conversion/).
- [Adding or subtracting an _interval_ value to/from a _timestamptz_ or plain _timestamp_ value](../date-time-data-types-semantics/type-interval/interval-arithmetic/moment-interval-overloads-of-plus-and-minus/).

## [Timezones and UTC offsets](../timezones/)

This section explains: the purpose and significance of the _set timezone_ SQL statement; the _at time zone_ operator for plain _timestamp_ and _timestamptz_ expressions; the various other ways that, ultimately, the intended _UTC offset_ is specified; and which operations are sensitive to the specified _UTC offset_. It has these child pages:

- **[The pg_timezone_names and pg_timezone_abbrevs catalog views](../timezones/catalog-views/)**
- **[The extended_timezone_names view](../timezones/extended-timezone-names/)**
  - **[extended_timezone_names—unrestricted full projection](../timezones/extended-timezone-names/unrestricted-full-projection/)**
  - **[Real timezones that observe Daylight Savings Time](../timezones/extended-timezone-names/canonical-real-country-with-dst/)**
  - **[Real timezones that don't observe Daylight Savings Time](../timezones/extended-timezone-names/canonical-real-country-no-dst/)**
  - **[Synthetic timezones (do not observe Daylight Savings Time)](../timezones/extended-timezone-names/canonical-no-country-no-dst/)**
- **[Scenarios that are sensitive to the UTC offset or explicitly to the timezone](../timezones/timezone-sensitive-operations/)**
  - **[Sensitivity of converting between timestamptz and plain timestamp to the UTC offset](../timezones/timezone-sensitive-operations/timestamptz-plain-timestamp-conversion/)**
  - **[Sensitivity of timestamptz-interval arithmetic to the current timezone](../timezones/timezone-sensitive-operations/timestamptz-interval-day-arithmetic/)**
- **[Four ways to specify the UTC offset](../timezones/ways-to-spec-offset/)**
  - **[Rules for resolving a string that's intended to identify a UTC offset](../timezones/ways-to-spec-offset/name-res-rules/)**
    - **[Rule 1](../timezones/ways-to-spec-offset/name-res-rules/rule-1/)** — It's resolved case-insensitively.
    - **[Rule 2](../timezones/ways-to-spec-offset/name-res-rules/rule-2/)** — It's never resolved in _pg_timezone_names.abbrev_.
    - **[Rule 3](../timezones/ways-to-spec-offset/name-res-rules/rule-3/)** — It's never resolved in _pg_timezone_abbrevs.abbrev_ as the argument of set timezone but is resolved there as the argument of _at time zone_ (and, equivalently, in _timezone()_) and as the argument of _make_timestamptz()_ (and equivalently within a text literal for a _timestamptz_ value).
    - **[Rule 4](../timezones/ways-to-spec-offset/name-res-rules/rule-4/)** — It's is resolved first in _pg_timezone_abbrevs.abbrev_ and, only if this fails, then in _pg_timezone_names.name_. This applies only in those syntax contexts where _pg_timezone_abbrevs.abbrev_ is a candidate for the resolution—so not for _set timezone_, which looks only in _pg_timezone_names.name_.
    - **[Helper functions](../timezones/ways-to-spec-offset/name-res-rules/helper-functions/)**
- **[Three syntax contexts that use the specification of a UTC offset](../timezones/syntax-contexts-to-spec-offset/)**
- **[Recommended practice for specifying the UTC offset](../timezones/recommendation/)**

## [Typecasting between date-time values and text values](../typecasting-between-date-time-and-text/)

Many of the code examples rely on typecasting—especially from/to _text_ values to/from plain _timestamp_ and _timestamptz_ values. It's unlikely that you'll use such typecasting in actual application code. (Rather, you'll use dedicated built-in functions for the conversions.) But you'll rely heavily on typecasting for _ad hoc_ tests while you develop such code.

## [The semantics of the date-time data types](../date-time-data-types-semantics/)

This section defines the semantics of the _date_ data type, the _time_ data type, the plain _timestamp_ and _timestamptz_ data types, and the _interval_ data type. _Interval_ arithmetic is rather tricky. This explains the size of the subsection that's devoted to this data type. The section has these child pages:

- **[The date data type](../date-time-data-types-semantics/type-date/)**
- **[The time data type](../date-time-data-types-semantics/type-time/)**
- **[The plain timestamp and timestamptz data types](../date-time-data-types-semantics/type-timestamp/)**
- **[The interval data type](../date-time-data-types-semantics/type-interval/)**
  - **[How does YSQL represent an interval value?](../date-time-data-types-semantics/type-interval/interval-representation/)**
    - **[Ad hoc examples of defining interval values](../date-time-data-types-semantics/type-interval/interval-representation/ad-hoc-examples/)**
    - **[Modeling the internal representation and comparing the model with the actual implementation](../date-time-data-types-semantics/type-interval/interval-representation/internal-representation-model/)**
  - **[Interval value limits](../date-time-data-types-semantics/type-interval/interval-limits/)**
  - **[Declaring intervals](../date-time-data-types-semantics/type-interval/declaring-intervals/)**
  - **[The justify() and extract(epoch ...) functions for interval values](../date-time-data-types-semantics/type-interval/justfy-and-extract-epoch/)**
  - **[Interval arithmetic](../date-time-data-types-semantics/type-interval/interval-arithmetic/)**
    - **[Comparing two interval values](../date-time-data-types-semantics/type-interval/interval-arithmetic/interval-interval-comparison/)**
    - **[Adding or subtracting a pair of interval values](../date-time-data-types-semantics/type-interval/interval-arithmetic/interval-interval-addition/)**
    - **[Multiplying or dividing an interval value by a number](../date-time-data-types-semantics/type-interval/interval-arithmetic/interval-number-multiplication/)**
    - **[The moment-moment overloads of the "-" operator for timestamptz, timestamp, and time](../date-time-data-types-semantics/type-interval/interval-arithmetic/moment-moment-overloads-of-minus/)**
    - **[The moment-interval overloads of the "+" and "-" operators for timestamptz, timestamp, and time](../date-time-data-types-semantics/type-interval/interval-arithmetic/moment-interval-overloads-of-plus-and-minus/)**
  - **[Custom domain types for specializing the native interval functionality](../date-time-data-types-semantics/type-interval/custom-interval-domains/)**
  - **[User-defined interval utility functions](../date-time-data-types-semantics/type-interval/interval-utilities/)**

## [Typecasting between values of different date-time datatypes](../typecasting-between-date-time-values/)

This section presents the five-by-five matrix of all possible conversions between values of the _date-time_ datatypes. Many of the cells are empty because they correspond to operations that aren't supported (or, because the cell is on the diagonal representing the conversion between values of the same data type, it's tautologically uninteresting). This still leaves *twenty* typecasts whose semantics you need to understand. However, many can be understood as combinations of others, and this leaves only a few that demand careful study. The critical conversions are between plain _timestamp_ and _timestamptz_ values in each direction.

## [Date and time operators](../operators/)

This section describes the _date-time_ operators and presents tests for them grouped as follows:

- **[Test the date-time comparison overloads](../operators/test-date-time-comparison-overloads/)**
- **[Test the date-time addition overloads](../operators/test-date-time-addition-overloads/)**
- **[Test the date-time subtraction overloads](../operators/test-date-time-subtraction-overloads/)**
- **[Test the date-time multiplication overloads](../operators/test-date-time-multiplication-overloads/)**
- **[Test the date-time division overloads](../operators/test-date-time-division-overloads/)**

## [General-purpose date and time functions](../functions/)

This section describes the general-purpose _date-time_ functions in the following groups:

- **[Functions for creating date-time values](../functions/creating-date-time-values/)**
- **[Functions for manipulating date-time values](../functions/manipulating-date-time-values/)**
- **[Functions that return the current date-time moment](../functions/current-date-time-moment/)**
- **[Functions for delaying execution](../functions/delaying-execution/)**
- **[Miscellaneous date-time functions](../functions/miscellaneous/)**

## [Date and time formatting functions](../formatting-functions/)

This section describes:

- The use of the _to_char()_ built-in function for converting a _date-time_ value to a _text_ value.
- The use of the _to_date()_ and _to_timestamp()_ built-in functions for converting a _text_ value to a _date-time_ value.

The conversions, in each direction, are controlled by a so-called _template_. A template, in turn, is made up of a mixture of pre-defined so-called _template patterns_ and free text in a user-defined order. See the section [Date-time template patterns](../formatting-functions/#date-time-template-patterns). These template patterns, again in turn, can be _modified_. See the section [Date-time template pattern modifiers](../formatting-functions/#date-time-template-pattern-modifiers).

## [Case study—implementing a stopwatch with SQL](../stopwatch/)

This shows you how to implement a SQL stopwatch that allows you to start it with a procedure call before starting what you want to time and to read it with a _select_ statement when what you want to time finishes. This reading goes to the spool file along with all other _select_ results. Using a SQL stopwatch brings many advantages over using \\_timing on_.

## [Download and install the date-time utilities code](../download-date-time-utilities/)

This short page gives the instructions for downloading and installing all of the reusable code that's defined within this _date-time_ data types major section.
