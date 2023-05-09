---
title: Catalog views for timezone information [YSQL]
headerTitle: The pg_timezone_names and pg_timezone_abbrevs catalog views
linkTitle: Catalog views
description: Explains the information content of the pg_timezone_names and pg_timezone_abbrevs catalog views. [YSQL]
menu:
  preview:
    identifier: catalog-views
    parent: timezones
    weight: 10
type: docs
---

There are just two relevant catalog views:

```plpgsql
select table_name as "name"
from information_schema.views
where lower(table_schema) = 'pg_catalog'
and (
  lower(table_name) like '%zone%' or
  lower(table_name) like '%time%')
order by 1;
```

This is the result:

```output
 pg_timezone_abbrevs
 pg_timezone_names
```

## pg_timezone_names

The \\_d_ meta-command produces this result:

```outout
   Column   |   Type
------------+---------
 name       | text
 abbrev     | text
 utc_offset | interval
 is_dst     | boolean
```

The business unique key is _name_:

```plpgsql
do $body$
declare
  x boolean not null := false;
begin
  select
    (select count(*) from pg_timezone_names)             =
    (select count(distinct name) from pg_timezone_names)
  into x;
  assert x, 'assert failed';
end;
$body$;
```

The block finishes silently without error showing that the assertion holds.

## How to enrich pg_timezone_names with both Standard Time and Daylight Savings Time information

The bare _pg_timezone_names_ view doesn't necessarily tell you explicitly if a particular timezone observes Daylight Savings Time. But for a timezone that does this, the value of _is_dst_ will be _true_ when, but only when, Daylight Savings Time is in force. So you might be lucky, according to when you do your query, and learn something that at a different time of year you would not. This feels to be unsatisfactory. Here's the inspiration for an obvious technique to fix this irritation.

<a name="pg-timezone-names-query"></a>Try this:

```plpgsql
set timezone = 'America/Los_Angeles';
select
  current_setting('timezone')                                     as "timezone",
  to_char('2021-01-01 12:00:00 UTC'::timestamptz, 'TZH:TZM : TZ') as "January regime",
  to_char('2021-07-01 12:00:00 UTC'::timestamptz, 'TZH:TZM : TZ') as "July regime";
```

This is the result:

```output
      timezone       | January regime | July regime
---------------------+----------------+-------------
 America/Los_Angeles | -08 : PST      | -07 : PDT
```

### The jan_and_jul_tz_abbrevs_and_offsets() table function

Create the function like this:

```plpgsql
drop function if exists jan_and_jul_tz_abbrevs_and_offsets() cascade;

create function jan_and_jul_tz_abbrevs_and_offsets()
  returns table(
  name        text,
  jan_abbrev  text,
  jul_abbrev  text,
  jan_offset  interval,
  jul_offset  interval)
  language plpgsql
as $body$
declare
  set_timezone constant text not null := $$set timezone = '%s'$$;
  tz_set                text not null := '';
  tz_on_entry           text not null := '';
begin
  show timezone into tz_on_entry;

  for tz_set in (
    select pg_timezone_names.name as a
    from pg_timezone_names
  ) loop
    execute format(set_timezone, tz_set);
    select
      current_setting('timezone'),
      to_char('2021-01-01 12:00:00 UTC'::timestamptz, 'TZ'),
      to_char('2021-07-01 12:00:00 UTC'::timestamptz, 'TZ'),
      to_char('2021-01-01 12:00:00 UTC'::timestamptz, 'TZH:TZM')::interval,
      to_char('2021-07-01 12:00:00 UTC'::timestamptz, 'TZH:TZM')::interval
    into
      name,
      jan_abbrev,
      jul_abbrev,
      jan_offset,
      jul_offset;
    return next;
  end loop;

  execute format(set_timezone, tz_on_entry);
end;
$body$;
```

Test it like this:

```plpgsql
select
  name,
  jan_abbrev,
  jul_abbrev,
  lpad(jan_offset::text, 9) as jan_offset,
  lpad(jul_offset::text, 9) as jul_offset
from jan_and_jul_tz_abbrevs_and_offsets()
where
name = 'America/Los_Angeles' or name = 'Europe/London'
order by name;
```

