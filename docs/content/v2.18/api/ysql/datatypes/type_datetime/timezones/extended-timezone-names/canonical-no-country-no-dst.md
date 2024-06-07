---
title: Synthetic timezones (do not observe DST) [YSQL]
headerTitle: Synthetic timezones (do not observe Daylight Savings Time)
linkTitle: Synthetic timezones no DST
description: Synthetic timezones no DST table. [YSQL]
menu:
  v2.18:
    identifier: canonical-no-country-no-dst
    parent: extended-timezone-names
    weight: 40
type: docs
---

This table shows canonically-named timezones that are not associated with a specific country or region—in other words, they are _synthetic_ timezones, a.k.a. _time standards_. _UTC_ is the primary time standard. The remaining rows map all the offsets found among the real timezones that are multiples of one hour. (There are no synthetic timezones that correspond to real timezones with offsets that are non-integral multiples of one hour.) Do this to list the unusual timezones:

```plpgsql
select
  name,
  lpad(std_offset::text, 9) as "STD offset",
  lpad(dst_offset::text, 9) as "DST offset"
from extended_timezone_names
where
  extract(minutes from std_offset) <> 0 or
  extract(minutes from dst_offset) <> 0
order by name, std_offset;
```

This is the result:

```output
         name          | STD offset | DST offset
-----------------------+------------+------------
 America/St_Johns      | -03:30:00  | -02:30:00
 Asia/Calcutta         |  05:30:00  |  05:30:00
 Asia/Colombo          |  05:30:00  |  05:30:00
 Asia/Kabul            |  04:30:00  |  04:30:00
 Asia/Kathmandu        |  05:45:00  |  05:45:00
 Asia/Katmandu         |  05:45:00  |  05:45:00
 Asia/Kolkata          |  05:30:00  |  05:30:00
 Asia/Rangoon          |  06:30:00  |  06:30:00
 Asia/Tehran           |  03:30:00  |  04:30:00
 Asia/Yangon           |  06:30:00  |  06:30:00
 Australia/Adelaide    |  09:30:00  |  10:30:00
 Australia/Broken_Hill |  09:30:00  |  10:30:00
 Australia/Darwin      |  09:30:00  |  09:30:00
 Australia/Eucla       |  08:45:00  |  08:45:00
 Australia/LHI         |  10:30:00  |  11:00:00
 Australia/Lord_Howe   |  10:30:00  |  11:00:00
 Australia/North       |  09:30:00  |  09:30:00
 Australia/South       |  09:30:00  |  10:30:00
 Australia/Yancowinna  |  09:30:00  |  10:30:00
 Canada/Newfoundland   | -03:30:00  | -02:30:00
 Indian/Cocos          |  06:30:00  |  06:30:00
 Iran                  |  03:30:00  |  04:30:00
 NZ-CHAT               |  12:45:00  |  13:45:00
 Pacific/Chatham       |  12:45:00  |  13:45:00
 Pacific/Marquesas     | -09:30:00  | -09:30:00
```

Notice that _Australia/Lord_Howe_ and _Australia/LHI_ change the offset by just thirty minutes for Daylight Savings Time.

The results that the table below presents are based on the view _canonical_no_country_no_dst_ and are ordered by the _utc_offset_ column and then by the _name_ column. Trivial code adds the Markdown table notation. The view is defined thus:

```plpgsql
drop view if exists canonical_no_country_no_dst cascade;

create view canonical_no_country_no_dst as
select name, utc_offset
from extended_timezone_names
where
  name = 'UTC'

  or

  (
    -- There are rows with names 'Etc/GMT-0', 'Etc/GMT', and 'Etc/GMT0', all of
    -- which have an offset of zero from UTC. These entries are therefore redundant.
    name like 'Etc/GMT%'           and
    utc_offset <> make_interval()  and

    lower(status) = 'canonical'    and

    std_offset = dst_offset        and

    (
      country_code is null or
      country_code = ''
    )                              and

    (
      lat_long is null or
      lat_long = ''
    )                              and

    (
      region_coverage is null or
      region_coverage = ''
    )                              and

    lower(name) not in (select lower(abbrev) from pg_timezone_names) and
    lower(name) not in (select lower(abbrev) from pg_timezone_abbrevs)
  );
```

Here is the result:

| name                             | UTC offset |
| ----                             | ---------- |
| Etc/GMT+12                       | -12:00     |
| Etc/GMT+11                       | -11:00     |
| Etc/GMT+10                       | -10:00     |
| Etc/GMT+9                        | -09:00     |
| Etc/GMT+8                        | -08:00     |
| Etc/GMT+7                        | -07:00     |
| Etc/GMT+6                        | -06:00     |
| Etc/GMT+5                        | -05:00     |
| Etc/GMT+4                        | -04:00     |
| Etc/GMT+3                        | -03:00     |
| Etc/GMT+2                        | -02:00     |
| Etc/GMT+1                        | -01:00     |
| UTC                              |  00:00     |
| Etc/GMT-1                        |  01:00     |
| Etc/GMT-2                        |  02:00     |
| Etc/GMT-3                        |  03:00     |
| Etc/GMT-4                        |  04:00     |
| Etc/GMT-5                        |  05:00     |
| Etc/GMT-6                        |  06:00     |
| Etc/GMT-7                        |  07:00     |
| Etc/GMT-8                        |  08:00     |
| Etc/GMT-9                        |  09:00     |
| Etc/GMT-10                       |  10:00     |
| Etc/GMT-11                       |  11:00     |
| Etc/GMT-12                       |  12:00     |
| Etc/GMT-13                       |  13:00     |
| Etc/GMT-14                       |  14:00     |

Notice that these timezones are named in accordance with the [POSIX](https://www.postgresql.org/docs/11/datetime-posix-timezone-specs.html) convention. This defines locations to the west of Greenwich UK to have a _positive_ offset. But the value of the offset in _pg_timezone_names_ for such a location is shown as a _negative_ value.

You must be very careful to type correctly when you type these names. Because, for example, _Etc/GMT-99_ is not found in _pg_timezone_names_, it is interpreted at a POSIX-syntax specification of the offset—in this example _positive_ ninety-nine hours.
