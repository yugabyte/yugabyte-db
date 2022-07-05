---
title: Three syntax contexts that use the specification of a UTC offset [YSQL]
headerTitle: Three syntax contexts that use the specification of a UTC offset
linkTitle: Syntax contexts for offset
description: Explains the three syntax contexts that use the specification of a UTC offset. [YSQL]
menu:
  preview:
    identifier: syntax-contexts-to-spec-offset
    parent: timezones
    weight: 50
type: docs
---

You can specify the _UTC_ offset, either _directly_ as an _interval_ value or _indirectly_ as a timezone name in three different syntax contexts:

- [Using the session environment parameter _TimeZone_](#specify-the-utc-offset-using-the-session-environment-parameter-timezone)
- [Using the _at time zone_ operator](#specify-the-utc-offset-using-the-at-time-zone-operator)
- [Within the text of a _timestamptz_ literal or for _make_timestamptz()_'s _timezone_ parameter](#specify-the-utc-offset-explicitly-within-the-text-of-a-timestamptz-literal-or-for-make-timestamptz-s-timezone-parameter)

## Specify the UTC offset using the session environment parameter 'TimeZone'

The session environment parameter _TimeZone_ is defined by a precedence scheme,

- It can be set in the file _postgresql.conf_, or in any of the other standard ways described in [Chapter 19. Server Configuration](https://www.postgresql.org/docs/11/runtime-config.html) of the PostgreSQL documentation. This shows up in a newly-started session as the value of the _'TimeZone'_ session parameter when the _PGTZ_ environment variable is not set.

- It can be set using the _PGTZ_ environment variable. This is used by _libpq_ clients to send a _set time zone_ command to the server upon connection. This will override the value from _postgresql.conf_ so that this, too, then shows up in a newly-started session as the value of the _'TimeZone'_ session parameter.

- It can be set in an ongoing session using the generic _set timezone = \<text literal\>_ syntax that is used for every session environment parameter. (This generic syntax does not allow a _text_ expression.) Or it can be set with the timezone-specific _set time zone interval \<arg\>_ syntax. This spelling also allow _set time zone interval \<text literal\>_ (but again, a _text_ expression is illegal). Additionally, it uniquely allows the set _time zone interval \<interval literal\>_ syntax; notably, _only_ this syntax for the _interval_ value is allowed. You cannot use an arbitrary expression whose data type is _interval_. (See the section [Four ways to specify the _UTC offset_](../ways-to-spec-offset/).)

Here are examples of the three syntax variants:

```plpgsql
set timezone = 'America/New_York';

set time zone 'Asia/Tehran';

set time zone interval '1 hour';
```

**Note:** If the supplied text literal isn't found in _pg_timezone_names_ (see [Rules for resolving a string that's intended to identify a _UTC offset_](../ways-to-spec-offset/name-res-rules/), then an attempt is made to parse it as [POSIX syntax](https://www.postgresql.org/docs/11/datetime-posix-timezone-specs.html) (see [Directly using POSIX syntax](../ways-to-spec-offset/#directly-using-posix-syntax)). The result might surprise you. Try this:

```plpgsql
deallocate all;
prepare stmt as
select '2008-07-01 13:00 America/Los_Angeles'::timestamptz;

set timezone = 'America/Los_Angeles';
execute stmt;

set timezone = '-07:00';
execute stmt;

set timezone = 'Foo-7';
execute stmt;
```

These are the results:

```output
 2008-07-01 13:00:00-07

 2008-07-02 03:00:00+07

 2008-07-02 03:00:00+07
```

This brings a potential risk: an unnoticed typo that introduces a digit into an otherwise legal timezone name will silently produce a dramatically different effect than was intended. Yugabyte recommends that you avoid this risk. Notice the contrast between this outcome and the one that uses _"-07:00"_ as in the section [Specify the _UTC offset_ explicitly within the text of a _timestamptz_ literal or for _make_timestamptz()_'s _timezone_ parameter](#specify-the-utc-offset-explicitly-within-the-text-of-a-timestamptz-literal-or-for-make-interval-s-timezone-parameter) below. Here, both _"-07:00'"_ and _"Foo-7"_ are interpreted as [POSIX syntax](https://www.postgresql.org/docs/11/datetime-posix-timezone-specs.html). But below, only _"Foo-7"_ is interpreted as POSIX syntax while _"-07:00'"_ is interpreted as [ISO 8601](https://en.wikipedia.org/wiki/ISO_8601) syntax. See [Recommended practice for specifying the _UTC offset_](../recommendation/).

## Specify the UTC offset using the 'at time zone' operator

This syntax is used:

- _either_ to decorate a plain _timestamp_ value in order to convert it to a _timestamptz_ value;

- _or_ to decorate a _timestamptz_ value in order to convert it to a plain _timestamp_ value;

The operator's legal operands are _either_ a _text_ expression or an _interval_ expression. The _operator syntax_ has a semantically identical _function syntax_, _timezone()_, with overloads for these indirect and direct ways of specifying the offset.

Here are examples of the four syntax variants:

```plpgsql
drop function if exists timezone_name() cascade;
create function timezone_name()
  returns text
  language plpgsql
as $body$
begin
  -- Some logic would go here. Just return a manifest constant for this demo.
  return 'Europe/Amsterdam';
end;
$body$;

select make_timestamp(2021, 6, 1, 12, 13, 19) at time zone timezone_name();

select timezone(timezone_name(), make_timestamp(2021, 6, 1, 12, 13, 19));

select make_timestamptz(2021, 6, 1, 12, 13, 19, 'Europe/Helsinki') at time zone make_interval(hours=>4, mins=>30);

select timezone(make_interval(hours=>4, mins=>30), make_timestamptz(2021, 6, 1, 12, 13, 19, 'Europe/Helsinki'));
```

## Specify the UTC offset explicitly within the text of a timestamptz literal or for make_timestamptz()'s 'timezone' parameter

Of course, the _timestamptz_ literal can be an expression. Here's an example:

```plpgsql
select ('1984-04-01 13:00 '||timezone_name())::timestamptz;
```

There's a possibility here, too, of interpreting a _text_ value with an embedded digit. Try this:

```plpgsql
set timezone = 'America/Los_Angeles';
\x on
with c as (select '2008-07-01 13:00' as ts)
select
  (select ((select ts from c)||' America/Los_Angeles')::timestamptz) as "Spelling 'LA' timezone in text",
  (select ((select ts from c)||' -07:00'             )::timestamptz) as "Attempting to specify the PDT offset",
  (select ((select ts from c)||' Foo99'              )::timestamptz) as "Specifying 'Foo99'";
\x off
```

This is the result:

```output
Spelling 'LA' timezone in text       | 2008-07-01 13:00:00-07
Attempting to specify the PDT offset | 2008-07-01 13:00:00-07
Specifying 'Foo99'                   | 2008-07-05 09:00:00-07
```

Notice the contrast between this outcome and the one that uses "-07:00" as in the section [Specify the _UTC offset_ using the session environment parameter _TimeZone_](#specify-the-utc-offset-using-the-session-environment-parameter-timezone) above. Here, only "Foo-7" is interpreted as [POSIX syntax](https://www.postgresql.org/docs/11/datetime-posix-timezone-specs.html) while "-07:00'" is interpreted as [ISO 8601](https://en.wikipedia.org/wiki/ISO_8601) syntax. But above, both "-07:00'" and "Foo-7" are interpreted as POSIX syntax. So there's all the more reason to embrace what the section [Recommended practice for specifying the _UTC offset_](../recommendation/) describes.

The same rules apply if you use the _make_timestamptz()_ built-in function.

```plpgsql
set timezone = 'America/Los_Angeles';
\x on
select
  (select make_timestamptz(2008, 7, 1, 13, 0, 0, 'America/Los_Angeles')) as "Spelling 'LA' timezone in text",
  (select make_timestamptz(2008, 7, 1, 13, 0, 0, '-07:00'             )) as "Attempting to specify the PDT offset",
  (select make_timestamptz(2008, 7, 1, 13, 0, 0, 'Foo99'              )) as "Specifying 'Foo99'";
\x off
```

The result is identical to that of the query, immediately above, that used the _timestamptz_ literal.