This is the result:

```output
        name         | jan_abbrev | jul_abbrev | jan_offset | jul_offset
---------------------+------------+------------+------------+------------
 America/Los_Angeles | PST        | PDT        | -08:00:00  | -07:00:00
 Europe/London       | GMT        | BST        |  00:00:00  |  01:00:00
```

The definition of the _[extended_timezone_names](../extended-timezone-names/)_ view uses this technique and it extends the column list further by joining with facts from the [tz&nbsp;database](https://en.wikipedia.org/wiki/Tz_database). like, for example, the status of a timezone as _Canonical_ , _Alias_, or _Deprecated_.

## pg_timezone_abbrevs

The \\_d_ meta-command produces this result:

```output
   Column   |   Type
------------+----------
 abbrev     | text
 utc_offset | interval
 is_dst     | boolean
```

The business unique key is _abbrev_:

```plpgsql
do $body$
declare
  x boolean not null := false;
begin
  select
    (select count(*) from pg_timezone_abbrevs)             =
    (select count(distinct abbrev) from pg_timezone_abbrevs)
  into x;
  assert x, 'assert failed';
end;
$body$;
```

<a name="pg-timezone-abbrevs-query"></a>The block finishes silently without error showing that the assertion holds. Here is a telling example query:

```plpgsql
select abbrev, utc_offset::text, is_dst::text
from pg_timezone_abbrevs
where abbrev in ('PST', 'PDT')
order by abbrev;
```

This is the result:

```output
 abbrev | utc_offset | is_dst
--------+------------+--------
 PDT    | -07:00:00  | true
 PST    | -08:00:00  | false
```

## The difference between the two timezone catalog views

- _pg_timezone_names_ maps from its key, _name_, to an _abbrev_ and _utc_offset_ whose values, in general, are different during the Daylight Savings Time and the Standard Time periods. (The mapping _is_ unique on any particular date.) See the [query](#pg-timezone-names-query) at the end of the _pg_timezone_names_ section above.

- _pg_timezone_abbrevs_ maps from its key _abbrev_ to a fixed, unique _utc_offset_ value. When a timezone observes Daylight Savings Time, it has two different abbreviations: one for the Daylight Savings Time period, and one for the Standard Time period. See the [query](#pg-timezone-abbrevs-query) at the end of the _pg_timezone_abbrevs_ section above.

- The columns _pg_timezone_names.abbrev_ and _pg_timezone_abbrevs.abbrev_ record different kinds of facts

The column structure and naming of the _pg_timezone_names_ and _pg_timezone_abbrevs_ catalog views might suggest to SQL professionals that _pg_timezone_abbrevs_ implements a list of values that controls the _[abbrev, utc_offset, is_dst]_ tuple in _pg_timezone_names_—and they might wonder why the _utc_offset_ and _is_dst_ columns are repeated in the referring controllee, _pg_timezone_names_. But this is _not_ the case. The explanation is simple—albeit surprising:

- The _pg_timezone_names_ view exposes an unrestricted projection of the _tz&nbsp;database_ facts. (See the section [The _extended_timezone_names_ view](../extended-timezone-names/).) The _tz&nbsp;database_ facts are determined by committee and they change periodically in response to changes in convention. See, for example, [Daylight saving time in Canada](https://en.wikipedia.org/wiki/Daylight_saving_time_in_Canada):

  > In 2020, the territory of Yukon abandoned seasonal time change to permanently observe year-round Mountain Standard Time (MST).

  The _pg_timezone_names_ view's population is controlled by operating system files. See the PostgreSQL documentation appendix [B.4. Date/Time Configuration Files](https://www.postgresql.org/docs/11/datetime-config-files.html). The PostgreSQL developers aim, with each successive release, to update the content of  _pg_timezone_names_ to keep it current with the _tz&nbsp;database_. (YugabyteDB Version 2.4 has therefore fallen behind currency. [GitHub issue #8550](https://github.com/yugabyte/yugabyte-db/issues/8550) tracks this.) The administrator could fix this for a particular database by editing the appropriate files. The server refuses to start if _pg_timezone_names.name_ is not unique.

- The contents of the _pg_timezone_abbrevs_ view, too, can be changed, for a particular database, by the administrator. The population is controlled by operating system files. See the PostgreSQL documentation appendix [B.4. Date/Time Configuration Files](https://www.postgresql.org/docs/11/datetime-config-files.html). The PostgreSQL developers provide a default population. Further, which of these files are used can be controlled at the session level by setting the  _timezone_abbreviations_ run-time parameter. See the PostgreSQL documentation section [19.11. Client Connection Defaults](https://www.postgresql.org/docs/current/runtime-config-client.html#GUC-TIMEZONE-ABBREVIATIONS). The server refuses to start if _pg_timezone_abbrevs.abbrev_ is not unique.

- There is no requirement that every _abbrev_ value found in _pg_timezone_names_, establishing this set over the whole year, will be found in _pg_timezone_abbrevs_.

- A particular _abbrev_ that's found in _pg_timezone_names_ can denote different values of _utc_offset_ according to the timezone to which it belongs. For example, _PST_ that maps both to _-08:00_ (for, for example, _America/Los_Angeles_) and to _+08:00_ for _Asia/Manila_). See [List multiply defined \[abbrev, utc_offset\]; tuples](#list-multiply-defined-abbrev-utc-offset-tuples) below.

- There is no requirement that every _abbrev_ value from _pg_timezone_names_, establishing this set over the whole year maps to the same _utc_offset_ value as does its match in _pg_timezone_abbrevs_ when such a match is found.

- Nor could there be, given what the previous bullet states. The purpose of the _pg_timezone_abbrevs view_, these days, is dubious. In historical versions of PostgreSQL, it provided a useful way to allow the specification of a timezone that wasn't defined in the shipped files described in appendix [B.4](https://www.postgresql.org/docs/11/datetime-config-files.html). of the PostgreSQL documenation.

## Interesting pg_timezone_names and pg_timezone_abbrevs queries

The following queries shed light on the information content of the _pg_timezone_names_ and _pg_timezone_abbrevs_ views. The section [Four ways to specify the _UTC offset_](../ways-to-spec-offset/) explains that either a _name_ value from _pg_timezone_names_ or an _abbrev_ value from _pg_timezone_abbrevs_ is legal as the argument of the _at time zone_ operator that can be used to decorate a plain _timestamp_ value or a _timestamptz_ value. The queries show that nothing prevents what you might think is the abbreviation for a timezone of interest specifying a different _utc_offset_ value in _pg_timezone_abbrevs_ than is specified for its _name_ value in _pg_timezone_names_. This brings a risk that you don't get the outcome that you intend. This is spelled out more carefully in the section [Rules for resolving a string that's intended to identify a _UTC offset_](../ways-to-spec-offset/name-res-rules/).

The section [Recommended practice for specifying the _UTC offset_](../recommendation/) shows how to implement a discipline that avoids this risk.

### Timezones whose name is the same as its abbreviation

A few timezones have the same name and abbreviation. Do this:

```plpgsql
select name from pg_timezone_names
where name = abbrev
order by name;
```

This is the result:

```output
 EST
 GMT
 HST
 MST
 UCT
 UTC
```

### Timezones whose name is found as an abbreviation in pg_timezone_abbrevs

```plpgsql
select name from pg_timezone_names
where name in (
  select abbrev from pg_timezone_abbrevs)
order by name;
```

This is the result:

```output
 CET
 EET
 EST
 GMT
 HST
 MET
 MST
 UCT
 UTC
 WET
```

You can see that, for example, the string _UCT_ is the _name_ value in _pg_timezone_names_ of a timezone that the _[tz&nbsp;database](https://en.wikipedia.org/wiki/Tz_database)_ marks as _Deprecated_; and that it is the _abbrev_ value of a row in _pg_timezone_abbrevs_. The _utc_offset_ value in these two differtent rows is not guaranteed to be the same.

### Distinct [abbrev, utc_offset] tuples from pg_timezone_names not in pg_timezone_abbrevs.

```plpgsql
with v as (
  select distinct abbrev, utc_offset
  from pg_timezone_names
  where
    abbrev not like '%0%' and
    abbrev not like '%1%' and
    abbrev not like '%2%' and
    abbrev not like '%3%' and
    abbrev not like '%4%' and
    abbrev not like '%5%' and
    abbrev not like '%6%' and
    abbrev not like '%7%' and
    abbrev not like '%8%' and
    abbrev not like '%9%'
  )
select abbrev, lpad(utc_offset::text, 9) as "UTC offset" from v
except
select abbrev, lpad(utc_offset::text, 9) as "UTC offset" from pg_timezone_abbrevs
order by abbrev;
```

This is the result:

```output
 abbrev | UTC offset
--------+------------
 CAT    |  02:00:00
 CDT    | -04:00:00
 CST    |  08:00:00
 ChST   |  10:00:00
 HDT    | -09:00:00
 IST    |  05:30:00
 IST    |  01:00:00
 PST    |  08:00:00
 SST    | -11:00:00
 WEST   |  01:00:00
 WIB    |  07:00:00
 WIT    |  09:00:00
 WITA   |  08:00:00
```

Timezones whose _abbrev_ value contains a digit are excluded because there are 228 such rows and none is present in the _pg_timezone_abbrevs_ view.

### List multiply defined [abbrev, utc_offset] tuples

This query depends on the [jan_and_jul_tz_abbrevs_and_offsets()](#the-jan-and-jul-tz-abbrevs-and-offsets-table-function) table function function. It's convenient first to create the _abbrevs_with_many_offsets_ view thus:

```plpgsql
drop view if exists abbrevs_with_many_offsets cascade;

create view abbrevs_with_many_offsets(abbrev, utc_offset) as
with
  jan_distinct_abbrev_offset_values(abbrev, utc_offset) as (
    select distinct jan_abbrev, jan_offset
    from jan_and_jul_tz_abbrevs_and_offsets()),

  jul_distinct_abbrev_offset_values(abbrev, utc_offset) as (
    select distinct jul_abbrev, jul_offset
    from jan_and_jul_tz_abbrevs_and_offsets()),

  all_distinct_abbrev_offset_values(abbrev, utc_offset) as (
    select abbrev, utc_offset from jan_distinct_abbrev_offset_values
    union
    select abbrev, utc_offset from jul_distinct_abbrev_offset_values),

  multiply_defined_abbrevs(abbrev) as (
    select abbrev
    from all_distinct_abbrev_offset_values
    group by abbrev
    having count(*) > 1)

select abbrev, utc_offset
from all_distinct_abbrev_offset_values
where abbrev in (
  select abbrev from multiply_defined_abbrevs);
```

Check what it exposes this:

```plpgsql
select
  abbrev,
  lpad(utc_offset ::text, 9) as "UTC offset"
from abbrevs_with_many_offsets
order by abbrev, utc_offset;
```

This is the result:

```output
 abbrev | UTC offset
--------+------------
 CDT    | -05:00:00
 CDT    | -04:00:00

 CST    | -06:00:00
 CST    | -05:00:00
 CST    |  08:00:00

 IST    |  01:00:00
 IST    |  02:00:00
 IST    |  05:30:00

 PST    | -08:00:00
 PST    |  08:00:00
```

The blank lines were added by hand to improve the readability.

Now look at the _pg_timezone_names_ rows that have _PST_ as an abbreviation at some time of the year:

```plpgsql
select
  name,
  jan_abbrev,
  jul_abbrev,
  lpad(jan_offset::text, 9) as "Jan offset",
  lpad(jul_offset::text, 9) as "Jul offset"
from jan_and_jul_tz_abbrevs_and_offsets()
where
  jan_abbrev = 'PST' or
  jul_abbrev = 'PST'
order by jan_offset;
```

This is the result:

```output
         name         | jan_abbrev | jul_abbrev | Jan offset | Jul offset
----------------------+------------+------------+------------+------------
 Canada/Yukon         | PST        | PDT        | -08:00:00  | -07:00:00
 America/Santa_Isabel | PST        | PDT        | -08:00:00  | -07:00:00
 America/Dawson       | PST        | PDT        | -08:00:00  | -07:00:00
 America/Whitehorse   | PST        | PDT        | -08:00:00  | -07:00:00
 America/Vancouver    | PST        | PDT        | -08:00:00  | -07:00:00
 America/Los_Angeles  | PST        | PDT        | -08:00:00  | -07:00:00
 America/Ensenada     | PST        | PDT        | -08:00:00  | -07:00:00
 America/Tijuana      | PST        | PDT        | -08:00:00  | -07:00:00
 US/Pacific           | PST        | PDT        | -08:00:00  | -07:00:00
 PST8PDT              | PST        | PDT        | -08:00:00  | -07:00:00
 Mexico/BajaNorte     | PST        | PDT        | -08:00:00  | -07:00:00
 Canada/Pacific       | PST        | PDT        | -08:00:00  | -07:00:00

 Asia/Manila          | PST        | PST        |  08:00:00  |  08:00:00
```

The blank line was added by hand to improve the readability.

Notice that the abbreviation stands for _Pacific Standard Time_ in the context of the _Canonical_ timezone _America/Los_Angeles_ and for _Philippine Standard Time_ in the context of the _Canonical_ timezone _Asia/Manila_.

Now look at the _pg_timezone_names_ rows that have _IST_ as an abbreviation at some time of the year:

```plpgsql
select
  name,
  jan_abbrev,
  jul_abbrev,
  lpad(jan_offset::text, 9) as "Jan offset",
  lpad(jul_offset::text, 9) as "Jul offset"
from jan_and_jul_tz_abbrevs_and_offsets()
where
  jan_abbrev = 'IST' or
  jul_abbrev = 'IST'
order by jan_offset;
```

This is the result:

```output
      name      | jan_abbrev | jul_abbrev | Jan offset | Jul offset
----------------+------------+------------+------------+------------
 Europe/Dublin  | GMT        | IST        |  00:00:00  |  01:00:00
 Eire           | GMT        | IST        |  00:00:00  |  01:00:00

 Israel         | IST        | IDT        |  02:00:00  |  03:00:00
 Asia/Tel_Aviv  | IST        | IDT        |  02:00:00  |  03:00:00
 Asia/Jerusalem | IST        | IDT        |  02:00:00  |  03:00:00

 Asia/Kolkata   | IST        | IST        |  05:30:00  |  05:30:00
 Asia/Calcutta  | IST        | IST        |  05:30:00  |  05:30:00
```

The blank lines were added by hand to improve the readability.

Notice that the abbreviation stands for _Irish Summer Time_ in the context of the _Canonical_ timezone _Europe/Dublin_, for _Israel Standard Time_ in the context of the _Canonical_ timezone _Asia/Jerusalem_, and for _India Standard Time_ in the context of the _Canonical timezone _Asia/Kolkata_.

These observations prompt the question "How are the different meanings of the values of _pg_timezone_names.abbrev_ reflected in the _pg_timezone_abbrevs.abbrev_ column?" Try this:

```plpgsql
select
  abbrev,
  lpad(utc_offset ::text, 9) as "UTC offset"
from pg_timezone_abbrevs
where abbrev in (
  select abbrev from abbrevs_with_many_offsets)
order by abbrev, utc_offset;
```

This is the result:

```output
 abbrev | UTC offset
--------+------------
 CDT    | -05:00:00
 CST    | -06:00:00
 IST    |  02:00:00
 PST    | -08:00:00
```

In other words, an arbitrary choice has been made to favor, for example, _Pacific Standard Time_ over _Philippine Standard Time_ and _Israel Standard Time_ over _Irish Summer Time_ or _India Standard Time_.
