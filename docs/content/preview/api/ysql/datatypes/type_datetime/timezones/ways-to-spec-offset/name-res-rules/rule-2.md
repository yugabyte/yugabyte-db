---
title: Rule 2 (for string intended to specify the UTC offset) [YSQL]
headerTitle: Rule 2
linkTitle: 2 ~names.abbrev never searched
description: Substantiates the rule that a string that's intended to identify a UTC offset is never resolved in pg_timezone_names.abbrev. [YSQL]
menu:
  preview:
    identifier: rule-2
    parent: name-res-rules
    weight: 20
type: docs
---

{{< tip title="" >}}
A string that's intended to identify a _UTC offset_ is never resolved in _pg_timezone_names.abbrev_.
{{< /tip >}}

You can discover, with _ad hoc_ queries, that the string _WEST_ occurs uniquely in _pg_timezone_names.abbrev_. Use the function [_occurrences()_](../helper-functions/#function-occurrences-string-in-text) to confirm it thus:

```plpgsql
with c as (select occurrences('WEST') as r)
select
  (c.r).names_name     ::text as "~names.name",
  (c.r).names_abbrev   ::text as "~names.abbrev",
  (c.r).abbrevs_abbrev ::text as "~abbrevs.abbrev"
from c;
```

This is the result:

```output
 ~names.name | ~names.abbrev | ~abbrevs.abbrev
-------------+---------------+-----------------
 false       | true          | false
```

This means that the string _WEST_ can be used as a probe, using the function [_legal_scopes_for_syntax_context()_](../helper-functions/#function-legal-scopes-for-syntax-context-string-in-text)_:

```plpgsql
select x from legal_scopes_for_syntax_context('WEST');
```

This is the result:

```output
 WEST:               names_name: false / names_abbrev: true / abbrevs_abbrev: false
 ------------------------------------------------------------------------------------------
 set timezone = 'WEST';                                       > invalid_parameter_value
 select timezone('WEST', '2021-06-07 12:00:00');              > invalid_parameter_value
 select '2021-06-07 12:00:00 WEST'::timestamptz;              > invalid_datetime_format
```

You can copy-and-paste each offending statement to see each error occurring "live".

**This outcome supports the formulation of the rule that this page addresses.**

The values in _pg_timezone_names.abbrev_ are useful only for decorating the _to_char()_ rendition of a _timestamptz_ value, thus:

```plpgsql
set timezone = 'Atlantic/Faeroe';
with v as (
  select
    '2021-01-01 12:00:00 UTC'::timestamptz as t1,
    '2021-07-01 12:00:00 UTC'::timestamptz as t2
  )
select
  to_char(t1, 'hh24:mi:ss TZ TZH:TZM') as t1,
  to_char(t2, 'hh24:mi:ss TZ TZH:TZM') as t2
from v;
```

This is the result:

```output
         t1          |          t2
---------------------+----------------------
 12:00:00 WET +00:00 | 13:00:00 WEST +01:00
```

The strings _WET_ and _WEST_ are respectively the _Winter Time_ and _Summer Time_ abbreviations for the _Atlantic/Faeroe_ timezone.

**Note:** the strings in the _pg_timezone_names.abbrev_ column don't necessarily uniquely denote a _utc_offset_ value. You should think of these abbreviations, therefore, not as globally understood mappings. Suppose that you live in Los Angeles and your friend lives in Manila, and that you're planning a Holiday phone call. Imagine the confusion that would ensue if you said _"Let's schedule our call for Saturday at seven PM PST"_— (as people often do). Maybe you even speak that into a modern artificially "intelligent" assistant for your calendar app.

Try this:

```plpgsql
drop function if exists appointment_details() cascade;
create function appointment_details()
  returns table (z text)
  language plpgsql
as $body$
declare
  set_timezone constant text  not null := $$set timezone = '%s'$$;
  tz_on_entry  constant text not null := current_setting('timezone');
  t constant   timestamptz   not null := '2021-12-25 19:00 America/Los_Angeles'::timestamptz;
begin
   execute format(set_timezone, 'America/Los_Angeles');
   z := 'Los Angeles time: '||to_char(t, 'Dy dd hh24:mi TZ'); return next;


   execute format(set_timezone, 'Asia/Manila');
   z := 'Manila time:      '||to_char(t, 'Dy dd hh24:mi TZ'); return next;

   execute format(set_timezone, tz_on_entry);
end;
$body$;

select z from appointment_details();
```

This is the result:

```output
 Los Angeles time: Sat 25 19:00 PST
 Manila time:      Sun 26 11:00 PST
```

The calendar assistant, at least if it knew already what your location and your friend's location were and could map from these to timezones, would have to ask you _"Do you mean PST in Los Angeles or PST in Manila on that date?"_.

A timezone abbreviation (in the world of human convention) is unique _only_ within the scope of the timezone to which it belongs. Even then, it's not a very useful notion. with one caveat<sup>[1]</sup>, because the combination of date-time and timezone tells you unambiguously if it's Winter Time or Summer Time.

This is why _pg_timezone_names.abbrev_ is never used to resolve a string that's intended to identify a _UTC offset_.

[1] The caveat is that the abbreviation _is_ useful, just once per year, during the period of one hour at the "fall back" moment when your wall-clock reads the same time after that moment as it did before. See the [discussion](../../../../date-time-data-types-semantics/type-timestamp/#just-after-fall-back), in the section _"The plain timestamp and timestamptz data types"_ of this code example:

```plpgsql
set timezone = 'America/Los_Angeles';
select
  to_char('2021-11-07 08:30:00 UTC'::timestamptz, 'hh24:mi:ss TZ (OF)') as "1st 1:30",
  to_char('2021-11-07 09:30:00 UTC'::timestamptz, 'hh24:mi:ss TZ (OF)') as "2nd 1:30";
```
