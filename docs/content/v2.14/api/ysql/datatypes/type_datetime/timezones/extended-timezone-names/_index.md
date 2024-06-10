---
title: The extended_timezone_names view [YSQL]
headerTitle: The extended_timezone_names view
linkTitle: Extended_timezone_names
description: The extended_timezone_names extends the pg_timezone_names view with extra columns from the tz database. [YSQL]
image: /images/section_icons/api/subsection.png
menu:
  v2.14:
    identifier: extended-timezone-names
    parent: timezones
    weight: 20
type: indexpage
---

{{< tip title="Download and install the date-time utilities code." >}}
The code on this page and its child pages doesn't depend on the _date-time utilities_ code. However, the code that the section [Recommended practice for specifying the _UTC offset_](../recommendation/) describes does depend on the _extended_timezone_names_ view. You might also find the views that this page and its child-pages describe to be ordinarily useful by letting you use the power of SQL to get the same information that would be rather tedious to get simply by reading the source data that the [List of tz&nbsp;database time zones](https://en.wikipedia.org/wiki/List_of_tz_database_time_zones) presents.

The code-kit creates a table in a PostgreSQL or YugabyteDB database with the data that the _"List of tz&nbsp;database time zones"_ shows. The simplest way to get the data is just to copy-and-paste the table from the browser display into a plain text file. This naïve approach ends up with a file that has the _\<tab\>_ character as the field separator—but these separator characters are missing on each line where, in the browser display, the first and maybe next few table cells are empty. There aren't many such rows, and it's easy to fix the missing _\<tab\>_ characters by hand. This cleaned up file is included in the code kit to save you that effort. (There are other ways to get the same data from the Internet, and you may prefer to use one of these.)

Once you have the data in a plain text file, it's easy to use the \\_copy_ metacommand at the _psql_ or _ysqlsh_ prompt. (It uses _\<tab\>_ as the default column separator.) This stages the copied-and-pasted browser data into a table. It turns out that the resulting table content has the character `−` (i.e. _chr(8722)_) in place of the regular `-` character (i.e. _chr(45)_). This affects the columns that record the winter and summer offsets from _UTC_ and  the latitude and longitude. The built-in function _replace()_ is used to correct this anomaly.

The kit also includes the code to create the  user-defined function _[jan_and_jul_tz_abbrevs_and_offsets()](../catalog-views/#the-jan-and-jul-tz-abbrevs-and-offsets-table-function)_ that creating the _extended_timezone_names_ view depends upon. It also creates the views that are used to produce the lists that this page's child pages show.
{{< /tip >}}

**This page has these child pages:**

- [extended_timezone_names — unrestricted, full projection](./unrestricted-full-projection/)
- [Real timezones that observe Daylight Savings Time](./canonical-real-country-with-dst/)
- [Real timezones that don't observe Daylight Savings Time](./canonical-real-country-no-dst/)
- [Synthetic timezones (do not observe Daylight Savings Time)](./canonical-no-country-no-dst/)

The _pg_timezone_names_ view is populated from the _[tz&nbsp;database](https://en.wikipedia.org/wiki/Tz_database)_:

> The _tz&nbsp;database_ is a collaborative compilation of information about the world's time zones, primarily intended for use with computer programs and operating systems... [It has] the organizational backing of [ICANN](https://en.wikipedia.org/wiki/ICANN). The _tz&nbsp;database_ is also known as _tzdata_, the _zoneinfo database_ or _IANA time zone database_...

The population of _pg_timezone_names_ is refreshed with successive PostgreSQL Versions. As of the release date of PostgreSQL Version 13.2, the set of names in the _pg_timezone_names_ view in that environment is identical to the set of names in the _tz&nbsp;database_. (The _name_ column in each is unique.) But YugabyteDB Version 2.4, based on PostgreSQL Version 11.2, has three small discrepancies and a few other inconsistencies. [GitHub issue #8550](https://github.com/yugabyte/yugabyte-db/issues/8550) tracks this.

The _pg_timezone_names_ view shows a projection of the _tz&nbsp;database_'s columns. And the [List of tz&nbsp;database time zones](https://en.wikipedia.org/wiki/List_of_tz_database_time_zones) shows a different, but overlapping projection.

It's useful, therefore, to join these two projections as the _extended_timezone_names_ view.

## Create the 'extended_timezone_names' view

The _extended_timezone_names_ view is created as the inner join of  _pg_timezone_names_ and the _tz_database_time_zones_extended_ table, created by the code kit. The user-defined table function _[ jan_and_jul_tz_abbrevs_and_offsets()](../catalog-views/#the-jan-and-jul-tz-abbrevs-and-offsets-table-function)_ is used to populate this table from the staging table for the data from the [List of tz database time zones](https://en.wikipedia.org/wiki/List_of_tz_database_time_zones) page by adding the columns _std_abbrev_ (the Standard Time timezone abbreviation) and _dst_abbrev_ (the Summer Time timezone abbreviation).

Various quality checks are made during the whole process. These discover a few more anomalies. These, too, are tracked by [GitHub issue #8550](https://github.com/yugabyte/yugabyte-db/issues/8550). You can see all this in the downloaded code kit. Look for the spool file _YB-QA-reports.txt_. You can also install the kit using PostgreSQL. This will spool a corresponding _PG-QA-reports.txt_ file.

Here's an example query that selects all of the columns from the _extended_timezone_names_ view for three example timezones.:

```plpgsql
\x on
select
  name,
  abbrev,
  std_abbrev,
  dst_abbrev,
  to_char_interval(utc_offset) as utc_offset,
  to_char_interval(std_offset) as std_offset,
  to_char_interval(dst_offset) as dst_offset,
  is_dst::text,
  country_code,
  lat_long,
  region_coverage,
  status
from extended_timezone_names
where name in ('America/Los_Angeles', 'Asia/Manila', 'Europe/London')
order by name;
\x off
```

This is the result:

```output
name            | America/Los_Angeles
abbrev          | PDT
std_abbrev      | PST
dst_abbrev      | PDT
utc_offset      | -07:00
std_offset      | -08:00
dst_offset      | -07:00
is_dst          | true
country_code    | US
lat_long        | +340308-1181434
region_coverage | Pacific
status          | Canonical
----------------+--------------------
name            | Asia/Manila
abbrev          | PST
std_abbrev      | PST
dst_abbrev      | PST
utc_offset      |  08:00
std_offset      |  08:00
dst_offset      |  08:00
is_dst          | false
country_code    | PH
lat_long        | +1435+12100
region_coverage |
status          | Canonical
----------------+--------------------
name            | Europe/London
abbrev          | BST
std_abbrev      | GMT
dst_abbrev      | BST
utc_offset      |  01:00
std_offset      |  00:00
dst_offset      |  01:00
is_dst          | true
country_code    | GB
lat_long        | +513030-0000731
region_coverage |
status          | Canonical
```

Notice that the abbreviation _PST_ has two different meanings, as was emphasized in the section [The columns _pg_timezone_names.abbrev_ and _pg_timezone_abbrevs.abbrev_ record different kinds of facts](../#the-columns-pg-timezone-names-abbrev-and-pg-timezone-abbrevs-abbrev-record-different-kinds-of-facts).

The installation of the code kit finishes by spooling the _Markdown_ source snippets that define the lists that are presented on this page's child pages.
