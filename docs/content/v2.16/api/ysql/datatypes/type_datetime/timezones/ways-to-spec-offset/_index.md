---
title: Four ways to specify the UTC offset [YSQL]
headerTitle: Four ways to specify the UTC offset
linkTitle: Four ways to specify offset
description: Explains the four ways to specify the UTC offset. [YSQL]
image: /images/section_icons/api/subsection.png
menu:
  v2.16:
    identifier: ways-to-spec-offset
    parent: timezones
    weight: 40
type: indexpage
---

The  _UTC offset_ is, ultimately, expressed as an [_interval_](../../date-time-data-types-semantics/type-interval/) value. It can be specified in various different ways as this page explains.

{{< tip title="Yugabyte recommends using only a timezone name or an explicit 'interval' value to specify the 'UTC offset'." >}}
See the section [Recommended practice for specifying the _UTC offset_](../recommendation/). It presents a sufficient way to achieve all the functionality that you could need while protecting you from the many opportunities to go wrong brought by using the native functionality with no constraints.
{{< /tip >}}

### Directly as an interval value

To specify an _interval_ value for the session's _'TimeZone'_ setting, you must use the _set time zone \<arg\>_ syntax and not the _set timezone = \<arg\>_ syntax. For example:

```plpgsql
set time zone interval '-7 hours';
```

Notice that, in this syntax context, the _interval_ value can be specified only using the type name constructor. Each of these attempts:

```plpgsql
set time zone '-7 hours'::interval;
```

and:

```plpgsql
set time zone make_interval(hours => -7);
```

causes this error:

```output
42601: syntax error
```

This reflects an insuperable parsing challenge brought by the very general nature of the _set_ statement. (For example, it has variants for setting the transaction isolation level.)

In contrast, the _at time zone_ operator allows any arbitrary _interval_ expression. Try this:

```plpgsql
select '2021-05-27 12:00:00'::timestamp at time zone (make_interval(hours=>5) + make_interval(mins=>45));
```

You can also specify an _interval_ value within the text of a _timestamptz_ literal. But here, you use just the text that would be used with the _::interval_ typecast. Try this:

```plpgsql
select '2021-05-27 12:00:00 -03:15:00'::timestamptz;
```
The same rule applies if you use the _make_timestamptz()_ built-in function. Its _timezone_ formal parameter has data type _text_. There is no overload where this parameter has data type _interval_.

### Directly using POSIX syntax

The syntax is described in the PostgreSQL documentation in the appendix [B.5. POSIX Time Zone Specifications](https://www.postgresql.org/docs/11/datetime-posix-timezone-specs.html). This allows you to specify the two _UTC offset_ values, one for Standard Time and one for Summer Time, along with the "spring forward" and "fall back" moments. It's exceedingly unlikely that you will need to use this because, these days, everywhere on the planet falls within a canonically-named timezone that keys to the currently-understood rules for Daylight Savings Time from the indefinite past through the indefinite future. The rules are accessible to the PostgreSQL and YugabyteDB servers. (The source is the so-called _[tz&nbsp;database](https://en.wikipedia.org/wiki/Tz_database)_.) If a committee decision changes any rules, then the _tz&nbsp;database_ is updated and the new rules are thence adopted into the configuration data for PostgreSQL and YugabyteDB. Look at the tables on these two pages: [Real timezones that observe Daylight Savings Time](../extended-timezone-names/canonical-real-country-with-dst/); and [Real timezones that don't observe Daylight Savings Time](../extended-timezone-names/canonical-real-country-no-dst/).

Executing _show timezone_ after it has been set to an explicit _interval_ value reports back using POSIX syntax—albeit a simple form that doesn't specify Daylight Savings Time transitions. Try this:

```plpgsql
set time zone interval '-07:30:00';
show timezone;
```

This is the result:

```output
 <-07:30>+07:30
```

This is a legal argument for _set timezone_ thus:

```plpgsql
set timezone = '<-07:30>+07:30';
show timezone;
```

Of course, the result is exactly the same as what you set. It turns out that almost _any_ string that contains one or more digits can be interpreted as POSIX syntax. Try this:

```plpgsql
set timezone = 'FooBar5';
show timezone;
```

This is the result:

```output
 TimeZone
----------
 FOOBAR5
```

You can easily confirm that _FOOBAR5_ is not found in any of the columns (_pg_timezone_names.name_, _pg_timezone_names.abbrev_, or _pg_timezone_abbrevs.abbrev_) where timezone names or abbreviations are found.

Now see what effect this has, like this:

```plpgsql
\set bare_date_time '\'2021-04-15 12:00:00\''
select :bare_date_time::timestamptz;
```

This is the result:

```output
 2021-04-15 12:00:00-05
```

POSIX takes positive numbers to mean west of the Greenwich Meridian. And yet the PostgreSQL convention for displaying such a _UTC offset_ is to show it as a _negative_ value. (See [ISO 8601](https://en.wikipedia.org/wiki/ISO_8601).) This seems to be the overwhelmingly more common convention. Internet searches seem always to show timezones for places west of Greenwich with negative _UTC offset_ values. Try this:

```plpgsql
set time zone interval '-5 hours';
select :bare_date_time::timestamptz;
```

Here, the PostgreSQL convention was used to specify the _UTC offset_ value. Using the same _:bare_date_time_ text literal, the result, _2021-04-15 12:00:00-05_, is identical to what it was when the timezone was set to _FooBar5_.

Next, try this:

```plpgsql
set timezone = 'Foo5Bar';
select :bare_date_time::timestamptz;
```

Now the result has changed:

```output
2021-04-15 12:00:00-04
```

What? With the timezone set to _FooBar5_, the result of casting _2021-04-15 12:00:00_ to _timestamptz_ has a _UTC offset_ value of _negative five_ hours. But with the timezone set to _Foo5Bar_, the result of casting the same plain _timestamp_ value to _timestamptz_ has a _UTC offset_ value of _negative four_ hours. You can guess that this has something to do with the way POSIX encodes Daylight Savings Time rules—and with the defaults that are defined (in this case, Summer Time "springs forward" by _one hour_) when the tersest specification of Daylight Savings Time rules is given by using arbitrary text (in the example, at least) after the last digit in the POSIX text.

### Indirectly using a timezone name

This is overwhelmingly the preferred approach because it's this, and only this, that brings you the beneficial automatic mapping to the _UTC offset_ value that reigns at the moment of execution of a sensitive operation according to the rules for Daylight Savings Time that the name keys to in the internal representation of the [_tz database_](https://en.wikipedia.org/wiki/Tz_database).  (See the section [Scenarios that are sensitive to the _UTC offset_ or explicitly to the timezone](../timezone-sensitive-operations/).) The names are automatically looked up in the _pg_timezone_names_ catalog view. See the section [Rules for resolving a string that's intended to identify a _UTC offset_](./name-res-rules/)

### Indirectly using a timezone abbreviation

{{< tip title="Avoid using this approach." >}}
Though this approach is legal, Yugabyte strongly recommends that you avoid using it.
{{< /tip >}}

The best that this approach can do for you is to bring you a fixed value for the _UTC offset_ that's independent of the moment of lookup. But this is an exceedingly rare use case. If this is what you want, you can do it in a self-documenting way by specifying the _UTC offset_ [Directly as an _interval_ value](#directly-as-an-interval-value) as was explained above. Moreover, there are other risks brought by the attempt to use a timezone abbreviation to specify a _UTC offset_. See the section [Rules for resolving a string that's intended to identify a _UTC offset_](./name-res-rules/).
