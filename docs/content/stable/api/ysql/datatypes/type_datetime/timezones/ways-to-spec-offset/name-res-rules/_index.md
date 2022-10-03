---
title: Rules for resolving a string string intended to identify a UTC offset [YSQL]
headerTitle: Rules for resolving a string that's intended to identify a UTC offset
linkTitle: Name-resolution rules
description: Explains the rules for resolving a string that's intended to identify a UTC offset. [YSQL]
image: /images/section_icons/api/subsection.png
menu:
  stable:
    identifier: name-res-rules
    parent: ways-to-spec-offset
    weight: 10
type: indexpage
---
**Note:** If the text contains a digit, then it is taken as POSIX syntax. See the appendix [B.5. POSIX Time Zone Specifications](https://www.postgresql.org/docs/11/datetime-posix-timezone-specs.html) in the PostgreSQL documentation.

When a string is used to identify a _UTC offset_, there might seem _a priori_ to be three contexts in which it might be resolved:

- _either_ as a _pg_timezone_names.name_ value
- _or_ as a _pg_timezone_names.abbrev_ value
- _or_ as a _pg_timezone_abbrevs.abbrev_ value

Lest "seem" might leave you guessing, _Rule 2_ says that not all contexts are used to resolve all lookups.

Here are the rules for resolving a string that's intended to identify a _UTC offset_:

- [Rule 1](./rule-1/) — Lookup of the string is case-insensitive (discounting, of course, using an explicit _select_ statement _from_ one of the _pg_timezone_names_ or _pg_timezone_abbrevs_ catalog views).
- [Rule 2](./rule-2/) — The string is never resolved in _pg_timezone_names.abbrev_.
- [Rule 3](./rule-3/) — The string is never resolved in _pg_timezone_abbrevs.abbrev_ as the argument of _set timezone_ but it is resolved there as the argument of _timezone()_ and within a _text_ literal for a _timestamptz_ value.
- [Rule 4](./rule-4/) — The string is resolved first in _pg_timezone_abbrevs.abbrev_ and, only if this fails, then in _pg_timezone_names.name_. This applies only in those syntax contexts where _pg_timezone_abbrevs.abbrev_ is a candidate for the resolution—so not for _set timezone_, which looks only in _pg_timezone_names.name_.

The syntax contexts of interest are described in the section [Three syntax contexts that use the specification of a _UTC offset_](../../syntax-contexts-to-spec-offset/).

**Note:** The code that substantiates _Rule 4_ is able to do this only because there do exist cases where the same string is found in _both_ resolution contexts but with different _utc_offset_ values in the two contexts.

<a name="syntax-contexts-table"></a>This table summarises [Rule 2](./rule-3/), [Rule 3](./rule-3/), and [Rule 4](./rule-4/):

| Syntax context \ View column                    | pg_timezone_names.name | pg_timezone_names.abbrev    | pg_timezone_abbrevs.abbrev                 |
| ----------------------------------------------- | ---------------------- | --------------------------- |------------------------------------------- |
| _set timezone_<sup>[\[1\]](#note-1)</sup>       | **here only** _[Rule 3](./rule-3/)_         | never _[Rule 2](./rule-2/)_ | not for set timezone  |
| _at time zone_<sup>[\[2\]](#note-2)</sup>       | **second** priority _[Rule 4](./rule-4/)_   | never _[Rule 2](./rule-2/)_ | **first** priority    |
| _timestamptz_ value <sup>[\[3\]](#note-3)</sup> | **second** priority _[Rule 4](./rule-4/)_   | never _[Rule 2](./rule-2/)_ | **first** priority    |

<a name="note-1"></a>**Note 1:** This row applies for the two alternative syntax spellings:

```plpgsql
  set timezone = 'Europe/Amsterdam';
  set time zone 'Europe/Amsterdam';
```

<a name="note-2"></a>**Note 2:** This row applies for both the _operator syntax_ and the _function syntax_:

```plpgsql
     select (
         (select '2021-06-02 12:00:00'::timestamp at time zone 'Europe/Amsterdam') =
         (select timezone('Europe/Amsterdam', '2021-06-02 12:00:00'::timestamp))
       )::text;
```

You usually see the _operator syntax_ in blog posts and the like. But there are good reasons to prefer the _function syntax_ in industrial strength application code. The section [Recommended practice for specifying the _UTC offset_](../../recommendation/) explains why and encourages you to use the overloads of the _timezone()_ built-in function only via the user-defined wrapper function [_at_timezone()_](../../recommendation/#the-at-timezone-function-overloads).

<a name="note-3"></a>**Note 3:**  This row applies for both the _::timestamptz_ typecast of a _text_ literal and the invocation of the _make_timestamptz()_ built-in function:

```plpgsql
     select (
         (select '2021-06-02 12:00:00 Europe/Amsterdam'::timestamptz) =
         (select make_timestamptz(2021, 6, 2, 12, 0, 0, 'Europe/Amsterdam'))
       )::text;
```

## Summary

The rules for resolving a string that's intended to specify a _UTC offset_ can be summarized thus:

- The resolution of a string is case-insensitive.
- A string is never resolved in _pg_timezone_names.abbrev_.
- A string is always resolved in _pg_timezone_names.name_.
- A string used in _set timezone_ is resolved only in _pg_timezone_names.name_.
- A string that's used in _at time zone_ or in the explicit specification of a _timestamptz_ value is resolved first in _pg_timezone_abbrevs.abbrev_ and only if this fails, then in _pg_timezone_names.name_.
- If a string escapes all of the attempts at resolution that the previous five bullet points set out, then an attempt is made to resolve it as [POSIX](https://www.postgresql.org/docs/11/datetime-posix-timezone-specs.html) syntax.
